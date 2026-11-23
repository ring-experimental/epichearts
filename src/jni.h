/* Object model of the emulated Java side */
#pragma once
#include "host.h"

enum { JK_INSTANCE, JK_STRING, JK_ARRAY, JK_CLASS };
#define JOBJ_MAX_FIELDS 64

typedef struct JObj {
    const char *cls;
    int kind;
    int global;
    char *str;        /* JK_STRING: UTF-8 */
    void *data;       /* JK_ARRAY: elements */
    uint32_t len;
    int elem;
    void *native;     /* host-side state of an emulated Java object */
    int nfields;
    struct { const char *name; int64_t v; } fields[JOBJ_MAX_FIELDS];
    struct SDL_Mutex *mon;
    struct SDL_Condition *cond;
} JObj;

/* Java method implemented in C: args already decoded per signature (one slot each) */
typedef uint64_t (*JMethod)(Cpu *c, JObj *self, const uint64_t *args);

typedef struct {
    char *cls, *name, *sig;
    int is_static;
    int nargs;
    char args[16];
    char ret;
    JMethod fn;
} JMethodId;

void jni_register(const char *cls, const char *name, const char *sig, JMethod fn);
JObj *jobj_new(const char *cls, int kind);
JObj *jstr_new(const char *utf8);
JObj *jarr_new(const char *cls, int elem, uint32_t len);
JObj *jclass_get(const char *name);
int64_t jobj_field_get(JObj *o, const char *name);
void jobj_field_set(JObj *o, const char *name, int64_t v);
void jmon_enter(JObj *o);
void jmon_exit(JObj *o);
void jmon_wait(JObj *o, int64_t ms);
void jmon_notify_all(JObj *o);

#define JSTR(v) ((v) ? ((JObj *)GP((uint32_t)(v)))->str : NULL)
#define JARR(v) ((JObj *)GP((uint32_t)(v)))
#define JRET_OBJ(o) ((uint64_t)HPTR(o))

void java_register_all(void);
void jni_set_static_int(const char *name, int32_t v);
