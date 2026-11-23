// Item detail (replaces cFormDetail, form 2: cGameUi::ShowDetailPopup @0011f450 -> SetDatas @000e7ea8 / Finalize
// @000e8f24). Mode +0x68, data +0x98, count +0x94: 0xB a cItem* (Finalize_ForItemClass @000e8118), 0xC a
// tagITEM_INITINFO* (store buy lists; the original has no layout for it), 0xD a design record (Finalize_ForMDItemInfo
// @000e8ac8: +0 name offset, +4 description id, u16 +0x16 price / 10, materials s16 +0x18 + 8i type / +0x1A sub /
// +0x1C id / u16 +0x1E count (4), product +0x38 type / +0x3A sub / +0x3C id, u16 +0x3E count).
// Equipment rows as cItemImageList::FillItemDetailInfo @000ea468 builds them: base stats idx = u8 table vaddr 0x166418
// [type * 2 + i], value u16 item +0x10 + 2 idx, label = stat_name_imgs (vaddr 0x166B7C) words of sprite set 11; gems
// u8 +0x2A / infos +0x2C (bonus +0x14 stat / +0x16 value, +0x1A / +0x1C); refine u8 +0x24 / +0x25; durability s16 +0x20
// / +0x22; requirement u16 +0x1E (2 bits stat 1..3, 10 bits value, stat_name_imgs[8 + k], player +0x380 + 4k);
// class word item_clz_imgs (vaddr 0x166B74) [info +8]; grade cItem::GetGrade @0010e51c + gradeNames (vaddr 0x199130);
// sell price u16 info +0xA (hidden for types 0xC / 0xE and flag 0x8000). Close: SetDead (OnDead @000e9060 runs the
// callback).
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include "wordimg.h"
#include <mutex>

