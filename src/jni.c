/* Minimal Java VM for libtar-android.so: a JNIEnv / JavaVM function table whose entries are host
 * functions, an object model (instances, strings, arrays, classes) and a registry of Java methods
 * that are reimplemented in C (java_*.c) from the original classes.dex (smali). */
#include "host.h"
#include "jni.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include "platform.h"
#include "gas.h"
#include <stdlib.h>
#include <string.h>

/* JNIEnv / JavaVM: the guest dereferences env -> function table, so both live in guest memory (gas.h) */
static uint32_t *g_env_cellp, *g_vm_cellp, *g_env_table, *g_vm_table;
#define g_env_cell (*g_env_cellp)
#define g_vm_cell (*g_vm_cellp)
int g_jtrace;
static SDL_Mutex *g_jlock;

uint32_t jni_env(void) { return HPTR(g_env_cellp); }
uint32_t jni_vm(void) { return HPTR(g_vm_cellp); }

/* ------------------------------------------------------------------ objects */
JObj *jobj_new(const char *cls, int kind) {
    JObj *o = gas_calloc(1, sizeof(JObj));
    o->cls = cls;
    o->kind = kind;
    return o;
}
JObj *jstr_new(const char *utf8) {
    JObj *o = jobj_new("java/lang/String", JK_STRING);
    o->str = gas_strdup(utf8 ? utf8 : "");
    return o;
}
JObj *jarr_new(const char *cls, int elem, uint32_t len) {
    JObj *o = jobj_new(cls, JK_ARRAY);
    o->elem = elem;
    o->len = len;
    o->data = gas_calloc(len ? len : 1, elem);
    return o;
}
static void jobj_free(JObj *o) {
    if (!o || o->global || o->kind == JK_CLASS || o->kind == JK_INSTANCE) return;
    gas_free(o->str);
    gas_free(o->data);
    gas_free(o);
}
static JObj *g_classes[256];
static int g_nclasses;
JObj *jclass_get(const char *name) {
    SDL_LockMutex(g_jlock);
    for (int i = 0; i < g_nclasses; i++)
        if (!strcmp(g_classes[i]->cls, name)) { SDL_UnlockMutex(g_jlock); return g_classes[i]; }
    JObj *o = jobj_new(gas_strdup(name), JK_CLASS);
    o->global = 1;
    g_classes[g_nclasses++] = o;
    SDL_UnlockMutex(g_jlock);
    return o;
}
static const char *class_of(JObj *o) {
    if (!o) return "?";
    return o->kind == JK_CLASS ? o->cls : o->cls;
}
int64_t jobj_field_get(JObj *o, const char *name) {
    for (int i = 0; i < o->nfields; i++) if (!strcmp(o->fields[i].name, name)) return o->fields[i].v;
    return 0;
}
void jobj_field_set(JObj *o, const char *name, int64_t v) {
    for (int i = 0; i < o->nfields; i++) if (!strcmp(o->fields[i].name, name)) { o->fields[i].v = v; return; }
    if (o->nfields < JOBJ_MAX_FIELDS) { o->fields[o->nfields].name = name; o->fields[o->nfields++].v = v; }
}

/* monitors (MonitorEnter/Exit, Object.wait/notify) */
static void mon_init(JObj *o) {
    if (o->mon) return;
    SDL_LockMutex(g_jlock);
    if (!o->mon) { o->cond = SDL_CreateCondition(); o->mon = SDL_CreateMutex(); }
    SDL_UnlockMutex(g_jlock);
}
void jmon_enter(JObj *o) { mon_init(o); SDL_LockMutex(o->mon); }
void jmon_exit(JObj *o) { mon_init(o); SDL_UnlockMutex(o->mon); }
void jmon_wait(JObj *o, int64_t ms) {
    mon_init(o);
    if (ms > 0) SDL_WaitConditionTimeout(o->cond, o->mon, (Sint32)ms);
    else SDL_WaitCondition(o->cond, o->mon);
}
void jmon_notify_all(JObj *o) { mon_init(o); SDL_BroadcastCondition(o->cond); }

