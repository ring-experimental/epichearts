// Story dialogue controls: SKIP / AUTO / LOG at the top right while a scripted dialogue scene runs (talkctl.rml),
// and a log of the scene's lines. The dialogue box itself is talk.rml (screens.cpp update_talk).
//
// Scene = a script runs (cGameManager +0x210, read in state.cpp) and its dialogue is on screen: cTalkBallonWnd
// (type 4) / cMessageWnd waiting for a key (type 0) as the modal box of cMessageWndManager (work/msg_spec.md 2.1).
//
// FAST is the original's own script fast-forward, the fast button of cTalkBallonCommand scenes:
//  - cTalkBallonCommand::InitImpl @000df19c calls cGameWorld::ShowFastButton @00129558 (world+0x768 = 1 unless
//    gm+0x1B4 & 0x4000, which HideFastButton(1) @00129578 sets: the scene forbids it).
//  - cGameWorld::SetAutoScpKeyMode(world, on) @0012afb4 sets world+0x76C (copied to the script engine +0x84) and the
//    game fps to 50 (cGameManager::SetGameFps). While it is 1, cTalkBallonCommand::RunOneTickImpl @000df894,
//    cMsgCommand::RunOneTickImpl @000df9a8 and cMessageWndManager::ProcMessageBox @00101678 inject key 0x30 every tick
//    and cflScript passes it to cMultiTickCommand::WaitOrRunOneTick (waits are cut short).
//  - It leaves alone what must not be skipped blindly: the guide notice (cMsgCommand +0x20 = 1 for cmd 0x95,
//    cMsgCommand::InitImpl @000df1c4), the mission popup and UI forms (choices: the key is only injected by the
//    message commands). Here a form that opens during the fast-forward also turns it off (stop at a choice).
//  - The script end turns it off: cScriptEngine (@000e72xx, cScriptEngine.c:452) HideFastButton(0) +
//    SetAutoScpKeyMode(0). The log is cleared at that point as well (no persistence).
// SKIP (port design, not in the original: user request for a plain skip) runs that same fast-forward to the end of
// the scene with the frame pacing off (java_set_turbo: cNixSystem::Run's appLock.wait returns at once) behind a
// curtain, so the scene's script still executes every command (flags, items, warps) - only faster than it can be
// watched. The game thread drops the turbo the frame world+0x76C leaves 1 (script end); a choice / form ends the
// skip there, a guide notice pauses it (shown, then the skip goes on), and it gives up after a few seconds.
// The prologue / ending crawl (cFormTextScroller, form 0x24, script cmd 0xF1, work/msg_spec.md 12) gets FAST / SKIP too:
//  - FAST = the original's own speed-up held on: cFormTextScroller::ProcKey @0010233c adds 2 per step to +0x9C (max 0x12)
//    while a touch is held and takes 2 off otherwise; Process @001023c0 moves the text by +0x84 + that boost per step.
//    The original's "Fast:Tap" hint (Draw @00102144, string at vaddr 0x1669c8) is blanked: the chip replaces it.
//  - SKIP (port design): the start delay +0x78 and end hold +0x7C are zeroed and the text put past its end
//    (+0x74 = -label height, the state-1 end test of Process), so the form ends through its own fade (state 2 -> 9 -> 3,
//    or the ending caption 10) and its callback (OnDead @00101b8c) resumes the script; a caption (state 5 / 10) fades out.
// AUTO has no counterpart in the original: it taps the advance key (0x30, what a tap on the box sends,
// cTalkBallonWnd::ProcMsgBox @000ff748) once a waiting page is fully shown plus a reading delay.
//
// Input: the screen is modal during the scene (it owns keyboard / gamepad through screens::pad_button):
// the mapped menu keys (Options > key mapping): confirm advances (the #adv element), tabs log / auto, ui_skip skip
// (F / Start), ui_ffwd fast-forward (R / Y); back is the original's back key 0x3A (as input.cpp sends during dialogue).
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "ui.h"
#include "ui_internal.h"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <functional>

