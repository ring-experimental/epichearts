/* Java classes of the original APK (classes.dex) reimplemented for the host.
 * Each method cites the smali it reproduces. */
#include "jni.h"
#include "apk.h"
#include "java.h"
#include "bot.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textconv.h"
#include "ui/ui.h"

#define JM(name) static uint64_t name(Cpu *c, JObj *self, const uint64_t *a)
#define UNUSED (void)c; (void)self; (void)a

JObj *g_gofmgr, *g_filebr, *g_audiobr, *g_fontbr, *g_devbr, *g_glview, *g_applock, *g_eventlock;
volatile int g_thread_state; /* GofManager.threadState: 0 init, 1 running, 2 paused, 3 being killed */

/* ---------------------------------------------------------------- input queue
 * GofManager.setTouch / procTouch: up to 16 (type,x,y) events, overflow resets the queue */
static int g_events[48], g_nevents;
void java_set_touch(int type, int x, int y) {
    jmon_enter(g_eventlock);
    if (g_nevents >= 16) g_nevents = 0;
    g_events[g_nevents * 3] = type;
    g_events[g_nevents * 3 + 1] = x;
    g_events[g_nevents * 3 + 2] = y;
    g_nevents++;
    jmon_exit(g_eventlock);
}

static uint32_t g_native_EventHandleClet, g_native_ApplyWaitingKeyPressIfAny;

/* ---------------------------------------------------------------- com/ftt/gof2d/GofManager */
JM(gm_GetFileBridge) { UNUSED; return JRET_OBJ(g_filebr); }
JM(gm_GetAudioBridge) { UNUSED; return JRET_OBJ(g_audiobr); }
JM(gm_GetFontBridge) { UNUSED; return JRET_OBJ(g_fontbr); }
JM(gm_GetDeviceBridge) { UNUSED; return JRET_OBJ(g_devbr); }
JM(gm_GetGLSurface) { UNUSED; return JRET_OBJ(g_glview); }
JM(gm_getAppLock) { UNUSED; return JRET_OBJ(g_applock); }
JM(gm_getThreadState) { UNUSED; return (uint32_t)g_thread_state; }
JM(gm_getMainPackageName) { UNUSED; return JRET_OBJ(jstr_new(JAVA_PACKAGE)); }
JM(gm_procTouch) {
    UNUSED;
    ui_game_tick(c); /* modern UI: queued actions + state snapshot, on the game thread */
    jmon_enter(g_eventlock);
    bot_tick(c);
    /* The original delivers the whole batch (GofManager.procTouch). A tap shorter than a frame (about 37 ms) then gives
     * cAndInput::SetTouchEvt @0016394c a press and a release in the same frame; cGameInput::Update @00126508 sees the
     * touch no longer held, and cflUi::ProcessImpl @00154048 takes its release branch, which only clicks an element an
     * earlier frame's press captured: the tap is lost (the original's own behaviour, about half of 10-30 ms taps on a
     * phone). The port stops before a release that follows a press of the same batch and delivers it, with whatever
     * came after it, next frame, so every tap is seen pressed for at least one frame. */
    int n = 0, pressed = 0;
    for (; n < g_nevents; n++) {
        int type = g_events[n * 3];
        if (type == 2 && pressed) break;
        if (type == 1) pressed = 1;
        uint32_t args[5] = {jni_env(), HPTR(g_gofmgr), (uint32_t)type, (uint32_t)g_events[n * 3 + 1], (uint32_t)g_events[n * 3 + 2]};
        rc_call(c, g_native_EventHandleClet, 5, args);
    }
    memmove(g_events, g_events + n * 3, (size_t)(g_nevents - n) * 3 * sizeof g_events[0]);
    g_nevents -= n;
    jmon_exit(g_eventlock);
    uint32_t args[2] = {jni_env(), HPTR(g_gofmgr)};
    rc_call(c, g_native_ApplyWaitingKeyPressIfAny, 2, args);
    return 0;
}
/* requestQuit -> Activity.finish(): the activity's onDestroy stops the game thread */
JM(gm_requestQuit) { UNUSED; host_log("[java] requestQuit"); java_request_quit(); return 0; }
JM(gm_openUrl) { UNUSED; host_log("[java] openUrl %s", JSTR(a[0])); return 0; }
JM(gm_goAppStore) { UNUSED; host_log("[java] goAppStore %s", JSTR(a[0])); return 0; }
/* IFunterLibRequestListener hooks (memory hack detection): no listener findings */
JM(gm_zero) { UNUSED; return 0; }

