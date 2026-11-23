/* Host entry point: plays the role of the Android activity + GofManager + MyRenderer.
 * Startup order follows com/ftt/gof2d/GofManager.<init> (InitJNI -> bridges -> CacheJNI) and
 * MyRenderer.onSurfaceChanged -> GofManager.glViewCreated (GLViewCreated, start game thread).
 *
 * Presentation differs from the original on purpose (modernised port): the window is an OpenGL surface,
 * the original's software framebuffer is scaled to fill the window height (its width follows the display
 * aspect, see ui/render.cpp hook of cGlFbGraphics::Setup) and the RmlUi interface is drawn on top. */
#include "host.h"
#include "apk.h"
#include "java.h"
#include "platform.h"
#include "gas.h"
#include "ui/ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/console.h>
#endif
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char g_apk_path[1024] = "epichearts.apk";
char g_data_dir[1024] = "save";
SDL_Window *g_window;

static FILE *g_logf;
void host_log(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    SDL_Log("%s", buf);
#elif defined(__EMSCRIPTEN__)
    /* straight to this thread's console: stderr of a worker would wait for the page's thread to print it */
    emscripten_console_warn(buf);
#else
    fprintf(stderr, "%s\n", buf);
#endif
    if (g_logf) { fprintf(g_logf, "%s\n", buf); fflush(g_logf); }
}
void host_fatal(const char *msg) {
    host_log("FATAL: %s", msg);
    if (!getenv("EH_CTRL") && !getenv("EH_SCRIPT") && !getenv("EH_BOT")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EpicHearts", msg, g_window);
    exit(1);
}
const char *host_map_path(const char *p, char *buf, size_t n) {
    snprintf(buf, n, "%s", p);
    return buf;
}

static uint32_t sym(const char *name) {
    uint32_t v = rc_sym(name);
    if (!v) host_fatal(name);
    return v & ~1u;
}

static volatile int g_quit;
static volatile int g_game_done; /* the game thread returned from GofManager.run */
void java_request_quit(void) { g_quit = 1; }

static int SDLCALL game_thread(void *p) {
    (void)p;
    Cpu *c = rc_new_cpu(8 << 20);
    uint32_t args[2] = {jni_env(), HPTR(g_gofmgr)};
    rc_call(c, sym("Java_com_ftt_gof2d_GofManager_run"), 2, args);
    host_log("game thread finished");
    g_game_done = 1;
    g_quit = 1;
    return 0;
}

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <dbghelp.h>
#ifdef _M_X64
#define CTX_PC Rip
#define CTX_FP Rbp
#define CTX_SP Rsp
#define CTX_A Rcx
#define CTX_B Rdx
#define CTX_C Rax
#define CTX_D Rsi
#define CTX_E Rdi
#define CTX_MACHINE IMAGE_FILE_MACHINE_AMD64
#else
#define CTX_PC Eip
#define CTX_FP Ebp
#define CTX_SP Esp
#define CTX_A Ecx
#define CTX_B Edx
#define CTX_C Eax
#define CTX_D Esi
#define CTX_E Edi
#define CTX_MACHINE IMAGE_FILE_MACHINE_I386
#endif
static void dump_stack(CONTEXT *ctx, int depth);
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    CONTEXT *ctx = ep->ContextRecord;
    host_log("CRASH code=%08lx at pc=%llx addr=%p (%llx %llx %llx %llx %llx) guest base %p",
             ep->ExceptionRecord->ExceptionCode, (unsigned long long)ctx->CTX_PC,
             ep->ExceptionRecord->NumberParameters > 1 ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL,
             (unsigned long long)ctx->CTX_A, (unsigned long long)ctx->CTX_B, (unsigned long long)ctx->CTX_C, (unsigned long long)ctx->CTX_D,
             (unsigned long long)ctx->CTX_E, (void *)GAS_BASE);
    dump_stack(ctx, 40);
    exit(3);
}

/* debugging aid: log the current call stack (recompiled frames are named by original function) */
void host_backtrace(int depth) {
    void *fr[64];
    HANDLE proc = GetCurrentProcess();
    static int inited;
    if (!inited) { SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS); SymInitialize(proc, NULL, TRUE); inited = 1; }
    int n = CaptureStackBackTrace(1, depth < 64 ? depth : 64, fr, NULL);
    for (int i = 0; i < n; i++) {
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
        si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
        DWORD64 disp = 0;
        const char *nm = SymFromAddr(proc, (DWORD64)(uintptr_t)fr[i], &disp, si) ? si->Name : "?";
        unsigned va = 0;
        if (sscanf(nm, "f_%x", &va) == 1) host_log("  #%d %s+%llx  [%s]", i, nm, disp, rc_func_name(va));
        else host_log("  #%d %s+%llx", i, nm, disp);
    }
}

