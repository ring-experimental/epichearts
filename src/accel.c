/* Native versions of the original's hottest leaf routines, for the interpreter build (EH_INTERP).
 *
 * The interpreter (interp.c) runs the original's software renderer instruction by instruction. A profile of the bot
 * playing the opening (-DEH_IPROF) put most of the work in a few pixel loops of the original's graphics library
 * (alPaletteEf_R5G6B5 35%, fnGFFillRect_2b 17%, asm_blitpal_2b 10%, alFade_R5G6B5 7%, ...). Each is written here
 * from its disassembly, with the same arguments, memory effects and return value; the interpreter runs it in place of
 * the original when control enters the routine's address (hook map, interp.h).
 *
 * Checking (-DEH_ACCEL_CHECK build, EH_ACCEL_CHECK=1): every call first runs the original in the interpreter with
 * its stores logged, undoes them, runs the native routine, and compares each logged location (stores below the
 * entry sp, the original's own stack frame, are not compared). Mismatches are logged per routine.
 * EH_ACCEL=0 turns the native routines off (pure interpreter).
 *
 * Addresses are ELF vaddrs (Ghidra = vaddr + 0x10000). Pixels are RGB565. */
#include "rc.h"
#include "host.h"
#include "interp.h"
#include <stdlib.h>
#include <string.h>

#define A0 (c->r[0])
#define A1 (c->r[1])
#define A2 (c->r[2])
#define A3 (c->r[3])
#define ARG4 M32(c->r[13])

/* 0x0000f81f (magenta): the palette key the palette routines leave alone (literal @001616bc) */
#define KEY565 0xf81fu

/* the 32-bit color word of the graphics library -> RGB565: red from bits 11..15, green from bits 18..23, blue from
 * bits 27..31 (fnGFFillRect_2b @0015f434, alPaletteEf_R5G6B5 mode 5 @0016168a, alBlendColor_R5G6B5 @0016135c) */
static inline uint32_t color565(uint32_t c) { return (c & 0xf800u) | ((c >> 16) & 0xfcu) << 3 | c >> 27; }

/* call a guest routine (function pointer of the original) with four arguments */
static void call4(Cpu *c, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    c->r[0] = a0; c->r[1] = a1; c->r[2] = a2; c->r[3] = a3;
    rc_dispatch(c, fn);
}

/* alPaletteEf_R5G6B5(?, pal, fx) @001615b8: palette effect on pal {int count @0, u16 *colors @4}, entries equal to
 * the key are kept. fx: flags @0 (bit 11 = on), level u16 @8, mode u8 @0xa, color word ** @0xc.
 *  mode 0: scale each channel by level/32; mode 4: gray ((r + g5 + b) * 85 / 256 in all three); mode 5: fill */
static void nat_alPaletteEf(Cpu *c) {
    uint32_t fx = A2, pal = A1;
    if (!fx || !(M32(fx) & 0x800)) return;
    int32_t n = (int32_t)M32(pal);
    uint32_t p = M32(pal + 4);
    uint32_t mode = M8(fx + 0xa);
    if (n <= 0 || (mode != 0 && mode != 4 && mode != 5)) return;
    uint32_t end = p + (uint32_t)n * 2;
    if (mode == 0) {
        for (; p != end; p += 2) {
            uint32_t v = M16(p), lv = M16(fx + 8);
            if (v == KEY565) continue;
            uint32_t g = ((v & 0x7e0u) * lv >> 5) & 0x7e0u, r = ((v & 0xf800u) * lv >> 5) & 0xf800u;
            uint32_t b = ((v & 0x1fu) * lv >> 5) & 0x1fu;
            M16(p) = (uint16_t)(g | r | b);
        }
    } else if (mode == 4) {
        for (; p != end; p += 2) {
            uint32_t v = M16(p);
            if (v == KEY565) continue;
            uint32_t s = (v >> 11) + ((v >> 6) & 31) + (v & 31);
            s *= 5; s += s << 4; s >>= 8;
            M16(p) = (uint16_t)(s << 11 | s << 6 | s);
        }
    } else {
        uint32_t col = color565(M32(M32(fx + 0xc)));
        for (; p != end; p += 2)
            if (M16(p) != KEY565) M16(p) = (uint16_t)col;
    }
}

