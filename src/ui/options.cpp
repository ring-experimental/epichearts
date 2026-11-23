// Settings (replaces cFormOption, form 0x20). Game settings go through the original handlers
// (cFormOption::OnReflectBgm @00110fb8, OnReflectFx @00110f84, OnReflectSpeed @00110f44,
// OnReflectItemAutoRoot @00110b2c, OnReflectVib @00110e80) and are saved like the original's close path
// (key 0x3a -> cFormOption::somethingKeypress @00110fe4: DoSaveOpt + SetDead). Port settings: settings.cpp.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "settings.h"
#include "ui.h"
#include <SDL3/SDL.h>

void ui_relayout();
int ui_max_scale();

namespace screens {

class OptionsScreen : public Screen {
    struct Session {} ss;        // nothing per opening: the rows show the live settings
    uint32_t form = 0;           // per-frame mirror of the snapshot

    // data model "options" (options.rml): what the rows show, set from the settings every frame
    struct Seg {
        std::string act;         // o_<row>=<n>
        bool on = false;
        bool operator==(const Seg &o) const { return act == o.act && on == o.on; }
    };
    struct Slider {
        std::vector<Seg> segs;
        std::string val;
        bool operator==(const Slider &o) const { return segs == o.segs && val == o.val; }
    };
    struct View {
        Slider bgm, fx, speed, scale, vibs, vpx, vpy;
        Slider mexp, mgold, mlife, mref; // developer options (hidden until unlocked)
        bool dev = false;
        std::string ver, toast;  // version row text; the unlock countdown toast
        bool toast_on = false;
        bool loot = false, vib = false, full = false, direct = false, sharp = false;
        bool full_row = true;    // the fullscreen row (not on Android)
        std::string foot;        // key prompts (glyph markup)
    } m;

    static uint32_t sys() { return g::sysinfo(); }
    int bgm() { return M8(sys() + 6); }
    int fx() { return M8(sys() + 7); }
    int speed() { int f = M8(sys() + 0xb8); int l = (f - 11) / 2; return l < 0 ? 0 : l > 10 ? 10 : l; }
    bool loot() { return !(M32(sys() + 8) & 0x200); }
    bool vib() { return (M32(sys() + 8) & 0x40) != 0; }
    bool direct() { return (M32(sys() + 8) & 8) != 0; } // touch control: Direct (bit set) / V-Pad (cFormOption::OnReflectTouchType @00110e34)

    // a row's segments (the arrows around them are in options.rml)
    static Slider segs(const char *act, int v, int n, const std::string &label) {
        Slider r;
        for (int i = 0; i < n; i++) r.segs.push_back({Rml::CreateString("%s=%d", act, i + 1), i < v});
        r.val = label;
        return r;
    }

    void set_bgm(int v) { uint32_t f = form; v = v < 0 ? 0 : v > 100 ? 100 : v; state::post([f, v] { g::call(0x100fb8, {f, (uint32_t)v}); }); }
    void set_fx(int v) { uint32_t f = form; v = v < 0 ? 0 : v > 100 ? 100 : v; state::post([f, v] { g::call(0x100f84, {f, (uint32_t)v}); }); }
    // vibration strength (port setting): saved, and a short test pulse at the new strength
    void set_vibs(int v) {
        settings::S &ps = settings::get();
        ps.vib_strength = v < 0 ? 0 : v > 100 ? 100 : v;
        settings::save();
        if (vib()) input::vibrate(120);
    }
    void set_speed(int l) { uint32_t f = form; l = l < 0 ? 0 : l > 10 ? 10 : l; state::post([f, l] { g::call(0x100f44, {f, (uint32_t)(l * 10)}); }); }

