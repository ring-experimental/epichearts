/* Interpreter for the original game code (EH_INTERP builds: the web build, and a desktop build to check it).
 *
 * The recompiled builds run src/gen (tools/recomp.py: the original's machine code translated to C). That C is a
 * derivative of the original, so a build that is published (the web page) cannot contain it. This file runs the
 * original libtar-android.so that the player supplies, instruction by instruction, with the same register file
 * (Cpu, rc.h) and the same host side (imports, JNI, hooks) as the recompiled code.
 *
 * Instruction sets, from the original image (ELF program headers / Ghidra):
 *  - .text 0x0a3e40..0x163fcc: ARMv5TE Thumb (16-bit, BL/BLX as halfword pairs). ARM-mode code there is only the
 *    PLT (0x0a39a8, stubs "add ip, pc, #; add ip, ip, #; ldr pc, [ip, #]!"), reached by BLX to an even address,
 *    and __aeabi_atexit (ARM, @00173fbc), which the host replaces as the recompiler does (rc_arm_fn).
 *  - third PT_LOAD at vaddr 0x19c000 (file 0x19d000, 8 KB): Thumb-2 code (movw/movt, b.w, ldr.w ..., push.w).
 *    The subset that occurs there is implemented; anything else stops with a trap that names the address.
 *
 * Calls into host code: a transfer of control to an import cell, a host callback (rc_host_fn), a PLT stub or the
 * entry of a hooked function (hooks.h) runs that host function and continues at the return address (lr at the
 * moment of the transfer, as for the recompiled code, where those are C calls).
 * A host call of guest code (rc_call / rc_dispatch / rc_orig) sets lr to RC_RET_MAGIC and runs until control returns
 * there; nested host -> guest -> host -> guest calls nest C frames exactly as in the recompiled build.
 * Condition flags are local to a run: they are not preserved across calls by the ARM procedure call standard,
 * and the recompiled code keeps them in locals too. */
#include "rc.h"
#include "host.h"
#include "interp.h"
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#include <stdlib.h>
static inline uint32_t popcnt16(uint32_t x) { uint32_t n = 0; for (; x; x &= x - 1) n++; return n; }
#define __builtin_popcount popcnt16
#define __builtin_bswap32 _byteswap_ulong
#endif

/* guest stores. Check builds (-DEH_ACCEL_CHECK, accel.c) can log each store with the value it replaces */
#ifdef EH_ACCEL_CHECK
#define ST_LOG(a, n) do { if (interp_wlog_on) interp_wlog_add((a), (n)); } while (0)
#else
#define ST_LOG(a, n) ((void)0)
#endif
#define ST32(a, v) do { uint32_t _a = (a); ST_LOG(_a, 4); M32(_a) = (v); } while (0)
#define ST16(a, v) do { uint32_t _a = (a); ST_LOG(_a, 2); M16(_a) = (uint16_t)(v); } while (0)
#define ST8(a, v) do { uint32_t _a = (a); ST_LOG(_a, 1); M8(_a) = (uint8_t)(v); } while (0)

static inline uint32_t sx(uint32_t v, int bits) { return (uint32_t)((int32_t)(v << (32 - bits)) >> (32 - bits)); }

static void trap_op(Cpu *c, uint32_t pc, uint32_t op, const char *what) {
    char msg[128];
    snprintf(msg, sizeof msg, "interp: %s (opcode %04x%s)", what, op & 0xffff, op >> 16 ? "+" : "");
    if (op >> 16) snprintf(msg, sizeof msg, "interp: %s (opcode %04x %04x)", what, op & 0xffff, op >> 16);
    rc_trap(c, pc - IMG, msg);
}

/* ARM condition codes */
static inline int cond_ok(uint32_t cond, uint32_t n, uint32_t z, uint32_t cf, uint32_t v) {
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return cf;
    case 0x3: return !cf;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return cf && !z;
    case 0x9: return !cf || z;
    case 0xa: return n == v;
    case 0xb: return n != v;
    case 0xc: return !z && n == v;
    case 0xd: return z || n != v;
    default: return 1;
    }
}

