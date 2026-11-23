// Recording of the original interface's draw calls (game thread) for the generic modern renderer.
// Hooked primitives (Ghidra addresses; ELF vaddr = -0x10000), recorded only inside cflUi::Draw @00152d40:
//   cflSpriteSetInfo::DrawImage @00151b84 (this, tag_tFL_POINT*, idx, flip)
//   cflSpriteSetInfo::DrawImageFx @00151c14 (this, pt*, idx, flip, fx*)
//   cflSpriteSetInfo::DrawFrame @001522bc (this, parts, pt*, offset*, frame, fx, fx)
//   cflSpriteSetInfo::DrawNumber @00151ccc (this, pt*, img0, value, spacing, digits, align, fx*)
//   cSmartCacheFont::DrawText @0016b8dc (this, x, y, box*, utf8)  -- colour = gfx+0x294, glyph height font+6
//   cflUi::DrawImageImpl @00154608 (tiled background images: chrome, not recorded)
//   cflUiForm::Draw @0015958c (attribution: each form draws itself, then the next form in the chain)
// Coordinates of the primitives are view-relative: framebuffer = coord + view origin (gfx+0x50, +0x54).
#include "autoform.h"
#include "capture_api.h"
#include "game.h"
#include "sprites.h"
#include "ui_internal.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <cstdio>

extern "C" {
#include "../hooks.h"
}

namespace autoform {

static bool g_rec;
static int g_nest;        // inside a recorded composite (number / frame / tile): its parts are not recorded
static std::vector<uint32_t> g_form_stack;
static std::vector<uint32_t> g_form_ptrs;
static std::vector<Cmd> g_cmds;
struct RawSprite { size_t cmd; int set, idx, pal, flip; uint32_t fx[4]; };
static std::vector<RawSprite> g_raw;
static Frame g_pub;
static std::mutex g_mx;
static std::map<uint32_t, int> g_setno;
// the world map's (form 0x12) pixels as they were when its own Draw ended: the canvas must not take the forms drawn
// over it (cFormLinkstoneChoice 0x13, drawn by the dialog screen) from the finished frame
static std::vector<uint16_t> g_map_px;
static int g_map_w, g_map_h;
static bool is_world_map(uint32_t f) { return f && (int16_t)M16(f + 0x38) == 0x12; }
static void keep_map_px() {
    uint32_t g = g::gfx(), px = M32(g + 0x20);
    g_map_w = (int)M32(g + 0x24); g_map_h = (int)M32(g + 0x28);
    if (px) g_map_px.assign((const uint16_t *)GP(px), (const uint16_t *)GP(px) + (size_t)g_map_w * g_map_h);
}

static int set_of(uint32_t info) {
    auto it = g_setno.find(info);
    if (it != g_setno.end()) return it->second;
    uint32_t sets = g::sprite_sets();
    for (int k = 0; sets && k < 0x200; k++)
        if (M32(sets + 4 * k) == info) { g_setno[info] = k; return k; }
    return -1;
}

static void base_cmd(Cmd &c, int x, int y) {
    uint32_t g = g::gfx();
    int vx = (int32_t)M32(g + 0x50), vy = (int32_t)M32(g + 0x54);
    c.x = x + vx;
    c.y = y + vy;
    c.clip[0] = (int32_t)M32(g + 0x60) + vx;
    c.clip[1] = (int32_t)M32(g + 0x64) + vy;
    c.clip[2] = (int32_t)M32(g + 0x68);
    c.clip[3] = (int32_t)M32(g + 0x6c);
    uint32_t f = g_form_stack.empty() ? 0 : g_form_stack.back();
    int fi = -1;
    for (size_t i = 0; i < g_form_ptrs.size(); i++) if (g_form_ptrs[i] == f) fi = (int)i;
    if (fi < 0) { g_form_ptrs.push_back(f); fi = (int)g_form_ptrs.size() - 1; }
    c.form = fi;
    c.color = 0; c.font_h = 0; c.align = 0;
}

static void rec_sprite(int kind, uint32_t info, int x, int y, int idx, int flip, uint32_t fx = 0) {
    int set = set_of(info);
    if (set < 0) return;
    Cmd c;
    c.kind = kind;
    base_cmd(c, x, y);
    RawSprite rs{g_cmds.size(), set, idx, (int16_t)M16(info + 0x42), flip, {0, 0, 0, 0}};
    if (fx) for (int i = 0; i < 4; i++) rs.fx[i] = M32(fx + 4 * i);
    g_raw.push_back(rs);
    g_cmds.push_back(std::move(c));
}

void begin_ui_draw() { g_rec = true; g_nest = 0; g_map_px.clear(); }
void end_ui_draw() { g_rec = false; }

} // namespace autoform

