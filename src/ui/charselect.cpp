// Class selection for a new game (replaces cFormMakeChar, form 0x27; opened by the new-game script).
// The animated class sprites are the original's own draws (recorded in-situ, autoform.cpp), found inside the
// original class buttons. Actions: cFormMakeChar::SelectCreateCharClass @000fcb24 (choose),
// cFormMakeChar::makingClassSelectHandler @000fcd60 with the start / cancel button element (confirm / back).
#include "autoform.h"
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include <algorithm>
#include <map>

namespace screens {

class CharSelectScreen : public Screen {
    // resting pose of each class = its most frequently drawn sprite (idle frames dominate), counted while the
    // card is selected or has not been deselected yet. Unselected cards show it, so the animation frame the
    // original leaves frozen when the selection moves on (e.g. the lycan transform) is not shown.
    struct Seen { int n = 0, w = 0, h = 0; };
    struct Session {
        bool built = false;      // the opening's first update done (texts, sprite reset, focus)
        std::map<std::string, Seen> seen[3];
        bool deselected[3] = {};
        int last_sel = -1;
        Pending<int> cls_req;    // class asked of the original (SelectCreateCharClass), not selected yet
    } ss;
    uint32_t ptr = 0;            // per-frame mirrors of the snapshot
    int sel = -1;
    // data model "charsel" (charsel.rml), set in update() only. The class sprites (#spr0..2: source, size, place) stay
    // in C++ below (Doc): they come from the recorded draws each frame
    struct View {
        std::vector<std::string> names, descs; // the original's text (data-rml: names plain, descriptions markup)
        std::string hint;                      // glyph markup
        int sel = -1;
        bool can_cancel = false;
    } m;

public:
    CharSelectScreen() { z = 25; state::set_ported(0x27); register_screen(this); }
    const char *file() override { return "charsel.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("charsel");
        if (!c) return;
        c.RegisterArray<std::vector<std::string>>();
        m.names.resize(3);
        m.descs.resize(3);
        c.Bind("names", &m.names); c.Bind("descs", &m.descs); c.Bind("hint", &m.hint); c.Bind("sel", &m.sel); c.Bind("can_cancel", &m.can_cancel);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { return s.stripped && s.charsel.ptr && !s.forms.empty() && s.forms.back().id == 0x27; }
    void on_show() override { ss = {}; }
    void on_hide() override { ss.cls_req.clear(); }
    // SelectCreateCharClass(form, cls) @000fcb24 (sets +0x74 = cls, nothing when it already is), once per class asked
    void ask_class(int cls) {
        if (ss.cls_req.asked(cls)) return;
        ss.cls_req.ask(cls, 30);
        uint32_t f = ptr;
        state::post([f, cls] { g::call(0xecb24, {f, (uint32_t)cls}); });
    }

    void update(const state::Snapshot &s) override {
        const state::CharSelect &c = s.charsel;
        ptr = c.ptr;
        sel = c.selected;
        if (!ss.built) {
            // the cards are static (charsel.rml); they used to be made anew on each show, so the sprites start empty again
            std::vector<std::string> nm, ds;
            for (int i = 0; i < 3; i++) {
                nm.push_back(rml_safe(c.name[i]));
                ds.push_back(rml_safe(c.desc[i]));
                if (Rml::Element *img = d.el("spr" + std::to_string(i))) {
                    img->SetAttribute("src", "");
                    for (const char *p : {"width", "height", "left", "top"}) img->RemoveProperty(p);
                }
            }
            put(m.names, std::move(nm), "names");
            put(m.descs, std::move(ds), "descs");
            ss.built = true;
            nav.root = d.doc;
            nav.set(d.el("cc" + std::to_string(sel >= 0 ? sel : 0)));
        }
        // moving the focus picks the class (one selection state, as in current games); mouse hover only highlights
        ss.cls_req.settle(sel);
        if (nav.focus)
            for (int i = 0; i < 3; i++)
                if (nav.focus == d.el("cc" + std::to_string(i)) && i != sel) ask_class(i);
        put(m.sel, sel, "sel");
        put(m.hint, "<span>" + glyph("lr") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("advance") +
                        " \xeb\xaa\xa8\xed\x97\x98 \xec\x8b\x9c\xec\x9e\x91</span>" +
                        (c.can_cancel ? "<span>" + glyph("back") + " \xeb\x8f\x8c\xec\x95\x84\xea\xb0\x80\xea\xb8\xb0</span>" : std::string()), "hint");
        put(m.can_cancel, c.can_cancel, "can_cancel");

        // class sprites: recorded draws of this form whose box centre lies in the class button
        const autoform::Frame &fr = recorded();
        int fidx = -1;
        for (size_t i = 0; i < fr.forms.size(); i++) if (fr.forms[i].ptr == ptr) fidx = (int)i;
        if (ss.last_sel >= 0 && ss.last_sel != sel) ss.deselected[ss.last_sel] = true;
        ss.last_sel = sel;
        for (int i = 0; i < 3; i++) {
            const int *rc = c.rect[i];
            std::string best;
            int bestarea = 0, bw = 0, bh = 0;
            for (auto &cm : fr.cmds) {
                if (cm.form != fidx || cm.key.empty()) continue;
                int ox, oy, sw, sh;
                if (!sprites::info(cm.key, &ox, &oy, &sw, &sh)) continue;
                int cx = cm.x + ox + sw / 2, cy = cm.y + oy + sh / 2;
                if (cx < rc[0] || cy < rc[1] || cx > rc[0] + rc[2] || cy > rc[1] + rc[3]) continue;
                if (sw * sh >= rc[2] * rc[3] * 6 / 10) continue; // the button's own background
                if (sw * sh > bestarea) { bestarea = sw * sh; best = cm.key; bw = sw; bh = sh; }
            }
            Rml::Element *img = d.el("spr" + std::to_string(i));
            if (!best.empty() && (i == sel || !ss.deselected[i])) { Seen &e = ss.seen[i][best]; e.n++; e.w = bw; e.h = bh; }
            if (i != sel && !ss.seen[i].empty()) {
                auto m = std::max_element(ss.seen[i].begin(), ss.seen[i].end(), [](const auto &a, const auto &b) { return a.second.n < b.second.n; });
                best = m->first; bw = m->second.w; bh = m->second.h;
            }
            if (!img || best.empty()) continue;
            float k = std::min(210.f / bh, 280.f / bw); // dp per source pixel, fit into the stage
            k = std::floor(k * 2) / 2;
            d.attr(img, "src", sprite_src(best));
            d.prop(img, "width", Rml::CreateString("%.0fdp", bw * k));
            d.prop(img, "height", Rml::CreateString("%.0fdp", bh * k));
            d.prop(img, "left", Rml::CreateString("%.0fdp", 165 - bw * k / 2));
            d.prop(img, "top", Rml::CreateString("%.0fdp", 232 - bh * k));
        }
    }

    void press(int off) {
        uint32_t f = ptr;
        // the start (+0x68) / cancel (+0x64) container's first child is the button the handler compares with
        state::post([f, off] {
            uint32_t cont = M32(f + off);
            uint32_t kids = cont ? M32(cont + 0x24) : 0;
            if (kids) g::call(0xecd60, {f, 0x30, 0, M32(kids)});
        });
    }
    bool action(const std::string &a) override {
        if (a.rfind("cls", 0) == 0) {
            int cls = a[3] - '0';
            if (cls == sel) { press(0x68); return true; } // second press on the chosen card starts
            nav.set(d.el("cc" + std::to_string(cls)));
            ask_class(cls);
            return true;
        }
        if (a == "cs_start") { if (sel >= 0) press(0x68); return true; }
        if (a == "cs_cancel") { press(0x64); return true; }
        return false;
    }
    void back() override {
        if (state::get().charsel.can_cancel) press(0x64);
    }
};

static CharSelectScreen g_charsel;
}
