// Title screen and save slots (replaces cFormMainMenu, form 0x32; work/title_spec.md).
// Actions call the original handlers: cFormMainMenu::ProcSelectSlot @00148310 (load or new game, the same path
// charSelectHandler @001483cc takes), cNetAuthorize::onSaveFileDeleted @0014d45c (delete), the original options
// gear (element +0x9c) for settings. Network / premium / community entries of the original title are not offered.
// The original's server save keeper / sync buttons (+0xcc, form 0x3a; work/netpay_spec.md) come back as local
// export / import of one slot to a file (saveio.cpp).
#include "game.h"
#include "screen_util.h"
#include "sprites.h"
#include "screens.h"
#include "saveio.h"
#include <SDL3/SDL.h>
#include <memory>

extern "C" void java_request_quit(void);

namespace screens {

// game thread: the title (cMenuMgr state 9 with cFormMainMenu at +0x20, read_title in state.cpp) is still up
static bool title_up() {
    uint32_t mm = M32(IMG + 0x19b244);
    return mm && M32(mm + 8) == 9 && M32(mm + 0x20) && M32(g::gm() + 0xc) == 1;
}

class TitleScreen : public Screen {
    struct Session {
        bool slots_open = false;
        int confirm_slot = -1;
        std::string cards_key;
        // export / import (saveio.h)
        int pick = 0;            // 1: choose the slot to export, 2: choose the slot to import into
        int import_slot = -1;    // confirm: overwrite this slot with import_data
        saveio::Slot import_data;
        std::string note;
        bool note_err = false;
        uint64_t note_until = 0;
    } ss;
    bool busy = false;           // a file dialog / game-thread job is working: the job clears it when it answers
    // data model "title" (title.rml), set in update() only (the saveio callbacks change ss / busy, update() shows them)
    struct Card {
        std::string act, face, plus, no, nm, meta, del_act, go_icon; // plus / nm / meta / go_icon: markup (data-rml)
        bool exists = false, off = false, del = false, go = false;
        bool operator==(const Card &o) const {
            return act == o.act && face == o.face && plus == o.plus && no == o.no && nm == o.nm && meta == o.meta && del_act == o.del_act &&
                   go_icon == o.go_icon && exists == o.exists && off == o.off && del == o.del && go == o.go;
        }
    };
    struct View {
        std::vector<Card> cards;
        std::string start_d, s_hint, c_t, c_d, c_yes, n_icon, n_text, hints; // s_hint / c_d / n_icon / n_text / hints: markup
        int pick = 0;
        bool slots_open = false, busy = false, modal = false, yes_danger = false, yes_primary = false, note_on = false, note_err = false;
    } m;

public:
    TitleScreen() {
        z = 5;
        state::set_ported(0x32);
        // three cards from the start: their elements exist after the document's first Context::Update
        m.cards.resize(3);
        for (int i = 0; i < 3; i++) m.cards[i].act = Rml::CreateString("save%d", i);
        register_screen(this);
    }
    const char *file() override { return "title.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("title");
        if (!c) return;
        if (auto h = c.RegisterStruct<Card>()) {
            h.RegisterMember("act", &Card::act); h.RegisterMember("face", &Card::face); h.RegisterMember("plus", &Card::plus);
            h.RegisterMember("no", &Card::no); h.RegisterMember("nm", &Card::nm); h.RegisterMember("meta", &Card::meta);
            h.RegisterMember("del_act", &Card::del_act); h.RegisterMember("go_icon", &Card::go_icon); h.RegisterMember("exists", &Card::exists);
            h.RegisterMember("off", &Card::off); h.RegisterMember("del", &Card::del); h.RegisterMember("go", &Card::go);
        }
        c.RegisterArray<std::vector<Card>>();
        c.Bind("cards", &m.cards); c.Bind("start_d", &m.start_d); c.Bind("s_hint", &m.s_hint); c.Bind("c_t", &m.c_t); c.Bind("c_d", &m.c_d);
        c.Bind("c_yes", &m.c_yes); c.Bind("n_icon", &m.n_icon); c.Bind("n_text", &m.n_text); c.Bind("hints", &m.hints);
        c.Bind("pick", &m.pick); c.Bind("slots_open", &m.slots_open); c.Bind("busy", &m.busy); c.Bind("modal", &m.modal);
        c.Bind("yes_danger", &m.yes_danger); c.Bind("yes_primary", &m.yes_primary); c.Bind("note_on", &m.note_on); c.Bind("note_err", &m.note_err);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        return s.gm_state == 1 && s.menu_state == 9 && s.top_form == 0x32 && s.stripped;
    }
    void on_show() override { ss = {}; }

