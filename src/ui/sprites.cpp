// Pixel art of the original (item / skill icons, portraits, ...) rendered with the original's own sprite code
// into the back buffer at the end of a frame, black/white matted to RGBA, cached and handed to RmlUi as
// "sprite:<key>" textures. The back buffer and the graphics state are restored afterwards.
#include "sprites.h"
#include "game.h"
#include "ui_internal.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace sprites {

struct Img { int w = 0, h = 0, ox = 0, oy = 0, sw = 0, sh = 0; std::vector<uint8_t> rgba; }; // ox,oy: bbox origin - draw point; sw,sh: size in source pixels
static std::map<std::string, Img> g_cache; // key -> image (empty rgba = failed)
static std::mutex g_mx;
static const int UPSCALE = 3; // nearest-neighbour pre-scale so RmlUi's linear filtering stays sharp

const uint8_t *lookup(const char *key, int *w, int *h) {
    std::lock_guard<std::mutex> l(g_mx);
    auto it = g_cache.find(key);
    if (it == g_cache.end() || it->second.rgba.empty()) return nullptr;
    *w = it->second.w;
    *h = it->second.h;
    return it->second.rgba.data();
}

bool ready(const std::string &key) {
    std::lock_guard<std::mutex> l(g_mx);
    auto it = g_cache.find(key);
    return it != g_cache.end() && !it->second.rgba.empty();
}

// ---- rendering (game thread) --------------------------------------------------------------------------------
struct GfxState { uint32_t view[4], clip[4]; };
static uint32_t *s_pt_mem; // tag_tFL_POINT scratch passed to guest code: guest memory (gas.h)
#define s_pt (s_pt_mem ? s_pt_mem : (s_pt_mem = (uint32_t *)gas_calloc(4, 4)))

static void fill(uint16_t *px, int n, uint16_t v) { for (int i = 0; i < n; i++) px[i] = v; }

// draws with fn into a black and a white buffer and mattes the difference
template <class F> static bool render(const std::string &key, F draw, int px0 = 0, int py0 = 0) {
    uint32_t g = g::gfx();
    if (!g) return false;
    int W = (int)M32(g + 0x24), H = (int)M32(g + 0x28);
    uint16_t *px = (uint16_t *)GP(M32(g + 0x20));
    if (!px || W <= 0 || H <= 0) return false;
    std::vector<uint16_t> save(px, px + (size_t)W * H);
    GfxState st;
    for (int i = 0; i < 4; i++) { st.view[i] = M32(g + 0x50 + 4 * i); st.clip[i] = M32(g + 0x60 + 4 * i); }
    // full-buffer view and clip (cflGraphics view box +0x50.., clip box +0x60.., view-relative)
    M32(g + 0x50) = 0; M32(g + 0x54) = 0; M32(g + 0x58) = (uint32_t)W; M32(g + 0x5c) = (uint32_t)H;
    M32(g + 0x60) = 0; M32(g + 0x64) = 0; M32(g + 0x68) = (uint32_t)W; M32(g + 0x6c) = (uint32_t)H;
    std::vector<uint16_t> blk((size_t)W * H), wht((size_t)W * H);
    fill(px, W * H, 0x0000);
    draw(W, H);
    memcpy(blk.data(), px, blk.size() * 2);
    fill(px, W * H, 0xffff);
    draw(W, H);
    memcpy(wht.data(), px, wht.size() * 2);
    memcpy(px, save.data(), save.size() * 2);
    for (int i = 0; i < 4; i++) { M32(g + 0x50 + 4 * i) = st.view[i]; M32(g + 0x60 + 4 * i) = st.clip[i]; }

    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            if (blk[i] != 0x0000 || wht[i] != 0xffff) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
        }
    Img im;
    if (x1 >= x0) {
        int w = x1 - x0 + 1, h = y1 - y0 + 1;
        im.w = w * UPSCALE;
        im.h = h * UPSCALE;
        im.sw = w;
        im.sh = h;
        im.ox = x0 - px0;
        im.oy = y0 - py0;
        im.rgba.resize((size_t)im.w * im.h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint16_t b = blk[(size_t)(y + y0) * W + x + x0], wv = wht[(size_t)(y + y0) * W + x + x0];
                int bg = (b >> 5) & 63, wg = (wv >> 5) & 63;
                int a = 255 - (wg - bg) * 255 / 63; // coverage from the green channel (6 bits)
                if (a < 0) a = 0;
                if (a > 255) a = 255;
                uint8_t r = 0, gg = 0, bb = 0;
                if (a > 0) {
                    int br = ((b >> 11) & 31) * 255 / 31, bgc = bg * 255 / 63, bbl = (b & 31) * 255 / 31;
                    r = (uint8_t)std::min(255, br * 255 / a);
                    gg = (uint8_t)std::min(255, bgc * 255 / a);
                    bb = (uint8_t)std::min(255, bbl * 255 / a);
                }
                for (int sy = 0; sy < UPSCALE; sy++)
                    for (int sx = 0; sx < UPSCALE; sx++) {
                        uint8_t *o = &im.rgba[(((size_t)(y * UPSCALE + sy)) * im.w + x * UPSCALE + sx) * 4];
                        o[0] = r; o[1] = gg; o[2] = bb; o[3] = (uint8_t)a;
                    }
            }
    }
    bool okk = !im.rgba.empty();
    std::lock_guard<std::mutex> l(g_mx);
    g_cache[key] = std::move(im);
    return okk;
}

