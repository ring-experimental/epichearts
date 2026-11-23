/* com/ftt/gof2d/font/GofFontBridge + GofFontCanvas (smali in classes.dex) on stb_truetype.
 * Native users: cAndCacheFont::CompleteInit/PrepareBitmap/DrawCharImpl/ReadBackAndDiscardBitmap
 * (@ghidra 001683f0, 0016817c, 001682d0, 001681c4).
 * Glyphs mostly come from the pre-rendered assets/flfoo.png (cSmartCacheFont::ReadPresavedFile);
 * this renders the rest. */
#include "java.h"
#include "apk.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* createFontFace("fttcustom.otf", size) loads getFilesDir()/fttcustom.otf, which Tar.onCreate copies
 * from res/raw/fttcustom; read straight from the APK here. */
static stbtt_fontinfo g_font;
static int g_font_ok;
static int g_size = 14; /* GofFontBridge.defualtSize */
static float g_scale, g_ascent_f, g_descent_f;
static int g_ascender, g_descender;

static void load_font(void) {
    if (g_font_ok) return;
    uint32_t off, size;
    if (!apk_find_raw("fttcustom", &off, &size)) { host_log("[font] fttcustom not found"); return; }
    unsigned char *data = apk_read(off, size);
    if (!stbtt_InitFont(&g_font, data, stbtt_GetFontOffsetForIndex(data, 0))) { host_log("[font] init failed"); return; }
    g_font_ok = 1;
}

/* GofFontBridge.setFontSize: Paint.setTextSize(size); ascender = -fm.ascent, descender = fm.descent */
static void set_size(int size) {
    load_font();
    g_size = size;
    if (!g_font_ok) return;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    g_scale = stbtt_ScaleForMappingEmToPixels(&g_font, (float)size);
    g_ascent_f = -asc * g_scale;   /* Paint.ascent() (negative) */
    g_descent_f = -desc * g_scale; /* Paint.descent() (positive) */
    /* UNVERIFIED: FontMetricsInt rounding (Skia: ascent floor, descent ceil) */
    g_ascender = -(int)floorf(g_ascent_f);
    g_descender = (int)ceilf(g_descent_f);
}

/* Paint.measureText as observed on Android: each glyph advance is rounded to whole pixels (hinted
 * advances) and U+00AD (soft hyphen) measures 0. Verified against the glyph widths in the flfoo.png
 * cache the original game generated on Android 11 (2541/2542 match; tools/fontcmp.py). */
static float measure(const uint16_t *s, int n) {
    if (!g_font_ok) return 0;
    float w = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == 0xAD) continue;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font, s[i], &adv, &lsb);
        w += floorf(adv * g_scale + 0.5f);
    }
    return w;
}

#define JM(name) static uint64_t name(Cpu *c, JObj *self, const uint64_t *a)
#define UNUSED (void)c; (void)self; (void)a

JM(fb_createFontFace_s) { UNUSED; host_log("[font] createFontFace(%s, %d)", JSTR(a[0]), (int)a[1]); set_size((int)a[1]); return 0; }
JM(fb_createFontFace_i) { UNUSED; set_size((int)a[0]); return 0; }
JM(fb_setFontSize) { UNUSED; set_size((int)a[0]); return 0; }
JM(fb_getFontSize) { UNUSED; return (uint32_t)g_size; }
JM(fb_getFontAsceder) { UNUSED; return (uint32_t)g_ascender; }
JM(fb_getFontDescender) { UNUSED; return (uint32_t)g_descender; }
JM(fb_getFontHeight) { UNUSED; return (uint32_t)(int)(-g_ascent_f + g_descent_f); }
JM(fb_getFontWidth) { UNUSED; return 0; }
JM(fb_getStringWidth) {
    UNUSED;
    JObj *arr = JARR(a[0]);
    int n = (int)a[1];
    if (!arr || n > (int)arr->len) n = arr ? (int)arr->len : 0;
    return (uint32_t)(int)measure(arr ? arr->data : NULL, n);
}

typedef struct { int w, h, yoff; uint8_t *bmp; } Canvas;
static JObj *g_canvas_obj;

/* createWorkingCanvas(w, h): new ALPHA_8 bitmap, yOff = (int)-paint.ascent() */
JM(fb_createWorkingCanvas) {
    UNUSED;
    Canvas *cv = calloc(1, sizeof *cv);
    cv->w = (int)a[0]; cv->h = (int)a[1];
    cv->bmp = calloc((size_t)cv->w * cv->h + 1, 1);
    cv->yoff = (int)(-g_ascent_f);
    JObj *o = jobj_new("com/ftt/gof2d/font/GofFontCanvas", JK_INSTANCE);
    o->native = cv;
    o->global = 1;
    if (g_canvas_obj) { Canvas *old = g_canvas_obj->native; free(old->bmp); free(old); g_canvas_obj->native = NULL; }
    g_canvas_obj = o;
    return JRET_OBJ(o);
}
JM(fb_destroyWorkingCanvas) { UNUSED; return 0; }

