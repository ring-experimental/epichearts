/* Image loader for libtar-android.so: maps the PT_LOAD segments, applies .rel.dyn/.rel.plt,
 * provides import cells and the guest-address -> recompiled-function dispatch. */
#include "rc.h"
#ifdef EH_INTERP
#include "interp.h"
#include "hooks.h"
#else
#include "gen/gen.h"
#endif
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

uint32_t IMG;

typedef struct { uint32_t va; GuestFn fn; const char *name; } FuncEnt;
#ifdef EH_INTERP
/* function names for logs: the image's defined FUNC symbols (mangled), sorted by vaddr at load */
static FuncEnt *g_funcs;
static size_t g_nfuncs;
#else
static const FuncEnt g_funcs[] = {
#include "gen/functab.inc"
};
static const size_t g_nfuncs = sizeof(g_funcs) / sizeof(g_funcs[0]);
#endif

/* import cells live right after the image: vaddr IMPORT_VA + 4*i */
#define IMPORT_VA 0x1a0000u
#define HOSTFN_VA 0x1a1000u  /* host callbacks handed to guest (JNI tables etc.) */
#define HOSTFN_MAX 4096
#define MAP_SIZE 0x1a8000u

typedef struct { const char *name; GuestFn fn; } ImportEnt;
#ifdef EH_INTERP
#define IMPORT(n) void imp_##n(Cpu *c);
#include "imports.inc"
#undef IMPORT
void imp___aeabi_atexit(Cpu *c);
#define IMPORT(n) {#n, imp_##n},
static const ImportEnt g_imports[] = {
#include "imports.inc"
};
#else
#define IMPORT(n) {#n, imp_##n},
static const ImportEnt g_imports[] = {
#include "gen/imports.inc"
};
#endif
#undef IMPORT
#define NIMPORTS (sizeof(g_imports) / sizeof(g_imports[0]))

static GuestFn *g_disp;   /* indexed by vaddr >> 1 */
static GuestFn g_hostfn[HOSTFN_MAX];
static const char *g_hostfn_name[HOSTFN_MAX];
static int g_nhostfn;

uint32_t rc_import_addr(const char *name) {
    for (uint32_t i = 0; i < NIMPORTS; i++)
        if (!strcmp(g_imports[i].name, name)) return IMG + IMPORT_VA + 4 * i;
    return 0;
}

uint32_t rc_host_fn(GuestFn fn, const char *name) {
    if (g_nhostfn >= HOSTFN_MAX) { fprintf(stderr, "too many host fns\n"); abort(); }
    g_hostfn[g_nhostfn] = fn;
    g_hostfn_name[g_nhostfn] = name;
    return IMG + HOSTFN_VA + 4 * g_nhostfn++;
}

const char *rc_func_name(uint32_t va) {
    va &= ~1u;
    int lo = 0, hi = (int)g_nfuncs - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_funcs[mid].va <= va) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return best >= 0 ? g_funcs[best].name : "?";
}

/* nonzero if a named function starts exactly at va */
int rc_func_name_exact(uint32_t va) {
    va &= ~1u;
    int lo = 0, hi = (int)g_nfuncs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_funcs[mid].va == va) return 1;
        if (g_funcs[mid].va < va) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

void rc_trap(Cpu *c, uint32_t va, const char *why) {
    host_log("[rc] TRAP at vaddr %06x (ghidra %08x, in %s): %s\n  r0=%08x r1=%08x r2=%08x r3=%08x lr=%06x (%s) sp=%08x",
             va, va + 0x10000, rc_func_name(va), why, c->r[0], c->r[1], c->r[2], c->r[3], c->r[14] - IMG,
             rc_func_name(c->r[14] - IMG), c->r[13]);
    host_fatal("guest trap");
}

/* return address of host-initiated guest calls (rc_call). A callee can leave through a recompiled epilogue that
 * belongs to another generated function (shared epilogues, e.g. cFormPopup::somethingKeypress @000f2ed8 ->
 * FUN_000f2f7c): its pop {pc} then arrives here and must unwind back to rc_call instead of trapping. */
#define RET_MAGIC 0xfffffff1u

#ifdef EH_INTERP
/* ---- interpreter build: the image's code runs in interp.c ---- */
unsigned char *rc_hookmap;
typedef struct { uint32_t va; GuestFn fn; } HookEnt;
#define RC_HOOK_ENT(v) {0x##v##u, f_##v},
static const HookEnt g_hooks[] = {RC_HOOKS(RC_HOOK_ENT)};
#undef RC_HOOK_ENT

