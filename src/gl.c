/* OpenGL ES 1.x subset used by libtar-android.so.
 * The original only uploads its software framebuffer (RGB565) into one texture and blits it with
 * glDrawTexiOES (cGlFbGraphics::InitGL @ghidra 001674c0, cGlFbGraphics::Render @ghidra 001675d4).
 * Here those calls only hand the framebuffer to the host presenter (ui/render.cpp), which scales it to the
 * window with its own GL pipeline and composites the modern UI on top. */
#include "host.h"
#include "ui/ui.h"
#include <string.h>

#define GL_TEXTURE_CROP_RECT_OES 0x8b9d
#define GL_UNSIGNED_SHORT_5_6_5 0x8363

typedef struct { int w, h, is565; int crop[4]; } GlTex;
#define MAX_TEX 64
static GlTex g_tex[MAX_TEX];
static int g_bound;
static int g_ntex = 1;

#define IMP(name) void imp_##name(Cpu *c)

IMP(glEnable) { (void)c; }
IMP(glDisable) { (void)c; }
IMP(glShadeModel) { (void)c; }
IMP(glPixelStorei) { (void)c; }
IMP(glTexParameterf) { (void)c; }
IMP(glViewport) { (void)c; }
IMP(glClear) { (void)c; }
IMP(glColor4x) { (void)c; }
IMP(glGenTextures) {
    int n = (int)c->r[0];
    for (int i = 0; i < n; i++) M32(c->r[1] + 4 * i) = (uint32_t)g_ntex++;
}
IMP(glDeleteTextures) {
    int n = (int)c->r[0];
    for (int i = 0; i < n; i++) {
        uint32_t id = M32(c->r[1] + 4 * i);
        if (id < MAX_TEX) memset(&g_tex[id], 0, sizeof g_tex[id]);
    }
}
IMP(glBindTexture) { g_bound = (int)c->r[1] < MAX_TEX ? (int)c->r[1] : 0; }
IMP(glTexParameteriv) {
    if (c->r[1] == GL_TEXTURE_CROP_RECT_OES)
        for (int i = 0; i < 4; i++) g_tex[g_bound].crop[i] = (int)M32(c->r[2] + 4 * i);
}
/* glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels) */
IMP(glTexImage2D) {
    GlTex *t = &g_tex[g_bound];
    t->w = (int)ARG(c, 3); t->h = (int)ARG(c, 4);
    t->is565 = ARG(c, 7) == GL_UNSIGNED_SHORT_5_6_5;
}
/* glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels):
 * the whole back buffer copy (cGlFbGraphics::CopyPixels) of this frame */
IMP(glTexSubImage2D) {
    int w = (int)ARG(c, 4), h = (int)ARG(c, 5);
    uint32_t pixels = ARG(c, 8);
    if (!pixels || !g_tex[g_bound].is565) return;
    ui_present_game_frame((const uint16_t *)GP(pixels), w, h);
}
/* glDrawTexiOES(x, y, z, width, height): drawn by the presenter */
IMP(glDrawTexiOES) { (void)c; }
