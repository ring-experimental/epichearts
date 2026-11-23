/* Host implementations of the libc / libm / libgcc / pthread imports of libtar-android.so.
 * Calling convention: AAPCS soft-float (args r0-r3 then stack, 64-bit values in even/odd pairs). */
#include "host.h"
#include "gas.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define IMP(name) void imp_##name(Cpu *c)
#define A0 c->r[0]
#define A1 c->r[1]
#define A2 c->r[2]
#define A3 c->r[3]

static inline float f32(uint32_t x) { float f; memcpy(&f, &x, 4); return f; }
static inline uint32_t u32f(float f) { uint32_t x; memcpy(&x, &f, 4); return x; }
static inline double f64(uint32_t lo, uint32_t hi) { uint64_t x = ((uint64_t)hi << 32) | lo; double d; memcpy(&d, &x, 8); return d; }
static inline uint64_t u64d(double d) { uint64_t x; memcpy(&x, &d, 8); return x; }

/* ---------------- soft-float / integer helpers (libgcc __aeabi_*) ---------------- */
IMP(__aeabi_fadd) { RET(c, u32f(f32(A0) + f32(A1))); }
IMP(__aeabi_fsub) { RET(c, u32f(f32(A0) - f32(A1))); }
IMP(__aeabi_fmul) { RET(c, u32f(f32(A0) * f32(A1))); }
IMP(__aeabi_fdiv) { RET(c, u32f(f32(A0) / f32(A1))); }
IMP(__aeabi_fcmpge) { RET(c, f32(A0) >= f32(A1)); }
static int32_t sat_f2i(double d) {
    /* ARM VFP/libgcc semantics: saturate, NaN -> 0 */
    if (d != d) return 0;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return (int32_t)0x80000000;
    return (int32_t)d;
}
IMP(__aeabi_f2iz) { RET(c, (uint32_t)sat_f2i(f32(A0))); }
IMP(__aeabi_i2f) { RET(c, u32f((float)(int32_t)A0)); }
IMP(__aeabi_i2d) { RET64(c, u64d((double)(int32_t)A0)); }
IMP(__aeabi_f2d) { RET64(c, u64d((double)f32(A0))); }
IMP(__aeabi_d2f) { RET(c, u32f((float)f64(A0, A1))); }
IMP(__aeabi_d2iz) { RET(c, (uint32_t)sat_f2i(f64(A0, A1))); }
IMP(__aeabi_dadd) { RET64(c, u64d(f64(A0, A1) + f64(A2, A3))); }
IMP(__aeabi_dsub) { RET64(c, u64d(f64(A0, A1) - f64(A2, A3))); }
IMP(__aeabi_dmul) { RET64(c, u64d(f64(A0, A1) * f64(A2, A3))); }
IMP(__aeabi_ddiv) { RET64(c, u64d(f64(A0, A1) / f64(A2, A3))); }
/* UNVERIFIED: division by zero follows libgcc __aeabi_idiv0 default (returns 0) */
IMP(__aeabi_idiv) { int32_t a = (int32_t)A0, b = (int32_t)A1; RET(c, b ? (b == -1 ? (uint32_t)(-(int64_t)a) : (uint32_t)(a / b)) : 0); }
IMP(__aeabi_uidiv) { RET(c, A1 ? A0 / A1 : 0); }
IMP(__aeabi_idivmod) {
    int32_t a = (int32_t)A0, b = (int32_t)A1;
    if (!b) { c->r[0] = 0; c->r[1] = (uint32_t)a; return; }
    if (b == -1) { c->r[0] = (uint32_t)(-(int64_t)a); c->r[1] = 0; return; }
    c->r[0] = (uint32_t)(a / b); c->r[1] = (uint32_t)(a % b);
}
IMP(__aeabi_uidivmod) {
    if (!A1) { c->r[1] = A0; c->r[0] = 0; return; }
    uint32_t q = A0 / A1, r = A0 % A1; c->r[0] = q; c->r[1] = r;
}
IMP(__aeabi_ldivmod) {
    int64_t a = (int64_t)(((uint64_t)A1 << 32) | A0), b = (int64_t)(((uint64_t)A3 << 32) | A2);
    int64_t q = 0, r = a;
    if (b) { q = a / b; r = a % b; }
    c->r[0] = (uint32_t)q; c->r[1] = (uint32_t)((uint64_t)q >> 32);
    c->r[2] = (uint32_t)r; c->r[3] = (uint32_t)((uint64_t)r >> 32);
}
IMP(__aeabi_lmul) {
    uint64_t a = ((uint64_t)A1 << 32) | A0, b = ((uint64_t)A3 << 32) | A2;
    RET64(c, a * b);
}
IMP(__aeabi_unwind_cpp_pr0) { rc_trap(c, 0, "__aeabi_unwind_cpp_pr0 called"); }
IMP(__aeabi_unwind_cpp_pr1) { rc_trap(c, 0, "__aeabi_unwind_cpp_pr1 called"); }
IMP(__cxa_pure_virtual) { rc_trap(c, c->r[14] - IMG, "pure virtual call"); }
IMP(__stack_chk_fail) { rc_trap(c, c->r[14] - IMG, "stack check failed"); }
IMP(__cxa_atexit) { RET(c, 0); }
/* __aeabi_atexit @ vaddr 163fbc is ARM code in the .so: it forwards to __cxa_atexit(arg, func, dso).
 * Program-exit destructors are not needed by the port. */
