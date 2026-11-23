// Key mapping popup (Options > 키 설정): the bindings of the device in use (keyboard, or the connected gamepad),
// a row is rebound by selecting it and pressing the new key / button (Esc cancels). Port-only (bindings.h).
#include "bindings.h"
#include "screen_util.h"
#include "screens.h"
#include "settings.h"
#include "ui.h"
#include "ui_internal.h"

namespace screens {

class KeymapScreen : public Screen {
    struct Session {
        int dev = bind::KB;
        std::string waiting;     // action id waiting for a key
        std::string toast;       // "<label> -> <key>" after a swap
        double toast_until = 0;
    } ss;
    bool open = false;           // the open request (open_keymap), made before the screen shows
    int open_dev = bind::KB;     // the device page it asked for

    // data model "keymap" (keymap.rml): set in update() only (the capture callback changes ss outside it)
    struct Row {
        std::string act, label, v; // km_<id>, the action's name, its key / button glyph or the waiting prompt (markup)
        bool wait = false;
        bool operator==(const Row &o) const { return act == o.act && label == o.label && v == o.v && wait == o.wait; }
    };
    struct View {
        bool pad = false, has_pad = false;
        std::string pad_name, toast, note;
        std::vector<Row> g0, g1;   // field / menus and dialogue (keymap.rml puts a section header before each)
        int dev = -1;              // the device whose rows g0 / g1 hold (not bound; kept with the rows across openings)
    } m;

    static bool pad_connected() {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        SDL_free(ids);
        return n > 0;
    }

public:
    KeymapScreen() { z = 45; register_screen(this); }
    const char *file() override { return "keymap.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("keymap");
        if (!c) return;
        if (auto h = c.RegisterStruct<Row>()) {
            h.RegisterMember("act", &Row::act); h.RegisterMember("label", &Row::label);
            h.RegisterMember("v", &Row::v); h.RegisterMember("wait", &Row::wait);
        }
        c.RegisterArray<std::vector<Row>>();
        c.Bind("pad", &m.pad); c.Bind("has_pad", &m.has_pad); c.Bind("pad_name", &m.pad_name);
        c.Bind("toast", &m.toast); c.Bind("note", &m.note); c.Bind("g0", &m.g0); c.Bind("g1", &m.g1);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { (void)s; return open; }
    void show() {
        open = true;
        // the device in use: the gamepad when one is connected and it was used last, else the keyboard
        // (a mouse click on the button doesn't count: with a pad connected and no keyboard typing, the pad's page)
        open_dev = pad_connected() && (input::pad_input() || !input::keyboard_last()) ? bind::PAD : bind::KB;
        if (shown) { ss = {}; ss.dev = open_dev; } // asked again while open: start over on that page
    }
    void on_show() override { ss = {}; ss.dev = open_dev; nav.root = d.el("rows"); nav.set(nullptr); }

    void update(const state::Snapshot &s) override {
        (void)s;
        bool pad = ss.dev == bind::PAD, ps = input::pad_is_ps();
        put(m.pad, pad, "pad");
        put(m.has_pad, pad_connected(), "has_pad");
        put(m.pad_name, std::string(ps ? "PlayStation" : "Xbox"), "pad_name");
        std::vector<Row> g[2];
        for (auto &a : bind::actions((bind::Dev)ss.dev)) {
            int code = bind::get((bind::Dev)ss.dev, a.id);
            bool w = ss.waiting == a.id;
            std::string v = w ? std::string("<span class=\"wait\">") + (pad ? "\xeb\xb2\x84\xed\x8a\xbc\xec\x9d\x84 \xeb\x88\x84\xeb\xa5\xb4\xec\x84\xb8\xec\x9a\x94" : "\xed\x82\xa4\xeb\xa5\xbc \xeb\x88\x84\xeb\xa5\xb4\xec\x84\xb8\xec\x9a\x94") + " \xc2\xb7 Esc \xec\xb7\xa8\xec\x86\x8c</span>"
                              : pad ? input::pad_glyph(code, ps) : input::key_glyph(code);
            g[a.group ? 1 : 0].push_back({std::string("km_") + a.id, a.label, v, w});
        }
        // another device's rows: data-for reuses the row elements by index, so the focus follows its action
        // (found once the new rows are made, next frame; none when the other device has no such action)
        if (ss.dev != m.dev) {
            m.dev = ss.dev;
            if (nav.focus) { nav.want_act(nav.focus->GetAttribute<Rml::String>("data-act", "")); nav.clear(); }
        }
        put(m.g0, std::move(g[0]), "g0");
        put(m.g1, std::move(g[1]), "g1");
        put(m.toast, Rml::GetSystemInterface()->GetElapsedTime() < ss.toast_until ? ss.toast : std::string(), "toast");
        put(m.note, pad ? std::string("\xeb\xb0\xa9\xed\x96\xa5\xed\x8c\xa8\xeb\x93\x9c \xc2\xb7 Start(\xeb\x8b\xab\xea\xb8\xb0)\xeb\x8a\x94 \xeb\xa9\x94\xeb\x89\xb4\xec\x97\x90\xec\x84\x9c \xed\x95\xad\xec\x83\x81 \xeb\x8f\x99\xec\x9e\x91\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4") : std::string("\xeb\xb0\xa9\xed\x96\xa5\xed\x82\xa4 \xc2\xb7 Enter \xc2\xb7 Esc\xeb\x8a\x94 \xeb\xa9\x94\xeb\x89\xb4\xec\x97\x90\xec\x84\x9c \xed\x95\xad\xec\x83\x81 \xeb\x8f\x99\xec\x9e\x91\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4"), "note");
    }

    bool action(const std::string &a) override {
        if (a == "km_close") { back(); return true; }
        if (a == "km_reset") { bind::reset((bind::Dev)ss.dev); settings::save(); return true; }
        if (a == "km_kb") { ss.dev = bind::KB; ss.waiting.clear(); input::begin_capture(nullptr); return true; }
        if (a == "km_pad") { ss.dev = bind::PAD; ss.waiting.clear(); input::begin_capture(nullptr); return true; }
        if (a.rfind("km_", 0) == 0) {
            std::string id = a.substr(3);
            ss.waiting = id;
            int want_dev = ss.dev;
            input::begin_capture([this, id, want_dev](int got_dev, int code) {
                ss.waiting.clear();
                if (code < 0 || got_dev != want_dev) return; // cancelled, or the other device
                // the action that had this code gets the old one (swap): tell the player which row moved
                std::string other = bind::action_for((bind::Dev)want_dev, code);
                int old = bind::get((bind::Dev)want_dev, id.c_str());
                bind::set((bind::Dev)want_dev, id.c_str(), code);
                settings::save();
                if (!other.empty() && other != id) {
                    for (auto &a : bind::actions((bind::Dev)want_dev))
                        if (other == a.id) ss.toast = std::string(a.label) + " &#8594; " + (want_dev == bind::PAD ? input::pad_glyph(old, input::pad_is_ps()) : input::key_glyph(old)) + " (\xec\x84\x9c\xeb\xa1\x9c \xeb\xb0\x94\xeb\x80\x9c)";
                    ss.toast_until = Rml::GetSystemInterface()->GetElapsedTime() + 2.5;
                }
            });
            return true;
        }
        return false;
    }
    void back() override {
        if (!ss.waiting.empty()) { ss.waiting.clear(); input::begin_capture(nullptr); return; }
        open = false;
    }
};

static KeymapScreen g_keymap;
void open_keymap() { g_keymap.show(); }
}