    // developer options: unlocked like Android's (tap the version row 7 times; from the 3rd tap a toast counts down)
    int taps = 0;
    uint64_t last_tap = 0, toast_until = 0;
    std::string toast_text;
    void show_toast(const std::string &t) { toast_text = t; toast_until = SDL_GetTicks() + 1600; }
    void tap_version() {
        settings::S &ps = settings::get();
        uint64_t now = SDL_GetTicks();
        if (now - last_tap > 2500) taps = 0; // the taps must follow each other
        last_tap = now;
        if (ps.dev) { show_toast("이미 개발자 옵션이 켜져 있습니다"); return; }
        if (++taps >= 7) {
            taps = 0;
            ps.dev = true;
            settings::save();
            settings::apply();
            show_toast("개발자 옵션이 켜졌습니다");
        } else if (taps >= 3)
            show_toast(Rml::CreateString("개발자 옵션까지 %d번 남았습니다", 7 - taps));
    }
    static int mul_index(int v) { int i = 0; for (int k = 0; k < settings::MUL_N; k++) if (settings::MUL_STEPS[k] <= v) i = k; return i; }
    static std::string mul_label(int v) { return v % 100 ? Rml::CreateString("×%d.%d", v / 100, v % 100 / 10) : Rml::CreateString("×%d", v / 100); }
    static int *mul_of(const std::string &row) {
        settings::S &ps = settings::get();
        return row == "o_mexp" ? &ps.mul_exp : row == "o_mgold" ? &ps.mul_gold : row == "o_mlife" ? &ps.mul_life : row == "o_mref" ? &ps.mul_refine : nullptr;
    }
    static void set_mul(int *p, int idx) { *p = settings::MUL_STEPS[idx < 0 ? 0 : idx >= settings::MUL_N ? settings::MUL_N - 1 : idx]; settings::save(); settings::apply(); }

public:
    OptionsScreen() { z = 40; state::set_ported(0x20); register_screen(this); }
    const char *file() override { return "options.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("options");
        if (!c) return;
        if (auto h = c.RegisterStruct<Seg>()) { h.RegisterMember("act", &Seg::act); h.RegisterMember("on", &Seg::on); }
        c.RegisterArray<std::vector<Seg>>();
        if (auto h = c.RegisterStruct<Slider>()) { h.RegisterMember("segs", &Slider::segs); h.RegisterMember("val", &Slider::val); }
        c.Bind("bgm", &m.bgm); c.Bind("fx", &m.fx); c.Bind("speed", &m.speed); c.Bind("scale", &m.scale); c.Bind("vibs", &m.vibs);
        c.Bind("vpx", &m.vpx); c.Bind("vpy", &m.vpy);
        c.Bind("mexp", &m.mexp); c.Bind("mgold", &m.mgold); c.Bind("mlife", &m.mlife); c.Bind("mref", &m.mref);
        c.Bind("dev", &m.dev); c.Bind("ver", &m.ver); c.Bind("toast", &m.toast); c.Bind("toast_on", &m.toast_on);
        c.Bind("loot", &m.loot); c.Bind("vib", &m.vib); c.Bind("full", &m.full); c.Bind("direct", &m.direct); c.Bind("sharp", &m.sharp);
        c.Bind("full_row", &m.full_row); c.Bind("foot", &m.foot);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        form = 0;
        for (auto &f : s.forms) if (f.id == 0x20) form = f.ptr;
        return s.stripped && s.top_form == 0x20 && form;
    }
    void on_show() override { ss = {}; nav.root = d.el("body"); nav.set(d.el("r_bgm")); }

    void update(const state::Snapshot &s) override {
        (void)s;
        settings::S &ps = settings::get();
        put(m.bgm, segs("o_bgm", (bgm() + 5) / 10, 10, std::to_string(bgm()) + "%"), "bgm");
        put(m.fx, segs("o_fx", (fx() + 5) / 10, 10, std::to_string(fx()) + "%"), "fx");
        put(m.speed, segs("o_speed", speed(), 10, Rml::CreateString("%.2f\xc3\x97", (11 + speed() * 2) / 29.0) /* frames per second (systemInfo+0xB8) relative to the original default 29 */), "speed");
        put(m.loot, loot(), "loot");
        put(m.vib, vib(), "vib");
        put(m.direct, direct(), "direct");
        put(m.full, ps.fullscreen, "full");
        put(m.sharp, ps.sharp, "sharp");
        int mx = ui_max_scale(), shown = std::min(ps.ui_scale, mx);
        put(m.scale, segs("o_scale", (shown - 80) / 10 + 1, (mx - 80) / 10 + 1, std::to_string(shown) + "%"), "scale");
#ifdef __ANDROID__
        put(m.full_row, false, "full_row");
#endif
        put(m.vibs, segs("o_vibs", ps.vib_strength / 10, 10, std::to_string(ps.vib_strength) + "%"), "vibs");
        // V-Pad position (port setting): steps right / up from the original's place, 0 = the original's
        auto pos = [](int v) { return v ? Rml::CreateString("+%d", v) : std::string("기본"); }; // 기본
        put(m.vpx, segs("o_vpx", ps.vpad_x, 10, pos(ps.vpad_x)), "vpx");
        put(m.vpy, segs("o_vpy", ps.vpad_y, 10, pos(ps.vpad_y)), "vpy");
        put(m.ver, std::string("EpicHearts · ") + __DATE__, "ver"); // build date of this port
        put(m.dev, ps.dev, "dev");
        put(m.mexp, segs("o_mexp", mul_index(ps.mul_exp) + 1, settings::MUL_N, mul_label(ps.mul_exp)), "mexp");
        put(m.mgold, segs("o_mgold", mul_index(ps.mul_gold) + 1, settings::MUL_N, mul_label(ps.mul_gold)), "mgold");
        put(m.mlife, segs("o_mlife", mul_index(ps.mul_life) + 1, settings::MUL_N, mul_label(ps.mul_life)), "mlife");
        put(m.mref, segs("o_mref", mul_index(ps.mul_refine) + 1, settings::MUL_N, mul_label(ps.mul_refine)), "mref");
        put(m.toast_on, SDL_GetTicks() < toast_until, "toast_on");
        put(m.toast, toast_text, "toast");
        put(m.foot, "<span>" + glyph("advance") + " \xeb\xb3\x80\xea\xb2\xbd</span><span>" + glyph("back") + " \xeb\x8b\xab\xea\xb8\xb0</span>", "foot");
    }