/* ------------------------------------------------------------------ method registry */
typedef struct { const char *cls, *name, *sig; JMethod fn; } JReg;
static JReg g_reg[512];
static int g_nreg;
void jni_register(const char *cls, const char *name, const char *sig, JMethod fn) {
    g_reg[g_nreg++] = (JReg){cls, name, sig, fn};
}
static JMethod reg_find(const char *cls, const char *name, const char *sig) {
    JMethod any = NULL;
    for (int i = 0; i < g_nreg; i++) {
        if (strcmp(g_reg[i].name, name)) continue;
        if (g_reg[i].cls && strcmp(g_reg[i].cls, cls)) continue;
        if (!g_reg[i].sig || !strcmp(g_reg[i].sig, sig)) return g_reg[i].fn;
        if (!any) any = NULL;
    }
    return any;
}

static JMethodId *mid_new(const char *cls, const char *name, const char *sig, int is_static) {
    static JMethodId *cache[1024];
    static int ncache;
    SDL_LockMutex(g_jlock);
    for (int i = 0; i < ncache; i++)
        if (!strcmp(cache[i]->cls, cls) && !strcmp(cache[i]->name, name) && !strcmp(cache[i]->sig, sig)) { SDL_UnlockMutex(g_jlock); return cache[i]; }
    JMethodId *m = gas_calloc(1, sizeof *m);
    m->cls = gas_strdup(cls); m->name = gas_strdup(name); m->sig = gas_strdup(sig); m->is_static = is_static;
    m->fn = reg_find(cls, name, sig);
    /* signature -> argument kinds */
    const char *p = sig + 1;
    while (*p && *p != ')') {
        char k = *p;
        if (k == '[') { while (*p == '[') p++; if (*p == 'L') { while (*p != ';') p++; } p++; k = 'L'; }
        else if (k == 'L') { while (*p != ';') p++; p++; }
        else p++;
        if (m->nargs < 16) m->args[m->nargs++] = k;
    }
    m->ret = p[0] == ')' ? p[1] : 'V';
    if (!m->fn) host_log("[jni] no implementation for %s.%s%s", cls, name, sig);
    if (ncache < 1024) cache[ncache++] = m;
    SDL_UnlockMutex(g_jlock);
    return m;
}

/* arguments: 0 = varargs (C promotion rules), 1 = va_list, 2 = jvalue array */
static int collect_args(JMethodId *m, ArgStream *as, uint32_t jvals, int mode, uint64_t *out) {
    for (int i = 0; i < m->nargs; i++) {
        char k = m->args[i];
        if (mode == 2) {
            uint64_t v; memcpy(&v, GP(jvals + 8 * i), 8);
            switch (k) {
            case 'Z': case 'B': out[i] = (uint64_t)(int64_t)(int8_t)v; if (k == 'Z') out[i] = (uint8_t)v; break;
            case 'C': out[i] = (uint16_t)v; break;
            case 'S': out[i] = (uint64_t)(int64_t)(int16_t)v; break;
            case 'I': case 'F': case 'L': out[i] = (uint32_t)v; break;
            default: out[i] = v;
            }
        } else {
            if (k == 'J' || k == 'D') out[i] = as_u64(as);
            else if (k == 'F') { uint64_t d = as_u64(as); double dv; memcpy(&dv, &d, 8); float f = (float)dv; uint32_t b; memcpy(&b, &f, 4); out[i] = b; }
            else out[i] = as_u32(as);
        }
    }
    return m->nargs;
}

static uint64_t invoke(Cpu *c, JObj *self, JMethodId *m, uint64_t *args) {
    if (!m) return 0;
    if (!m->fn) {
        host_log("[jni] call to unimplemented %s.%s%s", m->cls, m->name, m->sig);
        return 0;
    }
    uint64_t r = m->fn(c, self, args);
    if (g_jtrace) {
        char extra[256] = "";
        if (m->nargs && m->args[0] == 'L' && args[0] && ((JObj *)GP((uint32_t)args[0]))->kind == JK_STRING) snprintf(extra, sizeof extra, " \"%s\"", JSTR(args[0]));
        host_log("[jni] %s.%s%s%s -> %llx", m->cls, m->name, m->sig, extra, r);
    }
    return r;
}

static void set_ret(Cpu *c, char ret, uint64_t v) {
    switch (ret) {
    case 'J': case 'D': RET64(c, v); break;
    case 'Z': RET(c, (uint8_t)v); break;
    case 'B': RET(c, (uint32_t)(int32_t)(int8_t)v); break;
    case 'C': RET(c, (uint16_t)v); break;
    case 'S': RET(c, (uint32_t)(int32_t)(int16_t)v); break;
    case 'V': break;
    default: RET(c, (uint32_t)v);
    }
}

