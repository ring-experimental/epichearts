/* Interpreter build (EH_INTERP): interface between interp.c and the loader */
#pragma once
#include "rc.h"

/* return address of host-initiated guest calls (loader.c RET_MAGIC) */
#define RC_RET_MAGIC 0xfffffff1u
/* image vaddrs below this are the original's code and data; import cells and host callbacks follow (loader.c) */
#define RC_IMAGE_END 0x1a0000u

/* nonzero at [vaddr >> 1]: a host wrapper replaces the function entered at vaddr (hooks.h) */
extern unsigned char *rc_hookmap;
GuestFn rc_hook_fn(uint32_t va);
/* host function behind an absolute guest address (import cell, rc_host_fn callback), or NULL */
GuestFn rc_host_target(uint32_t addr);
/* host replacement of an ARM-mode function defined in the image (by symbol: __aeabi_atexit), or NULL */
GuestFn rc_arm_fn(uint32_t va);
/* native routines for hot leaf functions of the original (accel.c) */
void accel_init(unsigned char *hookmap); /* marks their entries in the hook map */
void rc_orig(Cpu *c, uint32_t va);       /* run the original at va (no hook at its entry; loader.c) */
GuestFn accel_fn(uint32_t va);           /* the native routine entered at va, or NULL (disabled / checking) */
/* check builds (-DEH_ACCEL_CHECK): store log of the interpreter */
extern
#ifdef _MSC_VER
__declspec(thread)
#else
_Thread_local
#endif
int interp_wlog_on;
void interp_wlog_add(uint32_t addr, int size);
/* run guest Thumb code at pc (absolute) until it returns to RC_RET_MAGIC */
void interp_run(Cpu *c, uint32_t pc);