/* Thumb-2 ThumbExpandImm_C (i:imm3:imm8) */
static inline uint32_t t2_imm(uint32_t hw1, uint32_t hw2, uint32_t *carry) {
    uint32_t i = (hw1 >> 10) & 1, imm3 = (hw2 >> 12) & 7, imm8 = hw2 & 0xff;
    uint32_t imm12 = i << 11 | imm3 << 8 | imm8;
    if ((imm12 >> 10) == 0) {
        switch ((imm12 >> 8) & 3) {
        case 0: return imm8;
        case 1: return imm8 << 16 | imm8;
        case 2: return imm8 << 24 | imm8 << 8;
        default: return imm8 << 24 | imm8 << 16 | imm8 << 8 | imm8;
        }
    }
    uint32_t v = ror32(0x80 | (imm12 & 0x7f), imm12 >> 7);
    *carry = v >> 31;
    return v;
}

/* Thumb-2 shift by immediate (DecodeImmShift + Shift_C) */
static inline uint32_t t2_shift(uint32_t x, uint32_t type, uint32_t imm, uint32_t *carry) {
    switch (type) {
    case 0: if (imm) { *carry = (x >> (32 - imm)) & 1; x <<= imm; } return x;
    case 1: if (!imm) imm = 32; *carry = (x >> (imm - 1)) & 1; return imm == 32 ? 0 : x >> imm;
    case 2: if (!imm) imm = 32; *carry = ((uint32_t)((int32_t)x >> (imm - 1))) & 1;
            return imm == 32 ? (uint32_t)((int32_t)x >> 31) : (uint32_t)((int32_t)x >> imm);
    default:
        if (!imm) { uint32_t r = (*carry << 31) | (x >> 1); *carry = x & 1; return r; } /* RRX */
        x = ror32(x, imm); *carry = x >> 31; return x;
    }
}

/* ARM PLT stub at a (absolute): add ip, pc, #A; add ip, ip, #B; ldr pc, [ip, #C]! -> the GOT slot it jumps through */
static int plt_slot(uint32_t a, uint32_t *slot) {
    uint32_t i0 = M32(a), i1 = M32(a + 4), i2 = M32(a + 8);
    if ((i0 & 0xfffff000u) != 0xe28fc000u || (i1 & 0xfffff000u) != 0xe28cc000u || (i2 & 0xfffff000u) != 0xe5bcf000u) return 0;
    uint32_t ip = a + 8 + ror32(i0 & 0xff, ((i0 >> 8) & 0xf) * 2);
    ip += ror32(i1 & 0xff, ((i1 >> 8) & 0xf) * 2);
    *slot = ip + (i2 & 0xfff);
    return 1;
}

#define R (c->r)

/* flag helpers */
#define SET_NZ(x) do { uint32_t _x = (x); fn = _x >> 31; fz = _x == 0; } while (0)
static inline uint32_t add_flags(uint32_t a, uint32_t b, uint32_t cin, uint32_t *n, uint32_t *z, uint32_t *cf, uint32_t *v) {
    uint64_t u = (uint64_t)a + b + cin;
    uint32_t r = (uint32_t)u;
    *n = r >> 31; *z = r == 0; *cf = (uint32_t)(u >> 32);
    *v = ((~(a ^ b) & (a ^ r)) >> 31) & 1;
    return r;
}
#define ADDF(a, b, cin) add_flags((a), (b), (cin), &fn, &fz, &fc, &fv)
#define SUBF(a, b) add_flags((a), ~(uint32_t)(b), 1, &fn, &fz, &fc, &fv)
#define SBCF(a, b) add_flags((a), ~(uint32_t)(b), fc, &fn, &fz, &fc, &fv)

