/* Reads the central directory of the original epichearts.apk. All entries the game needs are
 * STORED (uncompressed), which is what Android's AssetManager.openFd / openRawResourceFd require
 * (com/ftt/gof2d/file/GofFileBridge.getApkFileOffseet). */
#include "apk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *name; uint32_t data_off, size, method; } ApkEnt;
static ApkEnt *g_ent;
static int g_nent;
static FILE *g_apk;

int apk_open(const char *path) {
    g_apk = fopen(path, "rb");
    if (!g_apk) return 0;
    fseek(g_apk, 0, SEEK_END);
    long sz = ftell(g_apk);
    long tail = sz < 70000 ? sz : 70000;
    uint8_t *buf = malloc(tail);
    fseek(g_apk, sz - tail, SEEK_SET);
    fread(buf, 1, tail, g_apk);
    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--)
        if (buf[i] == 'P' && buf[i + 1] == 'K' && buf[i + 2] == 5 && buf[i + 3] == 6) { eocd = i; break; }
    if (eocd < 0) { free(buf); return 0; }
    uint32_t n = *(uint16_t *)(buf + eocd + 10), cd_size = *(uint32_t *)(buf + eocd + 12), cd_off = *(uint32_t *)(buf + eocd + 16);
    free(buf);
    uint8_t *cd = malloc(cd_size);
    fseek(g_apk, cd_off, SEEK_SET);
    fread(cd, 1, cd_size, g_apk);
    g_ent = calloc(n, sizeof(ApkEnt));
    uint8_t *p = cd;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t method = *(uint16_t *)(p + 10), nl = *(uint16_t *)(p + 28), el = *(uint16_t *)(p + 30), cl = *(uint16_t *)(p + 32);
        uint32_t csize = *(uint32_t *)(p + 20), lho = *(uint32_t *)(p + 42);
        ApkEnt *e = &g_ent[g_nent++];
        e->name = malloc(nl + 1);
        memcpy(e->name, p + 46, nl);
        e->name[nl] = 0;
        e->size = csize;
        e->method = method;
        uint8_t lh[30];
        fseek(g_apk, lho, SEEK_SET);
        fread(lh, 1, 30, g_apk);
        e->data_off = lho + 30 + *(uint16_t *)(lh + 26) + *(uint16_t *)(lh + 28);
        p += 46 + nl + el + cl;
    }
    free(cd);
    return 1;
}

int apk_find(const char *name, uint32_t *off, uint32_t *size) {
    for (int i = 0; i < g_nent; i++)
        if (!strcmp(g_ent[i].name, name)) {
            if (g_ent[i].method != 0) return 0; /* openFd fails on compressed entries */
            *off = g_ent[i].data_off;
            *size = g_ent[i].size;
            return 1;
        }
    return 0;
}

/* res/raw/<name>.* lookup by resource name (GofFileBridge.getResId: name before the first '.', lower case) */
int apk_find_raw(const char *resname, uint32_t *off, uint32_t *size) {
    size_t L = strlen(resname);
    for (int i = 0; i < g_nent; i++) {
        const char *n = g_ent[i].name;
        if (strncmp(n, "res/raw/", 8)) continue;
        if (!strncmp(n + 8, resname, L) && n[8 + L] == '.') {
            if (g_ent[i].method != 0) return 0;
            *off = g_ent[i].data_off;
            *size = g_ent[i].size;
            return 1;
        }
    }
    return 0;
}

void *apk_read(uint32_t off, uint32_t size) {
    void *b = malloc(size ? size : 1);
    fseek(g_apk, off, SEEK_SET);
    fread(b, 1, size, g_apk);
    return b;
}
