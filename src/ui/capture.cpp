// Frame capture on the game thread.
//
// The original draws its world, then its HUD / message windows (cGameWorld::Draw @0012e574) and then the cflUi
// forms (cflUi::Draw @00152d40) into one software back buffer, applies the screen fade (cFbGraphics::PrePresent
// @00168730) and hands it over (cGlFbGraphics::FlushImpl @00167368). The modern interface replaces the original's
// interface, so the presented frame is the back buffer as it was right before the first interface drawing; the
// original interface code still runs completely (its draw functions also advance state: typewriter text, message
// expiry, the mission popup's input), only its pixels are not shown.
#include "screens.h"
#include "ui_internal.h"
#include "state.h"
#include "sprites.h"
#include "autoform.h"
#include <cstring>
#include <cstdlib>
#include <SDL3/SDL.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <string>

extern "C" {
#include "host.h"
#include "../hooks.h"
}

namespace capture {
static uint8_t g_mm[40 * 40 * 4];
static uint32_t g_mm_seq;
static std::mutex g_mm_mx;
bool minimap(uint8_t *out, uint32_t *seq) {
    std::lock_guard<std::mutex> l(g_mm_mx);
    if (*seq == g_mm_seq) return false;
    *seq = g_mm_seq;
    memcpy(out, g_mm, sizeof g_mm);
    return true;
}
bool modern = true;          // EH_CLASSIC=1: show the original interface (comparison runs)
bool force_strip = false;    // EH_STRIP=1: always present the interface-free frame (development)
static std::vector<uint16_t> g_world;
static bool g_taken;
static int g_w, g_h;

static uint32_t gfx() { return M32(IMG + 0x19b364); } // the_g (cGlFbGraphics*)

// EH_SENTINEL=1 (test): every frame starts as a sentinel colour instead of the last frame / black, so pixels nothing
// painted this frame can be counted. Two measurements per frame: "world" = the interface-free frame the modern UI shows
// (snapshot(), before the fade), "full" = the whole back buffer at FlushImpl, what classic mode shows (and modern mode on
// screens without a world pass, e.g. the title). A non-zero fade (cFbGraphics::PrePresent) changes the sentinel's colour,
// so "full" lines carry the fade level. Read with "ui cover <file>".
namespace cover {
bool on; // EH_SENTINEL, read by ui_init (after main.c loaded the Android test environment)
const uint16_t S = 0xF81F; // RGB565 magenta: the original's art never outputs it (colour key)
struct Stat { uint32_t frame = 0; int w = 0, h = 0, fade = 0; long n = 0, tinted = 0; std::vector<uint16_t> col, tcol; int ink[4] = {0, 0, -1, -1}; };
static Stat g_world_st, g_full_st;
static Stat g_mark_st;           // the last measurement a hook asked for (cover_snap)
static const char *g_mark_name;
static std::vector<uint16_t> g_mark_px; // its pixels (written next to the dump as a BMP)
static uint32_t g_frame;
static std::mutex g_mx;
static void fill(uint16_t *px, size_t n) { for (size_t i = 0; i < n; i++) px[i] = S; }
static void measure(const uint16_t *px, int w, int h, Stat &o, int fade = 0) {
    Stat t;
    t.frame = g_frame; t.w = w; t.h = h; t.fade = fade;
    t.col.assign((size_t)w, 0);
    t.tcol.assign((size_t)w, 0);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t v = px[(size_t)y * w + x];
            if (v == S) { t.col[x]++; t.n++; }
            // the sentinel under a full-screen colour filter (cMenuMgr::DrawBgFilter @00146a58, screen fx): no green,
            // red and blue still close. Counted apart: real purple art can match too.
            else if (!(v & 0x7e0) && (v >> 11) >= 6 && (v & 31) >= 6 && std::abs((int)(v >> 11) - (int)(v & 31)) <= 3) { t.tcol[x]++; t.tinted++; }
            else if (v) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        }
    t.ink[0] = x0; t.ink[1] = y0; t.ink[2] = x1; t.ink[3] = y1;
    std::lock_guard<std::mutex> l(g_mx);
    o = std::move(t);
}
static void write(FILE *f, const char *name, const Stat &t) {
    if (!t.w) { fprintf(f, "%s none\n", name); return; }
    // column runs "x0-x1(most pixels in one column)"
    auto runs = [&](const std::vector<uint16_t> &col) {
        std::string r;
        int nr = 0;
        for (int x = 0; x < t.w;) {
            if (!col[x]) { x++; continue; }
            int a = x, mx = 0;
            while (x < t.w && col[x]) mx = std::max(mx, (int)col[x++]);
            if (nr++ < 12) r += " " + std::to_string(a) + "-" + std::to_string(x - 1) + "(" + std::to_string(mx) + ")";
        }
        if (nr > 12) r += " ...";
        return r.empty() ? std::string(" none") : r;
    };
    double area = (double)t.w * t.h;
    fprintf(f, "%s frame %u size %dx%d fade %d unpainted %ld (%.1f%%) cols%s\n", name, t.frame, t.w, t.h, t.fade, t.n, 100.0 * t.n / area,
            runs(t.col).c_str());
    fprintf(f, "%s tinted %ld (%.1f%%) cols%s\n", name, t.tinted, 100.0 * t.tinted / area, runs(t.tcol).c_str());
    if (t.ink[2] >= 0)
        fprintf(f, "%s ink %d,%d-%d,%d margins L %d R %d (centre offset %+d)\n", name, t.ink[0], t.ink[1], t.ink[2], t.ink[3], t.ink[0],
                t.w - 1 - t.ink[2], (t.ink[0] - (t.w - 1 - t.ink[2])) / 2);
    else
        fprintf(f, "%s ink none\n", name);
}
} // namespace cover