static int origin_x(int d) { uint32_t g = g::gfx(); return g ? (int)M32(g + 0x24) / 2 + d : 0; }
static int origin_y(int d) { uint32_t g = g::gfx(); return g ? (int)M32(g + 0x28) / 2 + d : 0; }

bool info(const std::string &key, int *ox, int *oy, int *sw, int *sh) {
    std::lock_guard<std::mutex> l(g_mx);
    auto it = g_cache.find(key);
    if (it == g_cache.end() || it->second.rgba.empty()) return false;
    *ox = it->second.ox; *oy = it->second.oy; *sw = it->second.sw; *sh = it->second.sh;
    return true;
}

// in-situ capture: draw() is the original call with its full drawing state (tints, palettes); the rect (framebuffer
// coordinates) is filled black, drawn, filled white, drawn, then restored. Returns a content-addressed key.
std::string capture_insitu(int x, int y, int w, int h, const std::function<void()> &draw) {
    uint32_t g = g::gfx();
    if (!g || w <= 0 || h <= 0) return "";
    int W = (int)M32(g + 0x24), H = (int)M32(g + 0x28);
    uint16_t *px = (uint16_t *)GP(M32(g + 0x20));
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
    if (!px || x1 <= x0 || y1 <= y0) return "";
    int rw = x1 - x0, rh = y1 - y0;
    std::vector<uint16_t> save((size_t)rw * rh), blk(save.size()), wht(save.size());
    auto get = [&](std::vector<uint16_t> &dst) { for (int r = 0; r < rh; r++) memcpy(&dst[(size_t)r * rw], px + (size_t)(y0 + r) * W + x0, rw * 2); };
    auto put = [&](const std::vector<uint16_t> &src) { for (int r = 0; r < rh; r++) memcpy(px + (size_t)(y0 + r) * W + x0, &src[(size_t)r * rw], rw * 2); };
    auto fillr = [&](uint16_t v) { for (int r = 0; r < rh; r++) for (int c = 0; c < rw; c++) px[(size_t)(y0 + r) * W + x0 + c] = v; };
    get(save);
    fillr(0x0000); draw(); get(blk);
    fillr(0xffff); draw(); get(wht);
    put(save);
    // matte + bbox
    int bx0 = rw, by0 = rh, bx1 = -1, by1 = -1;
    for (int r = 0; r < rh; r++)
        for (int c = 0; c < rw; c++) {
            size_t i = (size_t)r * rw + c;
            if (blk[i] != 0x0000 || wht[i] != 0xffff) { bx0 = std::min(bx0, c); by0 = std::min(by0, r); bx1 = std::max(bx1, c); by1 = std::max(by1, r); }
        }
    if (bx1 < 0) return "";
    int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
    Img im;
    im.sw = bw; im.sh = bh;
    im.ox = x0 + bx0 - x; im.oy = y0 + by0 - y;
    im.w = bw * UPSCALE; im.h = bh * UPSCALE;
    im.rgba.resize((size_t)im.w * im.h * 4);
    uint64_t hsh = 1469598103934665603ull;
    for (int r = 0; r < bh; r++)
        for (int c = 0; c < bw; c++) {
            size_t i = (size_t)(r + by0) * rw + c + bx0;
            uint16_t b = blk[i], wv = wht[i];
            hsh = (hsh ^ b) * 1099511628211ull;
            hsh = (hsh ^ wv) * 1099511628211ull;
            int bg = (b >> 5) & 63, wg = (wv >> 5) & 63;
            int a = 255 - (wg - bg) * 255 / 63;
            a = a < 0 ? 0 : a > 255 ? 255 : a;
            uint8_t rr = 0, gg = 0, bb = 0;
            if (a > 0) {
                rr = (uint8_t)std::min(255, ((b >> 11) & 31) * 255 / 31 * 255 / a);
                gg = (uint8_t)std::min(255, bg * 255 / 63 * 255 / a);
                bb = (uint8_t)std::min(255, (b & 31) * 255 / 31 * 255 / a);
            }
            for (int sy = 0; sy < UPSCALE; sy++)
                for (int sx = 0; sx < UPSCALE; sx++) {
                    uint8_t *o = &im.rgba[(((size_t)(r * UPSCALE + sy)) * im.w + c * UPSCALE + sx) * 4];
                    o[0] = rr; o[1] = gg; o[2] = bb; o[3] = (uint8_t)a;
                }
        }
    char k[40];
    snprintf(k, sizeof k, "c%016llx_%d_%d", (unsigned long long)hsh, im.ox, im.oy);
    std::string key = k;
    std::lock_guard<std::mutex> l(g_mx);
    if (!g_cache.count(key)) g_cache[key] = std::move(im);
    return key;
}