namespace state {
struct DetRow { std::string k, v; bool bad = false; };
struct Detail { uint32_t ptr = 0; int mode = 0; std::string name, name_rml, icon, type, cls, grade, desc; bool cls_bad = false; std::vector<DetRow> rows; std::vector<std::pair<std::string, std::string>> gems, mats; };
static Detail g_det_pub;
static std::mutex g_det_mx;

static std::string word16(uint32_t table_va, int i) { const char *w = wordimg::word(11, M16(IMG + table_va + 2 * i)); return w ? w : ""; }
static std::string pool(uint32_t rm, uint32_t off) { return g::plain_text(g::cstr(M32(rm + 0x34) + off)); }
static std::string lang(int id) { return g::plain_text(g::cstr(g::call(0x120aac, {(uint32_t)id}))); }

static void item_detail(Detail &d, uint32_t it, uint32_t rm, int count) {
    uint32_t info = M32(it + 0x38);
    uint16_t fl = M16(it + 4);
    int type = M16(it + 6);
    bool unscanned = type < 6 && (fl & 0x100);
    d.name = unscanned ? "??????" : g::plain_text(g::cstr(g::call(0xfed68, {it})));
    if (!unscanned) d.name_rml = g::item_name_rml(it);
    d.type = g::plain_text(g::cstr(g::call(0xfed38, {it})));
    if (!info) return;
    d.icon = sprites::image(0x6b, M8(info + 0x13), unscanned ? 0 : M8(info + 0x12));
    uint32_t ds = type == 0xe ? g::call(0xcd258, {rm, M32(info + 4)}) : g::call(0xcd0d8, {rm, 0, M32(info + 4)});
    d.desc = ds ? g::rml_text(g::cstr(ds)) : "";
    if (d.desc == "0") d.desc.clear();
    if (type < 6 && !unscanned) {
        int g = (int32_t)g::call(0xfe51c, {it}); // cItem::GetGrade
        uint32_t gn = g >= 0 && g < 8 ? M32(IMG + 0x199130 + 4 * g) : 0;
        // cFormDetail::Finalize_ForItemClass @000e8118 writes the grade line in gradeColor[grade] (vaddr 0x19902c),
        // the colour of the item name, then resets it (0x0B)
        uint32_t gc = g >= 0 && g < 5 ? M32(IMG + 0x19902c + 4 * g) : 0;
        if (gn && g::ok(gn)) d.grade = g::rml_text((std::string(gc && g::ok(gc) ? (const char *)GP(gc) : "") + (const char *)GP(gn) + "\x0b").c_str(), false);
        int cl = M8(info + 8);
        d.cls = cl < 4 ? word16(0x166b74, cl) : "";
        uint32_t pl = g::player();
        d.cls_bad = pl && cl < 3 && cl != M16(pl + 6);
        for (int i = 0; i < 2 && type < 6; i++) {
            int idx = M8(IMG + 0x166418 + type * 2 + i);
            d.rows.push_back({word16(0x166b7c, idx), std::to_string(M16(it + 0x10 + 2 * idx))});
        }
        int n = M8(it + 0x2a);
        for (int k = 0; k < n && k < 4; k++) {
            uint32_t gi = M32(it + 0x2c + 4 * k);
            if (!gi || !g::ok(gi)) continue;
            std::string b;
            for (int j = 0; j < 2; j++) {
                int st = (int16_t)M16(gi + 0x14 + 6 * j), v = (int16_t)M16(gi + 0x16 + 6 * j);
                if (st <= 0 || v == 0) continue;
                std::string w = st >= 0x1a && st <= 0x20 ? word16(0x166b7c, st - 0x1a) : "";
                b += (b.empty() ? "" : " \xc2\xb7 ") + (w.empty() ? std::to_string(st) : w) + " +" + std::to_string(v);
            }
            d.gems.push_back({sprites::image(0x6b, M8(gi + 0x13), M8(gi + 0x12)), pool(rm, M32(gi)) + (b.empty() ? "" : "  " + b)});
        }
        if (M8(it + 0x25) > 0) d.rows.push_back({"\xec\xa0\x9c\xeb\xa0\xa8", Rml::CreateString("+%d / +%d", M8(it + 0x24), M8(it + 0x25))}); // 제련
        d.rows.push_back({"\xeb\x82\xb4\xea\xb5\xac\xeb\x8f\x84", Rml::CreateString("%d / %d", (int16_t)M16(it + 0x20), (int16_t)M16(it + 0x22)), (fl & 0x80) != 0}); // 내구도
        uint16_t rq = M16(it + 0x1e);
        int k = rq >> 14;
        if (k >= 1 && k <= 3) {
            int need = rq & 0x3ff, have = pl ? (int32_t)M32(pl + 0x380 + 4 * (k - 1)) : need;
            d.rows.push_back({"\xed\x95\x84\xec\x9a\x94 " + word16(0x166b7c, 8 + k), std::to_string(need), need > have}); // 필요
        }
    } else if (count > 1 || M32(it + 0x3c) > 1) {
        d.rows.push_back({lang(0x1cd), std::to_string((int32_t)M32(it + 0x3c))});
    }
    if (type == 0xb) d.rows.push_back({lang(0x1d7), std::to_string(M8(info + 9))});
    if (type != 0xc && type != 0xe && !(fl & 0x8000) && M16(info + 0xa) > 0) d.rows.push_back({"\xed\x8c\x90\xeb\xa7\xa4\xea\xb0\x80", screens::num(M16(info + 0xa)) + " G"}); // 판매가
}

void capture_detail(const Snapshot &s) {
    Detail d;
    uint32_t rm = g::resmgr();
    if (!s.forms.empty() && s.forms.back().id == 2 && rm) {
        uint32_t f = s.forms.back().ptr;
        d.ptr = f;
        d.mode = (int32_t)M32(f + 0x68);
        uint32_t data = M32(f + 0x98);
        int count = (int32_t)M32(f + 0x94);
        if (d.mode == 0xb && data) item_detail(d, data, rm, count);
        else if (d.mode == 0xc && data) {
            d.name = pool(rm, M32(data));
            d.icon = sprites::image(0x6b, M8(data + 0x13), M8(data + 0x12));
            uint32_t ds = g::call(0xcd0d8, {rm, 0, M32(data + 4)});
            d.desc = ds ? g::rml_text(g::cstr(ds)) : "";
            if (d.desc == "0") d.desc.clear();
            if (M8(data + 9) > 1) d.rows.push_back({"\xed\x95\x84\xec\x9a\x94 \xeb\xa0\x88\xeb\xb2\xa8", "Lv " + std::to_string(M8(data + 9))}); // 필요 레벨
            if ((int32_t)M32(data + 0xc) > 0) d.rows.push_back({"\xea\xb0\x80\xea\xb2\xa9", screens::num((int32_t)M32(data + 0xc)) + " G"}); // 가격
        } else if (d.mode == 0xd && data) {
            d.name = pool(rm, M32(data));
            d.type = lang(0x1c7);
            uint32_t ds = g::call(0xcd0d8, {rm, 0, M32(data + 4)});
            d.desc = ds ? g::rml_text(g::cstr(ds)) : "";
            d.rows.push_back({lang(0x1cd), std::to_string(count)});
            for (int i = 0; i < 4; i++) {
                uint32_t m = data + 0x18 + 8 * i;
                int ty = (int16_t)M16(m), n = M16(m + 6);
                if (ty < 0 || n == 0) continue;
                uint32_t ii = g::call(0xcc3b4, {rm, (uint32_t)ty, (uint32_t)(int16_t)M16(m + 4), (uint32_t)(int16_t)M16(m + 2)});
                if (ii) d.mats.push_back({sprites::image(0x6b, M8(ii + 0x13), M8(ii + 0x12)), pool(rm, M32(ii)) + " x" + std::to_string(n)});
            }
            uint32_t pi = g::call(0xcc3b4, {rm, (uint32_t)(int16_t)M16(data + 0x38), (uint32_t)(int16_t)M16(data + 0x3c), (uint32_t)(int16_t)M16(data + 0x3a)});
            if (pi) { d.icon = sprites::image(0x6b, M8(pi + 0x13), M8(pi + 0x12)); d.rows.push_back({lang(0x1cf), pool(rm, M32(pi)) + " x" + std::to_string(M16(data + 0x3e))}); }
            d.rows.push_back({"\xea\xb0\x80\xea\xb2\xa9", screens::num(M16(data + 0x16) * 10) + " G"});
        }
    }
    std::lock_guard<std::mutex> l(g_det_mx);
    g_det_pub = std::move(d);
}
static Detail pull_detail() { std::lock_guard<std::mutex> l(g_det_mx); return g_det_pub; }
}