IMP(__aeabi_atexit) { RET(c, 0); }
IMP(__dso_handle) { rc_trap(c, 0, "__dso_handle called"); }
IMP(__stack_chk_guard) { rc_trap(c, 0, "__stack_chk_guard called"); }

/* ---------------- libm ---------------- */
IMP(sinf) { RET(c, u32f(sinf(f32(A0)))); }
IMP(cosf) { RET(c, u32f(cosf(f32(A0)))); }
IMP(tanf) { RET(c, u32f(tanf(f32(A0)))); }
IMP(atanf) { RET(c, u32f(atanf(f32(A0)))); }
IMP(floorf) { RET(c, u32f(floorf(f32(A0)))); }
IMP(ceilf) { RET(c, u32f(ceilf(f32(A0)))); }

/* ---------------- memory / strings ---------------- */
IMP(malloc) { RET(c, HPTR(gas_malloc(A0 ? A0 : 1))); }
IMP(free) { if (A0) gas_free(GP(A0)); }
IMP(realloc) { RET(c, HPTR(A0 ? gas_realloc(GP(A0), A1 ? A1 : 1) : gas_malloc(A1 ? A1 : 1))); }
IMP(memcpy) { memcpy(GP(A0), GP(A1), A2); }
IMP(memmove) { memmove(GP(A0), GP(A1), A2); }
IMP(memset) { memset(GP(A0), (int)A1, A2); }
IMP(memcmp) { RET(c, memcmp(GP(A0), GP(A1), A2)); }
IMP(strlen) { RET(c, strlen(GP(A0))); }
IMP(strcpy) { strcpy(GP(A0), GP(A1)); }
IMP(strncpy) { strncpy(GP(A0), GP(A1), A2); }
IMP(strcat) { strcat(GP(A0), GP(A1)); }
IMP(strcmp) { RET(c, strcmp(GP(A0), GP(A1))); }
IMP(strchr) { RET(c, HPTR(strchr(GP(A0), (int)A1))); }
IMP(strstr) { RET(c, HPTR(strstr(GP(A0), GP(A1)))); }

static int *g_errno_p; /* errno lives in guest memory (the guest reads it through __errno()) */
static int *errno_cell(void) { if (!g_errno_p) g_errno_p = gas_calloc(1, sizeof(int)); return g_errno_p; }
#define g_errno (*errno_cell())
IMP(__errno) { RET(c, HPTR(errno_cell())); }

/* ---------------- printf family ---------------- */
uint32_t as_u32(ArgStream *a) {
    if (a->va) { uint32_t v = M32(a->va); a->va += 4; return v; }
    if (a->next_reg < 4) return a->c->r[a->next_reg++];
    uint32_t v = M32(a->stack); a->stack += 4; return v;
}
uint64_t as_u64(ArgStream *a) {
    uint32_t lo, hi;
    if (a->va) {
        a->va = (a->va + 7) & ~7u;
        lo = M32(a->va); hi = M32(a->va + 4); a->va += 8;
    } else if (a->next_reg < 4) {
        if (a->next_reg & 1) a->next_reg++;
        if (a->next_reg < 4) { lo = a->c->r[a->next_reg]; hi = a->c->r[a->next_reg + 1]; a->next_reg += 2; }
        else goto stk;
    } else {
    stk:
        a->stack = (a->stack + 7) & ~7u;
        lo = M32(a->stack); hi = M32(a->stack + 4); a->stack += 8;
    }
    return ((uint64_t)hi << 32) | lo;
}

