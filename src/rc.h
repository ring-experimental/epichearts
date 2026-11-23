/* Runtime interface shared by the recompiled code (src/gen) and the platform layer.
 * The original libtar-android.so is mapped at IMG (see loader.c); all guest pointers are
 * 32-bit host pointers because this program is built for 32-bit x86. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#include "gas.h"

typedef struct Cpu {
    uint32_t r[16];  /* r13 = sp, r14 = lr */
    uint32_t unwind; /* nonzero: returning to this guest address, frames are being unwound */
} Cpu;

extern uint32_t IMG; /* host address where vaddr 0 of the image is mapped */

/* guest memory access: guest address -> host address (gas.h; identity on 32-bit hosts) */
#define M32(a) (*(uint32_t *)(GAS_BASE + (uint32_t)(a)))
#define M16(a) (*(uint16_t *)(GAS_BASE + (uint32_t)(a)))
#define M8(a) (*(uint8_t *)(GAS_BASE + (uint32_t)(a)))
#define MS16(a) ((uint32_t)(int32_t)(*(int16_t *)(GAS_BASE + (uint32_t)(a))))
#define MS8(a) ((uint32_t)(int32_t)(*(int8_t *)(GAS_BASE + (uint32_t)(a))))

static inline uint32_t ror32(uint32_t x, uint32_t k) { k &= 31; return k ? (x >> k) | (x << (32 - k)) : x; }

/* Thumb register-specified shifts: amount = Rs[7:0]; *c gets the shifter carry (unchanged if 0) */
static inline uint32_t sh_lsl(uint32_t x, uint32_t s, uint32_t *c) {
    if (s == 0) return x;
    if (s < 32) { *c = (x >> (32 - s)) & 1; return x << s; }
    *c = s == 32 ? (x & 1) : 0; return 0;
}
static inline uint32_t sh_lsr(uint32_t x, uint32_t s, uint32_t *c) {
    if (s == 0) return x;
    if (s < 32) { *c = (x >> (s - 1)) & 1; return x >> s; }
    *c = s == 32 ? (x >> 31) : 0; return 0;
}
static inline uint32_t sh_asr(uint32_t x, uint32_t s, uint32_t *c) {
    if (s == 0) return x;
    if (s < 32) { *c = ((uint32_t)((int32_t)x >> (s - 1))) & 1; return (uint32_t)((int32_t)x >> s); }
    *c = x >> 31; return (uint32_t)((int32_t)x >> 31);
}
static inline uint32_t sh_ror(uint32_t x, uint32_t s, uint32_t *c) {
    if (s == 0) return x;
    s &= 31;
    if (s == 0) { *c = x >> 31; return x; }
    x = (x >> s) | (x << (32 - s)); *c = x >> 31; return x;
}

/* call guest code at a guest code address (IMG + vaddr | 1) or an import cell */
void rc_dispatch(Cpu *c, uint32_t target);
/* transfer of control to a register value that is not our return address: a function entry is a
 * tail call, anything else is a return into an outer frame (starts unwinding) */
void rc_jump(Cpu *c, uint32_t target, uint32_t from_va);
void rc_trap(Cpu *c, uint32_t vaddr, const char *why);

typedef void (*GuestFn)(Cpu *c);
