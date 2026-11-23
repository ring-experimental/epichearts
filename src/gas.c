/* Guest address space (see gas.h) */
#include "gas.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

void host_fatal(const char *msg);
void host_log(const char *fmt, ...);

/* A guest free of something that is no heap block is ignored (and logged) instead of faulting.
 * The original's scripts can hand fl_Free (@ghidra 00160ab4) a number: script 58202 (the "...섬이 열렸습니다"
 * notice after riding the puffer, called by 58201) keeps its text in shared script variable 8 (op 0x33,
 * cflScriptBase::RunRunningReserved_0 @0016d558, cflScript +0x24) while the message waits, and the guide
 * process (cIdleGuideProcess::UpdateProcess @000d2b4c -> cGameManager::ExecScript) runs script 118 in the same
 * instance meanwhile, whose op 0x56 (cflScript::RunRunningReserved) writes the helmet's item id into that same
 * variable 8; op 0x34 then frees the item id. UNVERIFIED: what the original's bionic free did with it (a crash is
 * likely: the player report was a forced stop there). */

#if !GAS_64
void gas_init(void) {}
void *gas_malloc(size_t n) { return malloc(n ? n : 1); }
void *gas_calloc(size_t n, size_t m) { return calloc(n ? n : 1, m ? m : 1); }
void *gas_realloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void gas_free(void *p) { if ((uintptr_t)p < 0x10000) { if (p) host_log("[gas] free of non-heap %p ignored", p); return; } free(p); }
char *gas_strdup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); memcpy(d, s, n); return d; }
void *gas_image(size_t size) { return host_valloc(0x10000000, size); }
uint32_t gas_handle(void *p) { return (uint32_t)(uintptr_t)p; }
void *gas_unhandle(uint32_t h) { return (void *)(uintptr_t)h; }
#else

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

uintptr_t g_gas_base;
#define REGION 0x40000000ull     /* 1 GB of guest address space */
#define IMAGE_AT 0x10000000u     /* the .so image (IMG) */
#define HEAP_AT 0x20000000u
#define COMMIT_STEP (16u << 20)

static SDL_Mutex *g_mx;
static uint32_t g_top = HEAP_AT, g_committed = HEAP_AT;

static void commit(uint32_t from, uint32_t to) {
#ifdef _WIN32
    if (!VirtualAlloc((void *)(g_gas_base + from), to - from, MEM_COMMIT, PAGE_READWRITE)) host_fatal("guest memory commit failed");
#else
    if (mprotect((void *)(g_gas_base + from), to - from, PROT_READ | PROT_WRITE)) host_fatal("guest memory commit failed");
#endif
}

void gas_init(void) {
    if (g_gas_base) return;
#ifdef _WIN32
    void *p = VirtualAlloc(NULL, REGION, MEM_RESERVE, PAGE_NOACCESS);
#else
    void *p = mmap(NULL, REGION, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) p = NULL;
#endif
    if (!p) host_fatal("cannot reserve the guest address space");
    g_gas_base = (uintptr_t)p;
    g_mx = SDL_CreateMutex();
}

/* ---- allocator: 16-byte header, small blocks in segregated free lists, large blocks first-fit with coalescing */
typedef struct Hdr { uint32_t size, magic; uint32_t pad[2]; } Hdr;  /* size = whole block, header included */
#define MAGIC_USED 0xa110ca7e
#define MAGIC_FREE 0xf4eeb10c
#define SMALL_MAX 4096
static uint32_t g_small[SMALL_MAX / 16 + 1];   /* guest offsets of free small blocks (next in payload) */
static uint32_t g_large;                        /* address-ordered list of free large blocks */

static Hdr *H(uint32_t off) { return (Hdr *)(g_gas_base + off); }
static uint32_t *NEXT(uint32_t off) { return (uint32_t *)(g_gas_base + off + sizeof(Hdr)); }

static uint32_t bump(uint32_t size) {
    if ((uint64_t)g_top + size > REGION) host_fatal("guest heap exhausted");
    uint32_t off = g_top;
    g_top += size;
    if (g_top > g_committed) {
        uint32_t to = (g_top + COMMIT_STEP - 1) & ~(COMMIT_STEP - 1);
        if (to > REGION) to = (uint32_t)REGION;
        commit(g_committed, to);
        g_committed = to;
    }
    return off;
}

