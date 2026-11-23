// Input: SDL events -> RmlUi first; unclaimed pointer input goes to the game as touches (GofManager.setTouch:
// ACTION_DOWN 1, UP 2, MOVE 4, game framebuffer coordinates). Keyboard and gamepads are mapped onto the original's
// key codes (work/hud_spec.md 6.3: feature-phone layout 0x20..0x3B), which the original never received on Android
// but fully supports natively (cAndInput::OnKeyPressed @00163a10 / OnKeyReleased @001639fc).
#include "bindings.h"
#include "capture_api.h"
#include "game.h"
#include "settings.h"
#include "sprites.h"
#include "gas.h"
#include "screens.h"
#include "state.h"
#include "ui.h"
#include "ui_internal.h"
#include <RmlUi_Platform_SDL.h>
#include <RmlUi/Debugger.h>
#include <cmath>
#include <functional>
#include <vector>
#include <algorithm>
#include <cstring>

extern "C" void java_set_touch(int type, int x, int y);
extern "C" void java_back_pressed(void);

namespace input {

enum Device { KBM, TOUCH, XBOX, PS };
static Device g_dev =
#ifdef __ANDROID__
    TOUCH;
#else
    KBM;
#endif
static SDL_Gamepad *g_pad;   // the pad prompts follow (last used)
static SDL_Joystick *g_vjoy; // test: virtual gamepad (ui vpad / vbtn)
Device device() { return g_dev; }
bool touch_input() { return g_dev == TOUCH; }
bool pad_input() { return g_dev == XBOX || g_dev == PS; }
static bool g_kb_typed; // a keyboard key (not the mouse) was the last non-pad input
bool keyboard_last() { return g_kb_typed; }

static bool g_game_pointer;
static SDL_FingerID g_finger;
static bool g_finger_active;
// touch drag-scrolling of RmlUi lists (RmlUi has no touch scrolling): the finger's UI press scrolls the nearest
// scrollable ancestor once it moves past a threshold, and then does not click
static Rml::Element *g_scroll_el;
static float g_scroll_y0, g_scroll_top0;
static bool g_scrolling;
static Rml::Element *scrollable_at(Rml::Element *e) {
    // on a scrollbar the finger drags the bar (RmlUi's own drag, the content moves the other way to a swipe)
    for (Rml::Element *p = e; p; p = p->GetParentNode())
        if (p->GetTagName().rfind("scrollbar", 0) == 0 || p->GetTagName() == "sliderbar") return nullptr;
    for (; e; e = e->GetParentNode())
        if (e->GetScrollHeight() > e->GetClientHeight() + 1) return e;
    return nullptr;
}
// V-Pad: a floating joystick in the lower-left zone, on its own finger so the other thumb can attack / use skills.
// 4-way like the original's pad (DrawUiVirtualKey @000f7ab0 highlights left/right/up/down) and the gamepad stick.
static SDL_FingerID g_stick_finger;
static bool g_stick_active;
static float g_stick_cx, g_stick_cy, g_stick_x, g_stick_y; // window pixels
static int g_stick_key;
bool vpad_stick(float *cx, float *cy, float *x, float *y) { *cx = g_stick_cx; *cy = g_stick_cy; *x = g_stick_x; *y = g_stick_y; return g_stick_active; }
// V-pad geometry of the original, in its 320-px-high framebuffer (cBattleUi::RefreshComponentArea @000f84ac, HitTestImpl
// @000f8834): dpad centre (79, H-77), hit radius 90, 9 px dead square; kept as fractions of the screen height so the pad
// is as large on screen as the original's (hud.rml #vpad uses the same numbers in vh)
static const float VPAD_CX = 79.f / 320, VPAD_CY = 77.f / 320, VPAD_DEAD = 9.f / 320;
// HUD controls that act on touch-down like the original's hit test (attack, quickslots, set toggle: data-press)
static struct { SDL_FingerID id; Rml::Element *el; } g_press[4];

// original key codes (work/hud_spec.md 6.3)
enum { K_UP = 0x2f, K_DOWN = 0x2e, K_LEFT = 0x2d, K_RIGHT = 0x2c, K_OK = 0x30, K_BACK = 0x3a, K_MENU = 0x31,
       K_PAGE = 0x2a, K_MAP = 0x2b, K_SAVE = 0x34 };
static const int SLOT_KEYS[5] = {0x21, 0x23, 0x27, 0x29, 0x20}; // slot_key_map_list @0x19900c

// ------------------------------------------------------------------------------------------------ glyphs
static const char *pad_type_class(bool *ps) {
    *ps = g_dev == PS;
    return *ps ? "ps" : "xbox";
}

// glyph of a gamepad code (button or trigger) in the style of the pad in use
std::string pad_glyph(int code, bool ps) {
    struct G { int code; const char *x, *xc, *p, *pc; };
    static const G T[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, "A", "xa", "\xee\x86\xb2", "cross"},
        {SDL_GAMEPAD_BUTTON_EAST, "B", "xb", "\xee\x81\xb6", "circle"},
        {SDL_GAMEPAD_BUTTON_WEST, "X", "xx", "\xee\x85\xa7", "square"},
        {SDL_GAMEPAD_BUTTON_NORTH, "Y", "xy", "\xee\x86\x92", "triangle"},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB", "bumper", "L1", "bumper"},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB", "bumper", "R1", "bumper"},
        {bind::PAD_LT, "LT", "bumper", "L2", "bumper"},
        {bind::PAD_RT, "RT", "bumper", "R2", "bumper"},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, "LS", "bumper", "L3", "bumper"},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "RS", "bumper", "R3", "bumper"},
        {SDL_GAMEPAD_BUTTON_START, "\xee\x84\x95", "bumper", "\xee\x84\x95", "bumper"},       // Menu / Options: lucide menu
        {SDL_GAMEPAD_BUTTON_BACK, "\xee\x82\x9e", "bumper", "\xee\x8d\xba", "bumper"},        // View: lucide copy / touchpad
        {SDL_GAMEPAD_BUTTON_TOUCHPAD, "\xee\x8d\xba", "bumper", "\xee\x8d\xba", "bumper"},
        {SDL_GAMEPAD_BUTTON_GUIDE, "\xee\x83\x9f", "bumper", "\xee\x83\x9f", "bumper"},       // gamepad-2
    };
    static const char *DP[4] = {"\xee\x81\xb0", "\xee\x81\xad", "\xee\x81\xae", "\xee\x81\xaf"}; // chevrons up/down/left/right
    if (code >= SDL_GAMEPAD_BUTTON_DPAD_UP && code <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
        return std::string("<span class=\"glyph dpad\">") + DP[code - SDL_GAMEPAD_BUTTON_DPAD_UP] + "</span>";
    for (auto &g : T)
        if (g.code == code) {
            const char *t = ps ? g.p : g.x, *c = ps ? g.pc : g.xc;
            if (!strcmp(c, "bumper")) return std::string("<span class=\"glyph bumper") + ((unsigned char)t[0] == 0xee ? " ico" : "") + "\">" + t + "</span>";
            if (ps) return std::string("<span class=\"glyph pad ps ") + c + "\">" + t + "</span>";
            return std::string("<span class=\"glyph pad ") + c + "\">" + t + "</span>";
        }
    return code < 0 ? "<span class=\"glyph\">-</span>" : Rml::CreateString("<span class=\"glyph bumper\">%d</span>", code);
}
// keycap of a keyboard key
std::string key_glyph(int key) {
    if (key < 0) return "<span class=\"glyph\">-</span>";
    std::string n = SDL_GetKeyName((SDL_Keycode)key);
    if (n == "Escape") n = "Esc";
    else if (n == "Return") n = "Enter";
    else if (n.empty()) n = "?";
    return "<span class=\"glyph\">" + n + "</span>";
}
static bool is_ps(SDL_Gamepad *p);
bool pad_is_ps() { return g_dev == PS || (g_dev != XBOX && is_ps(g_pad)); }

std::string glyph_for(const char *a, bool always) {
    if (g_dev == XBOX || g_dev == PS) {
        bool ps = g_dev == PS;
        // menu keys (mapped) and the stick
        if (!strcmp(a, "advance")) return pad_glyph(bind::get(bind::PAD, "ui_confirm"), ps);
        if (!strcmp(a, "back")) return pad_glyph(bind::get(bind::PAD, "ui_back"), ps);
        if (!strcmp(a, "tabl")) return pad_glyph(bind::get(bind::PAD, "ui_tabl"), ps);
        if (!strcmp(a, "tabr")) return pad_glyph(bind::get(bind::PAD, "ui_tabr"), ps);
        if (!strcmp(a, "stick")) return std::string("<span class=\"glyph bumper\">") + (ps ? "L" : "LS") + "</span>";
        if (!strcmp(a, "lr")) return "<span class=\"glyph bumper ico\">\xee\x89\x8a</span>";
        if (!strcmp(a, "move")) return "<span class=\"glyph bumper ico\">\xee\x84\xa1</span>";
        // mapped actions
        std::string id = a;
        if (id.size() == 5 && id.rfind("slot", 0) == 0 && id[4] >= '5' && id[4] <= '9') id[4] = (char)(id[4] - 5); // set B = same buttons + hold
        if (id == "page") id = "setb";
        int code = bind::get(bind::PAD, id.c_str());
        if (code < 0) return "";
        return pad_glyph(code, ps);
    }
    if (g_dev == TOUCH) return "";
    // keyboard prompts only while the keyboard is in use: a mouse player sees none (and no layout change when a
    // prompt comes and goes); reference screens (help, key list) always list the keys
    if (!g_kb_typed && !always) return "";
    if (!strcmp(a, "menu")) return "<span class=\"glyph\">Esc</span>";
    if (!strcmp(a, "back")) return key_glyph(bind::get(bind::KB, "ui_back"));
    if (!strcmp(a, "tabl")) return key_glyph(bind::get(bind::KB, "ui_tabl"));
    if (!strcmp(a, "tabr")) return key_glyph(bind::get(bind::KB, "ui_tabr"));
    if (!strcmp(a, "page")) return "<span class=\"glyph\">Q</span>";
    if (!strcmp(a, "lr")) return "<span class=\"glyph\">\xe2\x86\x90 \xe2\x86\x92</span>";
    if (!strcmp(a, "stick") || !strcmp(a, "move")) {
        // the four movement keys as one cap when they are single characters (WASD), else the first one
        std::string k;
        for (const char *d : {"up", "left", "down", "right"}) {
            std::string n = SDL_GetKeyName((SDL_Keycode)bind::get(bind::KB, d));
            if (n.size() != 1) { k.clear(); break; }
            k += n;
        }
        if (k.size() == 4) k = std::string() + k[0] + k[1] + k[2] + k[3];
        return "<span class=\"glyph\">" + (k.empty() ? std::string("\xe2\x86\x91\xe2\x86\x93") : k) + "</span>";
    }
    std::string id = !strcmp(a, "advance") ? "ui_confirm" : a;
    int code = bind::get(bind::KB, id.c_str());
    if (code < 0) return "";
    return key_glyph(code);
}