GuestFn rc_hook_fn(uint32_t va) {
    if (rc_hookmap[va >> 1] == 2) return accel_fn(va);
    for (size_t i = 0; i < sizeof(g_hooks) / sizeof(g_hooks[0]); i++)
        if (g_hooks[i].va == va) return g_hooks[i].fn;
    return accel_fn(va); /* native versions of hot original routines (accel.c), NULL when declined */
}

GuestFn rc_host_target(uint32_t addr) {
    uint32_t va = (addr & ~1u) - IMG;
    if (va >= IMPORT_VA && va < IMPORT_VA + 4 * NIMPORTS) return g_imports[(va - IMPORT_VA) / 4].fn;
    if (va >= HOSTFN_VA && va < HOSTFN_VA + 4u * g_nhostfn) return g_hostfn[(va - HOSTFN_VA) / 4];
    return NULL;
}

/* ARM-mode functions of the image replaced by the host, found by symbol at load. tools/recomp.py does the same for
 * BLX to __aeabi_atexit (@ghidra 00173fbc): destructors registered at static init are never run (imp___aeabi_atexit) */
static uint32_t g_va_atexit;
GuestFn rc_arm_fn(uint32_t va) { return va && va == g_va_atexit ? imp___aeabi_atexit : NULL; }

/* f_<vaddr>_orig (hooks.h): the original function under a host wrapper */
void rc_orig(Cpu *c, uint32_t va) {
    c->r[14] = RET_MAGIC;
    interp_run(c, IMG + va);
}

#define RC_DEF_ORIG(v) void f_##v##_orig(Cpu *c) { rc_orig(c, 0x##v##u); }
RC_HOOKS(RC_DEF_ORIG)
#undef RC_DEF_ORIG

void rc_dispatch(Cpu *c, uint32_t target) {
    if (target == RET_MAGIC) return;
    uint32_t va = (target & ~1u) - IMG;
    if (va < IMPORT_VA) {
        GuestFn h = rc_hookmap[va >> 1] ? rc_hook_fn(va) : NULL;
        if (h) { h(c); return; }
        c->r[14] = RET_MAGIC;
        interp_run(c, IMG + va);
        return;
    }
    GuestFn f = rc_host_target(target);
    if (f) { f(c); return; }
    host_log("[rc] dispatch to unknown address %08x (vaddr %06x)", target, va);
    rc_trap(c, va, "bad dispatch");
}

uint32_t g_last_unwind_from;
void rc_jump(Cpu *c, uint32_t target, uint32_t from_va) { (void)from_va; rc_dispatch(c, target); }
#else
void rc_dispatch(Cpu *c, uint32_t target) {
    if (target == RET_MAGIC) { c->unwind = RET_MAGIC; return; }
    uint32_t va = (target & ~1u) - IMG;
    if (va < IMPORT_VA) {
        GuestFn f = g_disp[va >> 1];
        if (f) { f(c); return; }
    } else if (va < IMPORT_VA + 4 * NIMPORTS) {
        g_imports[(va - IMPORT_VA) / 4].fn(c);
        return;
    } else if (va >= HOSTFN_VA && va < HOSTFN_VA + 4u * g_nhostfn) {
        g_hostfn[(va - HOSTFN_VA) / 4](c);
        return;
    }
    host_log("[rc] dispatch to unknown address %08x (vaddr %06x) from lr vaddr %06x (%s)", target, va,
             c->r[14] - IMG, rc_func_name(c->r[14] - IMG));
    rc_trap(c, va, "bad dispatch");
}

uint32_t g_last_unwind_from;
void rc_jump(Cpu *c, uint32_t target, uint32_t from_va) {
    if (target == RET_MAGIC) { c->unwind = RET_MAGIC; g_last_unwind_from = from_va; return; }
    uint32_t va = (target & ~1u) - IMG;
    if (va >= IMPORT_VA || g_disp[va >> 1]) { rc_dispatch(c, target); return; }
    c->unwind = target;
    g_last_unwind_from = from_va;
}
#endif

/* ---- ELF parsing (only what this one file needs) ---- */
typedef struct { uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align; } Phdr;
typedef struct { uint32_t r_offset, r_info; } Rel;
typedef struct { uint32_t st_name, st_value, st_size; uint8_t st_info, st_other; uint16_t st_shndx; } Sym;