/* fnGFFillRect_2b(g, rect, color) @0015f434: rect {x, y, w, h} is moved by the origin g+0x10 / g+0x14 (written back
 * into rect), then filled; g: pixels @0, stride (bytes) @0xc */
static void nat_fnGFFillRect(Cpu *c) {
    uint32_t g = A0, rc = A1, col = color565(A2) & 0xffff;
    int32_t w = (int32_t)M32(rc + 8), h = (int32_t)M32(rc + 0xc);
    if (w <= 0 || h <= 0) return;
    uint32_t x = M32(g + 0x10) + M32(rc), y = M32(g + 0x14) + M32(rc + 4);
    M32(rc) = x; M32(rc + 4) = y;
    uint32_t stride = M32(g + 0xc), row = M32(g) + stride * y + x * 2;
    for (int32_t i = 0; i < h; i++, row += stride)
        for (int32_t j = 0; j < w; j++) M16(row + 2 * j) = (uint16_t)col;
}

/* palette row copies (unnamed, after cflConstStr::TextRenderW): (dst, src, pal16, n), dst[i] = pal16[src[i]];
 * @0015f3a4 forward, @0015f3c0 backward (dst steps back before each store) */
static void nat_palrow(Cpu *c) {
    uint32_t d = A0, s = A1, pal = A2, n = A3;
    for (uint32_t i = 0; i < n; i++, d += 2) M16(d) = M16(pal + M8(s + i) * 2);
}
static void nat_palrow_lr(Cpu *c) {
    uint32_t d = A0, s = A1, pal = A2, n = A3;
    for (uint32_t i = 0; i < n; i++) { d -= 2; M16(d) = M16(pal + M8(s + i) * 2); }
}

/* asm_blitpal_2b(param, n, dst, src) @0015f7a8 and its variants: a row of 8-bit pixels through the palette
 * param+4 (colors at +0x24). param+8 nonzero: that routine does the row instead, (dst, src, param+0xc, n).
 *  _lr @0015fc60: right to left. tblitpal_notcompress @0015f7e0 / _lr @0015fc98: pixel 0 is transparent. */
static void nat_blitpal(Cpu *c) {
    uint32_t prm = A0, n = A1, d = A2, s = A3, fn = M32(prm + 8);
    if (fn) { call4(c, fn, d, s, M32(prm + 0xc), n); return; }
    uint32_t pal = M32(prm + 4) + 0x24;
    if ((int32_t)n <= 0) return;
    for (uint32_t i = 0; i < n; i++, d += 2) M16(d) = M16(pal + M8(s + i) * 2);
}
static void nat_blitpal_lr(Cpu *c) {
    uint32_t prm = A0, n = A1, d = A2, s = A3, fn = M32(prm + 8);
    if (fn) { call4(c, fn, d, s, M32(prm + 0xc), n); return; }
    uint32_t pal = M32(prm + 4) + 0x24;
    if ((int32_t)n <= 0) return;
    for (uint32_t i = 0; i < n; i++) { d -= 2; M16(d) = M16(pal + M8(s + i) * 2); }
}
static void nat_tblitpal_nc(Cpu *c) {
    uint32_t prm = A0, n = A1, d = A2, s = A3, fn = M32(prm + 8);
    if (fn) { call4(c, fn, d, s, M32(prm + 0xc), n); return; }
    uint32_t pal = M32(prm + 4) + 0x24;
    if ((int32_t)n <= 0) return;
    for (uint32_t i = 0; i < n; i++, d += 2) { uint32_t v = M8(s + i); if (v) M16(d) = M16(pal + v * 2); }
}
static void nat_tblitpal_nc_lr(Cpu *c) {
    uint32_t prm = A0, n = A1, d = A2, s = A3, fn = M32(prm + 8);
    if (fn) { call4(c, fn, d, s, M32(prm + 0xc), n); return; }
    uint32_t pal = M32(prm + 4) + 0x24;
    if ((int32_t)n <= 0) return;
    for (uint32_t i = 0; i < n; i++) { uint32_t v = M8(s + i); d -= 2; if (v) M16(d) = M16(pal + v * 2); }
}

/* tint of a horizontal run (unnamed, after cGameTipMgr::GetInstance) @00122b84: (g, x, y, level, w), each channel
 * times level / 32, clipped to the screen size at vaddr 0x19b16c (height) / 0x19b170 (width) */