    void layout_art(const state::Snapshot &s) {
        int w, h;
        if (!sprite_size(s.keyart, &w, &h)) return;
        d.attr("art", "src", sprite_src(s.keyart));
        // cover the window, biased to the right so the menu column sits on the darker side
        float sc = std::max((float)ui::win_w / w, (float)ui::win_h / h) * 1.04f;
        float dw = w * sc, dh = h * sc;
        float x = (ui::win_w - dw) * 0.75f, y = (ui::win_h - dh) * 0.5f;
        d.prop("art", "width", Rml::CreateString("%.0fpx", dw));
        d.prop("art", "height", Rml::CreateString("%.0fpx", dh));
        d.prop("art", "left", Rml::CreateString("%.0fpx", x));
        d.prop("art", "top", Rml::CreateString("%.0fpx", y));
    }

    static std::string playtime(int secs) {
        int h = secs / 3600, m = secs % 3600 / 60;
        return h > 0 ? Rml::CreateString("%d\xec\x8b\x9c\xea\xb0\x84 %d\xeb\xb6\x84", h, m) : Rml::CreateString("%d\xeb\xb6\x84", m);
    }

    void notify(const std::string &text, bool err) {
        ss.note = text;
        ss.note_err = err;
        ss.note_until = SDL_GetTicks() + (err ? 4500 : 3200);
        ui_log("[title] notice: %s", text.c_str());
    }

    bool modal_up() const { return ss.confirm_slot >= 0 || ss.import_slot >= 0; }

    // keyboard / gamepad focus: the first selectable card (the export / import buttons come first in the panel).
    // It reads the model, not the cards' classes: data-class applies in the Context::Update after update()
    void focus_cards() {
        Rml::Element *c = d.el("cards");
        for (auto &k : m.cards)
            if (!k.off) {
                if (Rml::Element *e = c ? nav.find_act(k.act, c) : nullptr) { nav.set(e); return; }
                break;
            }
        nav.set(nullptr);
        nav.ensure();
    }