static void dump_stack(CONTEXT *ctx, int depth) {
    HANDLE proc = GetCurrentProcess();
    static int inited;
    if (!inited) { SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES); SymInitialize(proc, NULL, TRUE); inited = 1; }
    STACKFRAME64 sf = {0};
    sf.AddrPC.Offset = ctx->CTX_PC; sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx->CTX_FP; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx->CTX_SP; sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < depth; i++) {
        if (!StackWalk64(CTX_MACHINE, proc, GetCurrentThread(), &sf, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
        si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
        DWORD64 disp = 0;
        const char *nm = SymFromAddr(proc, sf.AddrPC.Offset, &disp, si) ? si->Name : "?";
        unsigned va = 0;
        if (sscanf(nm, "f_%x", &va) == 1) host_log("  #%d %s+%llx  [%s]", i, nm, disp, rc_func_name(va));
        else host_log("  #%d %s+%llx", i, nm, disp);
    }
}

/* desktop: the apk is the first argument, else epichearts.apk in the working directory, next to the exe or one
 * level up; the save folder goes next to the apk that was found */
/* Single-file distribution (tools/package_win.py): the executable carries its data appended at the end, an
 * uncompressed zip (the original APK, data/ui, data/flfoo_android.png) followed by a 16-byte footer
 * "EHPAYLD1" + u64 file offset of the zip. On first run (or when the payload changed) the files are unpacked to
 * %LOCALAPPDATA%\EpicHearts, which becomes the working folder; saves and settings go to Documents\EpicHearts. */
static int payload_unpack(char *dir, size_t dir_sz) {
    char exe[1024];
    if (!GetModuleFileNameA(NULL, exe, sizeof exe)) return 0;
    FILE *f = fopen(exe, "rb");
    if (!f) return 0;
    unsigned char foot[16];
    _fseeki64(f, -16, SEEK_END);
    long long end = _ftelli64(f);
    if (fread(foot, 1, 16, f) != 16 || memcmp(foot, "EHPAYLD1", 8)) { fclose(f); return 0; }
    long long base = 0;
    memcpy(&base, foot + 8, 8);
    const char *la = getenv("LOCALAPPDATA");
    if (!la) { fclose(f); return 0; }
    snprintf(dir, dir_sz, "%s\\EpicHearts", la);
    CreateDirectoryA(dir, NULL);
    /* unchanged payload: nothing to do (stamp = payload position and size) */
    char stamp_path[1100], stamp[64], old[64] = "";
    snprintf(stamp_path, sizeof stamp_path, "%s\\payload.stamp", dir);
    snprintf(stamp, sizeof stamp, "%lld:%lld", base, end);
    FILE *sf = fopen(stamp_path, "rb");
    if (sf) { size_t n = fread(old, 1, sizeof old - 1, sf); old[n] = 0; fclose(sf); }
    if (!strcmp(old, stamp)) { fclose(f); return 1; }
    /* end of central directory: the last 22 bytes of the zip (no comment) */
    unsigned char eocd[22];
    _fseeki64(f, end - 22, SEEK_SET);
    if (fread(eocd, 1, 22, f) != 22 || memcmp(eocd, "PK\x05\x06", 4)) { fclose(f); return 0; }
    unsigned n = eocd[10] | eocd[11] << 8;
    unsigned cd_off = eocd[16] | eocd[17] << 8 | eocd[18] << 16 | (unsigned)eocd[19] << 24;
    long long pos = base + cd_off;
    static unsigned char buf[1 << 16];
    for (unsigned i = 0; i < n; i++) {
        unsigned char h[46];
        _fseeki64(f, pos, SEEK_SET);
        if (fread(h, 1, 46, f) != 46 || memcmp(h, "PK\x01\x02", 4)) break;
        unsigned method = h[10] | h[11] << 8;
        unsigned size = h[20] | h[21] << 8 | h[22] << 16 | (unsigned)h[23] << 24;
        unsigned nl = h[28] | h[29] << 8, el = h[30] | h[31] << 8, cl = h[32] | h[33] << 8;
        unsigned lho = h[42] | h[43] << 8 | h[44] << 16 | (unsigned)h[45] << 24;
        char name[512] = "";
        if (nl >= sizeof name || fread(name, 1, nl, f) != nl) break;
        name[nl] = 0;
        pos += 46 + nl + el + cl;
        if (method != 0 || !nl || name[nl - 1] == '/') continue; /* stored files only */
        char out[1100];
        snprintf(out, sizeof out, "%s\\%s", dir, name);
        for (char *q = out + strlen(dir) + 1; *q; q++)
            if (*q == '/') { *q = 0; CreateDirectoryA(out, NULL); *q = '\\'; }
        unsigned char lh[30];
        _fseeki64(f, base + lho, SEEK_SET);
        if (fread(lh, 1, 30, f) != 30) break;
        _fseeki64(f, base + lho + 30 + (lh[26] | lh[27] << 8) + (lh[28] | lh[29] << 8), SEEK_SET);
        FILE *o = fopen(out, "wb");
        if (!o) { fclose(f); return 0; }
        for (unsigned left = size; left;) {
            size_t k = fread(buf, 1, left < sizeof buf ? left : sizeof buf, f);
            if (!k) break;
            fwrite(buf, 1, k, o);
            left -= (unsigned)k;
        }
        fclose(o);
    }
    fclose(f);
    sf = fopen(stamp_path, "wb");
    if (sf) { fputs(stamp, sf); fclose(sf); }
    return 1;
}

static void setup_paths(int argc, char **argv) {
    char cwd[1024];
    _getcwd(cwd, sizeof cwd);
    char pdir[1024];
    if (argc > 1) snprintf(g_apk_path, sizeof g_apk_path, "%s", argv[1]);
    else if (payload_unpack(pdir, sizeof pdir)) {
        /* packaged build: run from the unpacked data; saves / settings / log in Documents\EpicHearts.
         * The embedded payload wins over an epichearts.apk next to the exe: that file may be another build
         * (the Android APK has no lib/armeabi/libtar-android.so), and the packaged one is the original. */
        _chdir(pdir);
        snprintf(g_apk_path, sizeof g_apk_path, "%s\\epichearts.apk", pdir);
        /* the game opens its files with ANSI paths (fopen): SDL's UTF-8 folder goes through the system code page;
         * a folder it cannot express (characters outside the code page) falls back to the unpacked data folder */
        const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
        char home[1024];
        snprintf(home, sizeof home, "%s", pdir);
        if (docs) {
            wchar_t w[1024];
            char a[1024];
            BOOL lossy = FALSE;
            if (MultiByteToWideChar(CP_UTF8, 0, docs, -1, w, 1024) &&
                WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, a, sizeof a, NULL, &lossy) && !lossy)
                snprintf(home, sizeof home, "%sEpicHearts", a);
        }
        host_mkdir(home);
        for (char *q = home; *q; q++) if (*q == '\\') *q = '/';
        snprintf(g_data_dir, sizeof g_data_dir, "%s/save", home);
        host_mkdir(g_data_dir);
        return;
    }
    else {
        char exe[1024], cand[3][1024];
        GetModuleFileNameA(NULL, exe, sizeof exe);
        char *s = strrchr(exe, '\\'); if (s) *s = 0;
        snprintf(cand[0], sizeof cand[0], "%s", cwd);
        snprintf(cand[1], sizeof cand[1], "%s", exe);
        s = strrchr(exe, '\\'); if (s) *s = 0;
        snprintf(cand[2], sizeof cand[2], "%s", exe);
        for (int i = 0; i < 3; i++) {
            char p[1100]; snprintf(p, sizeof p, "%s\\epichearts.apk", cand[i]);
            if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
                snprintf(g_apk_path, sizeof g_apk_path, "%s", p);
                snprintf(cwd, sizeof cwd, "%s", cand[i]);
                _chdir(cwd);
                break;
            }
        }
    }
    for (char *q = cwd; *q; q++) if (*q == '\\') *q = '/';
    snprintf(g_data_dir, sizeof g_data_dir, "%s/save", cwd);
    host_mkdir(g_data_dir);
}
#elif defined(__EMSCRIPTEN__)
void host_backtrace(int depth) { (void)depth; }
/* Browser: the page (web/app.js) reads the original APK the player chose from its own storage and hands the bytes
 * over before main (eh_set_apk). It stays in wasm memory: the game's reads (fopen of the APK path + offsets, from
 * the game thread) go through fmemopen and never wait on the page's thread. Saves and settings live in /persist
 * (IndexedDB, mounted and synced by the page). */