int guest_vformat(char *out, size_t outsz, const char *fmt, ArgStream *a) {
    size_t o = 0;
    char spec[32], tmp[512];
    while (*fmt) {
        if (*fmt != '%') { if (o + 1 < outsz) out[o] = *fmt; o++; fmt++; continue; }
        const char *s = fmt++;
        if (*fmt == '%') { if (o + 1 < outsz) out[o] = '%'; o++; fmt++; continue; }
        while (strchr("-+ #0", *fmt)) fmt++;
        int star_w = 0, star_p = 0;
        if (*fmt == '*') { star_w = 1; fmt++; } else while (*fmt >= '0' && *fmt <= '9') fmt++;
        if (*fmt == '.') { fmt++; if (*fmt == '*') { star_p = 1; fmt++; } else while (*fmt >= '0' && *fmt <= '9') fmt++; }
        int lng = 0;
        while (strchr("hlLqjzt", *fmt)) { if (*fmt == 'l' || *fmt == 'q' || *fmt == 'L') lng++; fmt++; }
        char conv = *fmt++;
        size_t sl = (size_t)(fmt - s);
        if (sl >= sizeof spec - 4) sl = sizeof spec - 4;
        /* rebuild spec without length modifiers */
        size_t k = 0;
        for (size_t i = 0; i < sl - 1; i++) if (!strchr("hlLqjzt", s[i])) spec[k++] = s[i];
        int w = star_w ? (int)as_u32(a) : 0, p = star_p ? (int)as_u32(a) : 0;
        int n;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c':
            if (lng >= 2) {
                spec[k++] = 'l'; spec[k++] = 'l'; spec[k++] = conv; spec[k] = 0;
                uint64_t v = as_u64(a);
                n = star_w && star_p ? snprintf(tmp, sizeof tmp, spec, w, p, v) : star_w ? snprintf(tmp, sizeof tmp, spec, w, v) : star_p ? snprintf(tmp, sizeof tmp, spec, p, v) : snprintf(tmp, sizeof tmp, spec, v);
            } else {
                spec[k++] = conv; spec[k] = 0;
                uint32_t v = as_u32(a);
                n = star_w && star_p ? snprintf(tmp, sizeof tmp, spec, w, p, v) : star_w ? snprintf(tmp, sizeof tmp, spec, w, v) : star_p ? snprintf(tmp, sizeof tmp, spec, p, v) : snprintf(tmp, sizeof tmp, spec, v);
            }
            break;
        case 'p':
            n = snprintf(tmp, sizeof tmp, "%x", as_u32(a));
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            spec[k++] = conv; spec[k] = 0;
            uint64_t bits = as_u64(a);
            double d; memcpy(&d, &bits, 8);
            n = star_w && star_p ? snprintf(tmp, sizeof tmp, spec, w, p, d) : star_w ? snprintf(tmp, sizeof tmp, spec, w, d) : star_p ? snprintf(tmp, sizeof tmp, spec, p, d) : snprintf(tmp, sizeof tmp, spec, d);
            break;
        }
        case 's': {
            spec[k++] = 's'; spec[k] = 0;
            uint32_t v = as_u32(a);
            const char *str = v ? (const char *)GP(v) : "(null)";
            /* long strings: copy directly */
            if (!star_w && !star_p && k == 2) {
                size_t L = strlen(str);
                for (size_t i = 0; i < L; i++) { if (o + 1 < outsz) out[o] = str[i]; o++; }
                continue;
            }
            n = star_w && star_p ? snprintf(tmp, sizeof tmp, spec, w, p, str) : star_w ? snprintf(tmp, sizeof tmp, spec, w, str) : star_p ? snprintf(tmp, sizeof tmp, spec, p, str) : snprintf(tmp, sizeof tmp, spec, str);
            break;
        }
        default:
            n = 0;
        }
        if (n < 0) n = 0;
        if (n > (int)sizeof tmp - 1) n = sizeof tmp - 1;
        for (int i = 0; i < n; i++) { if (o + 1 < outsz) out[o] = tmp[i]; o++; }
    }
    if (outsz) out[o < outsz ? o : outsz - 1] = 0;
    return (int)o;
}

IMP(sprintf) {
    ArgStream a = {c, 2, c->r[13], 0};
    char *dst = GP(A0);
    RET(c, guest_vformat(dst, 0x7fffffff, GP(A1), &a));
}
IMP(puts) { host_log("[guest] %s", (const char *)GP(A0)); RET(c, 1); }