// key-mapping capture (Options > 키 설정): the next key / pad button goes to the callback instead of the game
static std::function<void(int dev, int code)> g_capture;
void begin_capture(std::function<void(int dev, int code)> cb) { g_capture = std::move(cb); }
bool capturing() { return (bool)g_capture; }
static bool capture(int dev, int code) {
    if (!g_capture) return false;
    auto cb = std::move(g_capture);
    g_capture = nullptr;
    cb(dev, code);
    return true;
}

// ------------------------------------------------------------------------------------------------ game actions
static int g_dir_held;       // original direction key currently held (0 none)
static int g_pad_dir_key;    // D-pad direction held in the original's own forms
static bool g_held[4];       // up, down, left, right (keyboard)
static int g_want_dir;

static void set_direction(int key) {
    g_want_dir = key;
    if (key == g_dir_held) return;
    int old = g_dir_held;
    g_dir_held = key;
    state::post([old, key] {
        if (old) state::key_up(old);
        if (key) state::key_down(key);
    });
}

static void tap(int key) { state::post([key] { state::key_tap(key); }); }

// V-pad: 4-way like the original (TranslateToAVK @000f7a34): the longer axis of the offset from the pad centre, none
// inside the dead square
static void stick_update() {
    float dx = g_stick_x - g_stick_cx, dy = g_stick_y - g_stick_cy, dead = VPAD_DEAD * ui::win_h;
    int k = 0;
    if (std::fabs(dx) > dead || std::fabs(dy) > dead) k = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? K_RIGHT : K_LEFT) : (dy > 0 ? K_DOWN : K_UP);
    if (k != g_stick_key) { g_stick_key = k; set_direction(k); }
}

// HUD buttons that have no key in the original: press the original HUD hit code for one tap
static void hud_hit(int code) { state::post([code] { state::hud_hit(code); }); }

void ui_action(const char *a) {
    const state::Snapshot &s = state::get();
    ui_log("[ui] action %s", a);
    // HUD actions that open a screen stop the walk first (the release would otherwise reach the original a frame late)
    if (!strcmp(a, "town") || !strcmp(a, "quest") || !strcmp(a, "save") || !strcmp(a, "map") || !strcmp(a, "menu")) {
        g_pad_dir_key = 0;
        for (bool &h : g_held) h = false;
        set_direction(0);
    }
    if (!strncmp(a, "slot", 4)) {
        int i = a[4] - '0';
        if (i < 0 || i > 9) return;
        // touch: the original's page toggle decides; keyboard / mouse: 10 slots (A 0-4, B 5-9); gamepad: set from LB
        if (g_dev == TOUCH) { if (i < 5) tap(SLOT_KEYS[i]); return; }
        int page = i / 5, k = i % 5;
        state::post([page, k] { state::use_slot(page, k); });
        return;
    }
    if (!strcmp(a, "page")) tap(K_PAGE);
    else if (!strcmp(a, "attack") || !strcmp(a, "advance")) tap(K_OK);
    else if (!strcmp(a, "menu")) { if ((s.can_menu || s.voyage) && !s.script) tap(K_MENU); }
    else if (!strcmp(a, "map")) tap(K_MAP);
    else if (!strcmp(a, "save")) { if (s.can_save) tap(K_SAVE); }
    else if (!strcmp(a, "town")) { bool t = g_dev == TOUCH; state::post([t] { state::town_button(t); }); }
    // quests: the HUD quest button (hit code 3) opens the in-game menu on its quest page; opening the page
    // directly (cGameManager / cGameUi calls in state::open_menu_tab) does not depend on a forced touch reaching
    // the original's HitTestImpl, which was lost while the player was moving
    else if (!strcmp(a, "quest")) { if (s.can_menu && !s.script) { bool t = g_dev == TOUCH; state::post([t] { state::quest_button(t); }); } }
    else if (!strcmp(a, "fast")) state::post([] { state::toggle_fast(); });
    else if (!strcmp(a, "bagbuy")) screens::open_bagbuy();
    else if (!strcmp(a, "ctrlmode")) { bool direct = s.vpad; state::post([direct] { state::set_touch_type(direct); }); }
    else if (!strcmp(a, "stats")) state::post([] { state::open_menu_tab(0); });
    else if (!strcmp(a, "skills")) state::post([] { state::open_menu_tab(3); });
    else screens::action(a);
}

// ------------------------------------------------------------------------------------------------ pointer routing
static bool ui_wants_pointer() {
    Rml::Element *h = ui::context->GetHoverElement();
    for (Rml::Element *e = h; e; e = e->GetParentNode()) {
        if (e->HasAttribute("data-passthrough")) return false;
        // before the body test: a modern screen's <body data-block> keeps every touch from the original form under it
        // (the menu's empty areas reached cFormInvenMDI form 4, whose swipe moves to the hidden net / pay tabs 5-7)
        if (e->HasAttribute("data-act") || e->HasAttribute("data-block")) return true;
        if (e->GetTagName() == "body") return false;
    }
    return h != nullptr && h != ui::context->GetRootElement();
}

// where a touch at window (wx, wy) goes in the game; false: outside the drawn frame (the black bars of a window whose
// aspect differs from the frame's), where a touch-down starts no game pointer. Before, bar touches were clamped to the
// frame edge and pressed whatever the original had there. Moves / ups of a pointer that started inside stay clamped.
// "ui geom" checks the bars.
bool touch_target(float wx, float wy, int *gx, int *gy) {
    bool in = ui_window_to_game(wx, wy, gx, gy) != 0; // the frame as drawn (game_rect: the world and the original's forms)
    *gx = *gx < 0 ? 0 : *gx >= g_game_w ? g_game_w - 1 : *gx;
    *gy = *gy < 0 ? 0 : *gy >= g_game_h ? g_game_h - 1 : *gy;
    return in;
}

// type 1 down, 4 move, 2 up; returns false when a down was not passed on (moves / ups of a passed pointer always are)
static bool game_touch(int type, float wx, float wy) {
    int gx, gy;
    bool in = touch_target(wx, wy, &gx, &gy);
    static int log = getenv("EH_TOUCHLOG") != nullptr; // test: touch-downs and where they went
    if (log && type == 1) ui_log("[touch] down window %.0f,%.0f -> %s %d,%d", wx, wy, in ? "game" : "dropped (outside the frame)", gx, gy);
    if (type == 1 && !in) return false;
    java_set_touch(type, gx, gy);
    return true;
}

// ------------------------------------------------------------------------------------------------ keyboard
static int kb_dir() {
    // most recent wins would need ordering; prefer vertical then horizontal like a d-pad
    if (g_held[0]) return K_UP;
    if (g_held[1]) return K_DOWN;
    if (g_held[2]) return K_LEFT;
    if (g_held[3]) return K_RIGHT;
    return 0;
}

static bool in_modern_menu() { return screens::menu_active(); }

static int kb_direction(SDL_Keycode key) {
    switch (key) { // the arrow keys always move; the mapped movement keys too
    case SDLK_UP: return 0;
    case SDLK_DOWN: return 1;
    case SDLK_LEFT: return 2;
    case SDLK_RIGHT: return 3;
    }
    static const char *D[4] = {"up", "down", "left", "right"};
    for (int i = 0; i < 4; i++) if ((int)key == bind::get(bind::KB, D[i])) return i;
    return -1;
}

// one menu step per press: with Steam running, a pad on a game outside the Steam library also drives the desktop
// layout, which sends arrow keys for the D-pad, so one press arrives as a pad button and as a key (two steps). A
// step in the same direction from another source (keyboard / D-pad / stick) within 80 ms is the same press.
enum { DIR_KB, DIR_DPAD, DIR_STICK };
static void menu_dir(int d, int src) {
    static int last_d = -1, last_src = -1;
    static Uint64 last_ms;
    Uint64 now = SDL_GetTicks();
    if (d >= 0 && d == last_d && src != last_src && now - last_ms < 80) {
        ui_log("[input] menu step %d from source %d dropped (source %d %llu ms before)", d, src, last_src, (unsigned long long)(now - last_ms));
        return;
    }
    if (d >= 0) { last_d = d; last_src = src; last_ms = now; }
    screens::pad_dir(d);
}

static bool key_event(const SDL_KeyboardEvent &k, bool down) {
    // F8: RmlUi's visual debugger, set up on first use (once set up it flags every logged warning with a beacon,
    // which players must not see)
    if (down && !k.repeat && k.key == SDLK_F8) {
        static bool dbg;
        if (!dbg) dbg = Rml::Debugger::Initialise(ui::context);
        if (dbg) Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
        return true;
    }
    // Android system keys (BACK, volume) are not a keyboard: the prompts stay as they are
    bool syskey = k.key == SDLK_AC_BACK || k.key == SDLK_VOLUMEUP || k.key == SDLK_VOLUMEDOWN || k.key == SDLK_MUTE || k.key == SDLK_MENU;
    if (down && !syskey) { g_dev = KBM; g_kb_typed = true; }
    if (down && !k.repeat && g_capture) { // key mapping: Esc cancels (-1)
        capture(bind::KB, k.key == SDLK_ESCAPE ? -1 : (int)k.key);
        return true;
    }
    int d = kb_direction(k.key);
    // menu keys (Options > key mapping, group 1); Enter / Esc always confirm / go back as well
    std::string ui = bind::action_for(bind::KB, (int)k.key, 1);
    bool ui_ok = ui == "ui_confirm" || k.key == SDLK_RETURN || k.key == SDLK_KP_ENTER;
    bool ui_back = ui == "ui_back" || k.key == SDLK_ESCAPE || k.key == SDLK_AC_BACK;
    bool confirm = (int)k.key == bind::get(bind::KB, "attack") || k.key == SDLK_RETURN || k.key == SDLK_KP_ENTER;
    if (in_modern_menu()) { // modern screens: arrows / movement keys move focus, then the mapped menu keys
        if (!down) return true;
        // dialogue: the mapped skip / fast-forward keys (talkctl.cpp); a held key counts once
        if (screens::talkctl_shown() && (ui == "ui_skip" || ui == "ui_ffwd")) { if (!k.repeat) screens::action(ui == "ui_skip" ? "tskip" : "tffwd"); return true; }
        if (ui_ok) { screens::pad_button(SDL_GAMEPAD_BUTTON_SOUTH, true); return true; }
        if (ui_back) { screens::pad_button(SDL_GAMEPAD_BUTTON_EAST, true); return true; }
        if (ui == "ui_tabl") { screens::pad_button(100, true); return true; }
        if (ui == "ui_tabr") { screens::pad_button(101, true); return true; }
        if (ui == "ui_subl") { screens::pad_button(102, true); return true; }
        if (ui == "ui_subr") { screens::pad_button(103, true); return true; }
        if (d >= 0) { menu_dir(d, DIR_KB); return true; }
        return false;
    }
    if (d >= 0) {
        g_held[d] = down;
        set_direction(kb_dir());
        return true;
    }
    if (!down || k.repeat) return confirm;
    const state::Snapshot &s = state::get();
    bool forms = !s.forms.empty();
    // the original's own windows and dialogue: the mapped menu confirm / back
    if ((forms || s.talk.active) && ui_ok) { tap(K_OK); return true; }
    if ((forms || s.talk.active) && ui_back) { tap(K_BACK); return true; }
    if (confirm) { tap(K_OK); return true; }
    if (k.key == SDLK_ESCAPE || k.key == SDLK_AC_BACK) {
        if (forms || s.talk.active) tap(K_BACK);
        else if (s.in_field) ui_action("menu");
        else java_back_pressed();
        return true;
    }
    if (k.key == SDLK_I) { ui_action("menu"); return true; }
    std::string a = bind::action_for(bind::KB, (int)k.key);
    if (a.empty()) return false;
    // HUD buttons (quests, town, fast-forward) only in the free field; quickslots / map / save as before
    if ((a == "quest" || a == "town" || a == "fast") && forms) return true;
    ui_action(a.c_str());
    return true;
}