static uint8_t *g_apk_mem;
static size_t g_apk_mem_size;
EMSCRIPTEN_KEEPALIVE void eh_set_apk(uint8_t *bytes, size_t size) { g_apk_mem = bytes; g_apk_mem_size = size; }
FILE *host_open_apk(void) { return g_apk_mem ? fmemopen(g_apk_mem, g_apk_mem_size, "rb") : NULL; }
static void setup_paths(int argc, char **argv) {
    (void)argc; (void)argv;
    snprintf(g_apk_path, sizeof g_apk_path, "/apk/epichearts.apk");
    host_mkdir("/persist");
    snprintf(g_data_dir, sizeof g_data_dir, "/persist/save");
    host_mkdir(g_data_dir);
}
#else
#include <unistd.h>
void host_backtrace(int depth) { (void)depth; }
/* Android: the original APK ships as an (uncompressed) asset of ours; it is copied once to internal storage
 * because the game reads it with fopen + offsets (GofFileBridge.getApkPath / getApkFileOffseet). */
static void setup_paths(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *base = SDL_GetAndroidInternalStoragePath();
    snprintf(g_data_dir, sizeof g_data_dir, "%s/save", base);
    host_mkdir(g_data_dir);
    snprintf(g_apk_path, sizeof g_apk_path, "%s/epichearts.apk", base);
    SDL_IOStream *src = SDL_IOFromFile("epichearts.apk", "rb");
    if (!src) host_fatal("epichearts.apk asset missing");
    Sint64 sz = SDL_GetIOSize(src);
    SDL_PathInfo pi;
    if (!SDL_GetPathInfo(g_apk_path, &pi) || (Sint64)pi.size != sz) {
        host_log("copying original apk (%lld bytes) to %s", (long long)sz, g_apk_path);
        SDL_IOStream *dst = SDL_IOFromFile(g_apk_path, "wb");
        if (!dst) host_fatal("cannot write internal storage");
        static char buf[1 << 16];
        size_t n;
        while ((n = SDL_ReadIO(src, buf, sizeof buf)) > 0) SDL_WriteIO(dst, buf, n);
        SDL_CloseIO(dst);
    }
    SDL_CloseIO(src);
    if (chdir(base) != 0) host_log("chdir %s failed", base);
    /* test runs on a phone: KEY=VALUE lines of <external files>/eh_env.txt (/sdcard/Android/data/<package>/files, which
     * only adb and the app write) become environment variables, so the desktop's test switches work here too
     * (EH_CTRL=<that folder>/ctrl.txt drives the game, ui geom / ui cover / dumpui write their files there) */
    const char *ext = SDL_GetAndroidExternalStoragePath();
    char envp[1100];
    snprintf(envp, sizeof envp, "%s/eh_env.txt", ext ? ext : "");
    FILE *ef = ext ? fopen(envp, "r") : NULL;
    if (ef) {
        char line[600];
        while (fgets(line, sizeof line, ef)) {
            line[strcspn(line, "\r\n")] = 0;
            char *eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            setenv(line, eq + 1, 1);
            host_log("[env] %s=%s (%s)", line, eq + 1, envp);
        }
        fclose(ef);
    }
    /* EH_SAVEDIR (test runs): saves in another folder, e.g. a test save pushed to the external files, so a phone run
     * never loads or writes the owner's saves in internal storage */
    const char *sd = getenv("EH_SAVEDIR");
    if (sd && *sd) {
        snprintf(g_data_dir, sizeof g_data_dir, "%s", sd);
        host_mkdir(g_data_dir);
        host_log("[env] saves in %s", g_data_dir);
    }
}
#endif

