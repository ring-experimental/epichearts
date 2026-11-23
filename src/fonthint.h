/* Light vertical hinting on top of stb_truetype (used by java_font.c and tools/fontfit.c).
 * Android renders the game's glyphs with FreeType light hinting: horizontal edges are snapped to whole
 * pixel rows, x stays unhinted. This finds horizontal edges in the glyph outline, snaps them (keeping
 * stem widths >= 1 px) and maps every y coordinate piecewise-linearly between the snapped edges.
 * Requires stb_truetype.h to be included first. */
#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float from, to; } FhMap;

static int fh_cmpf(const void *a, const void *b) {
    float x = ((const FhMap *)a)->from, y = ((const FhMap *)b)->from;
    return x < y ? -1 : x > y;
}

/* render codepoint cp hinted; returns malloc'd w*h A8 bitmap, origin offsets ox,oy relative to the pen/baseline */
static unsigned char *fh_render(const stbtt_fontinfo *fi, int cp, float scale, float shift_x, float shift_y, float hint_strength,
                                int *w, int *h, int *ox, int *oy) {
    stbtt_vertex *v;
    int nv = stbtt_GetCodepointShape(fi, cp, &v);
    *w = *h = 0;
    if (nv <= 0) return NULL;
    /* horizontal edges (font units, y up): consecutive on-curve points with equal y and some length */
    FhMap m[256]; int nm = 0;
    for (int i = 1; i < nv && nm < 254; i++) {
        if (v[i].type != STBTT_vline || v[i].y != v[i - 1].y) continue;
        if (abs(v[i].x - v[i - 1].x) * scale < 1.5f) continue;
        float y = v[i].y * scale; /* pixels, up */
        int dup = 0;
        for (int k = 0; k < nm; k++) if (fabsf(m[k].from - y) < 0.05f) dup = 1;
        if (!dup) { m[nm].from = y; m[nm].to = y; nm++; }
    }
    qsort(m, nm, sizeof m[0], fh_cmpf);
    /* snap: pairs of close edges are stems, keep their pixel width >= 1 */
    /* raster rows: y_down = shift_y - y, so an edge sits on a row boundary when y - shift_y is integral */
    for (int k = 0; k < nm; k++) m[k].to = floorf(m[k].from - shift_y + 0.5f) + shift_y;
    for (int k = 0; k + 1 < nm; k++) {
        float d = m[k + 1].from - m[k].from;
        if (d < 3.0f) { /* stem: bottom edge rounded, width rounded (min 1) */
            float wdt = floorf(d + 0.5f); if (wdt < 1) wdt = 1;
            m[k].to = floorf(m[k].from - shift_y + 0.5f) + shift_y;
            m[k + 1].to = m[k].to + wdt;
            k++;
        }
    }
    for (int k = 0; k < nm; k++) m[k].to = m[k].from + (m[k].to - m[k].from) * hint_strength;
    /* remap all y (font units) through the piecewise-linear map */
    for (int i = 0; i < nv; i++) {
        short *ys[3] = {&v[i].y, &v[i].cy, &v[i].cy1};
        for (int q = 0; q < 3; q++) {
            float y = *ys[q] * scale, ny = y;
            if (nm == 1) ny = y + (m[0].to - m[0].from);
            else if (nm > 1) {
                if (y <= m[0].from) ny = y + (m[0].to - m[0].from);
                else if (y >= m[nm - 1].from) ny = y + (m[nm - 1].to - m[nm - 1].from);
                else for (int k = 0; k + 1 < nm; k++)
                    if (y >= m[k].from && y <= m[k + 1].from) {
                        float t = (m[k + 1].from - m[k].from) > 1e-4f ? (y - m[k].from) / (m[k + 1].from - m[k].from) : 0;
                        ny = m[k].to + t * (m[k + 1].to - m[k].to);
                        break;
                    }
            }
            *ys[q] = (short)lrintf(ny / scale);
        }
    }
    int x0, y0, x1, y1;
    /* bounding box of the remapped outline */
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < nv; i++) {
        float xs[3] = {v[i].x * scale, v[i].cx * scale, v[i].cx1 * scale}, yy[3] = {v[i].y * scale, v[i].cy * scale, v[i].cy1 * scale};
        int np = v[i].type == STBTT_vcubic ? 3 : v[i].type == STBTT_vcurve ? 2 : 1;
        for (int q = 0; q < np; q++) {
            if (xs[q] < minx) minx = xs[q]; if (xs[q] > maxx) maxx = xs[q];
            if (yy[q] < miny) miny = yy[q]; if (yy[q] > maxy) maxy = yy[q];
        }
    }
    x0 = (int)floorf(minx + shift_x); x1 = (int)ceilf(maxx + shift_x);
    y0 = (int)floorf(-maxy + shift_y); y1 = (int)ceilf(-miny + shift_y);
    int gw = x1 - x0 + 1, gh = y1 - y0 + 1;
    stbtt__bitmap gbm;
    gbm.w = gw; gbm.h = gh; gbm.stride = gw;
    gbm.pixels = (unsigned char *)calloc((size_t)gw * gh, 1);
    stbtt_Rasterize(&gbm, 0.35f, v, nv, scale, scale, shift_x, shift_y, x0, y0, 1, fi->userdata);
    stbtt_FreeShape(fi, v);
    *w = gw; *h = gh; *ox = x0; *oy = y0;
    return gbm.pixels;
}