void cover_dump(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    std::lock_guard<std::mutex> l(cover::g_mx);
    if (!cover::on) fprintf(f, "sentinel off (run with EH_SENTINEL=1)\n");
    cover::write(f, "world", cover::g_world_st);
    cover::write(f, "full", cover::g_full_st);
    if (cover::g_mark_name) cover::write(f, cover::g_mark_name, cover::g_mark_st);
    fclose(f);
    if (cover::g_mark_name && !cover::g_mark_px.empty()) { // the marked buffer as it was measured: <file>.<name>.bmp
        std::string bmp = std::string(path) + "." + cover::g_mark_name + ".bmp";
        if (SDL_Surface *sf = SDL_CreateSurfaceFrom(cover::g_mark_st.w, cover::g_mark_st.h, SDL_PIXELFORMAT_RGB565, cover::g_mark_px.data(), cover::g_mark_st.w * 2)) {
            SDL_SaveBMP(sf, bmp.c_str());
            SDL_DestroySurface(sf);
        }
    }
}

static void snapshot() {
    if (g_taken || !modern) return;
    uint32_t g = gfx();
    if (!g) return;
    g_w = (int)M32(g + 0x24);
    g_h = (int)M32(g + 0x28);
    uint32_t px = M32(g + 0x20); // back buffer being drawn (FlushImpl copies +0x20 -> +0x374)
    if (!px || g_w <= 0 || g_h <= 0) return;
    g_world.resize((size_t)g_w * g_h);
    memcpy(g_world.data(), GP(px), g_world.size() * 2);
    g_taken = true;
    if (cover::on) cover::measure(g_world.data(), g_w, g_h, cover::g_world_st);
}
} // namespace capture

using namespace capture;