/* aspect of the surface the game will fill: the display (fullscreen target) unless EH_ASPECT/EH_WINSIZE say otherwise */
static void choose_window(int *ww, int *wh, double *aspect) {
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    SDL_Rect b = {0, 0, 1920, 1080};
    SDL_GetDisplayBounds(d, &b);
    *aspect = b.h > 0 ? (double)b.w / b.h : 16.0 / 9.0;
    if (*aspect < 1.0) *aspect = 1.0 / *aspect; /* landscape only */
#ifdef __ANDROID__
    *ww = b.w > b.h ? b.w : b.h; *wh = b.w > b.h ? b.h : b.w;
#elif defined(__EMSCRIPTEN__)
    /* the page's size when the game starts; a phone held upright still gets a landscape game (the page asks to turn it) */
    *ww = EM_ASM_INT({ return window.innerWidth; });
    *wh = EM_ASM_INT({ return window.innerHeight; });
    if (*ww > 0 && *wh > 0) *aspect = *ww > *wh ? (double)*ww / *wh : (double)*wh / *ww;
#else
    /* windowed default: about 80% of the display height */
    *wh = (int)(b.h * 0.8); *ww = (int)lround(*wh * *aspect);
#endif
    const char *ws = getenv("EH_WINSIZE");
    if (ws && sscanf(ws, "%dx%d", ww, wh) == 2 && *wh > 0) *aspect = (double)*ww / *wh;
    if (getenv("EH_ASPECT")) *aspect = atof(getenv("EH_ASPECT"));
}