    void update(const state::Snapshot &s) override {
        saveio::poll();
        layout_art(s);
        int used = 0, best = -1;
        for (int i = 0; i < 3; i++)
            if (s.saves[i].exists) { used++; if (best < 0 || s.saves[i].seconds > s.saves[best].seconds) best = i; }
        put(m.start_d, used ? Rml::CreateString("\xea\xb8\xb0\xeb\xa1\x9d %d\xea\xb0\x9c \xc2\xb7 \xec\xb5\x9c\xea\xb3\xa0 Lv %d", used, best >= 0 ? s.saves[best].level : 0)
                            : std::string("\xec\x83\x88\xeb\xa1\x9c\xec\x9a\xb4 \xeb\xaa\xa8\xed\x97\x98\xec\x9d\x84 \xec\x8b\x9c\xec\x9e\x91\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4"), "start_d");
        put(m.slots_open, ss.slots_open, "slots_open");
        put(m.pick, ss.pick, "pick");
        put(m.busy, busy, "busy");
        put(m.s_hint, std::string(ss.pick == 1 ? "<span class=\"gold\">내보낼 슬롯을 선택하세요.</span> 기록이 파일 하나로 저장됩니다."
                                : ss.pick == 2 ? "<span class=\"cyan\">가져올 위치를 선택하세요.</span> 그다음 .ehsave 파일을 고릅니다."
                                : busy ? "파일 창에서 선택을 기다리는 중…"
                                       : "\xec\x9d\xb4\xec\x96\xb4\xec\x84\x9c \xed\x95\xa0 \xea\xb8\xb0\xeb\xa1\x9d\xec\x9d\x84 \xea\xb3\xa0\xeb\xa5\xb4\xea\xb1\xb0\xeb\x82\x98 \xeb\xb9\x88 \xec\x8a\xac\xeb\xa1\xaf\xec\x97\x90\xec\x84\x9c \xec\x83\x88 \xeb\xaa\xa8\xed\x97\x98\xec\x9d\x84 \xec\x8b\x9c\xec\x9e\x91\xed\x95\x98\xec\x84\xb8\xec\x9a\x94."), "s_hint");
        // slot cards
        std::string key = Rml::CreateString("%d;", ss.pick);
        for (int i = 0; i < 3; i++) {
            const state::SaveSlot &v = s.saves[i];
            key += Rml::CreateString("%d,%d,%d,%d,%s;", v.exists, v.level, v.seconds, v.job, v.face.c_str());
        }
        if (key != ss.cards_key) {
            ss.cards_key = key;
            std::vector<Card> cards(3);
            std::vector<std::string> acts;
            for (int i = 0; i < 3; i++) {
                const state::SaveSlot &v = s.saves[i];
                Card &c = cards[i];
                c.act = Rml::CreateString("save%d", i);
                c.exists = v.exists;
                c.no = Rml::CreateString("SLOT %d", i + 1);
                if (v.exists) {
                    c.face = sprite_src(v.face);
                    c.nm = rml_safe(v.job_name);
                    c.meta = Rml::CreateString("<span class=\"icon\">&#xe176;</span>Lv %d&nbsp;&nbsp;&nbsp;<span class=\"icon\">&#xe087;</span>%s", v.level, playtime(v.seconds).c_str());
                    c.del = ss.pick == 0;
                    c.del_act = Rml::CreateString("del%d", i);
                    c.go = ss.pick != 0;
                    c.go_icon = ss.pick == 1 ? "&#xe19e;" : "&#xe0b2;";
                } else {
                    c.off = ss.pick == 1; // nothing to export from an empty slot
                    c.plus = ss.pick == 2 ? "&#xe0b2;" : "&#xe13d;";
                    c.nm = "\xec\x83\x88 \xeb\xaa\xa8\xed\x97\x98";
                    c.meta = "\xeb\xb9\x88 \xec\x8a\xac\xeb\xa1\xaf";
                }
                acts.push_back(c.act);
                if (c.del) acts.push_back(c.del_act);
            }
            put(m.cards, std::move(cards), "cards");
            // the cards used to be rebuilt here, which dropped a focus inside them and refocused the first card
            nav.drop_if_gone(d.el("cards"), acts);
            if (ss.slots_open && !modal_up()) focus_cards();
        }
        put(m.modal, modal_up(), "modal");
        if (ss.confirm_slot >= 0) {
            put(m.c_t, Rml::CreateString("SLOT %d \xec\x82\xad\xec\xa0\x9c", ss.confirm_slot + 1), "c_t");
            put(m.c_d, std::string("\xec\x9d\xb4 \xea\xb8\xb0\xeb\xa1\x9d\xec\x9d\x80 \xeb\xb3\xb5\xea\xb5\xac\xed\x95\xa0 \xec\x88\x98 \xec\x97\x86\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4."), "c_d");
            put(m.c_yes, std::string("\xec\x82\xad\xec\xa0\x9c"), "c_yes");
        } else if (ss.import_slot >= 0) {
            const saveio::Slot &im = ss.import_data;
            const state::SaveSlot &t = s.saves[ss.import_slot];
            put(m.c_t, Rml::CreateString("SLOT %d\xec\x97\x90 \xea\xb0\x80\xec\xa0\xb8\xec\x98\xa4\xea\xb8\xb0", ss.import_slot + 1), "c_t");
            std::string body = Rml::CreateString("<div class=\"src\"><span class=\"icon\">&#xe176;</span>Lv %d&nbsp;&nbsp;&nbsp;<span class=\"icon\">&#xe087;</span>%s</div>",
                                                 im.level(), playtime(im.seconds()).c_str());
            body += t.exists ? Rml::CreateString("SLOT %d의 기록(Lv %d · %s)을 덮어씁니다.<br/>기존 기록은 복구할 수 없습니다.", ss.import_slot + 1, t.level, playtime(t.seconds).c_str())
                             : std::string("빈 슬롯에 이 기록을 넣습니다.");
            put(m.c_d, body, "c_d");
            put(m.c_yes, std::string("가져오기"), "c_yes");
        }
        put(m.yes_danger, ss.confirm_slot >= 0 || (ss.import_slot >= 0 && s.saves[ss.import_slot].exists), "yes_danger");
        put(m.yes_primary, ss.import_slot >= 0 && !s.saves[ss.import_slot].exists, "yes_primary");
        // notice toast
        bool show_note = !ss.note.empty() && SDL_GetTicks() < ss.note_until;
        if (show_note) put(m.n_text, rml_safe(ss.note), "n_text");
        put(m.n_icon, std::string(ss.note_err ? "&#xe077;" : "&#xe226;"), "n_icon");
        put(m.note_on, show_note, "note_on");
        put(m.note_err, ss.note_err, "note_err");
        nav.root = modal_up() ? d.el("confirm") : ss.slots_open ? d.el("slots") : d.el("menu");
        std::string hints = "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span>";
        if (ss.slots_open || modal_up()) hints += "<span>" + glyph("back") + " \xeb\x92\xa4\xeb\xa1\x9c</span>";
        put(m.hints, hints, "hints");
    }