// ------------------------------------------------------------------------------------------------ gamepad
static bool is_ps(SDL_Gamepad *p) {
    SDL_GamepadType t = p ? SDL_GetGamepadType(p) : SDL_GAMEPAD_TYPE_UNKNOWN;
    return t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
}
// every connected pad is opened; prompts follow the pad that was used last (like Steam games)
static std::vector<SDL_JoystickID> g_recent; // connected pads, most recently used last
static void pad_used(SDL_JoystickID id) {
    g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), id), g_recent.end());
    g_recent.push_back(id);
}
static void pad_open(SDL_JoystickID id) {
    SDL_Gamepad *p = SDL_GetGamepadFromID(id);
    if (!p) p = SDL_OpenGamepad(id);
    if (!p) return;
    ui_log("[input] gamepad connected: %s (%s)", SDL_GetGamepadName(p), is_ps(p) ? "PlayStation" : "Xbox");
    g_pad = p;
    g_dev = is_ps(p) ? PS : XBOX;
    pad_used(id);
}
static void pad_active(SDL_JoystickID id) {
    SDL_Gamepad *p = SDL_GetGamepadFromID(id);
    if (p) { g_pad = p; pad_used(id); }
}

static bool g_lb_held; // the set-B button (LB / L1 by default) held: the slot buttons use set B
bool pad_set_b() { return g_lb_held && (g_dev == XBOX || g_dev == PS); }
static void pad_slot(int i) { char act[6] = "slot0"; act[4] = (char)('0' + i + (g_lb_held ? 5 : 0)); ui_action(act); }

// a pressed pad code (button or trigger) in the field: the mapped action
static void pad_code(int code) {
    const state::Snapshot &s = state::get();
    bool free_field = s.in_field && s.forms.empty() && !s.talk.active;
    std::string a = bind::action_for(bind::PAD, code);
    if (a.empty() || a == "setb") return;
    if (a == "attack") tap(K_OK);
    else if (a.rfind("slot", 0) == 0) pad_slot(a[4] - '0');
    else if (a == "map2") ui_action("map");
    else if (a == "quest" || a == "save" || a == "map" || a == "town" || a == "fast") { if (free_field) ui_action(a.c_str()); }
    else ui_action(a.c_str()); // menu
}

static void pad_button(int b, bool down) {
    if (b == bind::get(bind::PAD, "setb")) g_lb_held = down; // modifier (in menus LB also switches tabs)
    if (down) { g_dev = is_ps(g_pad) ? PS : XBOX; g_kb_typed = false; }
    if (down && g_capture) { capture(bind::PAD, b); return; }
    if (in_modern_menu()) {
        // the mapped menu buttons become the screens' canonical ones (A confirm, B back, LB / RB tabs);
        // the D-pad navigates and Start goes back regardless of the mapping
        std::string ui = bind::action_for(bind::PAD, b, 1);
        // dialogue: the mapped skip (Start) / fast-forward (Y) buttons, as the chips show (talkctl.cpp)
        if (screens::talkctl_shown() && (ui == "ui_skip" || ui == "ui_ffwd")) { if (down) screens::action(ui == "ui_skip" ? "tskip" : "tffwd"); return; }
        int c = ui == "ui_confirm" ? SDL_GAMEPAD_BUTTON_SOUTH : ui == "ui_back" ? SDL_GAMEPAD_BUTTON_EAST
              : ui == "ui_tabl" ? SDL_GAMEPAD_BUTTON_LEFT_SHOULDER : ui == "ui_tabr" ? SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER
              : ui == "ui_subl" ? 102 : ui == "ui_subr" ? 103 : -1;
        if (b == SDL_GAMEPAD_BUTTON_START || (b >= SDL_GAMEPAD_BUTTON_DPAD_UP && b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) c = b;
        if (c >= SDL_GAMEPAD_BUTTON_DPAD_UP && c <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) { if (down) menu_dir(c - SDL_GAMEPAD_BUTTON_DPAD_UP, DIR_DPAD); }
        else if (c >= 0) screens::pad_button(c, down);
        return;
    }
    const state::Snapshot &s = state::get();
    bool free_field = s.in_field && s.forms.empty() && !s.talk.active;
    bool dpad = b >= SDL_GAMEPAD_BUTTON_DPAD_UP && b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    if (dpad && !free_field) {
        // in the original's own forms / dialogue the D-pad is a direction pad (their lists and cursors use it)
        static bool held[4];
        held[b - SDL_GAMEPAD_BUTTON_DPAD_UP] = down;
        int k = held[0] ? K_UP : held[1] ? K_DOWN : held[2] ? K_LEFT : held[3] ? K_RIGHT : 0;
        g_pad_dir_key = k;
        set_direction(k);
        return;
    }
    if (!down) return;
    // the original's own windows and dialogue: the mapped menu confirm / back (Start always closes)
    bool busy = !s.forms.empty() || s.talk.active;
    std::string ui = bind::action_for(bind::PAD, b, 1);
    if (busy && ui == "ui_confirm") { tap(K_OK); return; }
    if (busy && (ui == "ui_back" || b == SDL_GAMEPAD_BUTTON_START)) { tap(K_BACK); return; }
    pad_code(b);
}

static void pad_axis(int axis, int v) {
    static bool lt, rt;
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        bool &st = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? lt : rt;
        int code = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? bind::PAD_LT : bind::PAD_RT;
        bool on = v > 16000;
        if (on != st) {
            st = on;
            if (code == bind::get(bind::PAD, "setb")) g_lb_held = on;
            if (on) {
                g_dev = is_ps(g_pad) ? PS : XBOX;
                if (g_capture) { capture(bind::PAD, code); return; }
                if (in_modern_menu()) {
                    std::string ui = bind::action_for(bind::PAD, code, 1);
                    if (ui.empty()) screens::pad_button(axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 102 : 103, true);
                    else pad_button(code, true); // a trigger mapped to a menu action
                }
                else pad_code(code);
            }
        }
    }
}

// per frame: left stick -> 4-way direction (the original's key movement is 4-directional)
void update() {
    // a modern screen owns navigation: the original must not keep a walking direction held underneath it (it would
    // never see the release, and its own forms then ignore other keys such as close)
    if (in_modern_menu() && g_dir_held) { g_pad_dir_key = 0; for (bool &h : g_held) h = false; set_direction(0); }
    if (!g_pad) return;
    // right stick: scrolls the active screen's box (help, notices, lists), faster the further it is pushed
    static Uint64 last_ms;
    Uint64 now_ms = SDL_GetTicks();
    float dt = last_ms ? std::min(0.05f, (now_ms - last_ms) / 1000.f) : 0.f;
    last_ms = now_ms;
    float ry = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.f;
    if (in_modern_menu() && std::fabs(ry) > 0.25f) screens::scroll_active(ry * std::fabs(ry) * 900.f * dt);
    if (getenv("EH_SCROLLDBG") && std::fabs(ry) > 0.25f) ui_log("[scroll] ry %.2f pad %s", ry, SDL_GetGamepadName(g_pad));
    float x = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
    float y = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
    static int stick_key;
    int k = 0;
    if (std::sqrt(x * x + y * y) > 0.45f) {
        if (std::fabs(x) > std::fabs(y)) k = x > 0 ? K_RIGHT : K_LEFT;
        else k = y > 0 ? K_DOWN : K_UP;
    }
    if (k != stick_key) {
        if (k) { SDL_GamepadType t = SDL_GetGamepadType(g_pad); g_dev = (t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5) ? PS : XBOX; }
        stick_key = k;
        if (in_modern_menu()) { menu_dir(k == K_UP ? 0 : k == K_DOWN ? 1 : k == K_LEFT ? 2 : k == K_RIGHT ? 3 : -1, DIR_STICK); return; }
        if (!g_pad_dir_key) set_direction(k);
    }
}

// ------------------------------------------------------------------------------------------------ vibration
#ifdef __ANDROID__
// phone vibrator: EpicActivity.onUnhandledMessage(0x8001, ms | amplitude << 16) (SDL_SendAndroidMessage; ms 0 cancels)
static void phone_vibrate(int ms, int amp) { SDL_SendAndroidMessage(0x8001, (ms & 0xffff) | ((amp & 0xff) << 16)); }
#endif
// UNVERIFIED: the original's durations are tuned for a phone motor (short taps); a pad motor needs some time to
// spin up, so pad pulses last at least 60 ms
void vibrate(int ms) {
    int st = settings::get().vib_strength;
    if (getenv("EH_VIBDBG")) ui_log("[vib] request %d ms, strength %d, pad %p dev %d", ms, st, (void *)g_pad, (int)g_dev);
    if (ms > 0 && st <= 0) return;
    float k = st / 100.f;
    if (g_pad && g_dev != TOUCH) {
        if (ms <= 0) { SDL_RumbleGamepad(g_pad, 0, 0, 0); return; }
        Uint16 lo = (Uint16)(0xFFFF * k), hi = (Uint16)(0xC000 * k);
        SDL_RumbleGamepad(g_pad, lo, hi, (Uint32)std::max(ms, 60));
        return;
    }
#ifdef __ANDROID__
    if (g_dev == TOUCH) phone_vibrate(ms > 0 ? ms : 0, std::max(1, (int)(255 * k)));
#endif
}