int g_game_w = 512, g_game_h = 320;

/* main loop state (main_frame) */
static Cpu *g_c;
static uint32_t g_env, g_render;
static const char *script;
static int script_wait = -1;
static const char *ctrl_path;
static long ctrl_off;
static int pending_up_frames, up_x, up_y;
static int monkey;
static uint32_t monkey_state;
static int frame, shot_every;
static int burst_left, burst_i; /* EH_CTRL burst <n>: a screenshot every frame (burst_000.bmp ...) */
static void main_frame(void);
static void main_shutdown(void);
#ifdef __EMSCRIPTEN__
static void web_frame(void);
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    remove("epichearts.prev.log");
    rename("epichearts.log", "epichearts.prev.log"); /* the previous run's log is kept */
    g_logf = fopen("epichearts.log", "w");
    SetUnhandledExceptionFilter(crash_filter);
#endif
    { extern int g_jtrace; g_jtrace = getenv("EH_JTRACE") != NULL; }
    gas_init();
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) host_fatal(SDL_GetError());
    setup_paths(argc, argv);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) /* the browser logs to the console only */
    { /* the previous run's log is kept (epichearts.prev.log): after a crash the player restarts the game */
        char lp[1100], pp[1100], dir[1100];
        snprintf(dir, sizeof dir, "%s/..", g_data_dir);
#ifdef __ANDROID__
        /* the external files folder (/sdcard/Android/data/<package>/files): adb can read it, the internal one only
         * a debuggable build */
        const char *ext = SDL_GetAndroidExternalStoragePath();
        if (ext && *ext) snprintf(dir, sizeof dir, "%s", ext);
#endif
        snprintf(lp, sizeof lp, "%s/epichearts.log", dir);
        snprintf(pp, sizeof pp, "%s/epichearts.prev.log", dir);
        remove(pp);
        rename(lp, pp);
        g_logf = fopen(lp, "w");
    }