    bool adjust(const std::string &row, int dir) {
        settings::S &ps = settings::get();
        if (row == "o_bgm") set_bgm(bgm() + 10 * dir);
        else if (row == "o_fx") set_fx(fx() + 10 * dir);
        else if (row == "o_speed") set_speed(speed() + dir);
        else if (row == "o_scale") { ps.ui_scale = std::max(80, std::min(ui_max_scale(), std::min(ps.ui_scale, ui_max_scale()) + 10 * dir)); settings::save(); ui_relayout(); }
        else if (row == "o_sharp") { ps.sharp = !ps.sharp; settings::apply(); settings::save(); }
        else if (row == "o_ctrl") { bool dr = !direct(); state::post([dr] { state::set_touch_type(dr); }); }
        else if (row == "o_vibs") set_vibs(ps.vib_strength + 10 * dir);
        else if (row == "o_vpx") { ps.vpad_x = std::max(0, std::min(10, ps.vpad_x + dir)); settings::save(); }
        else if (row == "o_vpy") { ps.vpad_y = std::max(0, std::min(10, ps.vpad_y + dir)); settings::save(); }
        else if (int *mp = ps.dev ? mul_of(row) : nullptr) set_mul(mp, mul_index(*mp) + dir);
        else return false;
        return true;
    }
    bool on_dir(int dir) override {
        if (dir < 2 || !nav.focus) return false;
        return adjust(nav.focus->GetAttribute<Rml::String>("data-act", ""), dir == 3 ? 1 : -1);
    }

    bool action(const std::string &a) override {
        uint32_t f = form;
        settings::S &ps = settings::get();
        if (a == "o_close") { back(); return true; }
        size_t eq = a.find('=');
        if (eq != std::string::npos) {
            std::string row = a.substr(0, eq);
            int v = atoi(a.c_str() + eq + 1);
            if (row == "o_bgm") set_bgm(v * 10);
            else if (row == "o_fx") set_fx(v * 10);
            else if (row == "o_speed") set_speed(v);
            else if (row == "o_scale") { ps.ui_scale = std::min(ui_max_scale(), 80 + (v - 1) * 10); settings::save(); ui_relayout(); }
            else if (row == "o_sharp") { ps.sharp = v != 0; settings::apply(); settings::save(); }
            else if (row == "o_ctrl") { bool dr = v != 0; state::post([dr] { state::set_touch_type(dr); }); }
            else if (row == "o_vibs") set_vibs(v * 10);
            // a click on the lit last segment goes back one step, so the first step can be turned off again
            else if (row == "o_vpx") { ps.vpad_x = v == ps.vpad_x ? v - 1 : v; settings::save(); }
            else if (row == "o_vpy") { ps.vpad_y = v == ps.vpad_y ? v - 1 : v; settings::save(); }
            else if (int *mp = ps.dev ? mul_of(row) : nullptr) set_mul(mp, v - 1);
            return true;
        }
        if (!a.empty() && (a.back() == '+' || a.back() == '-')) return adjust(a.substr(0, a.size() - 1), a.back() == '+' ? 1 : -1);
        if (a == "o_loot") { bool on = !loot(); state::post([f, on] { g::call(0x100b2c, {f, on ? 1u : 0u}); }); return true; }
        if (a == "o_vib") { bool on = !vib(); state::post([f, on] { g::call(0x100e80, {f, on ? 1u : 0u}); }); return true; }
        if (a == "o_full") { ui_toggle_fullscreen(); return true; }
        if (a == "o_keys") { open_keymap(); return true; }
        if (a == "o_bgm" || a == "o_fx" || a == "o_speed" || a == "o_scale" || a == "o_sharp" || a == "o_ctrl" || a == "o_vibs") return adjust(a, 1);
        if (a == "o_ver") { tap_version(); return true; }
        if (a == "o_devoff") { // hide the developer options again; the multipliers go back to the original's
            ps.dev = false; ps.mul_exp = ps.mul_gold = ps.mul_life = ps.mul_refine = 100;
            settings::save(); settings::apply();
            nav.set(d.el("r_ver"));
            show_toast("개발자 옵션을 껐습니다");
            return true;
        }
        if (int *mp = ps.dev ? mul_of(a) : nullptr) { set_mul(mp, mul_index(*mp) >= settings::MUL_N - 1 ? 0 : mul_index(*mp) + 1); return true; }
        if (a == "o_vpx" || a == "o_vpy") { int &v = a == "o_vpx" ? ps.vpad_x : ps.vpad_y; v = v >= 10 ? 0 : v + 1; settings::save(); return true; }
        return false;
    }
    void back() override {
        // the original close: key 0x3a on the focused option form saves systemInfo (SaveOpt) and closes it
        state::post([] { state::key_tap(0x3a); });
    }
};

static OptionsScreen g_options;
}