// ------------------------------------------------------------------------------------------------ events
bool handle_event(SDL_Event &e) {
    Rml::Context *ctx = ui::context;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED: pad_open(e.gdevice.which); return true;
    case SDL_EVENT_GAMEPAD_REMOVED: {
        // the handle may already be gone from SDL's lookup: match the active pad by its id
        bool active = g_pad && SDL_GetGamepadID(g_pad) == e.gdevice.which;
        g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), e.gdevice.which), g_recent.end());
        SDL_Gamepad *p = active ? g_pad : SDL_GetGamepadFromID(e.gdevice.which);
        if (p) SDL_CloseGamepad(p);
        if (active) {
            g_pad = nullptr;
            // the most recently used pad still connected takes over the prompts
            for (auto it = g_recent.rbegin(); it != g_recent.rend() && !g_pad; ++it)
                if (*it != e.gdevice.which) g_pad = SDL_GetGamepadFromID(*it);
            if (g_pad) g_dev = is_ps(g_pad) ? PS : XBOX;
            else
                g_dev =
#ifdef __ANDROID__
                    TOUCH;
#else
                    KBM;
#endif
            set_direction(0);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: pad_active(e.gbutton.which); pad_button(e.gbutton.button, true); return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP: pad_button(e.gbutton.button, false); return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs((int)e.gaxis.value) > 16000) pad_active(e.gaxis.which);
        pad_axis(e.gaxis.axis, e.gaxis.value);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which == SDL_TOUCH_MOUSEID) return true;
        g_dev = KBM;
        g_kb_typed = false;
        ctx->ProcessMouseMove((int)e.button.x, (int)e.button.y, RmlSDL::GetKeyModifierState());
        if (e.button.button == SDL_BUTTON_LEFT && !ui_wants_pointer()) {
            g_game_pointer = game_touch(1, e.button.x, e.button.y);
            return true;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID) return true;
        if (g_game_pointer) {
            game_touch(4, e.motion.x, e.motion.y);
            ctx->ProcessMouseMove((int)e.motion.x, (int)e.motion.y, RmlSDL::GetKeyModifierState());
            return true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.which == SDL_TOUCH_MOUSEID) return true;
        if (g_game_pointer && e.button.button == SDL_BUTTON_LEFT) {
            g_game_pointer = false;
            game_touch(2, e.button.x, e.button.y);
            return true;
        }
        break;
    case SDL_EVENT_FINGER_DOWN: {
        g_dev = TOUCH;
        g_kb_typed = false;
        float x = e.tfinger.x * ui::win_w, y = e.tfinger.y * ui::win_h;
        {
            const state::Snapshot &s = state::get();
            bool hud = s.in_field && s.forms.empty() && !s.talk.active && !in_modern_menu(), field = hud && s.vpad;
            // the stick zone moves with the pad (options: V-Pad position, settings.h)
            float pdx = settings::vpad_dx_vh() / 100 * ui::win_h, pdy = settings::vpad_dy_vh() / 100 * ui::win_h;
            if (field && !g_stick_active && x < ui::win_w * 0.38f + pdx && y > ui::win_h * 0.42f - pdy) {
                ctx->ProcessMouseMove((int)x, (int)y, 0);
                if (!ui_wants_pointer()) { // not on a HUD control
                    // the pad stays put (the original's dpad is fixed): the direction counts from its centre at once
                    g_stick_finger = e.tfinger.fingerID;
                    g_stick_active = true;
                    g_stick_cx = VPAD_CX * ui::win_h + pdx;
                    g_stick_cy = ui::win_h - VPAD_CY * ui::win_h - pdy;
                    g_stick_x = x;
                    g_stick_y = y;
                    stick_update();
                    return true;
                }
            }
            if (hud) {
                ctx->ProcessMouseMove((int)x, (int)y, 0);
                for (Rml::Element *h = ctx->GetHoverElement(); h; h = h->GetParentNode())
                    if (h->HasAttribute("data-press")) {
                        for (auto &p : g_press)
                            if (!p.el) { p.id = e.tfinger.fingerID; p.el = h; h->SetClass("press", true); break; }
                        ui_action(h->GetAttribute<Rml::String>("data-act", "").c_str());
                        ctx->ProcessMouseMove(-1000, -1000, 0);
                        return true;
                    }
            }
        }
        if (g_finger_active) return true;
        g_finger = e.tfinger.fingerID;
        g_finger_active = true;
        ctx->ProcessMouseMove((int)x, (int)y, 0);
        if (!ui_wants_pointer()) { g_game_pointer = game_touch(1, x, y); return true; }
        g_scroll_el = scrollable_at(ctx->GetHoverElement());
        g_scroll_y0 = y;
        g_scroll_top0 = g_scroll_el ? g_scroll_el->GetScrollTop() : 0;
        g_scrolling = false;
        ctx->ProcessMouseButtonDown(0, 0);
        return true;
    }
    case SDL_EVENT_FINGER_MOTION: {
        if (g_stick_active && e.tfinger.fingerID == g_stick_finger) {
            g_stick_x = e.tfinger.x * ui::win_w;
            g_stick_y = e.tfinger.y * ui::win_h;
            stick_update();
            return true;
        }
        if (!g_finger_active || e.tfinger.fingerID != g_finger) return true;
        float x = e.tfinger.x * ui::win_w, y = e.tfinger.y * ui::win_h;
        if (g_game_pointer) game_touch(4, x, y);
        if (!g_game_pointer && g_scroll_el && (g_scrolling || std::fabs(y - g_scroll_y0) > ui::win_h * 0.02f)) {
            // a drag scrolls, it doesn't click: the pointer leaves at once, else the rows sliding under the finger take
            // the hover look (the same as the focus) one after another
            if (!g_scrolling) ctx->ProcessMouseMove(-1000, -1000, 0);
            g_scrolling = true;
            g_scroll_el->SetScrollTop(g_scroll_top0 - (y - g_scroll_y0));
            return true;
        }
        ctx->ProcessMouseMove((int)x, (int)y, 0);
        return true;
    }
    case SDL_EVENT_FINGER_UP: {
        if (g_stick_active && e.tfinger.fingerID == g_stick_finger) {
            g_stick_active = false;
            g_stick_key = 0;
            set_direction(0);
            return true;
        }
        for (auto &p : g_press)
            if (p.el && p.id == e.tfinger.fingerID) { p.el->SetClass("press", false); p.el = nullptr; return true; }
        if (!g_finger_active || e.tfinger.fingerID != g_finger) return true;
        float x = e.tfinger.x * ui::win_w, y = e.tfinger.y * ui::win_h;
        g_finger_active = false;
        if (g_game_pointer) { g_game_pointer = false; game_touch(2, x, y); }
        else {
            ctx->ProcessMouseButtonUp(0, 0);
            ctx->ProcessMouseMove(-1000, -1000, 0); // a lifted finger hovers nothing (no hover look left behind)
        }
        g_scroll_el = nullptr;
        g_scrolling = false;
        ctx->ProcessMouseLeave();
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
        if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
            if (!e.key.repeat) ui_toggle_fullscreen();
            return true;
        }
        if (key_event(e.key, true)) return true;
        break;
    case SDL_EVENT_KEY_UP:
        if (key_event(e.key, false)) return true;
        break;
    }
    return !RmlSDL::InputEventHandler(ctx, ui::window, e);
}

// test driver (EH_CTRL "ui <cmd>"): click x y (window px) | key <sdl name> | pad <button> [up] | dev kbm|xbox|ps|touch
// test introspection: the snapshot the screens draw from (state.h), as text
static std::string strip_tags(const std::string &s) {
    std::string o;
    bool tag = false;
    for (char c : s) { if (c == '<') tag = true; else if (c == '>') tag = false; else if (!tag) o += c; }
    return o;
}
static void dump_state(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    const state::Snapshot &s = state::get();
    fprintf(f, "gm_state %d stripped %d script %d in_field %d field '%s'\n", s.gm_state, s.stripped, s.script, s.in_field, s.field_name.c_str());
    fprintf(f, "player '%s' lv %d hp %d/%d mp %d/%d gold %d gp %d sp %d bag %d/%d full %d\n", s.name.c_str(), s.level, s.hp, s.maxhp, s.mp, s.maxmp, s.gold, s.gp, s.sp, s.bag_cap > 0 ? (int)s.menu.items.size() : 0, s.bag_cap, s.bag_full);
    fprintf(f, "map %dx%d group %d fade %d/%d view %.0fx%.0f\n", s.map_w, s.map_h, s.field_group, s.fade, s.fade_max, ui::game_rect.w, ui::game_rect.h);
    fprintf(f, "forms:");
    for (auto &fm : s.forms) fprintf(f, " 0x%x%s", fm.id, fm.focused ? "*" : "");
    fprintf(f, " (top 0x%x)\n", s.top_form);
    fprintf(f, "fast button %d on %d locked %d\n", s.fast_button, s.fast_on, s.fast_locked);
    if (s.talk.active) fprintf(f, "talk kind %d name '%s' waiting %d reveal %.2f title '%s' text '%s'\n", s.talk.kind, s.talk.name.c_str(), s.talk.waiting, s.talk.reveal, s.talk.title.c_str(), strip_tags(s.talk.text).c_str());
    for (auto &t : s.toasts) fprintf(f, "toast kind %d at %d,%d %dx%d flags %x '%s'\n", t.kind, t.x, t.y, t.w, t.h, t.flags, strip_tags(t.text).c_str());
    const state::Dialog &d = s.dialog;
    if (d.id) {
        fprintf(f, "dialog form 0x%x kind %d mode %d title '%s' subtitle '%s'\n  body '%s'\n", d.id, d.kind, d.mode, strip_tags(d.title).c_str(), d.subtitle.c_str(), strip_tags(d.body).c_str());
        for (size_t i = 0; i < d.items.size(); i++) fprintf(f, "  item %d%s '%s'\n", (int)i, d.items[i].off ? " (off)" : "", strip_tags(d.items[i].text).c_str());
        for (size_t i = 0; i < d.buttons.size(); i++) fprintf(f, "  button %d '%s'\n", (int)i, d.buttons[i].c_str());
        for (auto &r : d.rows) fprintf(f, "  row '%s' icon %s\n", r.text.c_str(), r.icon.empty() ? "-" : "yes");
        if (d.id == 0xf) fprintf(f, "  count %d / %d\n", d.count, d.count_max);
    }
    if (s.qbox.ptr) fprintf(f, "qbox state %d result %d win %d box '%s'\n", s.qbox.state, s.qbox.result, s.qbox.win, s.qbox.box.c_str());
    if (s.menu.mdi) fprintf(f, "menu tab %d items %d inv_sel %d\n", s.menu.tab, (int)s.menu.items.size(), s.menu.inv_sel);
    if (s.menu.mdi && s.menu.tab == 1) for (size_t i = 0; i < s.menu.items.size(); i++) fprintf(f, "  item %d: type %d id %d %s\n", (int)i, s.menu.items[i].type, s.menu.items[i].id, s.menu.items[i].name.c_str()); // test: bag rows
    fclose(f);
}