extern "C" {
void java_set_turbo(int on, int (*keep)(void)); // java.c
int java_turbo(void);
}

namespace screens {

static bool g_shown;
bool talkctl_shown() { return g_shown; }

// the dialogue box of talk.rml is up (same condition as update_talk in screens.cpp)
static bool talk_box(const state::Snapshot &s) {
    const state::Talk &t = s.talk;
    return t.active && (t.kind == 4 || t.kind == 0) && s.stripped && (!t.text.empty() || !t.name.empty());
}

static size_t visible_chars(const std::string &rml) {
    size_t n = 0;
    bool tag = false;
    for (size_t i = 0; i < rml.size(); i++) {
        unsigned char c = (unsigned char)rml[i];
        if (c == '<') tag = true;
        if (tag) { if (c == '>') tag = false; continue; }
        if (c == '&') { size_t e = rml.find(';', i); if (e != std::string::npos) i = e; n++; continue; }
        if ((c & 0xC0) != 0x80 && c > ' ') n++;
    }
    return n;
}

// a page of the dialogue: Talk::id (box ^ page offset) repeats when the next balloon is allocated at the freed
// address of the previous one, so the text is part of the key
static uint32_t page_key(const state::Talk &t) { return t.id ^ (uint32_t)std::hash<std::string>()(t.name + "|" + t.text); }

static std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else o += c;
    }
    return o;
}

// game thread: the original's fast-forward on / off (see the top of the file)
static void set_fast(bool on) {
    uint32_t w = g::world();
    if (!w) return;
    if (on && !M32(w + 0x768)) g::call(0x119558, {w}); // cGameWorld::ShowFastButton @00129558 (keeps the 0x4000 veto)
    if (on && !M32(w + 0x768)) return;
    g::call(0x11afb4, {w, on ? 1u : 0u});              // cGameWorld::SetAutoScpKeyMode @0012afb4
}

// game thread, every frame of a skip: go on while the scene's fast mode is on
static int skip_keep() {
    uint32_t w = g::world();
    return w && M32(w + 0x76c) == 1;
}
// game thread: fast mode on, then unpaced frames until it ends
static void skip_start() {
    set_fast(true);
    uint32_t w = g::world();
    if (w && M32(w + 0x76c) == 1) java_set_turbo(1, skip_keep);
}

// ---- the crawl (game thread)
static uint32_t crawl_form() {
    uint32_t ui = g::gameui();
    if (!ui) return 0;
    for (uint32_t f = M32(ui + 0x14), n = 0; f && n < 32; f = M32(f + 0x40), n++)
        if ((int16_t)M16(f + 0x38) == 0x24 && !(M32(f + 0x14) & 0x1800000)) return f;
    return 0;
}
// held speed-up: ProcKey takes 2 off without a touch, so 0x14 steps at the maximum 0x12
static void crawl_fast() {
    uint32_t f = crawl_form();
    if (f && M32(f + 0x98) == 1) M32(f + 0x9c) = 0x14;
}
static void crawl_skip() {
    uint32_t f = crawl_form();
    if (!f) return;
    int st = (int)M32(f + 0x98);
    M32(f + 0x78) = 0; // start delay (s)
    M32(f + 0x7c) = 0; // end hold (s)
    uint32_t lab = M32(f + 0x5c);
    if (lab && (st == 0 || st == 1 || st == 4)) M32(f + 0x74) = (uint32_t)(-(int32_t)(int16_t)M16(lab + 0x1e) - 1);
    // a caption fades out from (state << 6) / 5 frames on (Draw); jump there
    if (st == 5 && (int32_t)M32(f + 0xa0) < 65) M32(f + 0xa0) = 65;
    if (st == 10 && (int32_t)M32(f + 0xa0) < 129) M32(f + 0xa0) = 129;
    ui_log("[talkctl] crawl skip (state %d)", st);
}