#ifdef EH_IPROF
/* profiling build (-DEH_IPROF): instructions executed per halfword address, logged by function at exit */
#include <stdlib.h>
static volatile uint32_t *g_prof;
static int cmp_prof(const void *a, const void *b) {
    uint64_t x = ((const uint64_t *)a)[0], y = ((const uint64_t *)b)[0];
    return x < y ? 1 : x > y ? -1 : 0;
}
void interp_prof_dump(void) {
    static uint64_t agg[65536][2];
    const char *last = NULL;
    int n = 0;
    uint64_t total = 0;
    if (!g_prof) return;
    for (uint32_t i = 0; i < RC_IMAGE_END / 2; i++) {
        if (!g_prof[i]) continue;
        const char *nm = rc_func_name(i * 2);
        /* unnamed routines follow named ones: a push {..., lr} also starts a new bucket */
        if ((M16(IMG + i * 2) & 0xff00) == 0xb500) last = NULL;
        if (nm != last || !n) { if (n < 65536) { agg[n][0] = 0; agg[n][1] = i * 2; n++; } last = nm; }
        agg[n - 1][0] += g_prof[i];
        total += g_prof[i];
    }
    qsort(agg, n, sizeof agg[0], cmp_prof);
    host_log("[iprof] %llu instructions", (unsigned long long)total);
    for (int i = 0; i < n && i < 40; i++)
        host_log("[iprof] %5.2f%% %06x %s", 100.0 * agg[i][0] / total, (unsigned)agg[i][1],
                 rc_func_name_exact((uint32_t)agg[i][1]) ? rc_func_name((uint32_t)agg[i][1]) : "(unnamed)");
}
#define PROF(pc) do { if (!g_prof) g_prof = calloc(RC_IMAGE_END / 2, 4); g_prof[((pc) - IMG) >> 1]++; } while (0)
#else
void interp_prof_dump(void) {}
#define PROF(pc) ((void)0)
#endif