static bool cached(const std::string &key, bool *good) {
    std::lock_guard<std::mutex> l(g_mx);
    auto it = g_cache.find(key);
    if (it == g_cache.end()) return false;
    *good = !it->second.rgba.empty();
    return true;
}

// cflSpriteSetInfo::DrawImage(this, tag_tFL_POINT*, idx, flip) @00151b84; palette override at +0x42
std::string image(int set, int idx, int pal, int flip) {
    std::string key = "i" + std::to_string(set) + "_" + std::to_string(idx) + "_" + std::to_string(pal) + "_" + std::to_string(flip);
    bool good;
    if (cached(key, &good)) return good ? key : "";
    uint32_t sets = g::sprite_sets();
    uint32_t info = sets ? M32(sets + 4 * set) : 0;
    if (!info || !M32(info + 0xc)) return ""; // sprite set not loaded (try again later)
    bool r = render(key, [&](int W, int H) {
        int16_t old = (int16_t)M16(info + 0x42);
        M16(info + 0x42) = (uint16_t)pal;
        s_pt[0] = (uint32_t)(W / 2 - 64); s_pt[1] = (uint32_t)(H / 2 - 64);
        g::call(0x141b84, {info, HPTR(s_pt), (uint32_t)idx, (uint32_t)flip});
        M16(info + 0x42) = (uint16_t)old;
    }, origin_x(-64), origin_y(-64));
    return r ? key : "";
}

