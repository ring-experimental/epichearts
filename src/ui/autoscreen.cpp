// Generic modern renderer for original forms without a dedicated screen (data from autoform.cpp).
// Layout follows the original form (scaled to the window); chrome is replaced by the modern panel style, text is
// re-set with the interface font, pixel art (icons, portraits) is shown sharp. Input is not intercepted: clicks
// reach the original form as touches and keys as the original's key codes.
#include "autoform.h"
#include "wordimg.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <set>
#include <map>
#include "screen_util.h"
#include "sprites.h"
#include "ui.h"
#include "render.h"

namespace screens {

class AutoScreen : public Screen {
    struct Session {
        std::string last;        // the markup drawn
    } ss;
    autoform::Frame fr;          // per-frame copy of the recorded draws

public:
    AutoScreen() { z = 30; register_screen(this); }
    const char *file() override { return "auto.rml"; }
    bool modal() override { return false; }
    bool want(const state::Snapshot &s) override {
        fr = recorded();
        bool any = false;
        for (auto &f : fr.forms) any |= f.generic;
        return s.stripped && any;
    }
    void on_show() override { ss = {}; }
    void on_hide() override { ss.last.clear(); d.rml("root", ""); }

    // test (EH_SPRDUMP=<dir>): each sprite once as <key>.raw (u32 w, u32 h, RGBA)
    static void dump_sprite(const char *dir, const std::string &key, const std::string &name) {
        static std::set<std::string> done;
        if (!done.insert(name).second) return;
        int w, h;
        const uint8_t *px = sprites::lookup(key.c_str(), &w, &h);
        if (!px) { done.erase(name); return; }
        FILE *f = fopen((std::string(dir) + "/" + name + ".raw").c_str(), "wb");
        if (!f) return;
        uint32_t hd[2] = {(uint32_t)w, (uint32_t)h};
        fwrite(hd, 4, 2, f);
        fwrite(px, 4, (size_t)w * h, f);
        fclose(f);
    }
    // the colour a word picture was drawn in (its opaque pixels, captured in situ), lifted for the dark glass
    static std::string word_color(const std::string &key) {
        static std::map<std::string, std::string> cache;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        int w, h;
        const uint8_t *px = sprites::lookup(key.c_str(), &w, &h);
        double r = 0, g = 0, b = 0, n = 0;
        for (int i = 0; px && i < w * h; i++) {
            const uint8_t *p = px + 4 * i;
            if (p[3] < 160) continue;
            r += p[0]; g += p[1]; b += p[2]; n++; // straight alpha (sprites.cpp)
        }
        if (n <= 0) return cache[key] = "#ffffff";
        r /= n; g /= n; b /= n;
        // dark tints were chosen for the original's light panels: brighten toward white but keep the hue (red = unusable)
        double lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
        if (lum < 0.55) { double k = 0.45; r += (255 - r) * k; g += (255 - g) * k; b += (255 - b) * k; }
        return cache[key] = Rml::CreateString("#%02x%02x%02x", (int)r, (int)g, (int)b);
    }
    static std::string color_css(uint32_t c) {
        int r = (c >> 8) & 0xff, g = (c >> 16) & 0xff, b = (c >> 24) & 0xff;
        float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.f;
        if (lum < 0.45f) { // colours chosen for the original's light panels: lift them for the dark glass
            r = 255 - (255 - r) / 4; g = 255 - (255 - g) / 4; b = 255 - (255 - b) / 4;
        }
        return Rml::CreateString("#%02x%02x%02x", r, g, b);
    }

