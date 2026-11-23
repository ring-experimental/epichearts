/* Host-side services used by the import layer, JNI emulation and main loop */
#pragma once
#include "rc.h"

#define ARG(c, i) ((i) < 4 ? (c)->r[i] : M32((c)->r[13] + 4 * ((i) - 4)))
#define RET(c, v) ((c)->r[0] = (uint32_t)(v))
#define RET64(c, v) do { uint64_t _v = (uint64_t)(v); (c)->r[0] = (uint32_t)_v; (c)->r[1] = (uint32_t)(_v >> 32); } while (0)
#define GP(x) ((void *)(GAS_BASE + (uint32_t)(x)))                 /* guest address -> host pointer */
#define HPTR(p) ((uint32_t)((uintptr_t)(p) - GAS_BASE))              /* host pointer inside guest memory -> guest address */

void host_fatal(const char *msg);
void host_log(const char *fmt, ...);

int rc_load_image(uint8_t *elf_file);
uint32_t rc_import_addr(const char *name);
uint32_t rc_host_fn(GuestFn fn, const char *name);
uint32_t rc_sym(const char *name);
uint32_t rc_call(Cpu *c, uint32_t va, int nargs, const uint32_t *args);
Cpu *rc_new_cpu(uint32_t stack_size);
void rc_run_init_array(Cpu *c);
const char *rc_func_name(uint32_t va);
int rc_func_name_exact(uint32_t va);

/* guest-style printf: format from guest memory, args pulled from an ARM argument stream */
typedef struct {
    Cpu *c;
    int next_reg;      /* next core register (0..3) */
    uint32_t stack;    /* next stacked argument address */
    uint32_t va;       /* when nonzero: va_list pointer (all args in memory) */
} ArgStream;
uint32_t as_u32(ArgStream *a);
uint64_t as_u64(ArgStream *a);
int guest_vformat(char *out, size_t outsz, const char *fmt, ArgStream *a);

/* paths */
extern char g_apk_path[1024];
#ifdef __EMSCRIPTEN__
#include <stdio.h>
FILE *host_open_apk(void); /* a read stream over the APK bytes the page handed over (main.c) */
#endif
extern char g_data_dir[1024];
const char *host_map_path(const char *guest_path, char *buf, size_t n);

/* graphics (gl.c) */
void gl_init_window(void);
void gl_present(void);

/* JNI (jni.c) */
uint32_t jni_env(void);
uint32_t jni_vm(void);
void jni_init(void);
uint32_t jni_new_object(const char *cls);