// a picture tiled from consecutive images of one set (cols x rows tiles of `tile` px, images base..), drawn with
// cflSpriteSetInfo::DrawImage @00151b84 in one capture (cFormHelpFull's help pictures, onDrawControlHelp @0010a5fc)
std::string grid(int set, int base, int cols, int rows, int tile) {
    std::string key = "g" + std::to_string(set) + "_" + std::to_string(base) + "_" + std::to_string(cols) + "x" + std::to_string(rows);
    bool good;
    if (cached(key, &good)) return good ? key : "";
    uint32_t sets = g::sprite_sets();
    uint32_t info = sets ? M32(sets + 4 * set) : 0;
    if (!info || !M32(info + 0xc)) return "";
    bool r = render(key, [&](int W, int H) {
        int x0 = W / 2 - cols * tile / 2, y0 = H / 2 - rows * tile / 2;
        for (int i = 0; i < cols * rows; i++) {
            s_pt[0] = (uint32_t)(x0 + (i % cols) * tile); s_pt[1] = (uint32_t)(y0 + (i / cols) * tile);
            g::call(0x141b84, {info, HPTR(s_pt), (uint32_t)(base + i), 0});
        }
    }, origin_x(-cols * tile / 2), origin_y(-rows * tile / 2));
    return r ? key : "";
}

// cflSpriteSetInfo::DrawImageFx(this, pt*, idx, flip, tag_tBLIT_FX*) @00151c14: tinted / alpha-mapped images
static uint32_t *s_fx_mem;
#define s_fx (s_fx_mem ? s_fx_mem : (s_fx_mem = (uint32_t *)gas_calloc(4, 4)))
std::string image_fx(int set, int idx, int pal, int flip, const uint32_t fx[4]) {
    if (!fx || !fx[0]) return image(set, idx, pal, flip);
    char k[96];
    snprintf(k, sizeof k, "x%d_%d_%d_%d_%x_%x_%x_%x", set, idx, pal, flip, fx[0], fx[1], fx[2], fx[3]);
    std::string key = k;
    bool good;
    if (cached(key, &good)) return good ? key : "";
    uint32_t sets = g::sprite_sets();
    uint32_t info = sets ? M32(sets + 4 * set) : 0;
    if (!info || !M32(info + 0xc)) return "";
    bool r = render(key, [&](int W, int H) {
        int16_t old = (int16_t)M16(info + 0x42);
        M16(info + 0x42) = (uint16_t)pal;
        memcpy(s_fx, fx, 16);
        s_pt[0] = (uint32_t)(W / 2 - 64); s_pt[1] = (uint32_t)(H / 2 - 64);
        g::call(0x141c14, {info, HPTR(s_pt), (uint32_t)idx, (uint32_t)flip, HPTR(s_fx)});
        M16(info + 0x42) = (uint16_t)old;
    }, origin_x(-64), origin_y(-64));
    return r ? key : "";
}

// cflSpriteSetInfo::DrawFrame(this, parts, tag_tFL_POINT*, offset*, frame, fx, fx) @001522bc (composed frame with hotspot)
std::string frame(int set, int frm, int pal, int dy) {
    std::string key = "f" + std::to_string(set) + "_" + std::to_string(frm) + "_" + std::to_string(pal) + "_" + std::to_string(dy);
    bool good;
    if (cached(key, &good)) return good ? key : "";
    uint32_t sets = g::sprite_sets();
    uint32_t info = sets ? M32(sets + 4 * set) : 0;
    if (!info || !M32(info + 0xc)) return "";
    if (M8(info + 0x40) & 4) return ""; // costume sprite set: needs the wearer's parts (DrawCostumeFrameImpl)
    bool r = render(key, [&](int W, int H) {
        int16_t old = (int16_t)M16(info + 0x42);
        M16(info + 0x42) = (uint16_t)pal;
        s_pt[0] = (uint32_t)(W / 2); s_pt[1] = (uint32_t)(H / 2 + dy);
        g::call(0x1422bc, {info, 0, HPTR(s_pt), 0, (uint32_t)frm, 0, 0});
        M16(info + 0x42) = (uint16_t)old;
    }, origin_x(0), origin_y(dy));
    return r ? key : "";
}

} // namespace sprites