/* ---------------- files ---------------- */
IMP(fopen) {
    char buf[1024];
    const char *p = host_map_path(GP(A0), buf, sizeof buf);
    const char *mode = GP(A1);
    char m[8]; size_t k = 0;
    for (; *mode && k < 5; mode++) if (*mode != 'b') m[k++] = *mode;
    m[k++] = 'b'; m[k] = 0;
#ifdef __EMSCRIPTEN__
    /* the original APK lives in memory (main.c): the game's reads of it never wait on the page's thread */
    FILE *f = !strcmp(p, g_apk_path) && m[0] == 'r' ? host_open_apk() : fopen(p, m);
#else
    FILE *f = fopen(p, m);
#endif
    if (!f) g_errno = ENOENT;
    { extern int g_jtrace; if (g_jtrace || getenv("EH_FTRACE")) host_log("[fopen] %s (%s) -> %s", p, m, f ? "ok" : "FAILED"); }
    RET(c, f ? gas_handle(f) : 0);
}
#define FH(h) ((FILE *)gas_unhandle(h))
static void dir_forget(FILE *f);
IMP(fclose) { FILE *f = FH(A0); dir_forget(f); RET(c, f ? fclose(f) : (uint32_t)-1); }
/* Update streams ("r+", "w+"): bionic allows switching between reading and writing directly, the MSVC CRT
 * (like ISO C) requires a positioning call in between, or the write is dropped. Track the last direction. */
#define MAX_FILES 64
static struct { FILE *f; int last; } g_fdir[MAX_FILES];
static SDL_SpinLock g_fdir_lock;
static void dir_switch(FILE *f, int dir) {
    SDL_LockSpinlock(&g_fdir_lock);
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (g_fdir[i].f == f) { slot = i; break; }
        if (!g_fdir[i].f && slot < 0) slot = i;
    }
    if (slot >= 0) {
        if (g_fdir[slot].f == f && g_fdir[slot].last && g_fdir[slot].last != dir) fseek(f, 0, SEEK_CUR);
        g_fdir[slot].f = f;
        g_fdir[slot].last = dir;
    }
    SDL_UnlockSpinlock(&g_fdir_lock);
}
static void dir_forget(FILE *f) {
    SDL_LockSpinlock(&g_fdir_lock);
    for (int i = 0; i < MAX_FILES; i++) if (g_fdir[i].f == f) g_fdir[i].f = NULL;
    SDL_UnlockSpinlock(&g_fdir_lock);
}
IMP(fread) { dir_switch(FH(A3), 1); RET(c, fread(GP(A0), A1, A2, FH(A3))); }
IMP(fwrite) { dir_switch(FH(A3), 2); RET(c, fwrite(GP(A0), A1, A2, FH(A3))); }
IMP(fseek) { dir_forget(FH(A0)); RET(c, fseek(FH(A0), (long)(int32_t)A1, (int)A2)); }
IMP(ftell) { RET(c, ftell(FH(A0))); }
IMP(remove) { char buf[1024]; RET(c, remove(host_map_path(GP(A0), buf, sizeof buf))); }
IMP(rename) {
    char a[1024], b[1024];
    const char *pa = host_map_path(GP(A0), a, sizeof a), *pb = host_map_path(GP(A1), b, sizeof b);
    remove(pb); /* POSIX rename replaces the target */
    int r = rename(pa, pb);
    if (getenv("EH_FTRACE")) host_log("[rename] %s -> %s = %d", pa, pb, r);
    RET(c, r);
}
IMP(close) { RET(c, (uint32_t)-1); }

/* ---------------- time ---------------- */
IMP(time) {
    uint32_t t = (uint32_t)time(NULL);
    if (A0) M32(A0) = t;
    RET(c, t);
}
IMP(gettimeofday) {
    SDL_Time now; SDL_GetCurrentTime(&now);
    M32(A0) = (uint32_t)(now / 1000000000LL);
    M32(A0 + 4) = (uint32_t)((now / 1000) % 1000000);
    RET(c, 0);
}
typedef struct { int32_t tm[9]; int32_t gmtoff; uint32_t zone; } BionicTm; /* bionic struct tm layout */
static BionicTm *g_tmp;
IMP(localtime) {
    time_t t = (time_t)(int32_t)M32(A0);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    int32_t v[9] = {lt.tm_sec, lt.tm_min, lt.tm_hour, lt.tm_mday, lt.tm_mon, lt.tm_year, lt.tm_wday, lt.tm_yday, lt.tm_isdst};
    if (!g_tmp) g_tmp = gas_calloc(1, sizeof *g_tmp); /* returned to the guest: guest memory */
    memcpy(g_tmp->tm, v, sizeof v);
    g_tmp->gmtoff = 0; g_tmp->zone = 0;
    RET(c, HPTR(g_tmp));
}
IMP(usleep) { SDL_DelayNS((Uint64)A0 * 1000); RET(c, 0); }
IMP(abort) { rc_trap(c, c->r[14] - IMG, "abort()"); }
IMP(exit) { host_log("guest exit(%d)", (int)A0); exit((int)A0); }