#endif

    int win_w, win_h;
    double aspect;
    choose_window(&win_w, &win_h, &aspect);
    /* the original sizes its framebuffer to 320 lines (CreateGraphics @001287b8); width follows the aspect.
     * 400..800 keeps the field camera and form layouts inside what the original's code handles */
    g_game_h = 320;
    g_game_w = ((int)lround(320.0 * aspect) + 1) & ~1;
    if (g_game_w < 400) g_game_w = 400;
    if (g_game_w > 800) g_game_w = 800;

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#ifdef __EMSCRIPTEN__
    /* WebGL 2 canvas that fills the page and follows its size */
    SDL_WindowFlags wf = SDL_WINDOW_OPENGL | SDL_WINDOW_FILL_DOCUMENT | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    SDL_WindowFlags wf = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_WindowFlags wf = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (getenv("EH_FULLSCREEN")) wf |= SDL_WINDOW_FULLSCREEN;
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#ifdef _WIN32
    /* window / taskbar icon: icon resource 1 of the executable (src/app.rc, the original app icon) */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    g_window = SDL_CreateWindow("EpicHearts", win_w, win_h, wf);
    if (!g_window) host_fatal(SDL_GetError());
    /* the original APK first: the interface's text font is the original's own (res/raw/fttcustom, ui.cpp) */
    if (!apk_open(g_apk_path)) host_fatal("cannot open epichearts.apk (pass its path as the first argument)");
    if (!ui_init(g_window)) host_fatal("UI init failed");
    host_log("window %dx%d, game framebuffer %dx%d", win_w, win_h, g_game_w, g_game_h);
    uint32_t so_off, so_size;
    if (!apk_find("lib/armeabi/libtar-android.so", &so_off, &so_size)) host_fatal("libtar-android.so not found in apk");
    if (!rc_load_image(apk_read(so_off, so_size))) host_fatal("image load failed");
    host_log("[boot] image loaded");
    /* optional: glyph cache generated by the original on Android (tools/pull_glyphcache.sh). The game reads
     * getFilesDir()/flfoo.png in cSmartCacheFont::ReadPresavedFile and only renders missing glyphs itself. */
    {
        char dst[1100], src[1100];
        snprintf(dst, sizeof dst, "%s/flfoo.png", g_data_dir);
        snprintf(src, sizeof src, "%s", getenv("EH_GLYPHCACHE") ? getenv("EH_GLYPHCACHE") : "data/flfoo_android.png");
        SDL_IOStream *f = SDL_IOFromFile(dst, "rb");
        if (f) SDL_CloseIO(f);
        else if ((f = SDL_IOFromFile(src, "rb")) != NULL) {
            SDL_IOStream *o = SDL_IOFromFile(dst, "wb");
            char buf[65536]; size_t n;
            while (o && (n = SDL_ReadIO(f, buf, sizeof buf)) > 0) SDL_WriteIO(o, buf, n);
            if (o) SDL_CloseIO(o);
            SDL_CloseIO(f);
            host_log("glyph cache seeded from %s", src);
        }
    }
    /* Tar.onCreate: copy res/raw/fttcustom (0x7f040016) to getFilesDir()/fttcustom.otf */
    {
        uint32_t fo, fs;
        char fp[1100];
        snprintf(fp, sizeof fp, "%s/fttcustom.otf", g_data_dir);
        FILE *f = fopen(fp, "rb");
        if (f) fclose(f);
        else if (apk_find_raw("fttcustom", &fo, &fs) && (f = fopen(fp, "wb"))) {
            void *d = apk_read(fo, fs);
            fwrite(d, 1, fs, f);
            fclose(f);
            free(d);
        }
    }
    jni_init();
    java_register_all();
    host_log("[boot] jni ready");

    Cpu *c = rc_new_cpu(8 << 20);
    rc_run_init_array(c);
    host_log("[boot] static init done");
    uint32_t env = jni_env(), gm = HPTR(g_gofmgr);
    {
        uint32_t a[2] = {env, gm};
        rc_call(c, sym("Java_com_ftt_gof2d_GofManager_InitJNI"), 2, a);
        rc_call(c, sym("Java_com_ftt_gof2d_GofManager_CacheJNI"), 2, a);
        host_log("[boot] InitJNI / CacheJNI done");
    }
    /* MyRenderer.onSurfaceChanged -> GofManager.glViewCreated(w, h): the game's surface is its own framebuffer
     * size, so touch coordinates are framebuffer pixels (ui_window_to_game maps window input) */
    {
        uint32_t a[4] = {env, gm, (uint32_t)g_game_w, (uint32_t)g_game_h};
        rc_call(c, sym("Java_com_ftt_gof2d_GofManager_GLViewCreated"), 4, a);
        host_log("[boot] GLViewCreated done");
    }
    g_thread_state = 1;
    SDL_Thread *gt = SDL_CreateThread(game_thread, "game", NULL);
    (void)gt;

    g_c = c;
    g_env = env;
    g_render = sym("Java_com_ftt_gof2d_GLView_MyRenderer_render");
    /* EH_SCRIPT="<frames> tap <x> <y>; ..." (test driver, game framebuffer coordinates) */
    script = getenv("EH_SCRIPT");
    ctrl_path = getenv("EH_CTRL");
    monkey = getenv("EH_MONKEY") != NULL;
    monkey_state = monkey ? (uint32_t)atoi(getenv("EH_MONKEY")) : 0;
    shot_every = getenv("EH_SHOT") ? atoi(getenv("EH_SHOT")) : 0;
#ifdef __EMSCRIPTEN__
    /* the browser drives the frames (requestAnimationFrame); main returns and the runtime stays */
    emscripten_set_main_loop(web_frame, 0, 0);
    return 0;
#else
    while (!g_quit) main_frame();
    main_shutdown();
    return 0;
#endif
}