static uint8_t *g_file;

#ifdef EH_INTERP
static uint32_t symtab_hash(const uint8_t *base, uint32_t dyn_off, uint32_t dyn_sz) {
    for (const uint32_t *d = (const uint32_t *)(base + dyn_off); d < (const uint32_t *)(base + dyn_off + dyn_sz) && d[0]; d += 2)
        if (d[0] == 4) return d[1]; /* DT_HASH */
    return 0;
}
static int cmp_func(const void *a, const void *b) {
    uint32_t x = ((const FuncEnt *)a)->va, y = ((const FuncEnt *)b)->va;
    return x < y ? -1 : x > y;
}
#endif

int rc_load_image(uint8_t *file) {
    g_file = file;

    uint8_t *base = gas_image(MAP_SIZE);
    if (!base) host_fatal("image allocation failed");
    IMG = HPTR(base);

    uint32_t phoff = *(uint32_t *)(g_file + 0x1c);
    uint16_t phnum = *(uint16_t *)(g_file + 0x2c);
    Phdr *ph = (Phdr *)(g_file + phoff);
    uint32_t dyn_off = 0, dyn_sz = 0;
    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type == 1) memcpy(base + ph[i].p_vaddr, g_file + ph[i].p_offset, ph[i].p_filesz);
        if (ph[i].p_type == 2) { dyn_off = ph[i].p_vaddr; dyn_sz = ph[i].p_memsz; }
    }
    uint32_t symtab = 0, strtab = 0, rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0;
    for (uint32_t *d = (uint32_t *)(base + dyn_off); d < (uint32_t *)(base + dyn_off + dyn_sz) && d[0]; d += 2) {
        switch (d[0]) {
        case 6: symtab = d[1]; break;
        case 5: strtab = d[1]; break;
        case 17: rel = d[1]; break;
        case 18: relsz = d[1]; break;
        case 23: jmprel = d[1]; break;
        case 2: pltrelsz = d[1]; break;
        }
    }
    Sym *syms = (Sym *)(base + symtab);
    const char *strs = (const char *)(base + strtab);
    /* data imports: give them their own cell contents */
    M32(rc_import_addr("__stack_chk_guard")) = 0x4b1d5eedu;
    M32(rc_import_addr("__dso_handle")) = 0;

    for (int pass = 0; pass < 2; pass++) {
        Rel *r = (Rel *)(base + (pass ? jmprel : rel));
        uint32_t n = (pass ? pltrelsz : relsz) / 8;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t type = r[i].r_info & 0xff, si = r[i].r_info >> 8;
            uint32_t *p = (uint32_t *)(base + r[i].r_offset);
            uint32_t S = 0;
            if (si) {
                Sym *s = &syms[si];
                if (s->st_shndx == 0) {
                    S = rc_import_addr(strs + s->st_name);
                    if (!S) { fprintf(stderr, "unresolved import %s\n", strs + s->st_name); return 0; }
                } else S = IMG + s->st_value;
            }
            switch (type) {
            case 23: *p += IMG; break;           /* R_ARM_RELATIVE */
            case 2: *p += S; break;              /* R_ARM_ABS32 */
            case 21: case 22: *p = S; break;     /* R_ARM_GLOB_DAT / R_ARM_JUMP_SLOT */
            default: fprintf(stderr, "reloc type %u\n", type); return 0;
            }
        }
    }

#ifdef EH_INTERP
    rc_hookmap = calloc(IMPORT_VA >> 1, 1);
    for (size_t i = 0; i < sizeof(g_hooks) / sizeof(g_hooks[0]); i++) rc_hookmap[g_hooks[i].va >> 1] = 1;
    accel_init(rc_hookmap);
    /* names for logs: defined FUNC symbols (st_info type 2), Thumb bit dropped */
    {
        uint32_t nsym = ((uint32_t *)(base + symtab_hash(base, dyn_off, dyn_sz)))[1];
        g_funcs = calloc(nsym, sizeof(FuncEnt));
        for (uint32_t i = 1; i < nsym; i++)
            if (syms[i].st_shndx && (syms[i].st_info & 15) == 2 && syms[i].st_value)
                g_funcs[g_nfuncs++] = (FuncEnt){syms[i].st_value & ~1u, NULL, strs + syms[i].st_name};
        qsort(g_funcs, g_nfuncs, sizeof(FuncEnt), cmp_func);
        for (uint32_t i = 1; i < nsym; i++)
            if (syms[i].st_shndx && !strcmp(strs + syms[i].st_name, "__aeabi_atexit")) g_va_atexit = syms[i].st_value & ~1u;
    }
