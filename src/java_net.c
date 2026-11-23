/* com/ftt/tar/SimpleBackgroundNet (classes.dex). The HTTP request runs on a Java thread and its
 * outcome is written back through the native WriteBackSimpleNetResult(I) into the int* that the
 * caller passed (static field __simpleNetResult; natives @ghidra 000b44ec / 000b452c).
 * The port has no network: the request fails like an exception in sendDevicePPILogCB /
 * sendDeviceLogCB, whose catch blocks write back 2. */
#include "java.h"
#include <SDL3/SDL.h>

static const char *SBN = "com/ftt/tar/SimpleBackgroundNet";
static uint32_t g_init_native, g_writeback_native;
static SDL_AtomicInt g_inst; /* SimpleBackgroundNet.__inst != null */

static int SDLCALL net_thread(void *p) {
    (void)p;
    Cpu *c = rc_new_cpu(256 << 10);
    SDL_SetAtomicInt(&g_inst, 0);   /* __inst = null */
    uint32_t args[3] = {jni_env(), HPTR(jclass_get(SBN)), 2};
    rc_call(c, g_writeback_native, 3, args);
    return 0;
}

static uint64_t start_request(Cpu *c, uint32_t result_ptr) {
    /* sput __simpleNetResult; InitSimpleNetResult() */
    jni_set_static_int("__simpleNetResult", (int32_t)result_ptr);
    uint32_t args[2] = {jni_env(), HPTR(jclass_get(SBN))};
    rc_call(c, g_init_native, 2, args);
    if (!SDL_CompareAndSwapAtomicInt(&g_inst, 0, 1)) return 0; /* a request is already running */
    SDL_Thread *t = SDL_CreateThread(net_thread, "SimpleBackgroundNet", NULL);
    SDL_DetachThread(t);
    return 1;
}

/* sendDevicePPILog(II[BI)Z : result pointer is the 4th argument */
static uint64_t sbn_sendDevicePPILog(Cpu *c, JObj *self, const uint64_t *a) { (void)self; return start_request(c, (uint32_t)a[3]); }
/* sendDeviceLog(I[BI)Z : result pointer is the 3rd argument */
static uint64_t sbn_sendDeviceLog(Cpu *c, JObj *self, const uint64_t *a) { (void)self; return start_request(c, (uint32_t)a[2]); }

void java_net_register(void) {
    jni_register(SBN, "sendDevicePPILog", NULL, sbn_sendDevicePPILog);
    jni_register(SBN, "sendDeviceLog", NULL, sbn_sendDeviceLog);
    g_init_native = rc_sym("Java_com_ftt_tar_SimpleBackgroundNet_InitSimpleNetResult") & ~1u;
    g_writeback_native = rc_sym("Java_com_ftt_tar_SimpleBackgroundNet_WriteBackSimpleNetResult") & ~1u;
}