void *gas_malloc(size_t n) {
    if (!g_gas_base) gas_init();
    uint32_t size = (uint32_t)((n + sizeof(Hdr) + 15) & ~(size_t)15);
    if (size < 32) size = 32;
    SDL_LockMutex(g_mx);
    uint32_t off = 0;
    if (size <= SMALL_MAX) {
        uint32_t *l = &g_small[size / 16];
        if (*l) { off = *l; *l = *NEXT(off); }
        else off = bump(size);
    } else {
        uint32_t *pp = &g_large;
        while (*pp && H(*pp)->size < size) pp = NEXT(*pp);
        if (*pp) {
            off = *pp;
            uint32_t have = H(off)->size;
            if (have - size >= 256) { /* split: remainder stays in place in the list */
                uint32_t rest = off + size;
                H(rest)->size = have - size;
                H(rest)->magic = MAGIC_FREE;
                *NEXT(rest) = *NEXT(off);
                *pp = rest;
            } else {
                size = have;
                *pp = *NEXT(off);
            }
        } else off = bump(size);
    }
    H(off)->size = size;
    H(off)->magic = MAGIC_USED;
    SDL_UnlockMutex(g_mx);
    return (void *)(g_gas_base + off + sizeof(Hdr));
}

void gas_free(void *p) {
    if (!p) return;
    uint32_t off = (uint32_t)((uintptr_t)p - g_gas_base) - sizeof(Hdr);
    if ((uintptr_t)p - g_gas_base < HEAP_AT + sizeof(Hdr) || off + sizeof(Hdr) > g_top) { /* not inside the heap (see above) */
        host_log("[gas] free of non-heap %08x ignored", (uint32_t)((uintptr_t)p - g_gas_base));
        return;
    }
    Hdr *h = H(off);
    if (h->magic != MAGIC_USED) return; /* not ours / double free */
    SDL_LockMutex(g_mx);
    h->magic = MAGIC_FREE;
    uint32_t size = h->size;
    if (size <= SMALL_MAX) {
        *NEXT(off) = g_small[size / 16];
        g_small[size / 16] = off;
    } else {
        uint32_t *pp = &g_large, prev = 0;
        while (*pp && *pp < off) { prev = *pp; pp = NEXT(*pp); }
        *NEXT(off) = *pp;
        *pp = off;
        uint32_t nx = *NEXT(off);
        if (nx && off + H(off)->size == nx) { H(off)->size += H(nx)->size; *NEXT(off) = *NEXT(nx); }
        if (prev && prev + H(prev)->size == off) { H(prev)->size += H(off)->size; *NEXT(prev) = *NEXT(off); }
    }
    SDL_UnlockMutex(g_mx);
}

void *gas_calloc(size_t n, size_t m) {
    size_t t = n * m;
    void *p = gas_malloc(t);
    memset(p, 0, t ? t : 1);
    return p;
}

void *gas_realloc(void *p, size_t n) {
    if (!p) return gas_malloc(n);
    uint32_t off = (uint32_t)((uintptr_t)p - g_gas_base) - sizeof(Hdr);
    size_t have = H(off)->size - sizeof(Hdr);
    if (n <= have) return p;
    void *q = gas_malloc(n);
    memcpy(q, p, have);
    gas_free(p);
    return q;
}

char *gas_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = gas_malloc(n);
    memcpy(d, s, n);
    return d;
}

void *gas_image(size_t size) {
    if (!g_gas_base) gas_init();
    commit(IMAGE_AT, IMAGE_AT + (uint32_t)((size + 0xffff) & ~(size_t)0xffff));
    return (void *)(g_gas_base + IMAGE_AT);
}

/* ---- handles */
static void **g_h;
static uint32_t g_nh, g_caph;
uint32_t gas_handle(void *p) {
    if (!p) return 0;
    if (!g_mx) gas_init();
    SDL_LockMutex(g_mx);
    for (uint32_t i = 0; i < g_nh; i++)
        if (g_h[i] == p) { SDL_UnlockMutex(g_mx); return 0x80000000u + i * 4; }
    if (g_nh == g_caph) { g_caph = g_caph ? g_caph * 2 : 256; g_h = realloc(g_h, g_caph * sizeof *g_h); }
    g_h[g_nh] = p;
    uint32_t h = 0x80000000u + g_nh * 4; /* high bits set: never mistaken for a static pthread initialiser */
    g_nh++;
    SDL_UnlockMutex(g_mx);
    return h;
}
void *gas_unhandle(uint32_t h) {
    uint32_t i = (h - 0x80000000u) / 4;
    return h >= 0x80000000u && i < g_nh ? g_h[i] : NULL;
}
#endif