/* Call<T>Method family. layout: (env, obj, mid, ...) or (env, obj, cls, mid, ...) for nonvirtual */
static void do_call(Cpu *c, int argbase, int mode) {
    JObj *self = GP(c->r[1]);
    JMethodId *m = GP(argbase == 3 ? c->r[2] : c->r[3]);
    uint64_t args[16] = {0};
    ArgStream as = {c, argbase, c->r[13], 0};
    uint32_t extra = ARG(c, argbase);
    if (mode == 1) { as.va = extra; }
    if (m) collect_args(m, &as, extra, mode, args);
    uint64_t r = invoke(c, self, m, args);
    set_ret(c, m ? m->ret : 'V', r);
}
#define CALLV(n, base, mode) static void jn_call_##n(Cpu *c) { do_call(c, base, mode); }
CALLV(p, 3, 0) CALLV(v, 3, 1) CALLV(a, 3, 2)
CALLV(np, 4, 0) CALLV(nv, 4, 1) CALLV(na, 4, 2)

static void jn_newobject(Cpu *c, int mode) {
    JObj *cls = GP(c->r[1]);
    JMethodId *m = GP(c->r[2]);
    JObj *o = jobj_new(cls->cls, JK_INSTANCE);
    uint64_t args[16] = {0};
    ArgStream as = {c, 3, c->r[13], 0};
    if (mode == 1) as.va = c->r[3];
    collect_args(m, &as, c->r[3], mode, args);
    if (m->fn) m->fn(c, o, args);
    RET(c, HPTR(o));
}
static void jn_newobject_p(Cpu *c) { jn_newobject(c, 0); }
static void jn_newobject_v(Cpu *c) { jn_newobject(c, 1); }
static void jn_newobject_a(Cpu *c) { jn_newobject(c, 2); }