static void nat_tintrow(Cpu *c) {
    uint32_t g = A0;
    int32_t x = (int32_t)A1, y = (int32_t)A2, w = (int32_t)ARG4;
    int32_t lv = (int32_t)A3;
    if (y < 0 || y >= (int32_t)M32(IMG + 0x19b16c)) return;
    if (x < 0) { w += x; x = 0; }
    int32_t sw = (int32_t)M32(IMG + 0x19b170);
    if (w + x > sw) w = sw - x;
    if (w <= 0) return;
    uint32_t p = M32(g) + (uint32_t)x * 2 + M32(g + 0xc) * (uint32_t)y, end = p + (uint32_t)w * 2;
    for (; p != end; p += 2) {
        uint32_t v = M16(p);
        uint32_t gg = (uint32_t)((int32_t)((v & 0x7e0u) * (uint32_t)lv) >> 5) & 0x7e0u;
        uint32_t r = (uint32_t)((int32_t)((v & 0xf800u) * (uint32_t)lv) >> 5) & 0xfffff800u;
        uint32_t b = (uint32_t)((int32_t)((v & 0x1fu) * (uint32_t)lv) >> 5) & 0x1fu;
        M16(p) = (uint16_t)(gg | r | b);
    }
}

/* alFade_R5G6B5(s, amount) @00161428 -> number of pixels changed. s: pixels @0, row width @4, rows @8,
 * stride @0xc, x @0x10, y @0x14. amount > 0: add to each 5-bit channel (green's low bit dropped), clamped at 31,
 * white (0xffff, literal @00161558) kept; amount < 0: subtract, clamped at 0, black kept */
static void nat_alFade(Cpu *c) {
    uint32_t s = A0;
    int32_t a = (int32_t)A1;
    uint32_t total = 0;
    if (a != 0 && (int32_t)M32(s + 8) > 0) {
        uint32_t row = M32(s) + M32(s + 0x10) * 2 + M32(s + 0x14) * M32(s + 0xc);
        int32_t rows = (int32_t)M32(s + 8);
        uint32_t stride = M32(s + 0xc);
        int neg = a < 0;
        if (neg) a = -a;
        for (int32_t i = 0;;) {
            uint32_t w = M32(s + 4), cnt = 0;
            if (w) {
                uint32_t p = row, end = row + w * 2;
                for (; p != end; p += 2) {
                    uint32_t v = M16(p);
                    int32_t r = (int32_t)(v >> 11), g = (int32_t)((v >> 6) & 31), b = (int32_t)(v & 31);
                    if (!neg) {
                        if (v == 0xffffu) continue;
                        r += a; g += a; b += a;
                        if (r > 31) r = 31;
                        if (g > 31) g = 31;
                        if (b > 31) b = 31;
                    } else {
                        if (v == 0) continue;
                        r -= a; g -= a; b -= a;
                        if (r < 0) r = 0;
                        if (g < 0) g = 0;
                        if (b < 0) b = 0;
                    }
                    M16(p) = (uint16_t)((uint32_t)r << 11 | (uint32_t)g << 6 | (uint32_t)b);
                    cnt++;
                }
                stride = M32(s + 0xc);
                rows = (int32_t)M32(s + 8);
            }
            total += cnt;
            if (rows <= ++i) break;
            row += stride;
        }
    }
    c->r[0] = total;
}

/* alBlendColor_R5G6B5(g, rect, fx) @0016134c: rect {x, y, w, h} moved by the origin g+0x10 / g+0x14 (written back),
 * every pixel blended toward the color fx+4 by alpha fx+0xb (out of 32) */
static void nat_alBlendColor(Cpu *c) {
    uint32_t g = A0, rc = A1, fx = A2;
    uint32_t col = color565(M32(fx + 4));
    uint32_t x = M32(g + 0x10) + M32(rc), y = M32(g + 0x14) + M32(rc + 4);
    M32(rc) = x; M32(rc + 4) = y;
    uint32_t al = M8(fx + 0xb), inv = 32 - al;
    uint32_t rb = (col & KEY565) * al, gr = (col & 0x7e0u) * al;
    int32_t h = (int32_t)M32(rc + 0xc);
    if (h <= 0) return;
    uint32_t stride = M32(g + 0xc), row = M32(g) + stride * y + x * 2;
    for (int32_t i = 0;;) {
        int32_t w = (int32_t)M32(rc + 8);
        if (w > 0) {
            uint32_t p = row;
            for (int32_t j = 0; j < (int32_t)M32(rc + 8); j++, p += 2) {
                uint32_t v = M16(p);
                M16(p) = (uint16_t)((((v & KEY565) * inv + rb) >> 5 & KEY565) | (((v & 0x7e0u) * inv + gr) >> 5 & 0x7e0u));
            }
            h = (int32_t)M32(rc + 0xc);
            stride = M32(g + 0xc);
        }
        if (h <= ++i) break;
        row += stride;
    }
}