/* GofFontCanvas.drawChar(x, y, ch): Canvas.drawText(ch, x, y + yOff, paint) with anti-aliasing */
JM(fc_drawChar) {
    UNUSED;
    Canvas *cv = self->native;
    if (!cv || !g_font_ok) return 0;
    int x = (int)a[0], y = (int)a[1], ch = (int)(uint16_t)a[2];
    int gx0, gy0, gx1, gy1;
    /* +0.25 px vertical sub-pixel offset: best least-squares fit to the Android-rendered glyph cache */
    const float dy = 0.25f;
    stbtt_GetCodepointBitmapBoxSubpixel(&g_font, ch, g_scale, g_scale, 0, dy, &gx0, &gy0, &gx1, &gy1);
    int gw = gx1 - gx0, gh = gy1 - gy0;
    if (gw <= 0 || gh <= 0) return 0;
    uint8_t *g = calloc((size_t)gw * gh, 1);
    stbtt_MakeCodepointBitmapSubpixel(&g_font, g, gw, gh, gw, g_scale, g_scale, 0, dy, ch);
    int bx = x + gx0, by = y + cv->yoff + gy0;
    for (int j = 0; j < gh; j++) {
        int yy = by + j;
        if (yy < 0 || yy >= cv->h) continue;
        for (int i = 0; i < gw; i++) {
            int xx = bx + i;
            if (xx < 0 || xx >= cv->w) continue;
            uint8_t v = g[j * gw + i];
            uint8_t *d = &cv->bmp[yy * cv->w + xx];
            *d = (uint8_t)(v + (*d * (255 - v)) / 255); /* src-over alpha */
        }
    }
    free(g);
    return 0;
}
/* getBitmapBuffer: copyPixelsToBuffer of the ALPHA_8 bitmap (rowBytes == width) */
JM(fc_getBitmapBuffer) {
    UNUSED;
    Canvas *cv = self->native;
    if (!cv) return 0;
    JObj *arr = jarr_new("[B", 1, (uint32_t)(cv->w * cv->h));
    memcpy(arr->data, cv->bmp, (size_t)cv->w * cv->h);
    return JRET_OBJ(arr);
}

void java_font_register(void) {
    const char *FB = "com/ftt/gof2d/font/GofFontBridge", *FC = "com/ftt/gof2d/font/GofFontCanvas";
    jni_register(FB, "createFontFace", "(Ljava/lang/String;I)V", fb_createFontFace_s);
    jni_register(FB, "createFontFace", "(I)V", fb_createFontFace_i);
    jni_register(FB, "setFontSize", NULL, fb_setFontSize);
    jni_register(FB, "getFontSize", NULL, fb_getFontSize);
    jni_register(FB, "getFontAsceder", NULL, fb_getFontAsceder);
    jni_register(FB, "getFontDescender", NULL, fb_getFontDescender);
    jni_register(FB, "getFontHeight", NULL, fb_getFontHeight);
    jni_register(FB, "getFontWidth", NULL, fb_getFontWidth);
    jni_register(FB, "getStringWidth", NULL, fb_getStringWidth);
    jni_register(FB, "createWorkingCanvas", NULL, fb_createWorkingCanvas);
    jni_register(FB, "destroyWorkingCanvas", NULL, fb_destroyWorkingCanvas);
    jni_register(FC, "drawChar", NULL, fc_drawChar);
    jni_register(FC, "getBitmapBuffer", NULL, fc_getBitmapBuffer);
}

/* host overlay helper (EditText emulation): UTF-8 line -> 8-bit alpha bitmap */
void font_render_utf8(const char *s, int size, uint8_t **out, int *w, int *h) {
    load_font();
    *out = NULL; *w = *h = 0;
    if (!g_font_ok) return;
    float sc = stbtt_ScaleForMappingEmToPixels(&g_font, (float)size);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    int base = (int)(asc * sc);
    uint32_t cps[256]; int n = 0;
    for (const uint8_t *p = (const uint8_t *)s; *p && n < 256;) {
        uint32_t cp = *p++;
        if (cp >= 0xf0) { cp = (cp & 7) << 18; cp |= (*p++ & 0x3f) << 12; cp |= (*p++ & 0x3f) << 6; cp |= *p++ & 0x3f; }
        else if (cp >= 0xe0) { cp = (cp & 15) << 12; cp |= (*p++ & 0x3f) << 6; cp |= *p++ & 0x3f; }
        else if (cp >= 0xc0) { cp = (cp & 31) << 6; cp |= *p++ & 0x3f; }
        cps[n++] = cp;
    }
    float x = 0;
    for (int i = 0; i < n; i++) { int adv, lsb; stbtt_GetCodepointHMetrics(&g_font, cps[i], &adv, &lsb); x += adv * sc; }
    *w = (int)x + 2; *h = (int)((asc - desc) * sc) + 2;
    uint8_t *bmp = calloc((size_t)*w * *h, 1);
    x = 0;
    for (int i = 0; i < n; i++) {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&g_font, cps[i], &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&g_font, cps[i], sc, sc, &x0, &y0, &x1, &y1);
        int ox = (int)x + x0, oy = base + y0;
        if (ox >= 0 && oy >= 0 && ox + (x1 - x0) <= *w && oy + (y1 - y0) <= *h)
            stbtt_MakeCodepointBitmap(&g_font, bmp + oy * *w + ox, x1 - x0, y1 - y0, *w, sc, sc, cps[i]);
        x += adv * sc;
    }
    *out = bmp;
}