class TalkCtlScreen : public Screen, public Rml::EventListener {
    struct Line { std::string name, text; };
    struct Session {
        bool log_open = false;
        int log_drawn = -1;      // log version drawn
        int scroll_frames = 0;
        // auto: page being timed, when it is due, when the advance key was sent
        uint32_t auto_id = 0;
        double auto_start = 0, auto_due = 0, auto_sent = 0;
        // skip: running, paused at a guide notice, the turbo was seen on, when it (re)started (on_hide ends it)
        bool skipping = false, skip_paused = false, turbo_seen = false;
        double skip_t0 = 0;
    } ss;
    // the scene: its log and AUTO last until the script ends (want() clears them), across the screen hiding under the
    // forms the scene opens; want() also tracks the times and the crawl while the screen is hidden
    std::vector<Line> log;
    uint32_t last_id = 0;
    std::vector<std::string> bal_seen; // balloon pages already logged (box address + text)
    int log_ver = 0;
    bool autoplay = false;
    double box_seen = -1e9, script_seen = -1e9;
    bool crawl = false, crawl_fast_on = false; // the prologue / ending crawl is up; its FAST is held on
    bool scene = false, own_keys = false, fast_on = false; // per frame (want)
    double key_last[3] = {-1e9, -1e9, -1e9}; // key repeat filter of the keydown listener
    bool listening = false;      // the listener is registered once
    // data model "talkctl" (talkctl.rml), set in update() only (the log changes in want(): log_ver says when)
    struct Entry {
        std::string who, say;    // markup (data-rml): the escaped name, the original's line
        bool head = false, cont = false, now = false; // name shown, same speaker as the line before, the newest
        bool operator==(const Entry &o) const { return who == o.who && say == o.say && head == o.head && cont == o.cont && now == o.now; }
    };
    struct View {
        std::vector<Entry> log;
        bool log_open = false, crawl = false, skip_cur = false, skip_on = false, ffwd_on = false, autoplay = false, locked = false;
        std::string k_log, k_auto, k_ffwd, k_skip; // key / button glyphs (markup)
        std::string autofill = "0.0%";
    } m;

    static double now() { return Rml::GetSystemInterface()->GetElapsedTime(); }
    // reading delay of a page: port design, not from the original (it has no auto mode)
    static double reading_delay(size_t chars) { return std::min(7.0, std::max(1.6, 1.0 + 0.075 * (double)chars)); }