    // ---- export: slot -> file (game thread reads the slot, then the save dialog, then the file is written here)
    void begin_export(int i) {
        busy = true;
        ss.pick = 0;
        auto out = std::make_shared<saveio::Slot>();
        state::post([this, i, out] {
            bool ok = title_up() && saveio::read_slot(i, *out);
            saveio::to_main([this, i, out, ok] {
                if (!ok) { busy = false; notify(Rml::CreateString("SLOT %d 기록을 읽지 못했습니다.", i + 1), true); return; }
                std::string name = Rml::CreateString("EpicHearts_SLOT%d_Lv%d.ehsave", i + 1, out->level());
                saveio::pick_save(name, [this, i, out](std::string path) {
                    busy = false;
                    if (path.empty()) { notify("내보내기를 취소했습니다.", false); return; }
                    if (!saveio::write_file(path, saveio::encode(*out))) { notify("파일을 저장하지 못했습니다.", true); return; }
                    size_t cut = path.find_last_of("/\\");
                    std::string base = path.compare(0, 10, "content://") == 0 || cut == std::string::npos ? "" : path.substr(cut + 1);
                    notify(Rml::CreateString("SLOT %d 기록을 내보냈습니다", i + 1) + (base.empty() ? "." : " · " + base), false);
                });
            });
        });
    }

    // ---- import: open dialog -> validate -> confirm (c_yes) -> game thread writes the slot
    void begin_import(int i) {
        busy = true;
        ss.pick = 0;
        saveio::pick_open([this, i](std::string path) {
            busy = false;
            if (path.empty()) { notify("가져오기를 취소했습니다.", false); return; }
            std::vector<uint8_t> f;
            std::string err;
            if (!saveio::read_file(path, f)) { notify("파일을 열 수 없습니다.", true); return; }
            if (!saveio::import_any(f, i, ss.import_data, err)) { notify(err, true); return; }
            ss.import_slot = i;
            nav.root = d.el("confirm");
            nav.set(d.el("c_no"));
        });
    }