/* run guest code from pc (absolute, Thumb) until it returns to RC_RET_MAGIC */
void interp_run(Cpu *c, uint32_t pc) {
    uint32_t fn = 0, fz = 0, fc = 0, fv = 0;
    uint32_t t; /* branch target (absolute, bit 0 = Thumb) */
    for (;;) {
        PROF(pc);
        uint32_t op = M16(pc);
        uint32_t next = pc + 2;
        switch (op >> 11) {
        case 0x00: { /* LSL Rd, Rm, #imm */
            uint32_t sh = (op >> 6) & 31, x = R[(op >> 3) & 7];
            if (sh) { fc = (x >> (32 - sh)) & 1; x <<= sh; }
            R[op & 7] = x; SET_NZ(x); pc = next; continue;
        }
        case 0x01: { /* LSR */
            uint32_t sh = (op >> 6) & 31, x = R[(op >> 3) & 7];
            if (!sh) { fc = x >> 31; x = 0; } else { fc = (x >> (sh - 1)) & 1; x >>= sh; }
            R[op & 7] = x; SET_NZ(x); pc = next; continue;
        }
        case 0x02: { /* ASR */
            uint32_t sh = (op >> 6) & 31, x = R[(op >> 3) & 7];
            if (!sh) { fc = x >> 31; x = (uint32_t)((int32_t)x >> 31); }
            else { fc = ((uint32_t)((int32_t)x >> (sh - 1))) & 1; x = (uint32_t)((int32_t)x >> sh); }
            R[op & 7] = x; SET_NZ(x); pc = next; continue;
        }
        case 0x03: { /* ADD/SUB register or imm3 */
            uint32_t a = R[(op >> 3) & 7], b = (op & 0x400) ? (op >> 6) & 7 : R[(op >> 6) & 7];
            R[op & 7] = (op & 0x200) ? SUBF(a, b) : ADDF(a, b, 0);
            pc = next; continue;
        }
        case 0x04: { uint32_t x = op & 0xff; R[(op >> 8) & 7] = x; SET_NZ(x); pc = next; continue; } /* MOV imm */
        case 0x05: SUBF(R[(op >> 8) & 7], op & 0xff); pc = next; continue;                          /* CMP imm */
        case 0x06: { uint32_t d = (op >> 8) & 7; R[d] = ADDF(R[d], op & 0xff, 0); pc = next; continue; } /* ADD imm8 */
        case 0x07: { uint32_t d = (op >> 8) & 7; R[d] = SUBF(R[d], op & 0xff); pc = next; continue; }    /* SUB imm8 */
        case 0x08:
            if (!(op & 0x400)) { /* data processing */
                uint32_t d = op & 7, s = R[(op >> 3) & 7], x = R[d];
                switch ((op >> 6) & 15) {
                case 0x0: x &= s; R[d] = x; SET_NZ(x); break;
                case 0x1: x ^= s; R[d] = x; SET_NZ(x); break;
                case 0x2: x = sh_lsl(x, s & 0xff, &fc); R[d] = x; SET_NZ(x); break;
                case 0x3: x = sh_lsr(x, s & 0xff, &fc); R[d] = x; SET_NZ(x); break;
                case 0x4: x = sh_asr(x, s & 0xff, &fc); R[d] = x; SET_NZ(x); break;
                case 0x5: R[d] = ADDF(x, s, fc); break;
                case 0x6: R[d] = SBCF(x, s); break;
                case 0x7: x = sh_ror(x, s & 0xff, &fc); R[d] = x; SET_NZ(x); break;
                case 0x8: SET_NZ(x & s); break;
                case 0x9: R[d] = SUBF(0, s); break;
                case 0xa: SUBF(x, s); break;
                case 0xb: ADDF(x, s, 0); break;
                case 0xc: x |= s; R[d] = x; SET_NZ(x); break;
                case 0xd: x *= s; R[d] = x; SET_NZ(x); break;
                case 0xe: x &= ~s; R[d] = x; SET_NZ(x); break;
                default: x = ~s; R[d] = x; SET_NZ(x); break;
                }
                pc = next; continue;
            } else { /* hi register operations / BX / BLX */
                uint32_t d = (op & 7) | ((op >> 4) & 8), m = (op >> 3) & 15;
                uint32_t mv = m == 15 ? pc + 4 : R[m];
                switch ((op >> 8) & 3) {
                case 0: /* ADD */
                    if (d == 15) { t = ((pc + 4 + mv) & ~1u) | 1; goto branch; }
                    R[d] += mv; pc = next; continue;
                case 1: /* CMP */
                    SUBF(d == 15 ? pc + 4 : R[d], mv); pc = next; continue;
                case 2: /* MOV */
                    if (d == 15) { t = (mv & ~1u) | 1; goto branch; }
                    R[d] = mv; pc = next; continue;
                default: /* BX / BLX */
                    t = mv;
                    if (op & 0x80) R[14] = next | 1;
                    goto branch;
                }
            }
        case 0x09: R[(op >> 8) & 7] = M32(((pc + 4) & ~3u) + (op & 0xff) * 4); pc = next; continue; /* LDR literal */
        case 0x0a: case 0x0b: { /* load/store register offset */
            uint32_t a = R[(op >> 3) & 7] + R[(op >> 6) & 7], d = op & 7;
            switch ((op >> 9) & 7) {
            case 0: ST32(a, R[d]); break;
            case 1: ST16(a, (uint16_t)R[d]); break;
            case 2: ST8(a, (uint8_t)R[d]); break;
            case 3: R[d] = MS8(a); break;
            case 4: R[d] = M32(a); break;
            case 5: R[d] = M16(a); break;
            case 6: R[d] = M8(a); break;
            default: R[d] = MS16(a); break;
            }
            pc = next; continue;
        }
        case 0x0c: ST32(R[(op >> 3) & 7] + ((op >> 6) & 31) * 4, R[op & 7]); pc = next; continue;
        case 0x0d: R[op & 7] = M32(R[(op >> 3) & 7] + ((op >> 6) & 31) * 4); pc = next; continue;
        case 0x0e: ST8(R[(op >> 3) & 7] + ((op >> 6) & 31), (uint8_t)R[op & 7]); pc = next; continue;
        case 0x0f: R[op & 7] = M8(R[(op >> 3) & 7] + ((op >> 6) & 31)); pc = next; continue;
        case 0x10: ST16(R[(op >> 3) & 7] + ((op >> 6) & 31) * 2, (uint16_t)R[op & 7]); pc = next; continue;
        case 0x11: R[op & 7] = M16(R[(op >> 3) & 7] + ((op >> 6) & 31) * 2); pc = next; continue;
        case 0x12: ST32(R[13] + (op & 0xff) * 4, R[(op >> 8) & 7]); pc = next; continue;
        case 0x13: R[(op >> 8) & 7] = M32(R[13] + (op & 0xff) * 4); pc = next; continue;
        case 0x14: R[(op >> 8) & 7] = ((pc + 4) & ~3u) + (op & 0xff) * 4; pc = next; continue; /* ADR */
        case 0x15: R[(op >> 8) & 7] = R[13] + (op & 0xff) * 4; pc = next; continue;            /* ADD Rd, SP */
        case 0x16: case 0x17: /* miscellaneous */
            switch ((op >> 8) & 15) {
            case 0x0: if (op & 0x80) R[13] -= (op & 0x7f) * 4; else R[13] += (op & 0x7f) * 4; pc = next; continue;
            case 0x1: case 0x3: case 0x9: case 0xb: { /* CBZ / CBNZ (Thumb-2 segment) */
                uint32_t z = R[op & 7] == 0;
                if (z != ((op >> 11) & 1)) { t = (pc + 4 + (((op >> 9) & 1) << 6 | ((op >> 3) & 31) << 1)) | 1; goto branch; }
                pc = next; continue;
            }
            case 0x2: { /* SXTH / SXTB / UXTH / UXTB */
                uint32_t m = R[(op >> 3) & 7], d = op & 7;
                switch ((op >> 6) & 3) {
                case 0: R[d] = sx(m, 16); break;
                case 1: R[d] = sx(m, 8); break;
                case 2: R[d] = m & 0xffff; break;
                default: R[d] = m & 0xff; break;
                }
                pc = next; continue;
            }
            case 0x4: case 0x5: { /* PUSH */
                uint32_t n = __builtin_popcount(op & 0x1ff), a = R[13] - 4 * n;
                R[13] = a;
                for (int i = 0; i < 8; i++) if (op & (1u << i)) { ST32(a, R[i]); a += 4; }
                if (op & 0x100) ST32(a, R[14]);
                pc = next; continue;
            }
            case 0xa: { /* REV / REV16 / REVSH */
                uint32_t m = R[(op >> 3) & 7], d = op & 7;
                switch ((op >> 6) & 3) {
                case 0: R[d] = __builtin_bswap32(m); break;
                case 1: R[d] = ((m >> 8) & 0x00ff00ffu) | ((m << 8) & 0xff00ff00u); break;
                case 3: R[d] = sx(((m >> 8) & 0xff) | ((m & 0xff) << 8), 16); break;
                default: trap_op(c, pc, op, "undefined instruction");
                }
                pc = next; continue;
            }
            case 0xc: case 0xd: { /* POP */
                uint32_t a = R[13];
                for (int i = 0; i < 8; i++) if (op & (1u << i)) { R[i] = M32(a); a += 4; }
                if (op & 0x100) { t = M32(a); R[13] = a + 4; goto branch; }
                R[13] = a; pc = next; continue;
            }
            case 0xf: /* IT / hints */
                if ((op & 0xf) == 0) { pc = next; continue; } /* NOP, YIELD, WFE, WFI, SEV */
                trap_op(c, pc, op, "IT block not supported");
                continue;
            default: trap_op(c, pc, op, "undefined instruction"); continue;
            }
        case 0x18: { /* STMIA */
            uint32_t b = (op >> 8) & 7, a = R[b];
            for (int i = 0; i < 8; i++) if (op & (1u << i)) { ST32(a, R[i]); a += 4; }
            R[b] = a; pc = next; continue;
        }
        case 0x19: { /* LDMIA */
            uint32_t b = (op >> 8) & 7, a = R[b];
            for (int i = 0; i < 8; i++) if (op & (1u << i)) { R[i] = M32(a); a += 4; }
            if (!(op & (1u << b))) R[b] = a;
            pc = next; continue;
        }
        case 0x1a: case 0x1b: { /* B<cond> / SVC */
            uint32_t cond = (op >> 8) & 15;
            if (cond >= 14) { trap_op(c, pc, op, cond == 15 ? "SVC" : "undefined instruction"); continue; }
            if (cond_ok(cond, fn, fz, fc, fv)) { t = (pc + 4 + (sx(op & 0xff, 8) << 1)) | 1; goto branch; }
            pc = next; continue;
        }
        case 0x1c: t = (pc + 4 + (sx(op & 0x7ff, 11) << 1)) | 1; goto branch; /* B */
        default: break; /* 32-bit instructions below */
        }

        /* ---- 32-bit: BL / BLX pairs (ARMv5) and the Thumb-2 subset of the patch segment ---- */
        {
            uint32_t hw1 = op, hw2 = M16(pc + 2);
            uint32_t op32 = hw1 | hw2 << 16;
            next = pc + 4;
            if ((hw1 >> 11) == 0x1e && (hw2 & 0x8000)) { /* branches */
                uint32_t s = (hw1 >> 10) & 1, j1 = (hw2 >> 13) & 1, j2 = (hw2 >> 11) & 1;
                uint32_t i1 = !(j1 ^ s), i2 = !(j2 ^ s);
                switch (hw2 & 0x5000) {
                case 0x5000: case 0x4000: { /* BL / BLX imm */
                    uint32_t off = sx(s << 24 | i1 << 23 | i2 << 22 | (hw1 & 0x3ff) << 12 | (hw2 & 0x7ff) << 1, 25);
                    R[14] = next | 1;
                    if (hw2 & 0x1000) t = (pc + 4 + off) | 1;
                    else t = (pc + 4 + off) & ~3u; /* BLX: ARM state */
                    goto branch;
                }
                case 0x1000: { /* B.W */
                    uint32_t off = sx(s << 24 | i1 << 23 | i2 << 22 | (hw1 & 0x3ff) << 12 | (hw2 & 0x7ff) << 1, 25);
                    t = (pc + 4 + off) | 1; goto branch;
                }
                default: { /* B<cond>.W */
                    uint32_t cond = (hw1 >> 6) & 15;
                    uint32_t off = sx(s << 20 | j2 << 19 | j1 << 18 | (hw1 & 0x3f) << 12 | (hw2 & 0x7ff) << 1, 21);
                    if (cond >= 14) break;
                    if (cond_ok(cond, fn, fz, fc, fv)) { t = (pc + 4 + off) | 1; goto branch; }
                    pc = next; continue;
                }
                }
            } else if ((hw1 >> 11) == 0x1e && !(hw1 & 0x200)) { /* data processing, modified immediate */
                uint32_t rn = hw1 & 15, rd = (hw2 >> 8) & 15, s = (hw1 >> 4) & 1, carry = fc;
                uint32_t imm = t2_imm(hw1, hw2, &carry), a = R[rn], x;
                switch ((hw1 >> 5) & 15) {
                case 0x0: x = a & imm; if (s) { SET_NZ(x); fc = carry; } if (rd != 15) R[rd] = x; pc = next; continue; /* AND/TST */
                case 0x1: x = a & ~imm; if (s) { SET_NZ(x); fc = carry; } R[rd] = x; pc = next; continue;
                case 0x2: x = rn == 15 ? imm : a | imm; if (s) { SET_NZ(x); fc = carry; } R[rd] = x; pc = next; continue;
                case 0x3: x = rn == 15 ? ~imm : a | ~imm; if (s) { SET_NZ(x); fc = carry; } R[rd] = x; pc = next; continue;
                case 0x4: x = a ^ imm; if (s) { SET_NZ(x); fc = carry; } if (rd != 15) R[rd] = x; pc = next; continue;
                case 0x8: if (s) x = ADDF(a, imm, 0); else x = a + imm; if (rd != 15) R[rd] = x; pc = next; continue; /* ADD/CMN */
                case 0xa: { uint32_t f[4] = {fn, fz, fc, fv}; x = ADDF(a, imm, fc); if (!s) { fn = f[0]; fz = f[1]; fc = f[2]; fv = f[3]; } R[rd] = x; pc = next; continue; }
                case 0xb: { uint32_t f[4] = {fn, fz, fc, fv}; x = SBCF(a, imm); if (!s) { fn = f[0]; fz = f[1]; fc = f[2]; fv = f[3]; } R[rd] = x; pc = next; continue; }
                case 0xd: if (s) x = SUBF(a, imm); else x = a - imm; if (rd != 15) R[rd] = x; pc = next; continue; /* SUB/CMP */
                case 0xe: if (s) x = SUBF(imm, a); else x = imm - a; R[rd] = x; pc = next; continue; /* RSB */
                default: break;
                }
            } else if ((hw1 >> 11) == 0x1e) { /* plain binary immediate */
                uint32_t rn = hw1 & 15, rd = (hw2 >> 8) & 15;
                uint32_t imm12 = ((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8 | (hw2 & 0xff);
                uint32_t imm16 = (hw1 & 15) << 12 | imm12;
                uint32_t base = rn == 15 ? (pc + 4) & ~3u : R[rn];
                switch ((hw1 >> 4) & 0x1f) {
                case 0x00: R[rd] = base + imm12; pc = next; continue; /* ADDW / ADR */
                case 0x0a: R[rd] = base - imm12; pc = next; continue; /* SUBW / ADR */
                case 0x04: R[rd] = imm16; pc = next; continue;        /* MOVW */
                case 0x0c: R[rd] = (R[rd] & 0xffff) | imm16 << 16; pc = next; continue; /* MOVT */
                default: break;
                }
            } else if ((hw1 & 0xfe40) == 0xe800) { /* LDM / STM (IA, DB) */
                uint32_t rn = hw1 & 15, w = (hw1 >> 5) & 1, l = (hw1 >> 4) & 1, list = hw2;
                uint32_t n = __builtin_popcount(list), a = R[rn];
                uint32_t mode = (hw1 >> 7) & 3; /* 1 = IA, 2 = DB */
                if (mode == 1 || mode == 2) {
                    uint32_t start = mode == 1 ? a : a - 4 * n, end = mode == 1 ? a + 4 * n : a - 4 * n;
                    if (l) {
                        uint32_t p = start, newpc = 0;
                        for (int i = 0; i < 16; i++) if (list & (1u << i)) { uint32_t v = M32(p); p += 4; if (i == 15) newpc = v; else R[i] = v; }
                        if (w && !(list & (1u << rn))) R[rn] = end;
                        if (list & 0x8000) { t = newpc; goto branch; }
                    } else {
                        uint32_t p = start;
                        for (int i = 0; i < 16; i++) if (list & (1u << i)) { ST32(p, R[i]); p += 4; }
                        if (w) R[rn] = end;
                    }
                    pc = next; continue;
                }
            } else if ((hw1 & 0xfe50) == 0xe840 || (hw1 & 0xfe50) == 0xe850) { /* LDRD / STRD immediate */
                uint32_t p = (hw1 >> 8) & 1, u = (hw1 >> 7) & 1, w = (hw1 >> 5) & 1, l = (hw1 >> 4) & 1;
                uint32_t rn = hw1 & 15, rt = (hw2 >> 12) & 15, rt2 = (hw2 >> 8) & 15, off = (hw2 & 0xff) * 4;
                if (p || w) {
                    uint32_t base = rn == 15 ? (pc + 4) & ~3u : R[rn];
                    uint32_t addr = u ? base + off : base - off, a = p ? addr : base;
                    if (l) { R[rt] = M32(a); R[rt2] = M32(a + 4); } else { ST32(a, R[rt]); ST32(a + 4, R[rt2]); }
                    if (w) R[rn] = addr;
                    pc = next; continue;
                }
            } else if ((hw1 & 0xfe00) == 0xea00) { /* data processing, shifted register */
                uint32_t rn = hw1 & 15, rd = (hw2 >> 8) & 15, rm = hw2 & 15, s = (hw1 >> 4) & 1, carry = fc;
                uint32_t b = t2_shift(R[rm], (hw2 >> 4) & 3, ((hw2 >> 12) & 7) << 2 | ((hw2 >> 6) & 3), &carry);
                uint32_t a = R[rn], x;
                switch ((hw1 >> 5) & 15) {
                case 0x0: x = a & b; if (s) { SET_NZ(x); fc = carry; } if (rd != 15) R[rd] = x; pc = next; continue;
                case 0x1: x = a & ~b; if (s) { SET_NZ(x); fc = carry; } R[rd] = x; pc = next; continue;
                case 0x2: x = rn == 15 ? b : a | b; if (s) { SET_NZ(x); fc = carry; } R[rd] = x; pc = next; continue;
                case 0x3: x = rn == 15 ? ~b : a | ~b; if (s) { SET_NZ(x); fc = carry; } R[rd] = x; pc = next; continue;
                case 0x4: x = a ^ b; if (s) { SET_NZ(x); fc = carry; } if (rd != 15) R[rd] = x; pc = next; continue;
                case 0x8: if (s) x = ADDF(a, b, 0); else x = a + b; if (rd != 15) R[rd] = x; pc = next; continue;
                case 0xd: if (s) x = SUBF(a, b); else x = a - b; if (rd != 15) R[rd] = x; pc = next; continue;
                case 0xe: if (s) x = SUBF(b, a); else x = b - a; R[rd] = x; pc = next; continue;
                default: break;
                }
            } else if ((hw1 & 0xfe00) == 0xf800 && (hw1 & 0x60) != 0x60) { /* load / store single */
                uint32_t sgn = (hw1 >> 8) & 1, size = (hw1 >> 5) & 3, l = (hw1 >> 4) & 1;
                uint32_t rn = hw1 & 15, rt = (hw2 >> 12) & 15, a, wb = 0, wbv = 0;
                uint32_t base = rn == 15 ? (pc + 4) & ~3u : R[rn];
                if ((hw1 & 0x80) || rn == 15) { /* imm12 (literal: U = bit 7) */
                    a = rn == 15 && !(hw1 & 0x80) ? base - (hw2 & 0xfff) : base + (hw2 & 0xfff);
                } else if (hw2 & 0x800) { /* imm8 with P/U/W */
                    uint32_t p = (hw2 >> 10) & 1, u = (hw2 >> 9) & 1, w = (hw2 >> 8) & 1, off = hw2 & 0xff;
                    uint32_t addr = u ? base + off : base - off;
                    a = p ? addr : base;
                    if (w) { wb = 1; wbv = addr; }
                } else if ((hw2 & 0xfc0) == 0) { /* register, LSL #imm2 */
                    a = base + (R[hw2 & 15] << ((hw2 >> 4) & 3));
                } else break;
                if (l) {
                    uint32_t v;
                    switch (size) {
                    case 0: v = sgn ? MS8(a) : M8(a); break;
                    case 1: v = sgn ? MS16(a) : M16(a); break;
                    default: v = M32(a); break;
                    }
                    if (wb) R[rn] = wbv;
                    if (rt == 15) { t = v; goto branch; }
                    R[rt] = v;
                } else {
                    switch (size) {
                    case 0: ST8(a, (uint8_t)R[rt]); break;
                    case 1: ST16(a, (uint16_t)R[rt]); break;
                    default: ST32(a, R[rt]); break;
                    }
                    if (wb) R[rn] = wbv;
                }
                pc = next; continue;
            } else if ((hw1 & 0xfff0) == 0xfb00 && (hw2 & 0xf0) == 0) { /* MUL / MLA */
                uint32_t ra = (hw2 >> 12) & 15, x = R[hw1 & 15] * R[hw2 & 15];
                R[(hw2 >> 8) & 15] = ra == 15 ? x : x + R[ra];
                pc = next; continue;
            } else if (((hw1 & 0xfff0) == 0xfba0 || (hw1 & 0xfff0) == 0xfb80) && (hw2 & 0xf0) == 0) { /* UMULL / SMULL */
                uint64_t x = (hw1 & 0x20) ? (uint64_t)R[hw1 & 15] * R[hw2 & 15]
                                          : (uint64_t)((int64_t)(int32_t)R[hw1 & 15] * (int32_t)R[hw2 & 15]);
                R[(hw2 >> 12) & 15] = (uint32_t)x; R[(hw2 >> 8) & 15] = (uint32_t)(x >> 32);
                pc = next; continue;
            } else if ((hw1 & 0xfff0) == 0xfa50 && (hw2 & 0xf0c0) == 0xf080) { /* UXTB.W */
                R[(hw2 >> 8) & 15] = ror32(R[hw2 & 15], ((hw2 >> 4) & 3) * 8) & 0xff;
                pc = next; continue;
            }
            trap_op(c, pc, op32, "unsupported Thumb-2 instruction");
            continue;
        }

    branch:
        /* t: absolute target, bit 0 = Thumb state */
        if (t == RC_RET_MAGIC) return;
        {
            GuestFn host = NULL;
            uint32_t va = (t & ~1u) - IMG;
            if (va < RC_IMAGE_END) {
                if (t & 1) {
                    /* a hooked entry runs its host wrapper or native routine (accel.c may decline: NULL) */
                    if (!rc_hookmap[va >> 1] || (host = rc_hook_fn(va)) == NULL) { pc = t & ~1u; continue; }
                } else { /* ARM state: the PLT, or an ARM function the host replaces (rc_arm_fn) */
                    uint32_t slot;
                    if ((host = rc_arm_fn(va)) == NULL) {
                        if (!plt_slot(t, &slot)) { rc_trap(c, va, "interp: branch to ARM code that is not a PLT stub"); return; }
                        t = M32(slot);
                        if (t == RC_RET_MAGIC) return;
                        host = rc_host_target(t);
                    }
                }
            } else host = rc_host_target(t);
            if (!host) { rc_trap(c, va, "interp: branch to an unknown address"); return; }
            uint32_t ret = R[14];
            host(c);
            t = ret;
            if (t == RC_RET_MAGIC) return;
            pc = t & ~1u; /* return addresses are always Thumb code of the image */
        }
    }
}