void test_command(const char *line) {
    float x = 0, y = 0;
    char cmd[32] = "", arg[32] = "";
    sscanf(line, "%31s", cmd);
    if (!strcmp(cmd, "click")) {
        sscanf(line, "%*s %f %f", &x, &y);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = x; e.motion.y = y;
        ui::context->ProcessMouseMove((int)x, (int)y, 0);
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y; e.button.down = true;
        if (!handle_event(e)) ui::context->ProcessMouseButtonDown(0, 0);
        e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.down = false;
        if (!handle_event(e)) ui::context->ProcessMouseButtonUp(0, 0);
    } else if (!strcmp(cmd, "key") || !strcmp(cmd, "keyup")) {
        sscanf(line, "%*s %31s", arg);
        SDL_Event e{};
        e.type = !strcmp(cmd, "key") ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = SDL_GetKeyFromName(arg);
        e.key.down = e.type == SDL_EVENT_KEY_DOWN;
        if (!handle_event(e)) RmlSDL::InputEventHandler(ui::context, ui::window, e);
    } else if (!strcmp(cmd, "pad")) {
        int b = 0; char up[8] = "";
        sscanf(line, "%*s %d %7s", &b, up);
        if (b >= 200) pad_axis(b - 200, up[0] ? 0 : 32767);
        else pad_button(b, up[0] == 0);
    } else if (!strcmp(cmd, "dumpui")) { // test: ui dumpui <file> -> the shown element trees + layout problems (screens.cpp)
        char path[512] = "";
        sscanf(line, "%*s %511s", path);
        screens::dump_ui(path);
    } else if (!strcmp(cmd, "geom")) { // test: ui geom <file> -> frame / interface / touch geometry checks (screens.cpp)
        char path[512] = "";
        sscanf(line, "%*s %511s", path);
        screens::dump_geom(path);
    } else if (!strcmp(cmd, "cover")) { // test: ui cover <file> -> unpainted pixels of the last frame (EH_SENTINEL=1, capture.cpp)
        char path[512] = "";
        sscanf(line, "%*s %511s", path);
        capture::cover_dump(path);
    } else if (!strcmp(cmd, "resize")) { // test: ui resize <w> <h> -> the window's size (logical), as a user would drag it
        int w = 0, h = 0;
        sscanf(line, "%*s %d %d", &w, &h);
        if (w > 0 && h > 0) { SDL_SetWindowSize(ui::window, w, h); SDL_SyncWindow(ui::window); }
    } else if (!strcmp(cmd, "dumpstate")) { // test: ui dumpstate <file> -> what the modern UI read from the game this frame
        char path[512] = "";
        sscanf(line, "%*s %511s", path);
        dump_state(path);
    } else if (!strcmp(cmd, "dev")) {
        sscanf(line, "%*s %31s", arg);
        // the original opens the crawl and the world map only from field scripts (cflScript, script cmds 0xF1 / 0xA7),
        // so a loaded field is their precondition: cGameWorld::Draw @0012e574 (game state 4, OpenUI(0)) reads the object
        // manager world+0x650 without a check (cObjectManager::DrawCheckProcess @000cf940 reads its +0x14), and
        // cFormWorldMap::IslandPanelInitialize @000fdc6c reads *gp_QuestMgrInstance and gp_GameWorldIntance's island flags;
        // on the title none of them exist yet
        auto in_field = [] {
            uint32_t w = g::world(), gm = g::gm();
            bool ok = w && M32(w + 0x650) && g::questmgr() && gm && (M32(gm + 0xc) == 3 || M32(gm + 0xc) == 4) && g::player();
            if (!ok) ui_log("[dev] not in a field: load a save first");
            return ok;
        };
        if (!strcmp(arg, "msg")) { // test: ui dev msg <flags> <pos> <text> -> cGameWorld::CreateMessageBox @0012c838
            unsigned fl = 2, pos = 0x20000; char txt[200] = "";
            sscanf(line, "%*s %*s %x %x %199[^\n]", &fl, &pos, txt);
            std::string t = txt;
            state::post([fl, pos, t] {
                uint32_t w = g::world();
                if (!w) return;
                char *gs = gas_strdup(t.c_str());
                g::call(0x11c838, {w, fl, 0, 1, pos, (uint32_t)HPTR(gs), 1, 0});
            });
            return;
        }
        if (!strcmp(arg, "rain")) { // test: ui dev rain 1|0 -> cGameWorld::SetWeatherEffect(0, on, 0) @0012af00 (as the scripts start it)
            int on = 1; sscanf(line, "%*s %*s %d", &on);
            state::post([on, in_field] {
                if (in_field()) g::call(0x11af00, {g::world(), 0, (uint32_t)on, 0});
            });
            return;
        }
        if (!strcmp(arg, "crawl")) { // test: the prologue crawl as script cmd 0xF1 opens it (cUICommand::InitImpl @000df200):
            // cGameUi::ShowUIWnd(0x24) @0011eea8, vtable +0x54, cFormTextScroller::Finalize @00102760 (delay 1 s, hold 2 s,
            // step 1 px / 50 ms), without the script callback
            state::post([in_field] {
                uint32_t ui = g::gameui();
                if (!ui || !in_field()) return;
                uint32_t f = g::call(0x10eea8, {ui, 0x24, 0, 0});
                if (!f) return;
                g::call(M32(M32(f) + 0x54) - IMG, {f}); // vtable entries are guest code addresses (IMG + vaddr | 1)
                std::string t;
                for (int i = 1; i <= 24; i++) t += "Crawl test line " + std::to_string(i) + "\n";
                char *gs = gas_strdup(t.c_str());
                g::call(0xf2760, {f, (uint32_t)HPTR(gs), 0, 0, 0, 102, 5001, 0});
                if (g::gm()) g::call(0x116b34, {g::gm(), 0}); // cGameManager::OpenUI(0) @00126b34: UI state 4 runs the form
            });
            return;
        }
        if (!strcmp(arg, "bagslot")) { // test: ui dev bagslot <n> -> cBackPack::IncreaseSlot(n) @0012f7a0 (limit +0x18 raised to fit)
            int n = 10; sscanf(line, "%*s %*s %d", &n);
            state::post([n] { uint32_t inv = g::inv(); if (!inv) return; if ((int32_t)M32(inv + 0x18) < (int32_t)M32(inv + 0x1c) + n) M32(inv + 0x18) = M32(inv + 0x1c) + n; g::call(0x11f7a0, {inv, (uint32_t)n}); });
            return;
        }
        if (!strcmp(arg, "count")) { // test: ui dev count <type> <id> -> cInventory::GetDuplicateItemCount @0012ff0c, bag use @0012f824
            int t = 0, id = 0; sscanf(line, "%*s %*s %d %d", &t, &id);
            state::post([t, id] { uint32_t inv = g::inv(); if (inv) ui_log("[dev] count %d %d = %d (bag %d)", t, id, (int)g::call(0x11ff0c, {inv, (uint32_t)t, (uint32_t)id}), (int)g::call(0x11f824, {inv})); });
            return;
        }
        if (!strcmp(arg, "islands")) { // test: ui dev islands -> GetIslandInfo(i) @000dc694 (+0 name) for res+0x284 islands, unlocked = IsIslandFlag
            state::post([] {
                uint32_t rm = g::resmgr(), w = g::world(); if (!rm || !w) return;
                for (int i = 0; i < (int32_t)M32(rm + 0x284) && i < 64; i++) {
                    uint32_t ii = g::call(0xcc694, {rm, (uint32_t)i});
                    if (ii) ui_log("[islands] %d %s open %d", i, g::plain_text(g::cstr(M32(rm + 0x34) + M32(ii))).c_str(), (M32(w + 0x128 + 4 * (i >> 5)) >> (i & 31)) & 1);
                }
            });
            return;
        }
        if (!strcmp(arg, "links")) { // test: ui dev links -> GetLinkStoneInfo(i) @000dc6b8 records (res+0x27c): +0 name, +0x10 desc, raw words
            state::post([] {
                uint32_t rm = g::resmgr(); if (!rm) return;
                for (int i = 0; i < (int32_t)M32(rm + 0x27c) && i < 64; i++) {
                    uint32_t li = g::call(0xcc6b8, {rm, (uint32_t)i});
                    if (li) ui_log("[links] %d %s | %08x %08x %08x %08x %08x %08x", i, g::plain_text(g::cstr(M32(rm + 0x34) + M32(li))).c_str(),
                                   M32(li + 4), M32(li + 8), M32(li + 0xc), M32(li + 0x10), M32(li + 0x14), M32(li + 0x18));
                }
            });
            return;
        }
        if (!strcmp(arg, "qboxtab")) { // test: ui dev qboxtab <box> <groups> -> GetQBoxProbInfo(box, g) @000dc5e8 groups (u16 +4 records,
            // 12-byte entries: +4 reward, +8/+0xA roll range of 1000) with GetQBoxRewardInfo @000dc4a8 (+8 type, +0xA, +0xC id, +0xE n)
            int bx = 0, ng = 24; sscanf(line, "%*s %*s %d %d", &bx, &ng);
            state::post([bx, ng] {
                uint32_t rm = g::resmgr(); if (!rm) return;
                ui_log("[qbox] box %d table %x rewards %d", bx, M32(rm + 0x25c + 4 * bx), (int)M32(rm + 0x1b0));
                for (int gi = 0; gi < ng; gi++) {
                    uint32_t h = g::call(0xcc5e8, {rm, (uint32_t)bx, (uint32_t)gi});
                    int n = M16(h + 4);
                    std::string line = Rml::CreateString("[qbox] box %d group %d n %d hdr %08x %08x %08x:", bx, gi, n, M32(h), M32(h + 4), M32(h + 8));
                    for (int e = 1; e < n && e < 40; e++) {
                        uint32_t en = h + 0xc * e;
                        int ri = M16(en + 4), lo = M16(en + 8), hi = M16(en + 0xa);
                        uint32_t r = g::call(0xcc4a8, {rm, (uint32_t)ri});
                        std::string nm;
                        if (r) { int ty = M8(r + 8), id = (int16_t)M16(r + 0xc); uint32_t ii = ty < 100 ? g::call(0xcc3b4, {rm, (uint32_t)ty, (uint32_t)id, 0}) : 0;
                            nm = ii ? g::plain_text(g::cstr(M32(rm + 0x34) + M32(ii))) : std::to_string(ty);
                            line += Rml::CreateString(" [%d-%d %d:%d/%d x%d g%d %s]", lo, hi, ri, ty, id, M16(r + 0xe), ii ? M8(ii + 0x19) : -1, nm.c_str()); }
                    }
                    ui_log("%s", line.c_str());
                }
            });
            return;
        }
        if (!strcmp(arg, "qboxrw")) { // test: ui dev qboxrw -> every GetQBoxRewardInfo @000dc4a8 record of grade >= 3, and the reel stand-ins
            // (GetQBoxRewardTempInfo @000dc4d0, u16 +0 reward index) of grade >= 3
            state::post([] {
                uint32_t rm = g::resmgr(); if (!rm) return;
                auto desc = [rm](int ri) {
                    uint32_t r = g::call(0xcc4a8, {rm, (uint32_t)ri}); if (!r) return std::string();
                    int ty = M8(r + 8), id = (int16_t)M16(r + 0xc); uint32_t ii = ty < 100 ? g::call(0xcc3b4, {rm, (uint32_t)ty, (uint32_t)id, 0}) : 0;
                    if (!ii || M8(ii + 0x19) < 3) return std::string();
                    return Rml::CreateString("%d:%d/%d g%d %s", ri, ty, id, M8(ii + 0x19), g::plain_text(g::cstr(M32(rm + 0x34) + M32(ii))).c_str());
                };
                for (int i = 0; i < (int32_t)M32(rm + 0x1b0); i++) { std::string d = desc(i); if (!d.empty()) ui_log("[qboxrw] reward %s", d.c_str()); }
                for (int i = 0; i < (int32_t)M32(rm + 0x1b8); i++) { uint32_t t = g::call(0xcc4d0, {rm, (uint32_t)i}); std::string d = t ? desc(M16(t)) : std::string(); if (!d.empty()) ui_log("[qboxrw] reel %d %s", i, d.c_str()); }
                ui_log("[qboxrw] rewards %d reels %d", (int)M32(rm + 0x1b0), (int)M32(rm + 0x1b8));
            });
            return;
        }
        if (!strcmp(arg, "socket")) { // test: ui dev socket <bag index | -1 newest | -10-slot equipped> <n> -> EquipInstData u8 +0x1A = n (what cItem::generateSocketCount
            // @0010f8e4 returns for a new item, at most 2) on the bag's n-th item
            int bi = 0, n = 2; sscanf(line, "%*s %*s %d %d", &bi, &n);
            state::post([bi, n] {
                uint32_t inv = g::inv(); if (!inv) return;
                uint32_t it = M32(inv + 4);
                if (bi <= -10) { uint32_t pl = g::player(); it = pl ? g::call(0xb0b54, {pl, (uint32_t)(-10 - bi)}) : 0; } // -10 - slot: an equipped item (cCharacter::GetEquipmentItem @000c0b54)
                else if (bi < 0) while (it && M32(it + 0x48)) it = M32(it + 0x48); // -1: the newest (last) bag item
                else for (int k = 0; it && k < bi; k++) it = M32(it + 0x48);
                uint32_t ed = it && M16(it + 6) < 5 ? g::call(0xfe508, {it}) : 0;
                if (ed) { M8(ed + 0x1a) = (uint8_t)n; ui_log("[dev] socket %x = %d", it, n); }
            });
            return;
        }
        if (!strcmp(arg, "procs")) { // test: ui dev procs -> the player's process chain (cProcessManager pl+0x48: list +4, next +4; bot.c
            // movetest) with each process's type +0xC, and the boarding object pl+0x43C (cCharacter::SetBoardingObj @000c1124)
            state::post([] {
                uint32_t pl = g::player(); if (!pl) return;
                uint32_t pm = M32(pl + 0x48);
                std::string l;
                for (uint32_t pr = pm ? M32(pm + 4) : 0, q = 0; pr && q < 12; pr = M32(pr + 4), q++) {
                    uint32_t o = M32(pr);
                    l += Rml::CreateString(" %06x/%x", o ? M32(o) - IMG : 0, o ? M32(o + 0xc) : 0);
                }
                ui_log("[procs] pl %x vt %06x board %x procs:%s", pl, M32(pl) - IMG, M32(pl + 0x43c), l.c_str());
            });
            return;
        }
        if (!strcmp(arg, "stat")) { // test: ui dev stat <0 str | 1 dex | 2 int> <n> -> invested s16 pl+0x3C6+2i += n, then the stat recompute
            // vfunc 0x138 (as the menu's GP button: cFormGpInvest::DoInvest @0011cf40, cFormCharStat::RefreshStat @0011c414)
            int i = 0, n = 0; sscanf(line, "%*s %*s %d %d", &i, &n);
            state::post([i, n] { uint32_t pl = g::player(); if (!pl || i < 0 || i > 2) return; M16(pl + 0x3c6 + 2 * i) = (uint16_t)(M16(pl + 0x3c6 + 2 * i) + n); g::call((M32(M32(pl) + 0x138) - IMG) & ~1u, {pl}); });
            return;
        }
        if (!strcmp(arg, "wearlast")) { // test: ui dev wearlast -> cCharacter::EquipmentItem(pl, type, the newest bag item, 0, 0) @000c3548
            state::post([] {
                uint32_t inv = g::inv(), pl = g::player(); if (!inv || !pl) return;
                uint32_t it = M32(inv + 4);
                while (it && M32(it + 0x48)) it = M32(it + 0x48);
                if (it && M16(it + 6) < 5) { g::call(0xb3548, {pl, M16(it + 6), it, 0, 0}); ui_log("[dev] wear %x type %d", it, M16(it + 6)); }
            });
            return;
        }
        if (!strcmp(arg, "sockets")) { // test: ui dev sockets -> bag equipment (type < 5) with cItem::GetEquipInstData @0010e508 sockets u8 +0x1A
            // and the stones in +0x1C.. (tagITEM_INITINFO*, cItem::EquipPStone @0010e870)
            state::post([] {
                uint32_t inv = g::inv(), rm = g::resmgr(); if (!inv || !rm) return;
                int k = 0;
                for (uint32_t it = M32(inv + 4); it; it = M32(it + 0x48), k++) {
                    if (M16(it + 6) >= 5) continue;
                    uint32_t ed = g::call(0xfe508, {it}); if (!ed) continue;
                    std::string st;
                    for (int j = 0; j < M8(ed + 0x1a) && j < 8; j++) { uint32_t gi = M32(ed + 0x1c + 4 * j); st += gi ? " [" + g::plain_text(g::cstr(M32(rm + 0x34) + M32(gi))) + "]" : " [-]"; }
                    ui_log("[sockets] bag %d %x %s sockets %d:%s", k, it, g::plain_text(g::cstr(g::call(0xfed68, {it}))).c_str(), M8(ed + 0x1a), st.c_str());
                }
            });
            return;
        }
        if (!strcmp(arg, "drop")) { // test: ui dev drop <type> <class> <id> -> cGameWorld::CreateDropItem(player, type, class, id)
            // @0012c158 (what script op 0xC7 does with a chest's drop table, e.g. script 58228)
            int t = 0, c = 0, id = 0; sscanf(line, "%*s %*s %d %d %d", &t, &c, &id);
            state::post([t, c, id] { uint32_t w = g::world(), pl = g::player(); if (w && pl) g::call(0x11c158, {w, pl, (uint32_t)t, (uint32_t)c, (uint32_t)id}); });
            return;
        }
        if (!strcmp(arg, "items")) { // test: ui dev items <type> -> every item definition of a type (GetItemInfo @000dc3b4)
            int t = 0; sscanf(line, "%*s %*s %d", &t);
            state::post([t] {
                uint32_t rm = g::resmgr(); if (!rm) return;
                for (int id = 0; id < 1000; id++) {
                    uint32_t info = g::call(0xcc3b4, {rm, (uint32_t)t, (uint32_t)id, 0});
                    if (info) ui_log("[items] type %d id %d lv %d %s", t, id, M8(info + 9), g::plain_text(g::cstr(M32(rm + 0x34) + M32(info))).c_str()); // req level u8 +9
                }
            });
            return;
        }
        if (!strcmp(arg, "grades")) { // test: ui dev grades <type> -> item definitions of a type whose grade (info +0x19,
            // what cItem::MakeName @0010fde0 colours by) is above 노멀; cResourceManager::GetItemInfo @000dc3b4
            int t = 0; sscanf(line, "%*s %*s %d", &t);
            state::post([t] {
                uint32_t rm = g::resmgr(); if (!rm) return;
                for (int id = 0; id < 400; id++) {
                    uint32_t info = g::call(0xcc3b4, {rm, (uint32_t)t, (uint32_t)id, 0});
                    if (info && M8(info + 0x19)) ui_log("[grades] type %d id %d grade %d %s", t, id, M8(info + 0x19), g::plain_text(g::cstr(M32(rm + 0x34) + M32(info))).c_str());
                }
            });
            return;
        }
        if (!strcmp(arg, "learntitle")) { // test: ui dev learntitle <i> -> cCharacter::LearnTitle(pl, i, 0) @000c29c8 (bit i of pl+0x354)
            int i = 0; sscanf(line, "%*s %*s %d", &i);
            state::post([i] { uint32_t pl = g::player(); if (pl) g::call(0xb29c8, {pl, (uint32_t)i, 0}); });
            return;
        }
        if (!strcmp(arg, "additem")) { // test: ui dev additem <type> <id> <count> <grade> -> cInventory::AddItem @001306ac
            int t = 0, id = 0, n = 1, gr = 0;
            sscanf(line, "%*s %*s %d %d %d %d", &t, &id, &n, &gr);
            state::post([t, id, n, gr] {
                uint32_t inv = g::inv();
                uint32_t it = inv ? g::call(0x1206ac, {inv, 1, (uint32_t)t, (uint32_t)id, (uint32_t)n, (uint32_t)gr, 0xffffffffu, 0}) : 0;
                if (inv) ui_log("[dev] additem %d %d x%d g%d -> %x %s", t, id, n, gr, it, it ? g::item_name_rml(it).c_str() : "");
            });
            return;
        }
        if (!strcmp(arg, "worldmap")) { // test: ui dev worldmap <linkstone> -> as script cmd 0xA7 (cflScript::MakeWorldMap @000e1674):
            // cGameUi::ShowWorldMap(ui, 0, stone, OnResultPumpCB @000de6d4, ctx) @0011f770 + OpenUI(0); ctx is a scratch block
            int id = 0; sscanf(line, "%*s %*s %d", &id);
            state::post([id, in_field] {
                uint32_t ui = g::gameui();
                if (!ui || !in_field()) return;
                static char *ctx = gas_strdup("................................................................");
                g::call(0x10f770, {ui, 0, (uint32_t)id, IMG + 0xce6d4 + 1, (uint32_t)HPTR(ctx)});
                if (g::gm()) g::call(0x116b34, {g::gm(), 0});
            });
            return;
        }
        // test: open original windows the modern menu never opens itself
        if (!strcmp(arg, "uiwnd")) { // ui dev uiwnd <id hex> <p2 hex> -> cGameUi::ShowUIWnd @0011eea8 + OpenUI(0) (as script cmd 0x99)
            unsigned id = 0, p2 = 0; sscanf(line, "%*s %*s %x %x", &id, &p2);
            state::post([id, p2] { uint32_t ui = g::gameui(); if (!ui) return; g::call(0x10eea8, {ui, id, p2, 0}); if (g::gm()) g::call(0x116b34, {g::gm(), 0}); });
            return;
        }
        if (!strcmp(arg, "detail")) { // ui dev detail <n>: cGameUi::ShowDetailPopup(ui, 0xB, 0, 0, the n-th bag item, 0, 0) @0011f450
            int n = 0; sscanf(line, "%*s %*s %d", &n);
            state::post([n] {
                uint32_t ui = g::gameui(), inv = g::inv();
                if (!ui || !inv) return;
                uint32_t it = 0;
                const state::Snapshot &sn = state::get(); // the bag page's items (the menu must be open on the bag)
                if (n < (int)sn.menu.items.size()) it = sn.menu.items[n].ptr;
                if (it) g::call(0x10f450, {ui, 0xb, 0, 0, it, 0, 0});
            });
            return;
        }
        if (!strcmp(arg, "invact")) { // ui dev invact <action hex>: cFormInvenItem::DoItemAction(bag page, action) @0010d344
            unsigned act = 0; sscanf(line, "%*s %*s %x", &act);
            uint32_t fi = state::get().menu.page_form[1];
            state::post([fi, act] { if (fi) g::call(0xfd344, {fi, act}); });
            return;
        }
        if (!strcmp(arg, "notice")) { // ui dev notice <kind>: ShowNoticePopup(ui, kind, 0, "", 0, 0, title, 0, the current quest) @0011f5dc
            int kind = 1; sscanf(line, "%*s %*s %d", &kind);
            state::post([kind] {
                uint32_t ui = g::gameui(), qm = g::questmgr();
                uint32_t q = qm ? M32(qm) : 0;
                if (kind == 6 && g::resmgr()) q = g::call(0xcc670, {g::resmgr(), 1}); // cResourceManager::GetCharTitleInfo
                if (kind == 1 && q && !M8(q + 9)) // a quest with rewards: cQuestManager::GetQuestByIndex(qm, group, i) @000d931c
                    for (int grp = 0; grp < 2 && !M8(q + 9); grp++)
                        for (int i = 0, n = (int)g::call(0xc92c8, {qm, (uint32_t)grp}); i < n; i++) {
                            uint32_t c = g::call(0xc931c, {qm, (uint32_t)grp, (uint32_t)i});
                            if (c && M8(c + 9)) { q = c; break; }
                        }
                if (!ui || !q) return;
                if (kind == 1 && !M8(q + 9)) { // test: a copy of the quest with three rewards (gold, exp, 3 potions)
                    uint32_t c = (uint32_t)HPTR(gas_calloc(0x100, 1));
                    for (int i = 0; i < 0x100; i += 4) M32(c + i) = M32(q + i);
                    M8(c + 9) = 3;
                    M16(c + 0x5c) = 100; M32(c + 0x60) = 500;
                    M16(c + 0x64) = 0x65; M32(c + 0x68) = 1200;
                    M16(c + 0x6c) = 6; M16(c + 0x70) = 3; M16(c + 0x72) = 0x18;
                    q = c;
                }
                ui_log("[dev] notice %d source %x (quest rewards %d)", kind, q, M8(q + 9));
                static char *empty = gas_strdup("");
                g::call(0x10f5dc, {ui, (uint32_t)kind, 0, (uint32_t)HPTR(empty), 0, 0, g::call(0x120aac, {0x1b8}), 0, q, 0});
            });
            return;
        }
        if (!strcmp(arg, "suicide")) { // test: cGameWorld::DoSuicide @0012a5ac (the help's rescue end)
            state::post([] { uint32_t w = g::world(); if (w) g::call(0x11a5ac, {w}); });
            return;
        }
        if (!strcmp(arg, "script")) { // test: ui dev script <id> -> cGameManager::FireScript(int) @00127858
            int id = 0; sscanf(line, "%*s %*s %d", &id);
            state::post([id] { if (g::gm()) g::call(0x117858, {g::gm(), (uint32_t)id}); });
            return;
        }
        if (!strcmp(arg, "extmove")) { // test: ui dev extmove <grp> <sub> <entry> <dir> -> cGameWorld::ExtendMoveMap @0012f548
            // (script op 0xA9, e.g. script 58201: 456 1 <island> 1 after the island-opened notice)
            int a = 456, b = 1, c = 1, d = 1; sscanf(line, "%*s %*s %d %d %d %d", &a, &b, &c, &d);
            state::post([a, b, c, d] { if (g::world()) g::call(0x11f548, {g::world(), (uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d}); });
            return;
        }
        if (!strcmp(arg, "minigame")) { // test: ui dev minigame <type> -> cGameWorld::CreateMbMinigame @0012c6c8 without a
            // script: cMbMinigame keeps the script at +0x60 and on success writes 1 to [script+0x24]+4
            // (ProcMsgBoxImpl @0014df78), so a zeroed stand-in is given; object ids 0 skip the NPC actions
            int type = 1; sscanf(line, "%*s %*s %d", &type);
            state::post([type, in_field] {
                if (!in_field()) return;
                uint32_t scp = g::call(0x150aec, {0x40}), res = g::call(0x150aec, {0x10}); // fl_ZiAlloc @00160aec (zeroed)
                M32(scp + 0x24) = res;
                g::call(0x11c6c8, {g::world(), scp, (uint32_t)type, 0, 0});
            });
            return;
        }
        if (!strcmp(arg, "warp")) { // test: ui dev warp <group> <sub> -> cGameManager::PendMoveField(gm, group, sub, 0) @00127884
            int grp = 0, sub = 0; sscanf(line, "%*s %*s %d %d", &grp, &sub);
            state::post([grp, sub] { if (g::gm()) g::call(0x117884, {g::gm(), (uint32_t)grp, (uint32_t)sub, 0}); });
            return;
        }
        if (!strcmp(arg, "chase")) { // test: ui dev chase 1|0 -> world flag 0x20000000 as cMonsterBossSalamander::InitState
            // @000bfbcc sets it: cActiveObj::MoveImpl @000cce00 then keeps the player inside the view (hooks.c logic box)
            int on = 1; sscanf(line, "%*s %*s %d", &on);
            state::post([on, in_field] {
                uint32_t w = g::world();
                if (in_field()) M32(w) = on ? M32(w) | 0x20000000u : M32(w) & ~0x20000000u;
            });
            return;
        }
        if (!strcmp(arg, "hurt")) { // test: ui dev hurt <pct> -> the boss in the gauge (xx_bui +0x128, cGameWorld::GetBossPtr
            // @00129358) loses pct % of its max HP through cCreature::SubHp(amount, isPercent) @000c421c
            int pct = 10; sscanf(line, "%*s %*s %d", &pct);
            state::post([pct, in_field] {
                uint32_t bui = in_field() ? M32(IMG + rc_sym("xx_bui")) : 0, boss = bui ? M32(bui + 0x128) : 0;
                if (boss) g::call(0x0b421c, {boss, (uint32_t)pct, 1});
                ui_log("[dev] hurt boss %08x by %d%%: hp %d", boss, pct, boss ? (int32_t)M32(boss + 0x94) : -1);
            });
            return;
        }
        if (!strcmp(arg, "where")) { // test: ui dev where -> log the player position, the camera scroll (cField +4 / +8) and view width
            state::post([] {
                uint32_t pl = g::player(), f = M32(IMG + 0x19af24u), gm = g::gm(); // gp_fieldInstance
                if (pl && f && gm)
                    ui_log("[where] player %d,%d scroll %d,%d view %d map %dx%d | world flags %08x field +0x70 %d pl flags +0x2c %08x proc %06x sys8 %08x | eng +4 %d +8 %x gm +0x210 %d +0x1fc %d", (int32_t)M32(pl + 0x14), (int32_t)M32(pl + 0x18), (int32_t)M32(f + 4),
                           (int32_t)M32(f + 8), (int32_t)M32(gm + 0x1e4), (int)M16(f + 0x78) * 16, (int)M16(f + 0x7a) * 16,
                           g::world() ? M32(g::world()) : 0, (int32_t)M32(f + 0x70), M32(pl + 0x2c), M32(pl + 0x48) && M32(M32(pl + 0x48) + 4) && M32(M32(M32(pl + 0x48) + 4)) ? M32(M32(M32(pl + 0x48) + 4)) - IMG : 0, M32(g::sysinfo() + 8), (int32_t)M32(g::call(0xd73c0, {}) + 4), M32(g::call(0xd73c0, {}) + 8), (int32_t)M32(gm + 0x210), (int32_t)M32(gm + 0x1fc)); // cScriptEngine::GetInstance @000e73c0
            });
            return;
        }
        if (!strcmp(arg, "pos")) { // test: ui dev pos <x> <y> -> the player's SetPos virtual (+0x18), the camera follows
            int x = 0, y = 0; sscanf(line, "%*s %*s %d %d", &x, &y);
            state::post([x, y] { uint32_t pl = g::player(); if (pl) g::call((M32(M32(pl) + 0x18) - IMG) & ~1u, {pl, (uint32_t)x, (uint32_t)y}); });
            return;
        }
        if (!strcmp(arg, "objs")) { // test: ui dev objs -> every cObjectManager (world+0x650) list entry with
            // GetObjectInfo 0..3 (cGameWorld::GetObjectInfo @00128c30: +0x3C, +0x3E, +0x42, virtual +0x94)
            state::post([] {
                uint32_t w = g::world(); if (!w) return;
                uint32_t om = M32(w + 0x650);
                for (int kind = 0; kind < 14; kind++) {
                    uint32_t L = M32(om + 0x14 + 4 * kind);
                    for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                        uint32_t o = M32(n + 4); if (!o) continue;
                        ui_log("[objs] list %d vt %06x type %d at %d,%d info %d %d %d %d", kind, M32(o) - IMG, (int16_t)M16(o + 6),
                               (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18), (int16_t)M16(o + 0x3C), (int16_t)M16(o + 0x3E),
                               (int16_t)M16(o + 0x42), (int32_t)g::call(0x118c30, {w, o, 3}));
                    }
                }
            });
            return;
        }
        if (!strcmp(arg, "trigger")) { // test: ui dev trigger <id> -> cGameWorld::fireSomethingEvent(player, obj)
            // @0012d024 on the list-5 object whose GetObjectInfo 3 is <id> (e.g. 202 in 78/1 = script 58201's island notice)
            int id = 0; sscanf(line, "%*s %*s %d", &id);
            state::post([id] {
                uint32_t w = g::world(), pl = g::player(); if (!w || !pl) return;
                uint32_t L = M32(M32(w + 0x650) + 0x14 + 4 * 5);
                for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    if (o && (int32_t)g::call(0x118c30, {w, o, 3}) == id) { ui_log("[dev] trigger %d", id); g::call(0x11d024, {w, pl, o}); return; }
                }
            });
            return;
        }
        if (!strcmp(arg, "door")) { // test: ui dev door <group> <sub> -> enter through the current field's exit to there
            int grp = 0, sub = 0; sscanf(line, "%*s %*s %d %d", &grp, &sub);
            state::post([grp, sub] {
                // exits: object list 5 of cObjectManager (world+0x650), type +6 == 0, destination +0x3C/+0x3E;
                // cGameWorld::fireSomethingEvent(0, exit) @0012d024 as walking into it (src/bot.c EH_WARPTO)
                uint32_t w = g::world();
                if (!w) return;
                uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 5);
                for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    if (o && (int16_t)M16(o + 6) == 0 && (int16_t)M16(o + 0x3c) == grp && (int16_t)M16(o + 0x3e) == sub) { g::call(0x11d024, {w, 0, o}); return; }
                }
                ui_log("[dev] no exit to %d/%d; exits here:", grp, sub);
                for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    if (o && (int16_t)M16(o + 6) == 0) ui_log("[dev]   exit to %d/%d", (int16_t)M16(o + 0x3c), (int16_t)M16(o + 0x3e));
                }
            });
            return;
        }
        if (!strcmp(arg, "fastbtn")) { // test: the original's script fast-forward button on (cGameWorld::ShowFastButton @00129558: world+0x768 = 1)
            // ShowFastButton itself leaves it off under the gm+0x1B4 0x4000 veto: set world+0x768 like it does
            state::post([] { if (g::world()) M32(g::world() + 0x768) = 1; });
            return;
        }
        if (!strcmp(arg, "dumpset")) { // test: ui dev dumpset <set> <dir> -> every image of a sprite set as <dir>/i<set>_<idx>.raw
            int set = 0; char dir[512] = "";
            sscanf(line, "%*s %*s %d %511s", &set, dir);
            std::string dd = dir;
            state::post([set, dd] {
                int n = 0;
                for (int i = 0; i < 2000; i++) {
                    std::string k = sprites::image(set, i);
                    int w, h;
                    const uint8_t *px = k.empty() ? nullptr : sprites::lookup(k.c_str(), &w, &h);
                    if (!px) continue;
                    FILE *f = fopen((dd + "/i" + std::to_string(set) + "_" + std::to_string(i) + ".raw").c_str(), "wb");
                    if (!f) continue;
                    uint32_t hd[2] = {(uint32_t)w, (uint32_t)h};
                    fwrite(hd, 4, 2, f);
                    fwrite(px, 4, (size_t)w * h, f);
                    fclose(f);
                    n++;
                }
                ui_log("[dev] dumped %d images of set %d", n, set);
            });
            return;
        }
        if (!strcmp(arg, "book")) { // test: ui dev book <i> -> book i owned (cStudyBook @00131c88 bitset +0x48, as LoadFromBinary @00140c98 reads it)
            int i = 0; sscanf(line, "%*s %*s %d", &i);
            state::post([i] { uint32_t sb = g::call(0x121c88, {}); if (sb && i >= 0 && i < 64) M32(sb + 0x48 + 4 * (i >> 5)) |= 1u << (i & 31); });
            return;
        }
        if (!strcmp(arg, "style")) { // test: ui dev style <file.rml> <id> <property> <value> -> one property on one element
            // (e.g. pad a popup past the 600 dp the screens always have, to try its scrolling)
            char file[64] = "", id[64] = "", prop[64] = "", val[128] = "";
            sscanf(line, "%*s %*s %63s %63s %63s %127[^\n]", file, id, prop, val);
            for (int i = 0; i < ui::context->GetNumDocuments(); i++) {
                Rml::ElementDocument *doc = ui::context->GetDocument(i);
                if (doc->GetSourceURL().find(file) == Rml::String::npos) continue;
                if (Rml::Element *e = doc->GetElementById(id)) { e->SetProperty(prop, val); ui_log("[dev] style %s #%s %s: %s", file, id, prop, val); }
            }
            return;
        }
        if (!strcmp(arg, "collect")) { // test: gather the nearest cCollectStuff (object list 9) the life skill allows, as
            // cIdleKeyProcess::KeyProcessForSelectedObj @000d3150 starts it after its checks: player vfunc +0xCC(
            // cProcessManager::MakeCollectProcess(player, stuff) @000d8c48, 0); the player is put just below the stuff
            state::post([in_field] {
                uint32_t w = g::world(), pl = g::player();
                if (!in_field() || !pl) return;
                uint32_t L = M32(M32(w + 0x650) + 0x14 + 4 * 9), best = 0;
                int px = (int32_t)M32(pl + 0x14), py = (int32_t)M32(pl + 0x18), bd = 1 << 30;
                for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    if (!o || M8(o + 4) != 9 || !(M32(o + 0x2c) & 2)) continue;
                    int ty = (int16_t)M16(o + 6);
                    if (M8(pl + 0x360 + ty % 3) < ty / 3 + 1) continue; // the skill level check of KeyProcessForSelectedObj
                    int dx = (int32_t)M32(o + 0x14) - px, dy = (int32_t)M32(o + 0x18) - py;
                    if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = o; }
                }
                if (!best) { ui_log("[dev] no stuff to gather here"); return; }
                int x = (int32_t)M32(best + 0x14), y = (int32_t)M32(best + 0x18) + (int16_t)M16(best + 0xe) + (int16_t)M16(best + 0x12) + 6;
                ui_log("[dev] gather stuff type %d at %d,%d", (int16_t)M16(best + 6), x, y);
                g::call((M32(M32(pl) + 0x18) - IMG) & ~1u, {pl, (uint32_t)x, (uint32_t)y});
                uint32_t pr = g::call(0xc8c48, {pl, best});
                g::call((M32(M32(pl) + 0xcc) - IMG) & ~1u, {pl, pr, 0});
            });
            return;
        }
        if (!strcmp(arg, "gold")) { // test: ui dev gold <n> -> cInventory::AddGold @0012facc
            int n = 0; sscanf(line, "%*s %*s %d", &n);
            state::post([n] { uint32_t inv = g::inv(); if (inv) g::call(0x11facc, {inv, (uint32_t)n}); });
            return;
        }
        g_dev = !strcmp(arg, "xbox") ? XBOX : !strcmp(arg, "ps") ? PS : !strcmp(arg, "touch") ? TOUCH : KBM;
    } else if (!strcmp(cmd, "vpad")) {
        // test: attach an SDL virtual gamepad (real device path: GAMEPAD_ADDED / BUTTON events, type from VID/PID)
        sscanf(line, "%*s %31s", arg);
        bool ps = !strcmp(arg, "ps");
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = ps ? 0x054c : 0x045e;   // Sony / Microsoft
        desc.product_id = ps ? 0x0ce6 : 0x0b12;  // DualSense / Xbox Series controller
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_MISC1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_MISC1) - 1;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.name = ps ? "Virtual DualSense" : "Virtual Xbox Controller";
        desc.Rumble = [](void *, Uint16 lo, Uint16 hi) -> bool { ui_log("[vib] rumble lo %u hi %u", (unsigned)lo, (unsigned)hi); return true; };
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        g_vjoy = id ? SDL_OpenJoystick(id) : nullptr;
        ui_log("[input] virtual pad %s: id %u %s", desc.name, (unsigned)id, g_vjoy ? "" : SDL_GetError());
    } else if (!strcmp(cmd, "vbtn")) {
        int b = 0; char up[8] = "";
        sscanf(line, "%*s %d %7s", &b, up);
        if (g_vjoy) SDL_SetJoystickVirtualButton(g_vjoy, b, up[0] == 0);
    } else if (!strcmp(cmd, "act")) { // test: act <name> -> the action a click on data-act="<name>" sends
        sscanf(line, "%*s %31s", arg);
        ui_action(arg);
    } else if (!strcmp(cmd, "finger")) { // test: finger <id> down|move|up <x> <y> (window pixels)
        int id = 0; char ph[8] = ""; float x = 0, y = 0;
        sscanf(line, "%*s %d %7s %f %f", &id, ph, &x, &y);
        SDL_Event e{};
        e.type = !strcmp(ph, "down") ? SDL_EVENT_FINGER_DOWN : !strcmp(ph, "up") ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_MOTION;
        e.tfinger.fingerID = (SDL_FingerID)(id + 1);
        e.tfinger.x = x / ui::win_w; e.tfinger.y = y / ui::win_h;
        handle_event(e);
    } else if (!strcmp(cmd, "vaxis")) {
        int ax = 0, v = 0;
        sscanf(line, "%*s %d %d", &ax, &v);
        if (g_vjoy) SDL_SetJoystickVirtualAxis(g_vjoy, ax, (Sint16)v);
    } else if (!strcmp(cmd, "vdetach")) {
        if (g_vjoy) { SDL_JoystickID id = SDL_GetJoystickID(g_vjoy); SDL_CloseJoystick(g_vjoy); g_vjoy = nullptr; SDL_DetachVirtualJoystick(id); }
    }
}

} // namespace input

namespace screens {
std::string glyph(const char *action) { return input::glyph_for(action, false); }
std::string glyph_ref(const char *action) { return input::glyph_for(action, true); }
}
extern "C" void ui_vibrate(int ms) { input::vibrate(ms); }
