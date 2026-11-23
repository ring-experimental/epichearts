/* Guest address space.
 * The recompiled ARM code uses 32-bit pointers. On a 32-bit host they are host pointers (GAS_BASE = 0).
 * On a 64-bit host (arm64 phones, x64 desktop) all memory the guest can see lives in one reserved region and a
 * guest address is an offset into it: host = GAS_BASE + guest. Host objects the guest only passes around
 * (FILE*, mutexes) are stored as handles. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX == 0xffffffffu
#define GAS_64 0
#define GAS_BASE ((uintptr_t)0)
#else
#define GAS_64 1
extern uintptr_t g_gas_base;
#define GAS_BASE g_gas_base
#endif

#ifdef __cplusplus
extern "C" {
#endif
void gas_init(void);
/* memory visible to the guest (malloc imports, JNI objects, stacks, scratch structs passed to guest code) */
void *gas_malloc(size_t n);
void *gas_calloc(size_t n, size_t m);
void *gas_realloc(void *p, size_t n);
void gas_free(void *p);
char *gas_strdup(const char *s);
/* the image mapping at guest address 0x10000000 (loader) */
void *gas_image(size_t size);
/* opaque host pointers kept in guest memory as 32-bit values */
uint32_t gas_handle(void *p);
void *gas_unhandle(uint32_t h);
#ifdef __cplusplus
}
#endif