using namespace autoform;

extern "C" {
// in-situ capture of an image draw (DrawImage / DrawImageFx): the call is repeated onto black and white with the
// original's drawing state, so tinted glyph images and palette effects come out exactly as drawn
static void insitu(Cpu *c, void (*orig)(Cpu *)) {
    uint32_t info = c->r[0], pt = c->r[1];
    uint32_t is = M32(info + 0xc);
    uint32_t regs[4] = {c->r[0], c->r[1], c->r[2], c->r[3]};
    g::cpu = c;
    uint32_t img = is ? g::call(0x1411b4, {is, c->r[2]}) : 0; // cflImageSet::GetImagePtr
    memcpy(c->r, regs, sizeof regs);
    if (!img) return;
    uint32_t g = g::gfx();
    int x = (int32_t)M32(pt) + (int32_t)M32(g + 0x50), y = (int32_t)M32(pt + 4) + (int32_t)M32(g + 0x54);
    int w = M16(img + 4), h = M16(img + 6);
    Cmd cm;
    cm.kind = 0;
    cm.iw = w; cm.ih = h;
    base_cmd(cm, (int32_t)M32(pt), (int32_t)M32(pt + 4));
    cm.set = set_of(info);
    cm.idx = (int)regs[2];
    if (cm.set < 0 && getenv("EH_SPRDUMP")) { // test: where an unlisted sprite set lives
        static std::set<uint32_t> seen;
        if (seen.insert(info).second) {
            uint32_t ui = g::gameui(), f = g_form_stack.empty() ? 0 : g_form_stack.back();
            std::string where;
            char b[32];
            for (int o = 0; o < 0x400; o += 4) {
                if (ui && M32(ui + o) == info) { snprintf(b, sizeof b, "ui+%x ", o); where += b; }
                if (f && M32(f + o) == info) { snprintf(b, sizeof b, "form+%x ", o); where += b; }
            }
            ui_log("[spr] unlisted set %08x idx %d form %d: %s", info, cm.idx, f ? (int16_t)M16(f + 0x38) : -1, where.c_str());
        }
    }
    uint32_t sp = c->r[13];
    cm.key = sprites::capture_insitu(x, y, w, h, [&] {
        memcpy(c->r, regs, sizeof regs);
        c->r[13] = sp;
        orig(c);
    });
    memcpy(c->r, regs, sizeof regs);
    c->r[13] = sp;
    if (!cm.key.empty()) g_cmds.push_back(std::move(cm));
}
void f_141b84(Cpu *c) { // DrawImage
    if (g_rec && !g_nest) insitu(c, f_141b84_orig);
    f_141b84_orig(c);
}
void f_141c14(Cpu *c) { // DrawImageFx
    uint32_t a4 = ARG(c, 4);
    (void)a4;
    if (g_rec && !g_nest) insitu(c, f_141c14_orig);
    g_nest++;
    f_141c14_orig(c);
    g_nest--;
}
void f_1422bc(Cpu *c) { // DrawFrame: composed frame (hotspot unknown): in-situ capture of the area around the point
    uint32_t pt = c->r[2], off = c->r[3];
    if (g_rec && !g_nest && pt) {
        int x = (int32_t)M32(pt), y = (int32_t)M32(pt + 4);
        if (off) { x += (int32_t)M32(off); y += (int32_t)M32(off + 4); }
        uint32_t g = g::gfx();
        int vx = (int32_t)M32(g + 0x50), vy = (int32_t)M32(g + 0x54);
        Cmd cm;
        cm.kind = 0;
        base_cmd(cm, x, y);
        cm.set = set_of(c->r[0]);
        cm.idx = (int)ARG(c, 4);
        cm.frm = true;
        int ax = x + vx, ay = y + vy;
        // area: the clip box around the hotspot (frames extend up and to both sides of it)
        int rx = std::max(cm.clip[0], ax - 160), ry = std::max(cm.clip[1], ay - 200);
        int rx1 = std::min(cm.clip[0] + cm.clip[2], ax + 160), ry1 = std::min(cm.clip[1] + cm.clip[3], ay + 80);
        uint32_t regs[4] = {c->r[0], c->r[1], c->r[2], c->r[3]};
        uint32_t sp = c->r[13];
        g_nest++;
        cm.key = sprites::capture_insitu(rx, ry, rx1 - rx, ry1 - ry, [&] {
            memcpy(c->r, regs, sizeof regs);
            c->r[13] = sp;
            f_1422bc_orig(c);
        });
        g_nest--;
        memcpy(c->r, regs, sizeof regs);
        c->r[13] = sp;
        if (!cm.key.empty()) {
            // capture_insitu offsets are relative to the rect origin: make them relative to the draw point
            cm.x = rx; cm.y = ry;
            g_cmds.push_back(std::move(cm));
        }
    }
    g_nest++;
    f_1422bc_orig(c);
    g_nest--;
}
void f_141ccc(Cpu *c) { // DrawNumber
    if (g_rec && !g_nest) {
        Cmd cm;
        cm.kind = 3;
        base_cmd(cm, (int32_t)M32(c->r[1]), (int32_t)M32(c->r[1] + 4));
        cm.text = std::to_string((int32_t)c->r[3]);
        cm.align = (int)ARG(c, 6) & ~0x100;
        // digit height: cflImageSet::GetImagePtr(set+0xc, img0) -> cflImage +6 h
        uint32_t is = M32(c->r[0] + 0xc);
        uint32_t regs[4] = {c->r[0], c->r[1], c->r[2], c->r[3]};
        uint32_t img = is ? (g::cpu = c, g::call(0x1411b4, {is, c->r[2]})) : 0;
        memcpy(c->r, regs, sizeof regs);
        cm.font_h = img ? M16(img + 6) : 10;
        cm.color = 0xffffff00;
        g_cmds.push_back(std::move(cm));
    }
    g_nest++;
    f_141ccc_orig(c);
    g_nest--;
}
void f_144608(Cpu *c) { // cflUi::DrawImageImpl: tiled background
    g_nest++;
    f_144608_orig(c);
    g_nest--;
}
void f_15b8dc(Cpu *c) { // cSmartCacheFont::DrawText
    if (g_rec && !g_nest) {
        const char *s = (const char *)GP(ARG(c, 4));
        if (s && *s) {
            Cmd cm;
            cm.kind = 2;
            base_cmd(cm, (int32_t)c->r[1], (int32_t)c->r[2]);
            uint32_t g = g::gfx();
            cm.color = M32(g + 0x294);
            cm.font_h = (int16_t)M16(c->r[0] + 6);
            cm.text = g::rml_text(s, false);
            // outlined labels draw the same run 8 times around the final position: keep the last
            for (auto it = g_cmds.rbegin(); it != g_cmds.rend() && it - g_cmds.rbegin() < 10; ++it)
                if (it->kind == 2 && it->text == cm.text && std::abs(it->x - cm.x) <= 2 && std::abs(it->y - cm.y) <= 2) {
                    it->kind = -1;
                }
            g_cmds.push_back(std::move(cm));
            // world map labels (region names) are drawn by the renderer as text: keep them out of the canvas pixels
            // under it, or the original's glyphs show around the modern ones
            if (capture::modern && is_world_map(g_form_stack.empty() ? 0 : g_form_stack.back())) return;
        }
    }
    f_15b8dc_orig(c);
}
void f_14958c(Cpu *c) { // cflUiForm::Draw
    // a form's Draw draws the next form of the chain inside it: the world map's own pixels are complete when the
    // next form starts (or when the map's Draw returns, with nothing over it)
    uint32_t f = c->r[0];
    if (g_rec && g_map_px.empty() && !g_form_stack.empty() && is_world_map(g_form_stack.back())) keep_map_px();
    g_form_stack.push_back(f);
    f_14958c_orig(c);
    g_form_stack.pop_back();
    if (g_rec && g_map_px.empty() && is_world_map(f)) keep_map_px();
}
}