/* one frame of the activity: input, test drivers, MyRenderer.onDrawFrame, presentation */
static void main_frame(void) {
    Cpu *c = g_c;
    uint32_t env = g_env, render = g_render;
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (java_text_event(c, &e)) continue;
            switch (e.type) {
            case SDL_EVENT_QUIT: g_quit = 1; return;
            /* Activity onPause/onResume -> GofManager.suspend (threadState 2) / resume (1 + appLock.notifyAll) */
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                jmon_enter(g_applock); g_thread_state = 2; jmon_exit(g_applock); continue;
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                if (g_thread_state == 2) { jmon_enter(g_applock); g_thread_state = 1; jmon_notify_all(g_applock); jmon_exit(g_applock); }
                continue;
            }
            ui_handle_event(&e); /* modern UI first; it forwards unclaimed input to the game (ui/input.cpp) */
        }
        if (script && *script) {
            if (script_wait < 0) script_wait = atoi(script);
            if (script_wait-- == 0) {
                char cmd[16] = "";
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0, nfr;
                sscanf(script, "%d %15[a-z] %d %d %d %d", &nfr, cmd, &x0, &y0, &x1, &y1);
                if (!strcmp(cmd, "tap")) { java_set_touch(1, x0, y0); java_set_touch(2, x0, y0); }
                else if (!strcmp(cmd, "down")) java_set_touch(1, x0, y0);
                else if (!strcmp(cmd, "up")) java_set_touch(2, x0, y0);
                else if (!strcmp(cmd, "move")) java_set_touch(4, x0, y0);
                else if (!strcmp(cmd, "shot")) {
                    char fn[64];
                    snprintf(fn, sizeof fn, "script_%05d.bmp", frame);
                    ui_request_shot(fn);
                } else if (!strcmp(cmd, "quit")) g_quit = 1;
                host_log("[script] frame %d: %s %d %d", frame, cmd, x0, y0);
                const char *semi = strchr(script, ';');
                script = semi ? semi + 1 : "";
                while (*script == ' ') script++;
                script_wait = -1;
            }
        }
        /* EH_CTRL=<file>: live test driver, one command per line (tap x y | qtap x y | down x y | up x y | move x y | shot name |
         * fb name | key n | back | quit | ui <command>) — tap/down/up/move in game framebuffer coordinates */
        if (ctrl_path && frame % 5 == 0 && pending_up_frames == 0) {
            FILE *cf = fopen(ctrl_path, "rb");
            if (cf) {
                fseek(cf, ctrl_off, SEEK_SET);
                char line[256];
                if (fgets(line, sizeof line, cf) && strchr(line, '\n')) {
                    ctrl_off = ftell(cf);
                    char cmd[16] = "", arg[128] = "";
                    int x0 = 0, y0 = 0;
                    sscanf(line, "%15s", cmd);
                    if (!strcmp(cmd, "shot")) {
                        sscanf(line, "%*s %127s", arg);
                        ui_request_shot(arg);
                    } else if (!strcmp(cmd, "ui")) {
                        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
                        ui_test_command(line + 3);
                    } else {
                        sscanf(line, "%*s %d %d", &x0, &y0);
                        if (!strcmp(cmd, "tap")) { java_set_touch(1, x0, y0); pending_up_frames = 6; up_x = x0; up_y = y0; }
                        else if (!strcmp(cmd, "qtap")) { java_set_touch(1, x0, y0); java_set_touch(2, x0, y0); } /* press + release in one batch */
                        else if (!strcmp(cmd, "down")) java_set_touch(1, x0, y0);
                        else if (!strcmp(cmd, "up")) java_set_touch(2, x0, y0);
                        else if (!strcmp(cmd, "move")) java_set_touch(4, x0, y0);
                        else if (!strcmp(cmd, "quit")) g_quit = 1;
                        else if (!strcmp(cmd, "burst")) { burst_left = x0; burst_i = 0; }
                        else if (!strcmp(cmd, "back")) java_back_pressed(); /* Android BACK, as the Escape key */
                        else if (!strcmp(cmd, "fb")) { /* raw back buffer (cGlFbGraphics the_g+0x374, RGB565) */
                            char fn[128] = "fb.bmp";
                            sscanf(line, "%*s %127s", fn);
                            uint32_t g = M32(IMG + 0x19b364);
                            if (g) {
                                int fw = (int)M32(g + 0x24), fh = (int)M32(g + 0x28);
                                SDL_Surface *sf = SDL_CreateSurfaceFrom(fw, fh, SDL_PIXELFORMAT_RGB565, GP(M32(g + 0x374)), fw * 2);
                                if (sf) { SDL_SaveBMP(sf, fn); SDL_DestroySurface(sf); }
                            }
                        }
                        else if (!strcmp(cmd, "key")) { /* GofManager.SimulateKeyPress(key) */
                            uint32_t ka[3] = {jni_env(), HPTR(g_gofmgr), (uint32_t)x0};
                            rc_call(c, sym("Java_com_ftt_gof2d_GofManager_SimulateKeyPress"), 3, ka);
                        }
                    }
                    host_log("[ctrl] %s", line);
                }
                fclose(cf);
            }
        }
        if (pending_up_frames > 0 && --pending_up_frames == 0) java_set_touch(2, up_x, up_y);
        /* EH_MONKEY=<seed>: random touches for stress testing */
        if (monkey && pending_up_frames == 0 && frame % 12 == 0) {
            monkey_state = monkey_state * 1103515245u + 12345u;
            uint32_t r = monkey_state >> 8;
            int x = (int)(r % (uint32_t)g_game_w), y = (int)((r / g_game_w) % (uint32_t)g_game_h);
            if ((r >> 20) % 5 == 0) { java_set_touch(1, x, y); java_set_touch(4, (x + 120) % g_game_w, y); pending_up_frames = 30; up_x = (x + 120) % g_game_w; up_y = y; }
            else { java_set_touch(1, x, y); pending_up_frames = 4; up_x = x; up_y = y; }
        }
        if (g_thread_state == 2) { /* GLSurfaceView paused: no onDrawFrame */
#ifndef __EMSCRIPTEN__
            SDL_Delay(50);
#endif
            return;
        }
        Uint64 t0 = SDL_GetTicksNS();
        ui_update();
        Uint64 t1 = SDL_GetTicksNS();
        /* MyRenderer.onDrawFrame: glClear(GL_COLOR_BUFFER_BIT); render() -> delivers the frame to the presenter */
        uint32_t a[2] = {env, 0};
        rc_call(c, render, 2, a);
        Uint64 t2 = SDL_GetTicksNS();
        frame++;
        if (burst_left > 0) { char fn[32]; snprintf(fn, sizeof fn, "burst_%03d.bmp", burst_i++); ui_request_shot(fn); burst_left--; }
        if (getenv("EH_FPS")) { /* every 5 s: presented frames, game frames, and the game's last frame wait */
            extern volatile uint32_t g_game_frames;
            extern volatile int32_t g_game_wait_ms;
            static Uint64 last;
            static int last_frame;
            static uint32_t last_game;
            Uint64 now = SDL_GetTicks();
            if (!last) { last = now; last_frame = frame; last_game = g_game_frames; }
            else if (now - last >= 5000) {
                uint32_t sys = M32(IMG + rc_sym("the_sys"));
                double s = (double)(now - last) / 1000.0;
                host_log("[fps] present %.1f, game %.1f (frame interval %d ms, last wait %d ms)", (frame - last_frame) / s,
                         (g_game_frames - last_game) / s, sys ? (int)M32(sys + 0x14) : -1, (int)g_game_wait_ms);
                last = now; last_frame = frame; last_game = g_game_frames;
            }
        }
        if (shot_every && frame % shot_every == 0) {
            char fn[64];
            snprintf(fn, sizeof fn, "shot_%04d.bmp", frame / shot_every);
            ui_request_shot(fn);
        }
        ui_render_and_swap();
        { Uint64 t3 = SDL_GetTicksNS(); static Uint64 prev; if (getenv("EH_FPS") && (t3 - t0 > 100000000 || (prev && t0 - prev > 200000000))) host_log("[slow] frame %d: since last %llu ms, ui %llu, render %llu, present %llu", frame, prev ? (unsigned long long)((t0 - prev) / 1000000) : 0ull, (unsigned long long)((t1 - t0) / 1000000), (unsigned long long)((t2 - t1) / 1000000), (unsigned long long)((t3 - t2) / 1000000)); prev = t3; }
    }
}

