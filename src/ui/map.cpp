// Area map (replaces cFormMinimap, form 0x1D; opened by HUD hit code 2 / key 0x2B, work/hud_spec.md).
// The map is redrawn from the original's data instead of its 554x277 scrolling canvas, so the whole island fits.
// Data (cFormMinimap::SetMinimapData @00101c78, OnConstruct @0010356c):
//   rooms  form+0xA0, count u16 form+0x9C, 8 bytes: x, y (10 px cells), u8 (w << 4 | h), u8 flags
//          (0x20 visited / drawn, 0x40 special room with icon, 0x10 + low nibble: only for that sub field),
//          s16 field group +4, u16 name string +6 (cField::GetString, FocusTheSelectedMap @00103098)
//   aisles form+0xA4, count u16 form+0x9E, 4 bytes: x, y, u8 (len << 4 | bit0 vertical | bit2 open | bit1 red), room
// Geometry and colours follow cFormMinimap::canvasDraw @00102b74: room box (x*10+4, y*10+4, w*10-8, h*10-8) with a
// 2 px border 0x39272700, fill 0x83666000 (0x9d8c6a00 for special rooms); aisle (x*10-4, y*10-4, len*10+8, 10),
// fill colour 0 / 0x83666000 / 0x594c9200. Selected room form+0xA8 (DrawCursor @00101da8), quest target group
// form+0xDC (quest marker +0x54), zoom view when panel form+0x80 is visible (Show2x @00101ca8; its canvas +0x84 is
// shown as captured pixels). Keys still go to the original: directions move the selection (canvasKeyPress
// @00103128), 0x2B zooms, 0x3A closes.
#include "autoform.h"
#include "game.h"
#include "render.h"
#include "screen_util.h"
#include "screens.h"
#include "ui.h"
#include "ui_internal.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace state {
struct MapRoom { int x, y, w, h, flags, group; };
struct MapAisle { int x, y, len, flags, room; };
struct MapData {
    uint32_t ptr = 0;
    std::vector<MapRoom> rooms;
    std::vector<MapAisle> aisles;
    int sel = -1, quest_group = -1, cur_group = -1, cur_sub = -1;
    bool zoomed = false;
    std::string sel_name;
};
static MapData g_map_pub;
static std::mutex g_map_mx;

void capture_map(const Snapshot &s) {
    MapData m;
    if (!s.forms.empty() && s.forms.back().id == 0x1d) {
        uint32_t f = s.forms.back().ptr;
        m.ptr = f;
        uint32_t R = M32(f + 0xa0), A = M32(f + 0xa4);
        int nr = M16(f + 0x9c), na = M16(f + 0x9e);
        for (int i = 0; R && i < nr && i < 512; i++) {
            uint32_t r = R + 8 * i;
            m.rooms.push_back({M8(r), M8(r + 1), M8(r + 2) >> 4, M8(r + 2) & 0xf, M8(r + 3), (int16_t)M16(r + 4)});
        }
        for (int i = 0; A && i < na && i < 1024; i++) {
            uint32_t a = A + 4 * i;
            m.aisles.push_back({M8(a), M8(a + 1), M8(a + 2) >> 4, M8(a + 2) & 0xf, M8(a + 3)});
        }
        m.sel = (int32_t)M32(f + 0xa8);
        m.quest_group = (int32_t)M32(f + 0xdc);
        uint32_t w = g::world();
        if (w) { m.cur_group = (int32_t)M32(w + 8); m.cur_sub = (int32_t)M32(w + 0xc); }
        uint32_t zp = M32(f + 0x80);
        m.zoomed = zp && !(M32(zp + 0x14) & 0x8000);
        uint32_t lab = M32(f + 0x68);
        if (lab) m.sel_name = g::plain_text(element_text(lab).c_str());
    }
    std::lock_guard<std::mutex> l(g_map_mx);
    g_map_pub = std::move(m);
}
static MapData pull_map() { std::lock_guard<std::mutex> l(g_map_mx); return g_map_pub; }
}

namespace screens {

class MapScreen : public Screen {
    struct Session {
        std::string last;        // the map markup drawn
        uint64_t seq = 0;        // recorded frame of the zoom canvas shown
    } ss;
    state::MapData m;            // per-frame copy of the captured form
    // data model "map" (map.rml), set in update() only: the head, the legend and the zoom state. The rooms / aisles
    // (#map, sized from the box's layout) and the zoom canvas (#zimg / #zbox) stay in C++ (Doc)
    struct View {
        std::string title, hint;     // markup (data-rml): the original's room / field name, glyphs
        std::string kick, zlabel;
        bool zoomed = false, any_quest = false, any_shut = false, any_sp = false;
    } v;