/* ---------------- network: the port runs offline ---------------- */
/* UNVERIFIED: offline behaviour; every socket call fails like an unreachable network */
IMP(socket) { g_errno = ENETUNREACH; RET(c, (uint32_t)-1); }
IMP(connect) { g_errno = ENETUNREACH; RET(c, (uint32_t)-1); }
IMP(send) { RET(c, (uint32_t)-1); }
IMP(recv) { RET(c, (uint32_t)-1); }
IMP(select) { RET(c, (uint32_t)-1); }
IMP(shutdown) { RET(c, (uint32_t)-1); }
IMP(gethostbyname) { RET(c, 0); }

/* ---------------- pthreads on SDL3 ---------------- */
/* bionic pthread_mutex_t / pthread_cond_t are one 32-bit word; we keep an SDL object pointer there. */
static SDL_SpinLock g_lazy_lock;
static SDL_Mutex *mtx_of(uint32_t p) {
    if (!M32(p) || (M32(p) & 0xffff0000u) == 0) {
        SDL_LockSpinlock(&g_lazy_lock);
        if (!M32(p) || (M32(p) & 0xffff0000u) == 0) M32(p) = gas_handle(SDL_CreateMutex());
        SDL_UnlockSpinlock(&g_lazy_lock);
    }
    return gas_unhandle(M32(p));
}
static SDL_Condition *cond_of(uint32_t p) {
    if (!M32(p)) {
        SDL_LockSpinlock(&g_lazy_lock);
        if (!M32(p)) M32(p) = gas_handle(SDL_CreateCondition());
        SDL_UnlockSpinlock(&g_lazy_lock);
    }
    return gas_unhandle(M32(p));
}
IMP(pthread_mutex_init) { M32(A0) = gas_handle(SDL_CreateMutex()); RET(c, 0); }
IMP(pthread_mutex_destroy) { RET(c, 0); }
IMP(pthread_mutex_lock) { SDL_LockMutex(mtx_of(A0)); RET(c, 0); }
IMP(pthread_mutex_unlock) { SDL_UnlockMutex(mtx_of(A0)); RET(c, 0); }
IMP(pthread_cond_init) { M32(A0) = gas_handle(SDL_CreateCondition()); RET(c, 0); }
IMP(pthread_cond_destroy) { RET(c, 0); }
IMP(pthread_cond_wait) { SDL_WaitCondition(cond_of(A0), mtx_of(A1)); RET(c, 0); }
IMP(pthread_cond_broadcast) { SDL_BroadcastCondition(cond_of(A0)); RET(c, 0); }

typedef struct { uint32_t fn, arg; } ThreadStart;
static int SDLCALL thread_main(void *p) {
    ThreadStart ts = *(ThreadStart *)p;
    free(p);
    Cpu *c = rc_new_cpu(1 << 20);
    uint32_t a = ts.arg;
    rc_call(c, (ts.fn & ~1u) - IMG, 1, &a);
    return 0;
}
IMP(pthread_create) {
    ThreadStart *ts = malloc(sizeof *ts);
    ts->fn = A2; ts->arg = A3;
    SDL_Thread *t = SDL_CreateThread(thread_main, "guest", ts);
    if (A0) M32(A0) = gas_handle(t);
    if (t) SDL_DetachThread(t);
    RET(c, t ? 0 : 11);
}
#define MAX_KEYS 64
static SDL_TLSID g_keys[MAX_KEYS];
static int g_nkeys;
IMP(pthread_key_create) { M32(A0) = (uint32_t)g_nkeys++; RET(c, 0); }
IMP(pthread_getspecific) { RET(c, A0 < MAX_KEYS ? HPTR(SDL_GetTLS(&g_keys[A0])) : 0); }
IMP(pthread_setspecific) { if (A0 < MAX_KEYS) SDL_SetTLS(&g_keys[A0], GP(A1), NULL); RET(c, 0); }