/* alpha blend of a palette row (unnamed, after alPaletteEf_R5G6B5) @001616c0: (dst, src, blend, n);
 * blend {u16 *pal @0, u8 *table @4}: s = pal[src[i]], each 5-bit channel out = table[dst channel * 32 + s channel]
 * (green from bits 6..10, its low bit dropped) */
static void nat_alpharow(Cpu *c) {
    uint32_t d = A0, s = A1, n = A3, pal = M32(A2), tab = M32(A2 + 4);
    for (uint32_t i = 0; i < n; i++, d += 2) {
        uint32_t sv = M16(pal + M8(s + i) * 2), dv = M16(d);
        uint32_t r = M8(tab + (dv >> 11) * 32 + (sv >> 11)) & 31;
        uint32_t g = M8(tab + ((dv >> 6) & 31) * 32 + ((sv >> 6) & 31)) & 31;
        uint32_t b = M8(tab + (dv & 31) * 32 + (sv & 31)) & 31;
        M16(d) = (uint16_t)(r << 11 | g << 6 | b);
    }
}

/* same, right to left (dst steps back before each pixel) @00161a64 */
static void nat_alpharow_lr(Cpu *c) {
    uint32_t d = A0, s = A1, n = A3, pal = M32(A2), tab = M32(A2 + 4);
    for (uint32_t i = 0; i < n; i++) {
        d -= 2;
        uint32_t sv = M16(pal + M8(s + i) * 2), dv = M16(d);
        uint32_t r = M8(tab + (dv >> 11) * 32 + (sv >> 11)) & 31;
        uint32_t g = M8(tab + ((dv >> 6) & 31) * 32 + ((sv >> 6) & 31)) & 31;
        uint32_t b = M8(tab + (dv & 31) * 32 + (sv & 31)) & 31;
        M16(d) = (uint16_t)(r << 11 | g << 6 | b);
    }
}
/* same, right to left, source pixel 0 transparent @00161ae4 */
static void nat_alpharow_t_lr(Cpu *c) {
    uint32_t d = A0, s = A1, n = A3, pal = M32(A2), tab = M32(A2 + 4);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = M8(s + i);
        d -= 2;
        if (!v) continue;
        uint32_t sv = M16(pal + v * 2), dv = M16(d);
        uint32_t r = M8(tab + (dv >> 11) * 32 + (sv >> 11)) & 31;
        uint32_t g = M8(tab + ((dv >> 6) & 31) * 32 + ((sv >> 6) & 31)) & 31;
        uint32_t b = M8(tab + (dv & 31) * 32 + (sv & 31)) & 31;
        M16(d) = (uint16_t)(r << 11 | g << 6 | b);
    }
}

/* constant alpha of a palette row @00161b70: (dst, src, blend {u16 *pal @0, alpha @4}, n), out of 32:
 * channel = (dst * (32 - alpha) + src * alpha) >> 5, green 6-bit */
static void nat_constalpha(Cpu *c) {
    uint32_t d = A0, s = A1, n = A3, pal = M32(A2), a = M32(A2 + 4), inv = 32 - a;
    for (uint32_t i = 0; i < n; i++, d += 2) {
        uint32_t dv = M16(d), sv = M16(pal + M8(s + i) * 2);
        uint32_t r = (dv >> 11) * inv + (sv >> 11) * a;
        uint32_t g = ((dv >> 5) & 63) * inv + ((sv >> 5) & 63) * a;
        uint32_t b = (sv & 31) * a + (dv & 31) * inv;
        M16(d) = (uint16_t)((r >> 5) << 11 | (g >> 5) << 5 | b >> 5);
    }
}