    void set_auto(bool on) {
        autoplay = on;
        ss.auto_id = 0;
    }
    void toggle_log(bool open) {
        if (open == ss.log_open) return;
        ss.log_open = open;
        if (open) {
            set_auto(false);
            if (fast_on) state::post([] { set_fast(false); });
            ss.scroll_frames = 3;
        }
        nav.clear();
    }
    void skip() {
        if (ss.skipping || ss.log_open) return;
        const state::Snapshot &s = state::get();
        if (s.fast_locked && !s.fast_button) return; // the scene forbids the fast-forward (HideFastButton(1))
        set_auto(false);
        ss.skipping = true; ss.skip_paused = false; ss.turbo_seen = false;
        ss.skip_t0 = now();
        ui_log("[talkctl] skip");
        state::post([] { skip_start(); });
    }
    void end_skip(const char *why) {
        if (!ss.skipping) return;
        ss.skipping = false;
        java_set_turbo(0, nullptr);
        ui_log("[talkctl] skip ends (%s, %.2fs)", why, now() - ss.skip_t0);
        // the skip gave up (the scene waits on play, e.g. a fight, which the fast mode cannot cut short): the original's
        // fast mode goes off too, or it keeps injecting key 0x30 every tick into the player's game
        if (!strcmp(why, "timeout") || !strcmp(why, "no fast mode") || !strcmp(why, "hidden")) state::post([] { set_fast(false); });
    }
    // the original's fast-forward toggle
    void ffwd() {
        if (ss.skipping || ss.log_open) return;
        if (fast_on) { state::post([] { set_fast(false); }); return; }
        set_auto(false);
        state::post([] { set_fast(true); });
    }
    void skip_tick(const state::Snapshot &s, double t) {
        if (!ss.skipping) return;
        bool turbo = java_turbo() != 0;
        if (turbo) ss.turbo_seen = true;
        bool notice = s.talk.active && (s.talk.kind == 7 || s.talk.kind == 100);
        if (!s.forms.empty()) end_skip("form");
        else if (ss.skip_paused) {
            if (!s.fast_on) end_skip("scene over");
            else if (!notice) { // the notice was read: go on
                ss.skip_paused = false; ss.turbo_seen = false; ss.skip_t0 = t;
                state::post([] { skip_start(); });
            }
        } else if (notice) { ss.skip_paused = true; java_set_turbo(0, nullptr); }
        else if (!turbo && (ss.turbo_seen || t - ss.skip_t0 > 1.0)) end_skip(ss.turbo_seen ? "scene over" : "no fast mode");
        // UNVERIFIED: a scene that waits on something the fast mode does not cut short; give it back to the player
        else if (t - ss.skip_t0 > 8.0) end_skip("timeout");
    }
    void advance() { state::post([] { state::key_tap(0x30); }); }

public:
    TalkCtlScreen() { z = 32; /* above the generic renderer (30): the crawl is one of its forms */ register_screen(this); }
    const char *file() override { return "talkctl.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("talkctl");
        if (!c) return;
        if (auto h = c.RegisterStruct<Entry>()) {
            h.RegisterMember("who", &Entry::who); h.RegisterMember("say", &Entry::say); h.RegisterMember("head", &Entry::head);
            h.RegisterMember("cont", &Entry::cont); h.RegisterMember("now", &Entry::now);
        }
        c.RegisterArray<std::vector<Entry>>();
        c.Bind("log", &m.log); c.Bind("log_open", &m.log_open); c.Bind("crawl", &m.crawl); c.Bind("skip_cur", &m.skip_cur);
        c.Bind("skip_on", &m.skip_on); c.Bind("ffwd_on", &m.ffwd_on); c.Bind("autoplay", &m.autoplay); c.Bind("locked", &m.locked);
        c.Bind("k_log", &m.k_log); c.Bind("k_auto", &m.k_auto); c.Bind("k_ffwd", &m.k_ffwd); c.Bind("k_skip", &m.k_skip);
        c.Bind("autofill", &m.autofill);
        model = c.GetModelHandle();
    }

    bool want(const state::Snapshot &s) override {
        double t = now();
        bool box = talk_box(s);
        fast_on = s.fast_on;
        if (s.script) script_seen = t;
        if (s.script && box) box_seen = t;
        // log every page of the scene's dialogue (cTalkBallonWnd / cMessageWnd page = Talk::id)
        if (s.script && box && page_key(s.talk) != last_id && !s.talk.text.empty()) {
            last_id = page_key(s.talk);
            if (log.empty() || log.back().name != s.talk.name || log.back().text != s.talk.text) {
                log.push_back({s.talk.name, s.talk.text});
                ui_log("[talkctl] line %d: %s", (int)log.size(), s.talk.text.c_str());
                log_ver++;
            }
        }
        // balloons that do not wait (toast kind 22: several speakers at once) are logged once per page as they appear
        if (s.script && s.stripped) {
            for (auto &to : s.toasts) {
                if (to.kind != 22) continue;
                std::string k = std::to_string(to.id) + '\n' + to.text;
                if (std::find(bal_seen.begin(), bal_seen.end(), k) != bal_seen.end()) continue;
                bal_seen.push_back(k);
                log.push_back({to.name, to.text});
                ui_log("[talkctl] balloon %d: %s", (int)log.size(), to.text.c_str());
                log_ver++;
            }
        }
        // the script ended (cScriptEngine: HideFastButton + SetAutoScpKeyMode(0)): the scene and its log are over
        if (!s.script && t - script_seen > 0.5 && (!log.empty() || autoplay || ss.log_open)) {
            log.clear();
            bal_seen.clear();
            last_id = 0;
            log_ver++;
            set_auto(false);
            ss.log_open = false;
        }
        skip_tick(s, t);
        // stop the fast-forward at a choice / any form the scene opens
        if (s.fast_on && !s.forms.empty()) { static double sent; if (t - sent > 0.5) { sent = t; ui_log("[talkctl] form %d opened: fast-forward stops", s.top_form); state::post([] { set_fast(false); }); } }
        if (!s.forms.empty() && ss.log_open) ss.log_open = false;
        scene = s.stripped && s.script && s.forms.empty() && (box || s.fast_button || ss.log_open || t - box_seen < 0.6);
        if (getenv("EH_CRAWLDBG")) { // test: the forms as they change
            static std::string last;
            std::string ids;
            for (auto &f : s.forms) ids += std::to_string(f.id) + " ";
            if (ids != last) { last = ids; ui_log("[crawl] forms: %s(stripped %d gm %d)", ids.c_str(), (int)s.stripped, s.gm_state); }
        }
        if (getenv("EH_CRAWLDBG")) state::post([] { // every form in the list, closed / hidden ones included
            static std::string last;
            std::string r;
            uint32_t ui = g::gameui();
            for (uint32_t f = ui ? M32(ui + 0x14) : 0, n = 0; f && n < 32; f = M32(f + 0x40), n++)
                r += std::to_string((int16_t)M16(f + 0x38)) + "/" + std::to_string(M32(f + 0x14) >> 20) + " ";
            if (r != last) { last = r; ui_log("[crawl] all forms: %s", r.c_str()); }
        });
        bool was = crawl;
        crawl = s.stripped && s.top_form == 0x24;
        if (crawl && !was) state::post([] { M8(IMG + 0x1669c8) = 0; }); // "Fast:Tap" (see the top of the file)
        if (!crawl) crawl_fast_on = false;
        bool curtain = ss.skipping && !ss.skip_paused;
        own_keys = scene || curtain || crawl;
        g_shown = own_keys;
        return own_keys;
    }
    bool modal() override { return own_keys; }