/* ---------------------------------------------------------------- com/ftt/tar/Tar (the Activity) */
static JObj *g_activity;
static uint32_t g_native_setStoreKitEvent;
JM(gm_GetActivity) { UNUSED; return JRET_OBJ(g_activity); }
/* com/ftt/google/GoogleActivity.IAP_GoStore: without a billing service iapLibInitFail is set and the
 * activity answers tar.setStoreKitEvent(0x408) synchronously */
JM(tar_IAP_GoStore) {
    UNUSED;
    host_log("[java] IAP_GoStore item=%d -> billing unavailable (0x408)", (int)a[0]);
    uint32_t args[3] = {jni_env(), HPTR(g_activity), 0x408};
    rc_call(c, g_native_setStoreKitEvent, 3, args);
    return 0;
}

/* ---------------------------------------------------------------- java/lang/Object */
JM(obj_wait) { UNUSED; jmon_wait(self, 0); return 0; }
/* EH_TURBO=1 (test runs only): cNixSystem::Run's per-frame appLock.wait(interval) returns at once.
 * The dialogue SKIP (ui/talkctl.cpp) does the same while it runs a scene to its end: java_set_turbo(1). */
static int g_turbo = -1;
static volatile int g_skip_turbo;
static int (*volatile g_turbo_keep)(void);
void java_set_turbo(int on, int (*keep)(void)) { g_turbo_keep = keep; g_skip_turbo = on; }
int java_turbo(void) { return g_skip_turbo; }
volatile uint32_t g_game_frames; /* cNixSystem::Run frames (its per-frame appLock.wait), for EH_FPS */
volatile int32_t g_game_wait_ms;  /* the last per-frame wait the game asked for: 0 or less = behind */
JM(obj_wait_ms) {
    UNUSED;
    if (self == g_applock) { g_game_frames++; g_game_wait_ms = (int32_t)a[0]; }
    if (g_turbo < 0) g_turbo = getenv("EH_TURBO") != NULL;
    /* checked on the game thread every frame, so not one frame past the skipped scene runs unpaced */
    if (g_skip_turbo && self == g_applock && g_turbo_keep && !g_turbo_keep()) g_skip_turbo = 0;
    if ((g_turbo || g_skip_turbo) && self == g_applock) { jmon_exit(self); SDL_Delay(0); jmon_enter(self); return 0; }
    jmon_wait(self, (int64_t)a[0]);
    return 0;
}
JM(obj_notifyAll) { UNUSED; jmon_notify_all(self); return 0; }
JM(obj_init) { UNUSED; return 0; }

/* ---------------------------------------------------------------- com/ftt/gof2d/file/GofFileBridge */
JM(fb_getApkPath) { UNUSED; return JRET_OBJ(jstr_new(g_apk_path)); }
JM(fb_getDocumentsPath) { UNUSED; return JRET_OBJ(jstr_new(g_data_dir)); }
/* GofFileBridge.getApkFileOffseet/Length: AssetManager.openFd(name) (Tar.onCreate clears
 * isUseAssetFilePngExtension, so no ".png" is appended), else res/raw/<name before '.', lower case> */