    void apply_import() {
        int i = ss.import_slot;
        auto data = std::make_shared<saveio::Slot>(std::move(ss.import_data));
        ss.import_slot = -1;
        busy = true;
        state::post([this, i, data] {
            bool ok = title_up() && saveio::write_slot(i, *data);
            saveio::to_main([this, i, data, ok] {
                busy = false;
                if (ok) notify(Rml::CreateString("SLOT %d에 기록을 가져왔습니다 · Lv %d", i + 1, data->level()), false);
                else notify("기록을 가져오지 못했습니다.", true);
            });
        });
        nav.root = d.el("slots");
        nav.set(nullptr);
    }

    bool action(const std::string &a) override {
        const state::Snapshot &s = state::get();
        if (busy) return true;   // a file dialog / the game thread owns the slots until it answers
        if (a == "t_start") { ss.slots_open = true; ss.pick = 0; nav.root = d.el("slots"); focus_cards(); return true; }
        if (a == "t_options") {
            uint32_t f = s.title_form;
            state::post([f] {
                // the gear button: cFormMainMenu+0x9c child[0] -> page 3, CreateForm(0x20), cFormOption::SetView(1)
                uint32_t gear = f ? M32(f + 0x9c) : 0;
                uint32_t kids = gear ? M32(gear + 0x24) : 0;
                if (kids) state::tap_element(M32(kids));
            });
            return true;
        }
        if (a == "t_quit") { java_request_quit(); return true; }
        if (a == "x_export" || a == "x_import") {
            int m = a == "x_export" ? 1 : 2;
            ss.pick = ss.pick == m ? 0 : m;
            if (ss.pick == 1 && !s.saves[0].exists && !s.saves[1].exists && !s.saves[2].exists) {
                ss.pick = 0;
                notify("내보낼 기록이 없습니다.", true);
            }
            return true;
        }
        if (a.rfind("save", 0) == 0) {
            int i = a[4] - '0';
            if (ss.pick == 1) { if (s.saves[i].exists) begin_export(i); return true; }
            if (ss.pick == 2) { begin_import(i); return true; }
            uint32_t f = s.title_form;
            bool exists = s.saves[i].exists;
            // cFormMainMenu::charSelectHandler @001483cc: new game -> +0x16c = slot; then ProcSelectSlot(slot) @00148310
            ui_log("[title] slot %d (exists %d) form %08x", i, exists, f);
            state::post([f, i, exists] {
                if (!f) return;
                ui_log("[title] ProcSelectSlot(%d) delete-mode %d menu state %d", i, M32(f + 0x168), M32(M32(IMG + 0x19b244) + 8));
                if (!exists) M16(f + 0x16c) = (uint16_t)i;
                g::call(0x138310, {f, (uint32_t)i});
            });
            return true;
        }
        if (a.rfind("del", 0) == 0) { ss.confirm_slot = a[3] - '0'; nav.root = d.el("confirm"); nav.set(d.el("c_no")); return true; }
        if (a == "c_no") { ss.confirm_slot = -1; ss.import_slot = -1; nav.root = d.el("slots"); focus_cards(); return true; }
        if (a == "c_yes") {
            if (ss.import_slot >= 0) { apply_import(); return true; }
            int i = ss.confirm_slot;
            ss.confirm_slot = -1;
            state::post([i] { g::call(0x13d45c, {(uint32_t)i}); }); // cNetAuthorize::onSaveFileDeleted @0014d45c
            nav.root = d.el("slots");
            nav.set(nullptr);
            return true;
        }
        return false;
    }

    void back() override {
        if (busy) return;
        if (modal_up()) action("c_no");
        else if (ss.pick) ss.pick = 0;
        else if (ss.slots_open) { ss.slots_open = false; nav.root = d.el("menu"); nav.set(d.el("m_start")); }
    }
};

static TitleScreen g_title;

} // namespace screens