    void on_show() override {
        ss = {};
        nav.root = d.doc;
        if (!listening && d.doc) {
            // keys the modal path of input.cpp leaves to RmlUi (F skip, Z advance as in the original keymap, L log)
            d.doc->GetContext()->GetRootElement()->AddEventListener(Rml::EventId::Keydown, this);
            listening = true;
        }
    }
    void on_hide() override { ss.log_open = false; g_shown = false; end_skip("hidden"); }

    void ProcessEvent(Rml::Event &ev) override {
        if (!shown || !own_keys) return;
        int k = ev.GetParameter<int>("key_identifier", 0);
        // skip: the key bound to "fast" (Options > key mapping) arrives through input.cpp as the "tskip" action
        // log / auto / advance come from the mapped menu keys (tabs / confirm, input.cpp); no fixed extra keys
        (void)k;
        int slot = -1;
        if (slot < 0) return;
        // RmlUi reports key repeats as key-downs: a held key counts once
        double t = now();
        bool repeat = t - key_last[slot] < 0.6;
        key_last[slot] = t;
        if (repeat) return;
        if (slot == 0 && !ss.log_open) skip();
        else if (slot == 1) toggle_log(!ss.log_open);
        else if (slot == 2 && !ss.log_open) advance();
    }

    bool action(const std::string &a) override {
        if (crawl) {
            if (a == "tskip") state::post([] { crawl_skip(); });
            else if (a == "tffwd") crawl_fast_on = !crawl_fast_on;
            else if (a != "tlog" && a != "tauto" && a != "advance") return false;
            return true;
        }
        if (a == "tlog") toggle_log(!ss.log_open);
        else if (a == "tlogclose") toggle_log(false);
        else if (a == "tauto") set_auto(!autoplay);
        else if (a == "tskip") skip();
        else if (a == "tffwd") ffwd();
        else return false;
        return true;
    }
    void back() override {
        if (crawl) return; // the crawl swallows keys (cFormTextScroller::ProcKeyPress @00101bac)
        if (ss.log_open) { toggle_log(false); return; }
        // B / Esc stay the original's back key during dialogue (skip comes from the bound key, see input.cpp)
        state::post([] { state::key_tap(0x3a); });
    }
    void tab(int dir) override {
        if (crawl) return;
        if (dir < 0) toggle_log(!ss.log_open);
        else if (!ss.log_open) set_auto(!autoplay);
    }
    bool on_dir(int dir) override {
        if (ss.log_open && dir < 2) {
            Rml::Element *l = d.el("loglist");
            if (l) l->SetScrollTop(l->GetScrollTop() + (dir == 0 ? -1.f : 1.f) * 90.f * d.doc->GetContext()->GetDensityIndependentPixelRatio());
        }
        return true; // no focus to move during the dialogue
    }