extern "C" {
/* EH_SENTINEL: a hook measures the back buffer right after one drawing step (e.g. the loading screen, hooks.c f_12273c,
 * before the original's interface is drawn over it); kept under that name until the next such measurement */
void cover_snap(const char *name) {
    if (!cover::on) return;
    uint32_t g = gfx(), px = g ? M32(g + 0x20) : 0;
    int w = g ? (int)M32(g + 0x24) : 0, h = g ? (int)M32(g + 0x28) : 0;
    if (!px || w <= 0 || h <= 0) return;
    cover::measure((const uint16_t *)GP(px), w, h, cover::g_mark_st);
    std::lock_guard<std::mutex> l(cover::g_mx);
    cover::g_mark_name = name;
    cover::g_mark_px.assign((const uint16_t *)GP(px), (const uint16_t *)GP(px) + (size_t)w * h);
}
/* cScreenFxManager::ApplyFx @0012a45c: last step of the world pass in cGameWorld::Draw (rain, screen fx);
 * cBattleUi::Draw follows */
void f_11a45c(Cpu *c) {
    f_11a45c_orig(c);
    snapshot();
}
/* cflUi::Draw @00152d40: the form chain; on the title / menu states the background was drawn just before
 * (cMenuMgr::DrawTitleBg @00146ca0, DrawMainMenuBg @00146d04) */
void f_142d40(Cpu *c) {
    snapshot();
    autoform::begin_ui_draw();
    f_142d40_orig(c);
    autoform::end_ui_draw();
}
/* cflUi::NeedRedrawWorld @001537d0 / NeedRedrawUI @001537f0: the original only redraws what changed and keeps
 * the rest of the last frame in the back buffer. The capture needs a complete world pass every frame. */
void f_1437d0(Cpu *c) {
    if (modern || cover::on) { c->r[0] = 1; return; }
    f_1437d0_orig(c);
}
void f_1437f0(Cpu *c) {
    if (modern || cover::on) { c->r[0] = 1; return; }
    f_1437f0_orig(c);
}
/* cGameWorld::Draw @0012e574 clears the screen (graphics vfunc +0x58, colour 0) only around maps narrower than the
 * view, which it centres; a wider map is assumed to cover the view. The port's view is wider than the original's
 * 512 px, so it reaches map corners holding no tiles (e.g. 세이프우드 동부지역, bottom right); nothing paints them and
 * every frame drew over the last (stale terrain, screen-fx clouds adding up to white). The back buffer starts black
 * each frame, as the original's own clear would leave it. */
void f_11e574(Cpu *c) {
    // only when the world pass runs: world flag 0x8000 skips it (the last frame stays)
    if ((modern || cover::on) && !(M32(c->r[0]) & 0x8000)) {
        uint32_t g = gfx();
        uint32_t px = g ? M32(g + 0x20) : 0;
        int w = g ? (int)M32(g + 0x24) : 0, h = g ? (int)M32(g + 0x28) : 0;
        if (px && w > 0 && h > 0) {
            if (cover::on) cover::fill((uint16_t *)GP(px), (size_t)w * h); // what the world pass leaves unpainted stays visible
            else memset(GP(px), 0, (size_t)w * h * 2);
        }
    }
    f_11e574_orig(c);
}
/* cMinimap::DrawMinimap @00104014: 40x40 map at (W-43, 3), shown by the modern HUD. Read right after it is drawn:
 * cGameWorld::Draw @0012e574 draws the script fast-forward button (cBattleUi::DrawFastButton @000f8678 while world+0x768,
 * at (W-0x32, 0)) after the minimap, over the same rect; it must not end up in the modern minimap */
void f_0f4014(Cpu *c) {
    f_0f4014_orig(c);
    if (!modern) return;
    uint32_t g = gfx();
    if (!g) return;
    uint32_t px = M32(g + 0x20);
    int W = (int)M32(g + 0x24);
    if (!px || W <= 50 || (int)M32(g + 0x28) < 43) return;
    std::lock_guard<std::mutex> l(g_mm_mx);
    const uint16_t *src = (const uint16_t *)GP(px);
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 40; x++) {
            uint16_t v = src[(size_t)(3 + y) * W + (W - 43 + x)];
            uint8_t *o = &g_mm[(y * 40 + x) * 4];
            o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255;
        }
    g_mm_seq++;
}
/* cGlFbGraphics::FlushImpl @00167368 (this = the_g): end of the frame, after PrePresent applied the fade */
void f_157368(Cpu *c) {
    uint32_t g = c->r[0];
    uint32_t args[4] = {c->r[0], c->r[1], c->r[2], c->r[3]}; // rc_call returns with r0/r1 changed
    state::capture_frame(c); // the interface snapshot that goes with this frame (state.cpp)
    if (cover::on) {
        uint32_t px = M32(g + 0x20);
        int w = (int)M32(g + 0x24), h = (int)M32(g + 0x28);
        if (px && w > 0 && h > 0) cover::measure((const uint16_t *)GP(px), w, h, cover::g_full_st, (int16_t)M16(g + 0x10));
    }
    if (modern && g_taken && (force_strip || state::strip_original_ui())) {
        uint32_t px = M32(g + 0x20);
        if (px && (int)M32(g + 0x24) == g_w && (int)M32(g + 0x28) == g_h) {
            memcpy(GP(px), g_world.data(), g_world.size() * 2);
            /* re-apply the fade like cFbGraphics::PrePresent: (this->+0x2e8)(this + 0x20, (s16)this->+0x10) */
            int16_t fade = (int16_t)M16(g + 0x10);
            uint32_t fn = M32(g + 0x2e8);
            if (fade && fn) {
                uint32_t a[2] = {g + 0x20, (uint32_t)(int32_t)fade};
                rc_call(c, (fn - IMG) & ~1u, 2, a);
            }
        }
    }
    g_taken = false;
    memcpy(c->r, args, sizeof args);
    f_157368_orig(c);
    if (cover::on) { // the next frame starts from the sentinel (FlushImpl copied +0x20 to the present buffer +0x374)
        uint32_t px = M32(g + 0x20);
        int w = (int)M32(g + 0x24), h = (int)M32(g + 0x28);
        if (px && w > 0 && h > 0) cover::fill((uint16_t *)GP(px), (size_t)w * h);
        cover::g_frame++;
    }
}
}

void capture_on_frame() {}
const uint8_t *sprites_lookup(const char *id, int *w, int *h) { return sprites::lookup(id, w, h); }