    bool is_current(const state::MapRoom &r) const {
        return r.group == m.cur_group && (!(r.flags & 0x10) || (r.flags & 0xf) == m.cur_sub);
    }

public:
    MapScreen() { z = 28; state::set_ported(0x1d); register_screen(this); }
    const char *file() override { return "map.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("map");
        if (!c) return;
        c.Bind("title", &v.title); c.Bind("hint", &v.hint); c.Bind("kick", &v.kick); c.Bind("zlabel", &v.zlabel);
        c.Bind("zoomed", &v.zoomed); c.Bind("any_quest", &v.any_quest); c.Bind("any_shut", &v.any_shut); c.Bind("any_sp", &v.any_sp);
        model = c.GetModelHandle();
    }
    bool modal() override { return false; } // keys go to the original canvas (selection, zoom, close)
    bool want(const state::Snapshot &s) override {
        m = state::pull_map();
        return s.stripped && m.ptr && !s.forms.empty() && s.forms.back().id == 0x1d;
    }
    void on_show() override { ss = {}; }

    void update(const state::Snapshot &s) override {
        // extent of what the original draws (visited rooms and their aisles), in original pixels
        int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
        auto grow = [&](int ax, int ay, int bx, int by) { x0 = std::min(x0, ax); y0 = std::min(y0, ay); x1 = std::max(x1, bx); y1 = std::max(y1, by); };
        for (auto &r : m.rooms) if (r.flags & 0x20) grow(r.x * 10 + 4, r.y * 10 + 4, r.x * 10 + r.w * 10 - 4, r.y * 10 + r.h * 10 - 4);
        for (auto &a : m.aisles) {
            if (a.room < 0 || a.room >= (int)m.rooms.size() || !(m.rooms[a.room].flags & 0x20)) continue;
            if (a.flags & 1) grow(a.x * 10 - 4, a.y * 10 - 4, a.x * 10 + 6, a.y * 10 + a.len * 10 + 4);
            else grow(a.x * 10 - 4, a.y * 10 - 4, a.x * 10 + a.len * 10 + 4, a.y * 10 + 6);
        }
        Rml::Element *box = d.el("map");
        std::string r;
        if (x1 > x0 && box) {
            float bw = box->GetClientWidth(), bh = box->GetClientHeight();
            float k = std::min({(bw - 60) / (x1 - x0), (bh - 60) / (y1 - y0), 4.f * ui::game_rect.w / g_game_w});
            float ox = (bw - (x1 - x0) * k) / 2 - x0 * k, oy = (bh - (y1 - y0) * k) / 2 - y0 * k;
            auto rect = [&](const char *cls, float x, float y, float w, float h, const std::string &extra = std::string()) {
                return Rml::CreateString("<div class=\"%s\" style=\"left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx\"%s>", cls, ox + x * k, oy + y * k,
                                         w * k, h * k, extra.c_str());
            };
            for (auto &a : m.aisles) {
                if (a.room < 0 || a.room >= (int)m.rooms.size() || !(m.rooms[a.room].flags & 0x20)) continue;
                const char *cls = !(a.flags & 4) ? "ai shut" : (a.flags & 2) ? "ai red" : "ai";
                if (a.flags & 1) r += rect(cls, a.x * 10 - 2, a.y * 10 - 2, 4, a.len * 10 + 4) + "</div>";
                else r += rect(cls, a.x * 10 - 2, a.y * 10 - 2, a.len * 10 + 4, 4) + "</div>";
            }
            for (size_t i = 0; i < m.rooms.size(); i++) {
                const auto &rm = m.rooms[i];
                if (!(rm.flags & 0x20)) continue;
                std::string cls = "rm";
                if (rm.flags & 0x40) cls += " sp";
                bool cur = is_current(rm), quest = rm.group == m.quest_group, sel = (int)i == m.sel;
                if (cur) cls += " cur";
                if (sel) cls += " sel";
                r += rect(cls.c_str(), rm.x * 10 + 4, rm.y * 10 + 4, rm.w * 10 - 8, rm.h * 10 - 8, Rml::CreateString(" data-act=\"room%d\"", (int)i));
                if (rm.flags & 0x40) r += "<span class=\"ic\">&#xe176;</span>"; // UNVERIFIED: meaning of flag 0x40 (canvasDraw draws sprite 0 of set +0x294 there)
                if (quest) r += "<span class=\"qm\">&#xe0d1;</span>";
                if (cur) r += "<span class=\"me\"/>";
                r += "</div>";
            }
        }
        if (r != ss.last) {
            ss.last = r;
            d.rml("map", r);
        }

        // zoom view: the original's zoomed canvas (+0x84), shown over the dimmed map
        put(v.zoomed, m.zoomed, "zoomed");
        if (m.zoomed) {
            const autoform::Frame &fr = recorded();
            int fi = -1;
            for (size_t i = 0; i < fr.forms.size(); i++) if (fr.forms[i].ptr == m.ptr) fi = (int)i;
            uint32_t zc = M32(m.ptr + 0x84);
            for (auto &b : fr.boxes)
                if (b.form == fi && b.kind == 5) {
                    // the zoom canvas is the one inside the +0x84 element's rect
                    bool inner = false;
                    for (auto &e : fr.boxes) if (e.form == fi && e.kind == 6 && e.el == zc && e.x <= b.x + 2 && e.y <= b.y + 2 && e.w >= b.w - 4) inner = true;
                    if (!inner || fr.seq == ss.seq) continue;
                    ss.seq = fr.seq;
                    const autoform::Canvas &cv = fr.canvases[b.canvas];
                    present::live_update("mapzoom", cv.rgba.data(), cv.w, cv.h);
                    d.attr(d.el("zimg"), "src", "live:mapzoom");
                    float k = std::floor(std::max(1.f, std::min(380.f * d.doc->GetContext()->GetDensityIndependentPixelRatio() / cv.h, 3.f * ui::game_rect.w / g_game_w)));
                    d.prop(d.el("zimg"), "width", Rml::CreateString("%.0fpx", cv.w * k));
                    d.prop(d.el("zimg"), "height", Rml::CreateString("%.0fpx", cv.h * k));
                    // the box is sized explicitly (an absolutely placed box would shrink to half the frame)
                    float pad = 12 * d.doc->GetContext()->GetDensityIndependentPixelRatio(); // #zbox padding
                    Rml::Element *zb = d.el("zbox");
                    d.prop(zb, "width", Rml::CreateString("%.0fpx", cv.w * k));
                    d.prop(zb, "height", Rml::CreateString("%.0fpx", cv.h * k));
                    d.prop(zb, "margin-left", Rml::CreateString("%.0fpx", -(cv.w * k / 2 + pad)));
                    d.prop(zb, "margin-top", Rml::CreateString("%.0fpx", -(cv.h * k / 2 + pad)));
                }
        }

        // legend: only what this map shows
        bool any_quest = false, any_shut = false, any_sp = false;
        for (auto &rm : m.rooms) if (rm.flags & 0x20) { any_quest |= rm.group == m.quest_group; any_sp |= (rm.flags & 0x40) != 0; }
        for (auto &a : m.aisles)
            if (a.room >= 0 && a.room < (int)m.rooms.size() && (m.rooms[a.room].flags & 0x20) && !(a.flags & 4)) any_shut = true;
        put(v.any_quest, any_quest, "any_quest");
        put(v.any_shut, any_shut, "any_shut");
        put(v.any_sp, any_sp, "any_sp");
        put(v.title, rml_safe(m.sel_name.empty() ? s.field_name : m.sel_name), "title");
        put(v.kick, std::string(m.zoomed ? "AREA MAP \xc2\xb7 \xed\x99\x95\xeb\x8c\x80 \xeb\xb3\xb4\xea\xb8\xb0" : "AREA MAP"), "kick");
        put(v.hint, "<span>" + glyph("move") + " \xec\x84\xa0\xed\x83\x9d \xec\x9d\xb4\xeb\x8f\x99</span><span>" + glyph("map") +
                        (m.zoomed ? " \xec\xb6\x95\xec\x86\x8c" : " \xed\x99\x95\xeb\x8c\x80") + "</span><span>" + glyph("back") + " \xeb\x8b\xab\xea\xb8\xb0</span>", "hint");
        put(v.zlabel, std::string(m.zoomed ? "\xec\xb6\x95\xec\x86\x8c" : "\xed\x99\x95\xeb\x8c\x80"), "zlabel");
    }

    static void key(int k) { state::post([k] { state::key_tap(k); }); }
    bool action(const std::string &a) override {
        if (a == "close") { back(); return true; }
        if (a == "zoom") { key(0x2b); return true; }
        if (a.rfind("room", 0) == 0) {
            int i = atoi(a.c_str() + 4);
            if (i == m.sel) { key(0x2b); return true; } // second tap: zoom (the original only zooms the current field)
            uint32_t f = m.ptr;
            // select a room like a tap on the canvas does: form+0xA8 = index, FocusTheSelectedMap @00103098
            state::post([f, i] { if (i < M16(f + 0x9c)) { M32(f + 0xa8) = (uint32_t)i; g::call(0xf3098, {f}); } });
            return true;
        }
        return false;
    }
    void back() override { key(0x3a); }
};

static MapScreen g_map;
}