namespace autoform {

// Map forms draw their content in element draw callbacks with fill / line primitives (cFormMinimap::canvasDraw
// @00102b74 / canvas2xDraw @00103da0 via cflUiCustom draw handler +0x24, cFormWorldMap panels via cflUiPanel draw handler +0x3c; event 7,
// cflUiCustom::SetEventHandler @00155a68 / cflUiPanel::SetEventHandler @00156b5c; work/ui_spec.md 289).
// Their pixels are taken from the frame as drawn (this runs at FlushImpl, before the world-only frame is restored).
static bool canvas_form(int id) { return id == 0x1d || id == 0x12; }
static void add_canvas(Frame &fr, int form, int x, int y, int w, int h) {
    uint32_t g = g::gfx();
    uint32_t px = M32(g + 0x20);
    int W = (int)M32(g + 0x24), H = (int)M32(g + 0x28);
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
    if (!px || x1 - x0 < 8 || y1 - y0 < 8) return;
    const uint16_t *src = (const uint16_t *)GP(px);
    if (fr.forms[form].id == 0x12 && g_map_w == W && g_map_h == H && g_map_px.size() == (size_t)W * H) src = g_map_px.data();
    Canvas cv;
    cv.name = "cv" + std::to_string(fr.canvases.size());
    cv.w = x1 - x0; cv.h = y1 - y0;
    cv.rgba.resize((size_t)cv.w * cv.h * 4);
    for (int yy = 0; yy < cv.h; yy++)
        for (int xx = 0; xx < cv.w; xx++) {
            uint16_t v = src[(size_t)(y0 + yy) * W + x0 + xx];
            uint8_t *o = &cv.rgba[((size_t)yy * cv.w + xx) * 4];
            o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255;
        }
    if (const char *dd = getenv("EH_CVDUMP")) { // test: the root canvas every 60 frames as <dir>/cv_<n>.raw (w, h, RGBA)
        static int n;
        if (fr.canvases.empty() && n++ % 60 == 0) {
            if (FILE *f = fopen((std::string(dd) + "/cv_" + std::to_string(n / 60) + ".raw").c_str(), "wb")) {
                uint32_t hd[2] = {(uint32_t)cv.w, (uint32_t)cv.h};
                fwrite(hd, 4, 2, f); fwrite(cv.rgba.data(), 1, cv.rgba.size(), f); fclose(f);
            }
        }
    }
    Box b{form, 5, x0, y0, cv.w, cv.h, false, false, false};
    b.canvas = (int)fr.canvases.size();
    fr.canvases.push_back(std::move(cv));
    fr.boxes.push_back(b);
}

// element tree -> panels / buttons / list cells (work/ui_spec.md 1.2-1.6)
static void walk(Frame &fr, int form, uint32_t e, int ox, int oy, uint32_t focus_el, int depth) {
    if (!e || depth > 12) return;
    uint32_t fl = M32(e + 0x14);
    if (fl & 0x8000) return;
    int x = ox + (int16_t)M16(e + 0x18), y = oy + (int16_t)M16(e + 0x1a);
    int w = (int16_t)M16(e + 0x1c), h = (int16_t)M16(e + 0x1e);
    int type = (fl >> 9) & 0x1f;
    bool focused = e == focus_el;
    bool disabled = (fl & 0x10000) != 0;
    if (canvas_form(fr.forms[form].id)) { Box eb{form, 6, x, y, w, h, false, false, false}; eb.el = e; fr.boxes.push_back(eb); }
    if (canvas_form(fr.forms[form].id) && w >= 24 && h >= 24 &&
        ((type == 7 && M32(e + 0x24)) || (type == 8 && M32(e + 0x3c)))) {
        add_canvas(fr, form, x, y, w, h);
        if (type == 7) return;
    }
    switch (type) {
    case 4: fr.boxes.push_back({form, 2, x, y, w, h, focused, (fl & 0x8000000) != 0, disabled}); break; // button
    case 8: case 0xe: {                                                                           // (draw) panel
        uint16_t scr = M16(e + 0x2a);
        if (scr && scr != 0xffff && w >= 24 && h >= 16 && !(fl & 0x8000000)) fr.boxes.push_back({form, 1, x, y, w, h, focused, false, false});
        break;
    }
    case 0xd: fr.boxes.push_back({form, 1, x, y, w, h, focused, false, false}); break;           // scroll container
    case 5: {                                                                                     // image list: cells
        int cw = (int16_t)M16(e + 0x28), ch = (int16_t)M16(e + 0x2a), gx = (int16_t)M16(e + 0x20), gy = (int16_t)M16(e + 0x22);
        int rows = (int16_t)M16(e + 0x2c), cols = (int16_t)M16(e + 0x2e), n = (int16_t)M16(e + 0x30), sel = (int16_t)M16(e + 0x32);
        if (cw > 0 && ch > 0 && cols > 0)
            for (int i = 0; i < n && i < 400; i++) {
                int cx = x + (i % cols) * (cw + gx), cy = y + (i / cols) * (ch + gy);
                if (cy + ch < oy || cy > y + h + (rows + 1) * (ch + gy)) continue;
                fr.boxes.push_back({form, 3, cx, cy, cw, ch, i == sel, false, false});
            }
        break;
    }
    }
    bool container = type == 8 || type == 9 || type == 0xd || type == 0xe || type == 0x11 || type == 0xa || depth == 0;
    if (!container) return;
    int n = M8(e + 0x20);
    uint32_t kids = M32(e + 0x24);
    if (!kids) return;
    bool paged = (type == 8 || type == 0xe) && (fl & 0x400000);
    int cur = M8(e + 0x21);
    for (int i = 0; i < n && i < 128; i++) {
        if (paged && i != cur) continue;
        uint32_t k = M32(kids + 4 * i);
        if (type == 9 && k) { // split container: selected cell
            int kx = x + (int16_t)M16(k + 0x18), ky = y + (int16_t)M16(k + 0x1a);
            if (i == cur && (fl & 0x20000000)) fr.boxes.push_back({form, 3, kx, ky, (int16_t)M16(k + 0x1c), (int16_t)M16(k + 0x1e), true, false, false});
        }
        walk(fr, form, k, x, y, focus_el, depth + 1);
    }
}

void end_frame(Cpu *c, const std::vector<int> &unported) {
    g::cpu = c;
    Frame fr;
    static uint64_t seq;
    fr.seq = ++seq;
    // forms in draw order
    uint32_t ui = g::gameui();
    uint32_t foc = ui ? M32(ui + 0x18) : 0;
    std::map<uint32_t, int> index;
    for (uint32_t f = ui ? M32(ui + 0x14) : 0, n = 0; f && n < 32; f = M32(f + 0x40), n++) {
        uint32_t fl = M32(f + 0x14);
        if (fl & 0x1808000) continue;
        int id = (int16_t)M16(f + 0x38);
        bool want = false;
        for (int u : unported) if (u == id) want = true;
        // every form's recorded draws are kept (dedicated screens embed original pixel art from them);
        // panels / buttons only for the forms the generic renderer draws
        FormInfo fi{id, f, (int16_t)M16(f + 0x18), (int16_t)M16(f + 0x1a), (int16_t)M16(f + 0x1c), (int16_t)M16(f + 0x1e), f == foc,
                    (fl & 0x8000000) != 0, want};
        index[f] = (int)fr.forms.size();
        fr.forms.push_back(fi);
        if (!want && !canvas_form(id)) continue; // dedicated map screens still need the canvas pixels
        fr.boxes.push_back({(int)fr.forms.size() - 1, 0, fi.x, fi.y, fi.w, fi.h, false, false, false});
        walk(fr, (int)fr.forms.size() - 1, f, 0, 0, M32(f + 0x44), 0);
    }
    // recorded commands of those forms; sprites rendered (cached) now, on the game thread
    for (auto &r : g_raw) {
        Cmd &cm = g_cmds[r.cmd];
        if (cm.kind < 0) continue;
        cm.set = r.set; cm.idx = r.idx;
        cm.key = cm.kind == 0 ? sprites::image_fx(r.set, r.idx, r.pal, r.flip, r.fx) : sprites::frame(r.set, r.idx, r.pal, 0);
    }
    // draws made outside every cflUiForm::Draw belong to the form drawn next: a form's own Draw override paints them
    // before calling the base Draw (cFormTextScroller::Draw @00102144: DrawTitleBG, the prologue art behind the crawl)
    {
        uint32_t next = 0;
        for (size_t i = g_cmds.size(); i-- > 0;) {
            Cmd &cm = g_cmds[i];
            uint32_t p = cm.form >= 0 && cm.form < (int)g_form_ptrs.size() ? g_form_ptrs[cm.form] : 0;
            if (p) { next = p; continue; }
            if (!next) continue;
            for (size_t k = 0; k < g_form_ptrs.size(); k++) if (g_form_ptrs[k] == next) cm.form = (int)k;
        }
    }
    for (auto &cm : g_cmds) {
        if (cm.kind < 0) continue;
        uint32_t fptr = cm.form >= 0 && cm.form < (int)g_form_ptrs.size() ? g_form_ptrs[cm.form] : 0;
        auto it = index.find(fptr);
        if (getenv("EH_AFDEBUG") && fr.seq % 60 == 0 && (it == index.end() || ((cm.kind == 0 || cm.kind == 1) && cm.key.empty())))
            ui_log("[af]  dropped k%d at %d,%d set %d idx %d form ptr %x key '%s'", cm.kind, cm.x, cm.y, cm.set, cm.idx, fptr, cm.key.c_str());
        if (it == index.end()) continue;
        if ((cm.kind == 0 || cm.kind == 1) && cm.key.empty()) continue;
        Cmd o = cm;
        o.form = it->second;
        fr.cmds.push_back(std::move(o));
    }
    if (getenv("EH_AFDEBUG") && fr.seq % 60 == 0) {
        ui_log("[af] forms %d boxes %d cmds %d (raw cmds %d, form ptrs %d)", (int)fr.forms.size(), (int)fr.boxes.size(), (int)fr.cmds.size(), (int)g_cmds.size(), (int)g_form_ptrs.size());
        for (auto &f : fr.forms) ui_log("[af]  form %x at %d,%d %dx%d", f.id, f.x, f.y, f.w, f.h);
        for (auto &c : fr.cmds) ui_log("[af]  cmd k%d %d,%d h%d col %08x key %s text %s", c.kind, c.x, c.y, c.font_h, c.color, c.key.c_str(), c.text.c_str());
    }
    g_cmds.clear();
    g_raw.clear();
    g_form_ptrs.clear();
    std::lock_guard<std::mutex> l(g_mx);
    g_pub = std::move(fr);
}

void pull(Frame &out) {
    std::lock_guard<std::mutex> l(g_mx);
    if (out.seq != g_pub.seq) out = g_pub;
}

} // namespace autoform