    void update(const state::Snapshot &s) override {
        (void)s;
        // original forms keep the unzoomed mapping (the field zoom is for the world); x and y scale separately
        const ui::FormMap m = ui::form_map();
        const float sx = m.sx, sy = m.sy;
        auto X = [&](float x) { return m.X(x); };
        auto Y = [&](float y) { return m.Y(y); };
        std::string r;
        r.reserve(8192);
        for (size_t fi = 0; fi < fr.forms.size(); fi++) {
            const autoform::FormInfo &f = fr.forms[fi];
            if (!f.generic) continue;
            if (f.dim_parent) r += "<div class=\"af-dim\"/>";
            // boxes
            std::vector<const autoform::Box *> btns;
            for (auto &b : fr.boxes) {
                if (b.form != (int)fi) continue;
                if (b.kind == 5) { // map canvas: the original's pixels, live texture
                    const autoform::Canvas &cv = fr.canvases[b.canvas];
                    present::live_update(cv.name, cv.rgba.data(), cv.w, cv.h);
                    r += Rml::CreateString("<img class=\"af-cv\" src=\"live:%s\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;height:%.0fpx\"/>", cv.name.c_str(),
                                           X(b.x), Y(b.y), b.w * sx, b.h * sy);
                    continue;
                }
                if (b.kind == 6) continue; // element rects for dedicated map screens
                const char *cls = b.kind == 0 ? "af-form" : b.kind == 1 ? "af-panel" : b.kind == 2 ? "af-btn" : "af-cell";
                std::string extra;
                if (b.kind == 2 && b.pressed) extra += " on";
                if (b.kind == 2 && b.disabled) extra += " off";
                if (b.kind == 3 && b.focused) extra += " sel";
                if (b.kind == 2 && b.focused) extra += " af-focus";
                if (b.kind == 0 && b.w >= g_game_w - 8 && b.h >= g_game_h - 8) cls = "af-dim"; // full-screen form: backdrop only
                r += Rml::CreateString("<div class=\"%s%s\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;height:%.0fpx\"/>", cls, extra.c_str(),
                                       X(b.x), Y(b.y), b.w * sx, b.h * sy);
                if (b.kind == 2) btns.push_back(&b);
            }
            // recorded draws, grouped by clip box
            int cur[4] = {-1, -1, -1, -1};
            bool open = false;
            // word pictures drawn side by side on one line ("강화" + "석") become one run of text
            struct { bool on = false; int y = 0, right = 0; float l = 0, t = 0, h = 0; std::string text, col; } pw;
            auto flush = [&] {
                if (!pw.on) return;
                r += Rml::CreateString("<span class=\"af-w\" style=\"left:%.0fpx;top:%.0fpx;height:%.0fpx;line-height:%.0fpx;font-size:%.0fpx;color:%s\">",
                                       pw.l, pw.t, pw.h, pw.h, pw.h * 1.12f, pw.col.c_str()) + pw.text + "</span>";
                pw.on = false;
            };
            // the crawl darkens its background art with cMenuMgr::DrawBgFilter(10) @00156a58 (a multiply by
            // #4F5A74 over the view, not recorded): after the art, a dark layer of about that strength
            // (UNVERIFIED: RmlUi has no multiply blend; a translucent navy is the approximation)
            bool crawl_bg = false, crawl_filter = false;
            for (auto &c : fr.cmds) {
                if (c.form != (int)fi) continue;
                if (f.id == 0x24 && c.set == 0x6e) crawl_bg = true;
                else if (crawl_bg && !crawl_filter) {
                    crawl_filter = true;
                    flush();
                    r += "<div class=\"af-filter\"/>";
                }
                if (memcmp(cur, c.clip, sizeof cur) != 0) {
                    flush();
                    if (open) r += "</div>";
                    memcpy(cur, c.clip, sizeof cur);
                    r += Rml::CreateString("<div class=\"af-clip\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;height:%.0fpx\">", X(c.clip[0]), Y(c.clip[1]),
                                           c.clip[2] * sx, c.clip[3] * sy);
                    open = true;
                }
                float lx = (c.x - c.clip[0]) * sx, ly = (c.y - c.clip[1]) * sy;
                if (c.kind == 0 || c.kind == 1) {
                    int ox, oy, sw, sh;
                    if (!sprites::info(c.key, &ox, &oy, &sw, &sh)) continue;
                    // chrome: a sprite that fills most of a button or the form is its old background
                    int ax = c.x + ox, ay = c.y + oy;
                    // (the crawl's background is a picture, not chrome: cFormTextScroller::DrawTitleBG @001020c8 draws the
                    // prologue art, cMenuMgr::DrawPrologueBg @00156adc, set 0x6E images 0-2, behind the text)
                    // judged by the image's own size: a background drawn twice side by side (cFormWorldMap::OnBgDraw
                    // @000fdacc: map and cloud layers at -(t mod w) and again at w - (t mod w)) has its second copy cut by
                    // the screen edge; captured small, it was drawn again over the canvas (cloudless map strip, hard-edged
                    // doubled clouds on the world map's right side)
                    int fw = c.iw > 0 ? c.iw : sw, fh = c.ih > 0 ? c.ih : sh;
                    bool chrome = fw * fh > (f.w * f.h) / 2 && f.id != 0x24;
                    // a sprite filling a button is its old face when the button also has a label; on a button without
                    // text it is the button's icon (arrows, magnifier) and is kept
                    for (auto *b : btns)
                        if (ax >= b->x - 3 && ay >= b->y - 3 && ax + sw <= b->x + b->w + 3 && ay + sh <= b->y + b->h + 3 && sw * sh * 10 >= b->w * b->h * 6) {
                            bool labelled = false;
                            for (auto &t : fr.cmds)
                                if (t.form == (int)fi && (t.kind == 2 || t.kind == 3) && t.x >= b->x - 2 && t.x <= b->x + b->w + 2 && t.y >= b->y - 2 && t.y <= b->y + b->h + 2) labelled = true;
                            if (labelled) chrome = true;
                        }
                    if (chrome) continue;
                    // a word the original draws as a picture (sprite set 11): set in the interface font, in the colour it was drawn
                    if (const char *w = !c.frm ? wordimg::word(c.set, c.idx) : nullptr) {
                        std::string col = word_color(c.key);
                        if (pw.on && std::abs(ay - pw.y) <= 1 && ax - pw.right >= -2 && ax - pw.right <= sh) {
                            pw.text += w;
                            pw.right = ax + sw;
                            continue;
                        }
                        flush();
                        pw.on = true; pw.y = ay; pw.right = ax + sw;
                        pw.l = lx + ox * sx; pw.t = ly + oy * sy; pw.h = sh * sy;
                        pw.text = w; pw.col = col;
                        continue;
                    }
                    flush();
                    if (const char *dd = getenv("EH_SPRDUMP")) dump_sprite(dd, c.key, c.set >= 0 ? Rml::CreateString("%c%d_%d", c.frm || c.kind ? 'f' : 'i', c.set, c.idx) : "x" + c.key); // test: collect the sprites generic forms draw
                    r += Rml::CreateString("<img class=\"af-s\" src=\"sprite:%s\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;height:%.0fpx\"/>", c.key.c_str(),
                                           lx + ox * sx, ly + oy * sy, sw * sx, sh * sy);
                } else if (c.kind == 2) {
                    flush();
                    float fs = (c.font_h > 0 ? c.font_h : 12) * sy * 0.92f;
                    r += Rml::CreateString("<span class=\"af-t\" style=\"left:%.0fpx;top:%.0fpx;font-size:%.0fpx;color:%s\">", lx, ly - fs * 0.08f, fs,
                                           color_css(c.color).c_str()) + c.text + "</span>";
                } else if (c.kind == 3) {
                    flush();
                    float fs = (c.font_h > 0 ? c.font_h : 10) * sy * 1.05f;
                    // DrawNumber: align 0 = x is the right edge, 1 = centre, 2 = left edge (cflSpriteSetInfo::DrawNumber @00151ccc)
                    float w = 400;
                    float left = c.align == 2 ? lx : c.align == 1 ? lx - w / 2 : lx - w;
                    const char *ta = c.align == 2 ? "left" : c.align == 1 ? "center" : "right";
                    r += Rml::CreateString("<span class=\"af-n\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;text-align:%s;font-size:%.0fpx\">", left, ly - fs * 0.1f, w, ta, fs) +
                         c.text + "</span>";
                }
            }
            flush();
            if (open) r += "</div>";
            // buttons the original draws only with tiled chrome (no sprite, no label) at the form's edges are scroll
            // arrows: give them a direction icon
            for (auto *b : btns) {
                bool content = false;
                for (auto &c : fr.cmds)
                    if (c.form == (int)fi && c.x >= b->x - 2 && c.x <= b->x + b->w + 2 && c.y >= b->y - 2 && c.y <= b->y + b->h + 2) content = true;
                if (content) continue;
                float cx = (b->x + b->w / 2.f - f.x) / std::max(1, f.w), cy = (b->y + b->h / 2.f - f.y) / std::max(1, f.h);
                const char *ic = cx < 0.12f ? "&#xe06e;" : cx > 0.88f ? "&#xe06f;" : cy < 0.15f ? "&#xe070;" : cy > 0.85f ? "&#xe06d;" : nullptr;
                if (!ic) continue;
                r += Rml::CreateString("<span class=\"af-ic\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;height:%.0fpx;line-height:%.0fpx;font-size:%.0fpx\">%s</span>",
                                       X(b->x), Y(b->y), b->w * sx, b->h * sy, b->h * sy, std::min(b->w * sx, b->h * sy) * 0.5f, ic);
            }
        }
        if (r != ss.last) {
            ss.last = r;
            d.rml("root", r);
        }
    }
};

static AutoScreen g_auto;
}