    void update(const state::Snapshot &s) override {
        double t = now();
        put(m.log_open, ss.log_open, "log_open");
        bool locked = s.fast_locked && !s.fast_button;
        if (crawl) locked = false;
        put(m.crawl, crawl, "crawl");
        put(m.skip_cur, ss.skipping && !ss.skip_paused && !crawl, "skip_cur");
        put(m.skip_on, ss.skipping && !crawl, "skip_on");
        put(m.locked, locked, "locked");
        put(m.ffwd_on, crawl ? crawl_fast_on : s.fast_on && !ss.skipping, "ffwd_on");
        if (crawl && crawl_fast_on) state::post([] { crawl_fast(); });
        if (crawl && getenv("EH_CRAWLDBG")) state::post([] { uint32_t f = crawl_form(); if (f) ui_log("[crawl] state %d y %d boost %d", (int)M32(f + 0x98), (int)M32(f + 0x74), (int)M32(f + 0x9c)); });
        put(m.autoplay, autoplay, "autoplay");

        // key / button hints on the buttons (the HUD's convention), from the key mapping
        put(m.k_log, glyph("tabl"), "k_log");
        put(m.k_auto, glyph("tabr"), "k_auto");
        put(m.k_ffwd, glyph("ui_ffwd"), "k_ffwd");
        put(m.k_skip, glyph("ui_skip"), "k_skip");

        // auto: advance a waiting page once it is fully shown and read
        const state::Talk &tk = s.talk;
        bool ready = autoplay && !ss.log_open && !s.fast_on && talk_box(s) && tk.waiting && tk.reveal >= 0.999f && s.forms.empty();
        float fill = 0;
        if (!ready) ss.auto_id = 0;
        else {
            if (page_key(tk) != ss.auto_id) {
                ss.auto_id = page_key(tk);
                ss.auto_start = t;
                ss.auto_due = t + reading_delay(visible_chars(tk.text));
                ss.auto_sent = 0;
            }
            fill = (float)std::min(1.0, (t - ss.auto_start) / std::max(0.01, ss.auto_due - ss.auto_start));
            // one key per page; again only if the page did not move on (key lost)
            if (t >= ss.auto_due && (ss.auto_sent == 0 || t - ss.auto_sent > 1.5)) {
                ui_log("[talkctl] auto advance after %.1fs (%d chars)", t - ss.auto_start, (int)visible_chars(tk.text));
                advance();
                ss.auto_sent = t;
            }
        }
        put(m.autofill, Rml::CreateString("%.1f%%", autoplay ? fill * 100 : 0.f), "autofill");

        // log
        if (log_ver != ss.log_drawn) {
            ss.log_drawn = log_ver;
            std::vector<Entry> v;
            for (size_t i = 0; i < log.size(); i++) {
                bool cont = i > 0 && log[i - 1].name == log[i].name;
                v.push_back({rml_safe(esc(log[i].name)), rml_safe(log[i].text), !cont && !log[i].name.empty(), cont, i + 1 == log.size()});
            }
            put(m.log, std::move(v), "log"); // empty: #logempty ("no lines yet")
            if (ss.log_open) ss.scroll_frames = 3;
        }
        if (ss.scroll_frames > 0) {
            ss.scroll_frames--;
            Rml::Element *l = d.el("loglist");
            if (l) l->SetScrollTop(l->GetScrollHeight()); // newest at the bottom
        }
    }
};

static TalkCtlScreen g_talkctl;

} // namespace screens