namespace screens {

class DetailScreen : public Screen {
    struct Session {
        uint32_t drawn = 0;      // the form drawn
    } ss;
    state::Detail dt;            // per-frame copy of the captured form

    // data model "detail" (detail.rml), set in update(). Every row is one piece of markup (the texts are the
    // original's; a gem row is an image followed by text)
    struct Line {
        std::string rml;
        bool operator==(const Line &o) const { return rml == o.rml; }
    };
    struct View {
        std::string icon, name, sub, desc; // name, sub, desc: markup
        std::vector<Line> rows, gems, mats;
    } m;

public:
    DetailScreen() { z = 48; state::set_ported(2); register_screen(this); }
    const char *file() override { return "detail.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("detail");
        if (!c) return;
        if (auto h = c.RegisterStruct<Line>()) h.RegisterMember("rml", &Line::rml);
        c.RegisterArray<std::vector<Line>>();
        c.Bind("icon", &m.icon); c.Bind("name", &m.name); c.Bind("sub", &m.sub); c.Bind("desc", &m.desc);
        c.Bind("rows", &m.rows); c.Bind("gems", &m.gems); c.Bind("mats", &m.mats);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { dt = state::pull_detail(); return s.stripped && dt.ptr; }
    void on_show() override { ss = {}; }
    void update(const state::Snapshot &s) override {
        (void)s;
        if (ss.drawn == dt.ptr) return;
        ss.drawn = dt.ptr;
        put(m.icon, sprite_src(dt.icon), "icon");
        put(m.name, dt.name_rml.empty() ? rml_safe(dt.name) : dt.name_rml, "name");
        std::string sub = rml_safe(dt.type);
        if (!dt.grade.empty()) sub += " \xc2\xb7 " + dt.grade; // markup in the grade colour
        if (!dt.cls.empty()) sub += std::string(" \xc2\xb7 <span class=\"") + (dt.cls_bad ? "bad" : "ok") + "\">" + rml_safe(dt.cls) + "</span>";
        put(m.sub, sub, "sub");
        std::vector<Line> rows, gems, mats; // in this order, then the materials heading (재료) before mats
        for (auto &row : dt.rows) rows.push_back({std::string("<span class=\"k\">") + rml_safe(row.k) + "</span><span class=\"" + (row.bad ? "bad" : "v") + "\">" + rml_safe(row.v) + "</span>"});
        for (auto &g : dt.gems) gems.push_back({"<img src=\"" + sprite_src(g.first) + "\"/>" + rml_safe(g.second)});
        for (auto &t : dt.mats) mats.push_back({"<img src=\"" + sprite_src(t.first) + "\"/>" + rml_safe(t.second)});
        put(m.rows, std::move(rows), "rows");
        put(m.gems, std::move(gems), "gems");
        put(m.mats, std::move(mats), "mats");
        put(m.desc, rml_safe(dt.desc), "desc"); // hidden when empty
        nav.root = d.el("box");
        nav.set(d.el("ok"));
    }
    bool action(const std::string &a) override { if (a == "ok") { back(); return true; } return false; }
    void back() override { uint32_t f = dt.ptr; state::post([f] { g::call(0x147760, {f, 1}); }); } // cflUiForm::SetDead
    bool on_dir(int dir) override {
        if (dir < 2) { Rml::Element *e = d.el("desc"); if (e) e->SetScrollTop(e->GetScrollTop() + (dir ? 60.f : -60.f)); return true; }
        return false;
    }
};

static DetailScreen g_detail;
}