/* alpha map row @00161c28: (dst, src, blend {u16 *pal @0, alpha @4, u8 *map @0x10}, n). With a map, pixel i uses
 * a = alpha * (map + 1) >> 5 out of 256 where its map byte is nonzero, and blend+0x10 advances one byte per pixel
 * (written back). Without a map (@00161cf6) the original runs. */
static void nat_alphamap(Cpu *c) {
    uint32_t bl = A2;
    if (!M32(bl + 0x10)) { rc_orig(c, 0x161c28); return; }
    uint32_t d = A0, s = A1, n = A3, pal = M32(bl);
    for (uint32_t i = 0; i < n; i++, d += 2) {
        uint32_t mp = M32(bl + 0x10), m = M8(mp);
        if (m) {
            int32_t a = (int32_t)(M32(bl + 4) * (m + 1)) >> 5;
            uint32_t inv = 256 - (uint32_t)a, ua = (uint32_t)a;
            uint32_t dv = M16(d), sv = M16(pal + M8(s + i) * 2);
            uint32_t b = (dv & 31) * inv + (sv & 31) * ua;
            uint32_t r = (dv >> 11) * inv + (sv >> 11) * ua;
            uint32_t g = ((dv >> 5) & 63) * inv + ((sv >> 5) & 63) * ua;
            M16(d) = (uint16_t)((r >> 8) << 11 | (g >> 8) << 5 | b >> 8);
        }
        M32(bl + 0x10) = mp + 1;
    }
}

/* ---- table ---- */
typedef struct { uint32_t va; GuestFn fn; const char *name; uint32_t calls, bad; } AccelEnt;
static AccelEnt g_accel[] = {
    {0x1615b8, nat_alPaletteEf, "alPaletteEf_R5G6B5"},
    {0x15f434, nat_fnGFFillRect, "fnGFFillRect_2b"},
    {0x15f3a4, nat_palrow, "palette row"},
    {0x15f3c0, nat_palrow_lr, "palette row, right to left"},
    {0x15f7a8, nat_blitpal, "asm_blitpal_2b"},
    {0x15fc60, nat_blitpal_lr, "asm_blitpal_lr_2b"},
    {0x15f7e0, nat_tblitpal_nc, "asm_tblitpal_notcompress_2b"},
    {0x15fc98, nat_tblitpal_nc_lr, "asm_tblitpal_notcompress_lr_2b"},
    {0x122b84, nat_tintrow, "tint row"},
    {0x161428, nat_alFade, "alFade_R5G6B5"},
    {0x16134c, nat_alBlendColor, "alBlendColor_R5G6B5"},
    {0x1616c0, nat_alpharow, "alpha blend row"},
    {0x161a64, nat_alpharow_lr, "alpha blend row, right to left"},
    {0x161ae4, nat_alpharow_t_lr, "alpha blend row, transparent, right to left"},
    {0x161b70, nat_constalpha, "constant alpha row"},
    {0x161c28, nat_alphamap, "alpha map row"},
};
#define NACCEL (sizeof(g_accel) / sizeof(g_accel[0]))
static int g_accel_on = 1;

#ifdef EH_ACCEL_CHECK
/* ---- checking: original (logged) vs native ---- */
#ifdef _MSC_VER
#define TLS __declspec(thread)
#else
#define TLS _Thread_local
#endif
typedef struct { uint32_t addr, size, oldv, newv; } WEnt;
TLS int interp_wlog_on;
static TLS WEnt *t_log;
static TLS uint32_t t_n, t_cap;
static TLS int t_checking;
static int g_check;

static uint32_t rd(uint32_t a, uint32_t n) { return n == 4 ? M32(a) : n == 2 ? M16(a) : M8(a); }
static void wr(uint32_t a, uint32_t n, uint32_t v) { if (n == 4) M32(a) = v; else if (n == 2) M16(a) = (uint16_t)v; else M8(a) = (uint8_t)v; }
void interp_wlog_add(uint32_t addr, int size) {
    if (t_n == t_cap) { t_cap = t_cap ? t_cap * 2 : 65536; t_log = realloc(t_log, t_cap * sizeof(WEnt)); }
    t_log[t_n].addr = addr; t_log[t_n].size = (uint32_t)size; t_log[t_n].oldv = rd(addr, (uint32_t)size);
    t_n++;
}

