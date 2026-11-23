// Quickslot registration popup (replaces the look of cFormPopup kinds 1 item / 2 skill, Finalize_HotSlot @000f1298):
// opened by the bag (cFormInvenItem::DoItemAction @0010d344, actions 0x42 register / 0x43 edit) and the skill page
// (cFormInvenSkill::RegSlot @001125ac / EditSlot @0011254c). +0x2F8 the object, +0x1B4 0 register / 1 edit,
// slot list +0x70 (image list, selection s16 +0x32, slot s = page * 5 + column), slots = cCreature::GetSlotInfo records
// player +0xB8 + 8 s (read by menu_pages.cpp read_hot). Choosing = selection + cFormPopup::somethingKeypress(f, 0x30, 0,
// list) @000f2ed8: register -> cCreature::SetSlotInfo + close; edit -> the move / copy / clear menu (a cFormChoice, drawn
// by dialogs.cpp), then the target slot. Close = its Close (+0x114, register) or OK (+0x10C, edit) button.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"

namespace screens {

class HotSlotScreen : public Screen {
    struct Session {
        std::string key;         // slots / selection the list and focus were built for
    } ss;
    uint32_t f = 0;              // per-frame mirrors of the popup form
    int kind = 1, mode = 0, sel = 0;

    // data model "hotslot" (hotslot.rml), set in update(): two sets (A / B) of five slots
    struct Slot {
        std::string act, src, k; // s<i>, the icon (none: empty slot), the key glyph (markup)
        bool sel = false, has = false;
        bool operator==(const Slot &o) const { return act == o.act && src == o.src && k == o.k && sel == o.sel && has == o.has; }
    };
    struct Set {
        std::string name;        // A / B
        std::vector<Slot> slots;
        bool operator==(const Set &o) const { return name == o.name && slots == o.slots; }
    };
    struct View {
        std::vector<Set> sets;
        std::string kicker, title, body, cur, btn; // cur: markup
    } m;

public:
    HotSlotScreen() { z = 49; register_screen(this); }
    const char *file() override { return "hotslot.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("hotslot");
        if (!c) return;
        if (auto h = c.RegisterStruct<Slot>()) {
            h.RegisterMember("act", &Slot::act); h.RegisterMember("src", &Slot::src); h.RegisterMember("k", &Slot::k);
            h.RegisterMember("sel", &Slot::sel); h.RegisterMember("has", &Slot::has);
        }
        c.RegisterArray<std::vector<Slot>>();
        if (auto h = c.RegisterStruct<Set>()) { h.RegisterMember("name", &Set::name); h.RegisterMember("slots", &Set::slots); }
        c.RegisterArray<std::vector<Set>>();
        c.Bind("sets", &m.sets); c.Bind("kicker", &m.kicker); c.Bind("title", &m.title); c.Bind("body", &m.body); c.Bind("cur", &m.cur); c.Bind("btn", &m.btn);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        f = 0;
        for (auto &fm : s.forms)
            if (fm.id == 0xf) { int k = (int32_t)M32(fm.ptr + 0x168); if (k == 1 || k == 2) f = fm.ptr; }
        if (!f || !s.stripped) return false;
        return s.forms.back().ptr == f || dialog_over(s);
    }
    void on_show() override { ss = {}; }
    void update(const state::Snapshot &s) override {
        // game-thread fields read here are plain memory reads of a live form (no calls)
        kind = (int32_t)M32(f + 0x168);
        mode = (int32_t)M32(f + 0x1b4);
        uint32_t il = M32(f + 0x70);
        sel = il ? (int16_t)M16(il + 0x32) : 0;
        uint32_t obj = M32(f + 0x2f8);
        put(m.kicker, std::string(kind == 2 ? "SKILL" : "ITEM"), "kicker");
        put(m.title, std::string(mode ? "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xed\x8e\xb8\xec\xa7\x91" : "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xeb\x93\xb1\xeb\xa1\x9d"), "title"); // 퀵슬롯 편집 / 등록
        put(m.body, std::string(mode ? "\xec\x8a\xac\xeb\xa1\xaf\xec\x9d\x84 \xea\xb3\xa8\xeb\x9d\xbc \xec\x9d\xb4\xeb\x8f\x99 \xc2\xb7 \xeb\xb3\xb5\xec\xa0\x9c \xc2\xb7 \xec\xa0\x9c\xea\xb1\xb0\xed\x95\x98\xec\x84\xb8\xec\x9a\x94" // 슬롯을 골라 이동 · 복제 · 제거하세요
                                     : "\xeb\x93\xb1\xeb\xa1\x9d\xed\x95\xa0 \xec\x8a\xac\xeb\xa1\xaf\xec\x9d\x84 \xea\xb3\xa0\xeb\xa5\xb4\xec\x84\xb8\xec\x9a\x94"), "body"); // 등록할 슬롯을 고르세요
        (void)obj;
        std::vector<Set> sets;
        std::string k;           // what the slots show, as the old markup key: a change (the key glyphs too) puts the focus back on s<sel>
        for (int p = 0; p < 2; p++) {
            Set st{p ? "B" : "A", {}};
            for (int c = 0; c < 5; c++) {
                int i = p * 5 + c;
                const state::HotRef &h = s.menu.hot[i];
                Slot sl{Rml::CreateString("s%d", i), h.kind >= 0 ? sprite_src(h.icon) : std::string(), glyph(Rml::CreateString("slot%d", i).c_str()), i == sel, h.kind >= 0};
                k += sl.act + (sl.sel ? "*" : "") + (sl.has ? sl.src : "-") + sl.k + "|";
                st.slots.push_back(std::move(sl));
            }
            sets.push_back(std::move(st));
        }
        const state::HotRef &h = s.menu.hot[sel >= 0 && sel < 10 ? sel : 0];
        k += std::to_string(sel);
        if (k != ss.key) {
            ss.key = k;
            put(m.sets, std::move(sets), "sets");
            put(m.cur, h.kind >= 0 ? h.name_rml : std::string("\xeb\xb9\x84\xec\x96\xb4 \xec\x9e\x88\xec\x9d\x8c"), "cur"); // 비어 있음
            nav.root = d.el("box");
            nav.set(nullptr);
            // the slots are made in this frame's Context::Update on the first opening
            std::string want = "s" + std::to_string(sel >= 0 && sel < 10 ? sel : 0); // no selection: the first slot (ensure)
            if (Rml::Element *e = nav.find_act(want)) nav.set(e); else nav.want_act(want);
            if (nav.want.empty()) nav.ensure();
        }
        put(m.btn, std::string(mode ? "\xec\x99\x84\xeb\xa3\x8c" : "\xeb\x8b\xab\xea\xb8\xb0"), "btn"); // 완료 / 닫기
    }
    bool action(const std::string &a) override {
        uint32_t p = f;
        if (a.size() > 1 && a[0] == 's' && isdigit((unsigned char)a[1])) {
            int i = atoi(a.c_str() + 1);
            state::post([p, i] {
                uint32_t il = M32(p + 0x70);
                if (!il) return;
                g::call(0x1457a0, {il, (uint32_t)i});              // cflUiImageList::Select
                g::call(0xe2ed8, {p, 0x30, 0, il});                // cFormPopup::somethingKeypress
            });
            return true;
        }
        if (a == "close") { back(); return true; }
        return false;
    }
    void back() override {
        uint32_t p = f;
        int m = mode;
        state::post([p, m] { g::call(0xe2ed8, {p, 0x30, 0, M32(p + (m ? 0x10c : 0x114))}); });
    }
};

static HotSlotScreen g_hotslot;
}