#else
    g_disp = calloc(IMPORT_VA >> 1, sizeof(GuestFn));
    for (size_t i = 0; i < g_nfuncs; i++) g_disp[g_funcs[i].va >> 1] = g_funcs[i].fn;
#endif
    return 1;
}

uint32_t rc_sym(const char *name) {
    /* look up a defined dynamic symbol by mangled name (vaddr, thumb bit kept) */
    uint32_t phoff = *(uint32_t *)(g_file + 0x1c);
    (void)phoff;
    uint8_t *base = (uint8_t *)GP(IMG);
    uint32_t dyn = 0x198098;
    uint32_t symtab = 0, strtab = 0, hash = 0;
    for (uint32_t *d = (uint32_t *)(base + dyn); d[0]; d += 2) {
        if (d[0] == 6) symtab = d[1];
        if (d[0] == 5) strtab = d[1];
        if (d[0] == 4) hash = d[1];
    }
    uint32_t nsym = ((uint32_t *)(base + hash))[1];
    Sym *syms = (Sym *)(base + symtab);
    for (uint32_t i = 1; i < nsym; i++)
        if (syms[i].st_shndx && !strcmp((const char *)base + strtab + syms[i].st_name, name)) return syms[i].st_value;
    fprintf(stderr, "symbol not found: %s\n", name);
    return 0;
}

/* ---- calling guest code from the host ---- */

uint32_t rc_call(Cpu *c, uint32_t va, int nargs, const uint32_t *args) {
    uint32_t save[16];
    memcpy(save, c->r, sizeof save);
    /* a host wrapper (hooks) that called the original and then calls guest code again while the original is leaving
     * through an outer frame (c->unwind set): the new call must run normally (every call site's unwind check would
     * return at once) and the outer unwind must go on afterwards */
    uint32_t pending = c->unwind, pending_from = g_last_unwind_from;
    if (pending) {
        static int logged;
        if (logged++ < 20) host_log("[rc] guest call of %06x while unwinding to %06x (from %06x in %s)", va, pending - IMG, pending_from, rc_func_name(pending_from));
        c->unwind = 0;
    }
    for (int i = 0; i < nargs && i < 4; i++) c->r[i] = args[i];
    uint32_t sp = c->r[13];
    if (nargs > 4) {
        sp -= 4 * (nargs - 4);
        sp &= ~7u;
        for (int i = 4; i < nargs; i++) M32(sp + 4 * (i - 4)) = args[i];
    }
    sp &= ~7u;
    c->r[13] = sp;
    c->r[14] = RET_MAGIC;
    rc_dispatch(c, IMG + (va | 1));
    if (c->unwind) {
        if (c->unwind != RET_MAGIC) {
            char msg[256];
            snprintf(msg, sizeof msg, "bad unwind: to %06x (%s) from %06x (%s) in guest call of %06x (%s)", c->unwind - IMG,
                     rc_func_name(c->unwind - IMG), g_last_unwind_from, rc_func_name(g_last_unwind_from), va, rc_func_name(va));
            host_log("[rc] %s", msg);
            host_fatal(msg);
        }
        c->unwind = 0;
    }
    if (pending) { c->unwind = pending; g_last_unwind_from = pending_from; }
    uint32_t r0 = c->r[0];
    uint32_t r1 = c->r[1];
    memcpy(c->r, save, sizeof save);
    c->r[0] = r0;
    c->r[1] = r1;
    return r0;
}

Cpu *rc_new_cpu(uint32_t stack_size) {
    Cpu *c = calloc(1, sizeof(Cpu));
    uint8_t *stk = GAS_64 ? gas_malloc(stack_size) : host_valloc(0, stack_size);
    if (!stk) host_fatal("guest stack allocation failed");
    c->r[13] = HPTR(stk + stack_size - 64);
    return c;
}

void rc_run_init_array(Cpu *c) {
    for (uint32_t a = 0x186000; a < 0x186008; a += 4) {
        uint32_t f = M32(IMG + a) - IMG;
        rc_call(c, f & ~1u, 0, NULL);
    }
}