/* ------------------------------------------------------------------ JNIEnv functions */
#define ENVF(name) static void jn_##name(Cpu *c)
ENVF(GetVersion) { RET(c, 0x00010006); }
ENVF(FindClass) { RET(c, HPTR(jclass_get(GP(c->r[1])))); }
ENVF(ExceptionOccurred) { RET(c, 0); }
ENVF(ExceptionDescribe) { (void)c; }
ENVF(ExceptionClear) { (void)c; }
ENVF(ExceptionCheck) { RET(c, 0); }
ENVF(PushLocalFrame) { RET(c, 0); }
ENVF(PopLocalFrame) { RET(c, c->r[1]); }
ENVF(NewGlobalRef) { JObj *o = GP(c->r[1]); if (o) o->global = 1; RET(c, c->r[1]); }
ENVF(DeleteGlobalRef) { (void)c; }
ENVF(DeleteLocalRef) { jobj_free(GP(c->r[1])); }
ENVF(IsSameObject) { RET(c, c->r[1] == c->r[2]); }
ENVF(NewLocalRef) { RET(c, c->r[1]); }
ENVF(EnsureLocalCapacity) { RET(c, 0); }
ENVF(GetObjectClass) { JObj *o = GP(c->r[1]); RET(c, o ? HPTR(jclass_get(class_of(o))) : 0); }
ENVF(IsInstanceOf) { JObj *o = GP(c->r[1]), *k = GP(c->r[2]); RET(c, o && k && !strcmp(o->cls, k->cls)); }
ENVF(GetMethodID) { JObj *k = GP(c->r[1]); RET(c, HPTR(mid_new(k ? k->cls : "?", GP(c->r[2]), GP(c->r[3]), 0))); }
ENVF(GetStaticMethodID) { JObj *k = GP(c->r[1]); RET(c, HPTR(mid_new(k ? k->cls : "?", GP(c->r[2]), GP(c->r[3]), 1))); }
ENVF(GetFieldID) { RET(c, HPTR(gas_strdup(GP(c->r[2])))); }
static void jn_getfield(Cpu *c) { JObj *o = GP(c->r[1]); int64_t v = o ? jobj_field_get(o, GP(c->r[2])) : 0; RET64(c, v); }
static void jn_setfield(Cpu *c) {
    JObj *o = GP(c->r[1]);
    if (o) jobj_field_set(o, GP(c->r[2]), (int64_t)(int32_t)c->r[3]);
}
static void jn_setfield64(Cpu *c) {
    JObj *o = GP(c->r[1]);
    uint64_t v = ((uint64_t)M32(c->r[13] + 4) << 32) | M32(c->r[13]); /* 64-bit value is 8-aligned on the stack */
    if (o) jobj_field_set(o, GP(c->r[2]), (int64_t)v);
}
static JObj g_statics = {"<statics>", JK_INSTANCE};
void jni_set_static_int(const char *name, int32_t v) { jobj_field_set(&g_statics, gas_strdup(name), v); }
static void jn_getstatic(Cpu *c) { RET64(c, jobj_field_get(&g_statics, GP(c->r[2]))); }
static void jn_setstatic(Cpu *c) { jobj_field_set(&g_statics, GP(c->r[2]), (int64_t)(int32_t)c->r[3]); }
ENVF(NewStringUTF) { RET(c, c->r[1] ? HPTR(jstr_new(GP(c->r[1]))) : 0); }
ENVF(GetStringUTFLength) { JObj *s = GP(c->r[1]); RET(c, s && s->str ? strlen(s->str) : 0); }
ENVF(GetStringUTFChars) {
    JObj *s = GP(c->r[1]);
    if (c->r[2]) M8(c->r[2]) = 0;
    RET(c, s && s->str ? HPTR(s->str) : 0);
}
ENVF(ReleaseStringUTFChars) { (void)c; }
static int utf8_to_utf16(const char *s, uint16_t *out, int max) {
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xe0) == 0xc0) { cp = (*p++ & 0x1f) << 6; if (*p) cp |= *p++ & 0x3f; }
        else if ((*p & 0xf0) == 0xe0) { cp = (*p++ & 0x0f) << 12; if (*p) cp |= (*p++ & 0x3f) << 6; if (*p) cp |= *p++ & 0x3f; }
        else { cp = '?'; p++; while ((*p & 0xc0) == 0x80) p++; }
        if (out && n < max) out[n] = (uint16_t)cp;
        n++;
    }
    return n;
}
ENVF(GetStringLength) { JObj *s = GP(c->r[1]); RET(c, s && s->str ? utf8_to_utf16(s->str, NULL, 0) : 0); }
ENVF(GetStringChars) {
    JObj *s = GP(c->r[1]);
    if (!s || !s->str) { RET(c, 0); return; }
    int n = utf8_to_utf16(s->str, NULL, 0);
    uint16_t *b = gas_calloc(n + 1, 2);
    utf8_to_utf16(s->str, b, n);
    if (c->r[2]) M8(c->r[2]) = 1;
    RET(c, HPTR(b));
}
ENVF(ReleaseStringChars) { gas_free(GP(c->r[2])); }
ENVF(NewString) {
    /* NewString(env, const jchar*, len) -> UTF-8 */
    const uint16_t *u = GP(c->r[1]);
    int n = (int)c->r[2];
    char *b = gas_malloc(n * 3 + 1), *p = b;
    for (int i = 0; i < n; i++) {
        uint32_t cp = u[i];
        if (cp < 0x80) *p++ = (char)cp;
        else if (cp < 0x800) { *p++ = (char)(0xc0 | (cp >> 6)); *p++ = (char)(0x80 | (cp & 0x3f)); }
        else { *p++ = (char)(0xe0 | (cp >> 12)); *p++ = (char)(0x80 | ((cp >> 6) & 0x3f)); *p++ = (char)(0x80 | (cp & 0x3f)); }
    }
    *p = 0;
    JObj *o = jstr_new(b);
    gas_free(b);
    RET(c, HPTR(o));
}
ENVF(GetArrayLength) { JObj *a = GP(c->r[1]); RET(c, a ? a->len : 0); }
ENVF(NewObjectArray) { JObj *a = jarr_new("[Ljava/lang/Object;", 4, c->r[1]); for (uint32_t i = 0; i < a->len; i++) ((uint32_t *)a->data)[i] = c->r[3]; RET(c, HPTR(a)); }
ENVF(GetObjectArrayElement) { JObj *a = GP(c->r[1]); RET(c, ((uint32_t *)a->data)[c->r[2]]); }
ENVF(SetObjectArrayElement) { JObj *a = GP(c->r[1]); ((uint32_t *)a->data)[c->r[2]] = c->r[3]; }
static void jn_newarr(Cpu *c, int elem, const char *cls) { RET(c, HPTR(jarr_new(cls, elem, c->r[1]))); }
ENVF(NewBooleanArray) { jn_newarr(c, 1, "[Z"); }
ENVF(NewByteArray) { jn_newarr(c, 1, "[B"); }
ENVF(NewCharArray) { jn_newarr(c, 2, "[C"); }
ENVF(NewShortArray) { jn_newarr(c, 2, "[S"); }
ENVF(NewIntArray) { jn_newarr(c, 4, "[I"); }
ENVF(NewLongArray) { jn_newarr(c, 8, "[J"); }
ENVF(NewFloatArray) { jn_newarr(c, 4, "[F"); }
ENVF(NewDoubleArray) { jn_newarr(c, 8, "[D"); }
ENVF(GetArrayElements) { JObj *a = GP(c->r[1]); if (c->r[2]) M8(c->r[2]) = 0; RET(c, a ? HPTR(a->data) : 0); }
ENVF(ReleaseArrayElements) { (void)c; }
ENVF(GetArrayRegion) {
    JObj *a = GP(c->r[1]);
    uint32_t start = c->r[2], len = c->r[3], buf = ARG(c, 4);
    memcpy(GP(buf), (uint8_t *)a->data + start * a->elem, len * a->elem);
}
ENVF(SetArrayRegion) {
    JObj *a = GP(c->r[1]);
    uint32_t start = c->r[2], len = c->r[3], buf = ARG(c, 4);
    memcpy((uint8_t *)a->data + start * a->elem, GP(buf), len * a->elem);
}
ENVF(GetStringUTFRegion) {
    JObj *s = GP(c->r[1]);
    memcpy(GP(ARG(c, 4)), s->str + c->r[2], c->r[3]);
    M8(ARG(c, 4) + c->r[3]) = 0;
}
ENVF(GetPrimitiveArrayCritical) { JObj *a = GP(c->r[1]); if (c->r[2]) M8(c->r[2]) = 0; RET(c, HPTR(a->data)); }
ENVF(ReleasePrimitiveArrayCritical) { (void)c; }
ENVF(MonitorEnter) { jmon_enter(GP(c->r[1])); RET(c, 0); }
ENVF(MonitorExit) { jmon_exit(GP(c->r[1])); RET(c, 0); }
ENVF(GetJavaVM) { M32(c->r[1]) = jni_vm(); RET(c, 0); }
ENVF(RegisterNatives) { RET(c, 0); }
ENVF(Throw) { host_log("[jni] Throw"); RET(c, 0); }
ENVF(ThrowNew) { host_log("[jni] ThrowNew %s", (const char *)GP(c->r[2])); RET(c, 0); }
ENVF(FatalError) { host_log("[jni] FatalError %s", (const char *)GP(c->r[1])); host_fatal("JNI FatalError"); }