static int asset_lookup(const char *name, uint32_t *off, uint32_t *size) {
    char buf[512];
    if (!name) return 0;
    snprintf(buf, sizeof buf, "assets/%s", name);
    if (apk_find(buf, off, size)) return 1;
    char res[256];
    const char *dot = strchr(name, '.');
    size_t L = dot && dot > name ? (size_t)(dot - name) : strlen(name);
    if (L >= sizeof res) L = sizeof res - 1;
    for (size_t i = 0; i < L; i++) res[i] = (char)tolower((unsigned char)name[i]);
    res[L] = 0;
    return apk_find_raw(res, off, size);
}
JM(fb_getApkFileOffseet) { UNUSED; uint32_t o, s; return asset_lookup(JSTR(a[0]), &o, &s) ? o : (uint32_t)-1; }
JM(fb_getApkFileLength) { UNUSED; uint32_t o, s; return asset_lookup(JSTR(a[0]), &o, &s) ? s : (uint32_t)-1; }
static int doc_exists(const char *name, long *size) {
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", g_data_dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    if (size) *size = ftell(f);
    fclose(f);
    return 1;
}
/* isExistFile(name, flags): flags&2 documents dir -> 2, flags&1 apk asset -> 1, else -1 */
JM(fb_isExistFile) {
    UNUSED;
    int flags = (int)a[1];
    if ((flags & 2) && doc_exists(JSTR(a[0]), NULL)) return 2;
    uint32_t o, s;
    if ((flags & 1) && asset_lookup(JSTR(a[0]), &o, &s)) return 1;
    return (uint32_t)-1;
}
/* getFileSize(name, flags): documents file length, else apk asset length, else -1 */
JM(fb_getFileSize) {
    UNUSED;
    int flags = (int)a[1];
    long sz;
    if ((flags & 2) && doc_exists(JSTR(a[0]), &sz)) return (uint32_t)sz;
    uint32_t o, s;
    if ((flags & 1)) return asset_lookup(JSTR(a[0]), &o, &s) ? s : (uint32_t)-1;
    return (uint32_t)-1;
}
/* getFreeSpace: StatFs(availableBlocks * blockSize) as int */
JM(fb_getFreeSpace) { UNUSED; return 0x40000000; }

/* ---------------------------------------------------------------- com/ftt/gof2d/device/GofDeviceBridge */
JM(db_getEsnNumber) { UNUSED; return JRET_OBJ(jstr_new("NotFoundEsnNumber")); }
JM(db_getModelName) { UNUSED; return JRET_OBJ(jstr_new("SDL3")); }
JM(db_getOSVersion) { UNUSED; return JRET_OBJ(jstr_new("8")); }
JM(db_getPhoneNumber) { UNUSED; return JRET_OBJ(jstr_new("01000000000")); }
/* getNetEnv: no active network */
JM(db_getNetEnv) { UNUSED; return 0; }
static char g_jappid[256];
JM(db_getjAppID) { UNUSED; return JRET_OBJ(jstr_new(g_jappid)); }
JM(db_setjAppID) { UNUSED; snprintf(g_jappid, sizeof g_jappid, "%s", JSTR(a[0]) ? JSTR(a[0]) : ""); return 0; }

/* ---------------------------------------------------------------- com/ftt/gof2d/utils/GofStringUtil
 * decode(BB)C : EUC-KR (charset 1) / EUC-JP (2) two-byte sequence to one UTF-16 unit, 0 if invalid */
JM(su_init) { UNUSED; jobj_field_set(self, "charset", (int64_t)a[0]); return 0; }
JM(su_decode) {
    UNUSED;
    char b[2] = {(char)a[0], (char)a[1]};
    if (jobj_field_get(self, "charset") == 2) return 0; /* EUC-JP: Japanese build only, not used by this APK */
    return cp949_decode2((unsigned char)b[0], (unsigned char)b[1]);
}

