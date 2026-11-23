// Help (replaces cFormHelp, form 0x23: cGameUi::ShowHelp @0011f564 from the system menu, cFormInvenSystem::DoSystem
// @001139a0 index 2). Topics: cResourceManager::GetHelpInfoCount @000dc758 / GetHelpInfo(i) @000dc728 (12-byte records:
// [0] title id, [1] body id, byte 8 bit 4 = the rescue button is offered on that page), texts through
// GetSubDescString(3, id) @000dd0d8 (cFormHelp::initContents @0010acec, ShowHelpDesc @0010aa5c).
// Rescue ("구조 요청", stuck-player escape): the original logs the position to its server (cFormHelpRescueResponder::
// LogRescue @0010a3b0, request 0x1e2) and then, answered or not (OnNetErrorPopupClosing @0010a39c), cFormHelp::Process
// @0010a9b4 closes the top form and calls cGameWorld::DoSuicide @0012a5ac (town: script 24 moves the player out;
// elsewhere the player falls and the death choices open). The port skips only the server log.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include <atomic>
#include <mutex>

namespace screens {

class HelpScreen : public Screen {
    struct Topic { std::string title, body; bool rescue = false; };
    struct Session {
        int page = -1;           // -1 list, else topic shown
        int sel = 0;
        bool confirm = false;
        std::string drawn;
    } ss;
    // the topics are read from the original's resources once and kept for every later opening
    std::vector<Topic> topics;      // main thread copy
    std::vector<Topic> pending;     // filled on the game thread
    std::mutex mx;
    std::atomic<bool> ready{false}, asked{false};
    uint32_t form = 0, menu = 0;    // per-frame mirrors of the snapshot

    // data model "help" (help.rml), set in update()
    struct Item {
        std::string act, rml;       // t<i>, the number and the title (markup)
        bool sel = false;
        bool operator==(const Item &o) const { return act == o.act && rml == o.rml && sel == o.sel; }
    };
    struct View {
        std::vector<Item> list;
        bool page = false, confirm = false, rescue = false;
        std::string ptitle, pbody;  // the shown topic (markup)
    } m;

public:
    HelpScreen() { z = 42; state::set_ported(0x23); register_screen(this); }
    const char *file() override { return "help.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("help");
        if (!c) return;
        if (auto h = c.RegisterStruct<Item>()) { h.RegisterMember("act", &Item::act); h.RegisterMember("rml", &Item::rml); h.RegisterMember("sel", &Item::sel); }
        c.RegisterArray<std::vector<Item>>();
        c.Bind("list", &m.list); c.Bind("page", &m.page); c.Bind("confirm", &m.confirm); c.Bind("rescue", &m.rescue);
        c.Bind("ptitle", &m.ptitle); c.Bind("pbody", &m.pbody);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        form = 0;
        for (auto &f : s.forms) if (f.id == 0x23) form = f.ptr;
        menu = s.menu.mdi;
        return s.stripped && s.top_form == 0x23 && form;
    }
    void on_show() override {
        ss = {};
        if (!asked.exchange(true)) {
            state::post([this] {
                uint32_t rm = g::resmgr();
                if (!rm) { asked = false; return; }
                std::vector<Topic> v;
                int n = (int)g::call(0xcc758, {rm});
                for (int i = 0; i < n && i < 64; i++) {
                    uint32_t rec = g::call(0xcc728, {rm, (uint32_t)i});
                    if (!rec) continue;
                    Topic t;
                    t.title = g::plain_text(g::cstr(g::call(0xcd0d8, {rm, 3, M32(rec)})));
                    t.body = g::rml_text(g::cstr(g::call(0xcd0d8, {rm, 3, M32(rec + 4)})), true);
                    t.rescue = ((M8(rec + 8) & 0x1f) >> 4) != 0;
                    v.push_back(std::move(t));
                }
                std::lock_guard<std::mutex> l(mx);
                pending = std::move(v);
                ready = true;
            });
        }
    }

    void update(const state::Snapshot &s) override {
        (void)s;
        if (ready.exchange(false)) { std::lock_guard<std::mutex> l(mx); topics = pending; ss.drawn.clear(); }
        std::string key = std::to_string(ss.page) + "/" + std::to_string(ss.confirm) + "/" + std::to_string(topics.size());
        if (key == ss.drawn) return;
        ss.drawn = key;
        put(m.page, ss.page >= 0, "page");
        put(m.confirm, ss.confirm, "confirm");
        std::vector<Item> l; // none: help.rml shows "불러오는 중…"
        for (size_t i = 0; i < topics.size(); i++)
            l.push_back({Rml::CreateString("t%d", (int)i), Rml::CreateString("<span class=\"n\">%02d</span>", (int)i + 1) + rml_safe(topics[i].title), (int)i == ss.sel});
        put(m.list, std::move(l), "list");
        if (ss.page >= 0 && ss.page < (int)topics.size()) {
            put(m.ptitle, rml_safe(topics[ss.page].title), "ptitle");
            put(m.pbody, rml_safe(topics[ss.page].body), "pbody");
            put(m.rescue, topics[ss.page].rescue, "rescue");
            Rml::Element *b = d.el("pbody");
            if (b) b->SetScrollTop(0); // the new text comes in this frame's Context::Update (after update()) and keeps this position
        }
        nav.root = ss.confirm ? d.el("cbox") : ss.page >= 0 ? d.el("pagewrap") : d.el("listwrap");
        nav.set(nullptr);
        if (ss.confirm) nav.set(d.el("c_no"));
        else if (ss.page < 0) {
            // the list's rows are made in this frame's Context::Update (after update()) when the topics just came in
            std::string a = "t" + std::to_string(ss.sel);
            if (Rml::Element *e = nav.find_act(a)) nav.set(e); else nav.want_act(a);
        }
        nav.ensure();
    }

    bool action(const std::string &a) override {
        if (a.size() > 1 && a[0] == 't' && isdigit((unsigned char)a[1])) { ss.sel = atoi(a.c_str() + 1); ss.page = ss.sel; return true; }
        if (a == "hback") { back(); return true; }
        if (a == "hclose") { close(); return true; }
        if (a == "rescue") { ss.confirm = true; return true; }
        if (a == "c_no") { ss.confirm = false; return true; }
        if (a == "c_yes") {
            // cFormHelp::Process's rescue end without the server log: close the help and the menu, then DoSuicide
            uint32_t f = form, m = menu;
            state::post([f, m] {
                g::call(0x147760, {f, 1});              // cflUiForm::SetDead @00157760
                if (m) g::call(0x147760, {m, 1});
                uint32_t w = g::world();
                if (w) g::call(0x11a5ac, {w});          // cGameWorld::DoSuicide @0012a5ac
            });
            ss.confirm = false;
            return true;
        }
        return false;
    }
    void close() { uint32_t f = form; state::post([f] { g::call(0x147760, {f, 1}); }); }
    void back() override {
        if (ss.confirm) { ss.confirm = false; return; }
        if (ss.page >= 0) { ss.page = -1; return; }   // page -> list (ProcKeyPressImpl @0010a6a0: 0x3A page -> list, list -> close)
        close();
    }
    bool on_dir(int dir) override {
        if (ss.page >= 0 && !ss.confirm && dir < 2) { // the page has one button: up / down scroll the text
            Rml::Element *b = d.el("pbody");
            if (b) b->SetScrollTop(b->GetScrollTop() + (dir == 0 ? -1.f : 1.f) * 80.f * d.doc->GetContext()->GetDensityIndependentPixelRatio());
            return true;
        }
        return false;
    }
};

static HelpScreen g_help;
}