static void jn_unimpl(Cpu *c) {
    host_log("[jni] unimplemented JNIEnv function called (lr vaddr %06x)", c->r[14] - IMG);
    RET(c, 0);
}

/* JavaVM */
static void vm_GetEnv(Cpu *c) { M32(c->r[1]) = jni_env(); RET(c, 0); }
static void vm_Detach(Cpu *c) { RET(c, 0); }

void jni_init(void) {
    g_jlock = SDL_CreateMutex();
    g_env_cellp = gas_calloc(1, 4);
    g_vm_cellp = gas_calloc(1, 4);
    g_env_table = gas_calloc(233, 4);
    g_vm_table = gas_calloc(8, 4);
    uint32_t un = rc_host_fn(jn_unimpl, "jni_unimpl");
    for (int i = 0; i < 233; i++) g_env_table[i] = un;
#define SET(i, f) g_env_table[i] = rc_host_fn(jn_##f, #f)
    SET(4, GetVersion); SET(6, FindClass); SET(13, Throw); SET(14, ThrowNew); SET(15, ExceptionOccurred);
    SET(16, ExceptionDescribe); SET(17, ExceptionClear); SET(18, FatalError); SET(19, PushLocalFrame);
    SET(20, PopLocalFrame); SET(21, NewGlobalRef); SET(22, DeleteGlobalRef); SET(23, DeleteLocalRef);
    SET(24, IsSameObject); SET(25, NewLocalRef); SET(26, EnsureLocalCapacity);
    SET(28, newobject_p); SET(29, newobject_v); SET(30, newobject_a);
    SET(31, GetObjectClass); SET(32, IsInstanceOf); SET(33, GetMethodID);
    {
        uint32_t p = rc_host_fn(jn_call_p, "Call"), v = rc_host_fn(jn_call_v, "CallV"), a = rc_host_fn(jn_call_a, "CallA");
        uint32_t np = rc_host_fn(jn_call_np, "CallNV"), nv = rc_host_fn(jn_call_nv, "CallNVV"), na = rc_host_fn(jn_call_na, "CallNVA");
        for (int k = 0; k < 10; k++) {
            g_env_table[34 + 3 * k] = p; g_env_table[35 + 3 * k] = v; g_env_table[36 + 3 * k] = a;
            g_env_table[64 + 3 * k] = np; g_env_table[65 + 3 * k] = nv; g_env_table[66 + 3 * k] = na;
            g_env_table[114 + 3 * k] = p; g_env_table[115 + 3 * k] = v; g_env_table[116 + 3 * k] = a;
        }
    }
    SET(94, GetFieldID);
    {
        uint32_t g = rc_host_fn(jn_getfield, "GetField"), s = rc_host_fn(jn_setfield, "SetField"), s64 = rc_host_fn(jn_setfield64, "SetField64");
        for (int k = 0; k < 9; k++) { g_env_table[95 + k] = g; g_env_table[104 + k] = (k == 6 || k == 8) ? s64 : s; }
        uint32_t gs = rc_host_fn(jn_getstatic, "GetStatic"), ss = rc_host_fn(jn_setstatic, "SetStatic");
        for (int k = 0; k < 9; k++) { g_env_table[145 + k] = gs; g_env_table[154 + k] = ss; }
    }
    SET(113, GetStaticMethodID); g_env_table[144] = g_env_table[94];
    SET(163, NewString); SET(164, GetStringLength); SET(165, GetStringChars); SET(166, ReleaseStringChars);
    SET(167, NewStringUTF); SET(168, GetStringUTFLength); SET(169, GetStringUTFChars); SET(170, ReleaseStringUTFChars);
    SET(171, GetArrayLength); SET(172, NewObjectArray); SET(173, GetObjectArrayElement); SET(174, SetObjectArrayElement);
    SET(175, NewBooleanArray); SET(176, NewByteArray); SET(177, NewCharArray); SET(178, NewShortArray);
    SET(179, NewIntArray); SET(180, NewLongArray); SET(181, NewFloatArray); SET(182, NewDoubleArray);
    {
        uint32_t ge = rc_host_fn(jn_GetArrayElements, "GetArrayElements"), re = rc_host_fn(jn_ReleaseArrayElements, "ReleaseArrayElements");
        uint32_t gr = rc_host_fn(jn_GetArrayRegion, "GetArrayRegion"), sr = rc_host_fn(jn_SetArrayRegion, "SetArrayRegion");
        for (int k = 0; k < 8; k++) { g_env_table[183 + k] = ge; g_env_table[191 + k] = re; g_env_table[199 + k] = gr; g_env_table[207 + k] = sr; }
    }
    SET(215, RegisterNatives); SET(217, MonitorEnter); SET(218, MonitorExit); SET(219, GetJavaVM);
    SET(221, GetStringUTFRegion); SET(222, GetPrimitiveArrayCritical); SET(223, ReleasePrimitiveArrayCritical);
    SET(228, ExceptionCheck);
#undef SET
    g_env_cell = HPTR(g_env_table);
    for (int i = 0; i < 8; i++) g_vm_table[i] = un;
    g_vm_table[4] = rc_host_fn(vm_GetEnv, "AttachCurrentThread");
    g_vm_table[5] = rc_host_fn(vm_Detach, "DetachCurrentThread");
    g_vm_table[6] = rc_host_fn(vm_GetEnv, "GetEnv");
    g_vm_table[7] = rc_host_fn(vm_GetEnv, "AttachCurrentThreadAsDaemon");
    g_vm_cell = HPTR(g_vm_table);
}

uint32_t jni_new_object(const char *cls) {
    JObj *o = jobj_new(cls, JK_INSTANCE);
    o->global = 1;
    return HPTR(o);
}