#ifdef __EMSCRIPTEN__
static void web_frame(void) {
    if (!g_quit) main_frame();
    if (g_quit) {
        emscripten_cancel_main_loop();
        main_shutdown();
        /* the page offers to start again (web/app.js) */
        EM_ASM({ if (Module.onGameQuit) Module.onGameQuit(); });
    }
}
#endif

static void main_shutdown(void) {
    host_log("quit");
#ifdef EH_INTERP
    { extern void interp_prof_dump(void); interp_prof_dump(); } /* -DEH_IPROF builds only */
#endif
    /* requestQuit -> Activity.finish -> onDestroy -> GofManager.kill: threadState 3 (+ appLock.notifyAll for a
     * paused thread). cNixSystem::Run @00166ebc then calls destroyClet and GofManager.releaseInstance and returns.
     * Before, the port only left this loop: on Android the activity closed but the game thread and the music kept
     * running in the process. */
    if (!g_game_done) {
        jmon_enter(g_applock); g_thread_state = 3; jmon_notify_all(g_applock); jmon_exit(g_applock);
        for (int ms = 0; !g_game_done && ms < 1000; ms += 10) SDL_Delay(10); /* best effort: it may wait on a frame */
        host_log(g_game_done ? "game thread stopped" : "game thread still running at exit");
    }
    java_audio_shutdown();
    ui_shutdown();
#ifdef __ANDROID__
    /* the recompiled game's memory is initialised once per process: a relaunch must start a new one */
    exit(0);
#endif
}