void java_register_all(void) {
    const char *GM = "com/ftt/gof2d/GofManager";
    jni_register(GM, "GetFileBridge", NULL, gm_GetFileBridge);
    jni_register(GM, "GetAudioBridge", NULL, gm_GetAudioBridge);
    jni_register(GM, "GetFontBridge", NULL, gm_GetFontBridge);
    jni_register(GM, "GetDeviceBridge", NULL, gm_GetDeviceBridge);
    jni_register(GM, "GetGLSurface", NULL, gm_GetGLSurface);
    jni_register(GM, "getAppLock", NULL, gm_getAppLock);
    jni_register(GM, "getThreadState", NULL, gm_getThreadState);
    jni_register(GM, "getMainPackageName", NULL, gm_getMainPackageName);
    jni_register(GM, "procTouch", NULL, gm_procTouch);
    jni_register(GM, "requestQuit", NULL, gm_requestQuit);
    jni_register(GM, "openUrl", NULL, gm_openUrl);
    jni_register(GM, "goAppStore", NULL, gm_goAppStore);
    jni_register(GM, "checkMemoryHack", NULL, gm_zero);
    jni_register(GM, "hasMemoryHack", NULL, gm_zero);
    jni_register(GM, "addMemoryHack", NULL, gm_zero);
    jni_register(GM, "resetMemoryHack", NULL, gm_zero);
    jni_register(GM, "releaseInstance", NULL, gm_zero);
    jni_register(GM, "GetActivity", NULL, gm_GetActivity);
    jni_register(NULL, "IAP_GoStore", NULL, tar_IAP_GoStore);
    jni_register(NULL, "wait", "()V", obj_wait);
    jni_register(NULL, "wait", "(J)V", obj_wait_ms);
    jni_register(NULL, "notifyAll", "()V", obj_notifyAll);
    jni_register("java/lang/Object", "<init>", NULL, obj_init);
    const char *FB = "com/ftt/gof2d/file/GofFileBridge";
    jni_register(FB, "getApkPath", NULL, fb_getApkPath);
    jni_register(FB, "getDocumentsPath", NULL, fb_getDocumentsPath);
    jni_register(FB, "getApkFileOffseet", NULL, fb_getApkFileOffseet);
    jni_register(FB, "getApkFileLength", NULL, fb_getApkFileLength);
    jni_register(FB, "isExistFile", NULL, fb_isExistFile);
    jni_register(FB, "getFileSize", NULL, fb_getFileSize);
    jni_register(FB, "getFreeSpace", NULL, fb_getFreeSpace);
    const char *DB = "com/ftt/gof2d/device/GofDeviceBridge";
    jni_register(DB, "getEsnNumber", NULL, db_getEsnNumber);
    jni_register(DB, "getModelName", NULL, db_getModelName);
    jni_register(DB, "getOSVersion", NULL, db_getOSVersion);
    jni_register(DB, "getPhoneNumber", NULL, db_getPhoneNumber);
    jni_register(DB, "getNetEnv", NULL, db_getNetEnv);
    jni_register(DB, "getjAppID", NULL, db_getjAppID);
    jni_register(DB, "setjAppID", NULL, db_setjAppID);
    jni_register("com/ftt/gof2d/utils/GofStringUtil", "<init>", NULL, su_init);
    jni_register("com/ftt/gof2d/utils/GofStringUtil", "decode", NULL, su_decode);
    java_font_register();
    java_audio_register();
    java_net_register();
    java_text_register();
    java_alert_register();

    g_gofmgr = GP(jni_new_object(GM));
    g_filebr = GP(jni_new_object(FB));
    g_audiobr = GP(jni_new_object("com/ftt/gof2d/audio/GofAudioBridge"));
    g_fontbr = GP(jni_new_object("com/ftt/gof2d/font/GofFontBridge"));
    g_devbr = GP(jni_new_object(DB));
    g_glview = GP(jni_new_object("com/ftt/gof2d/GLView/MyGLSurfaceView"));
    g_applock = GP(jni_new_object("java/lang/Object"));
    g_eventlock = GP(jni_new_object("java/lang/Object"));
    g_activity = GP(jni_new_object("com/ftt/tar/Tar"));
    g_native_setStoreKitEvent = rc_sym("Java_com_ftt_tar_Tar_setStoreKitEvent") & ~1u;
    g_native_EventHandleClet = rc_sym("Java_com_ftt_gof2d_GofManager_EventHandleClet") & ~1u;
    g_native_ApplyWaitingKeyPressIfAny = rc_sym("Java_com_ftt_gof2d_GofManager_ApplyWaitingKeyPressIfAny") & ~1u;
}