static void check_call(Cpu *c, AccelEnt *e) {
    uint32_t regs[16], base = t_n, sp = c->r[13];
    memcpy(regs, c->r, sizeof regs);
    int was_on = interp_wlog_on;
    t_checking++;
    interp_wlog_on = 1;
    c->r[14] = 0xfffffff1u;
    interp_run(c, IMG + e->va); /* the original */
    interp_wlog_on = was_on;
    t_checking--;
    uint32_t r0 = c->r[0], end = t_n;
    for (uint32_t i = base; i < end; i++) t_log[i].newv = rd(t_log[i].addr, t_log[i].size);
    for (uint32_t i = end; i-- > base;) wr(t_log[i].addr, t_log[i].size, t_log[i].oldv);
    memcpy(c->r, regs, sizeof regs);
    e->fn(c); /* native (may log more entries above end while nested; they are its own) */
    uint32_t bad = 0;
    for (uint32_t i = base; i < end; i++) {
        WEnt *w = &t_log[i];
        if (w->addr < sp && w->addr >= sp - 0x10000) continue; /* the original's stack frame */
        uint32_t v = rd(w->addr, w->size);
        if (v != w->newv) {
            if (bad < 4 && e->bad < 40)
                host_log("[accel] MISMATCH %s @%06x: %08x (%u bytes) original %x native %x (args %x %x %x %x)", e->name, e->va,
                         w->addr, w->size, w->newv, v, regs[0], regs[1], regs[2], regs[3]);
            bad++;
        }
    }
    if (e->va == 0x161428 && c->r[0] != r0) { host_log("[accel] MISMATCH %s return %u vs %u", e->name, r0, c->r[0]); bad++; }
    e->calls++;
    if (bad) e->bad++;
    if ((e->calls & (e->calls - 1)) == 0 && e->calls >= 1024)
        host_log("[accel] check %s: %u calls, %u with differences", e->name, e->calls, e->bad);
    t_n = base;
}

#define CHECK_TRAMP(i) static void chk_##i(Cpu *c) { check_call(c, &g_accel[i]); }
CHECK_TRAMP(0) CHECK_TRAMP(1) CHECK_TRAMP(2) CHECK_TRAMP(3) CHECK_TRAMP(4) CHECK_TRAMP(5) CHECK_TRAMP(6)
CHECK_TRAMP(7) CHECK_TRAMP(8) CHECK_TRAMP(9) CHECK_TRAMP(10) CHECK_TRAMP(11) CHECK_TRAMP(12) CHECK_TRAMP(13)
CHECK_TRAMP(14) CHECK_TRAMP(15)
static GuestFn g_chk[] = {chk_0, chk_1, chk_2, chk_3, chk_4, chk_5, chk_6, chk_7, chk_8, chk_9, chk_10, chk_11,
                          chk_12, chk_13, chk_14, chk_15};
#endif

void accel_init(unsigned char *hookmap) {
    const char *e = getenv("EH_ACCEL");
    g_accel_on = !(e && *e == '0');
#ifdef EH_ACCEL_CHECK
    g_check = getenv("EH_ACCEL_CHECK") != NULL;
    if (sizeof g_chk / sizeof g_chk[0] != NACCEL) host_fatal("accel: check table size");
    if (g_check) host_log("[accel] checking native routines against the original");
#endif
    if (!g_accel_on) { host_log("[accel] native routines off (EH_ACCEL=0)"); return; }
    for (size_t i = 0; i < NACCEL; i++) {
        uint32_t va = g_accel[i].va;
        /* the entry must look like the original's (push {..., lr}): another image is never patched blindly */
        uint16_t op = M16(IMG + va);
        if ((op & 0xff00) != 0xb500) { host_log("[accel] %s @%06x: unexpected entry %04x, left to the interpreter", g_accel[i].name, va, op); g_accel[i].fn = NULL; continue; }
        hookmap[va >> 1] = 2;
    }
}

GuestFn accel_fn(uint32_t va) {
    for (size_t i = 0; i < NACCEL; i++)
        if (g_accel[i].va == va) {
#ifdef EH_ACCEL_CHECK
            if (t_checking) return NULL;
            if (g_check && g_accel[i].fn) return g_chk[i];
#endif
            return g_accel[i].fn;
        }
    return NULL;
}
