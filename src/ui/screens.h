// Cross-module declarations of the modern UI (C++)
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <functional>

namespace screens {
bool talkctl_shown(); // dialogue / crawl controls up (talkctl.cpp)
void scroll_active(float dp);
void dump_ui(const char *path);      // test: the shown element trees with layout problems (EH_CTRL "ui dumpui <file>")
void dump_geom(const char *path);    // test: frame / interface / touch geometry checks (EH_CTRL "ui geom <file>")
void open_bagbuy();             // bag expansion purchase (bagbuy.cpp)  // scroll the active screen's box (right stick)
void init();
void action(const char *name);        // data-act of the current screen (not a HUD/game action)
bool menu_active();
void open_keymap();                   // Options > key mapping popup (keymap.cpp)                   // a modern menu/modal owns keyboard/gamepad navigation
void pad_button(int button, bool down); // gamepad while a modern menu is open (100/101 = LT/RT)
void pad_dir(int dir);                // 0 up 1 down 2 left 3 right, -1 released
bool talk_active();
std::string glyph(const char *action);
std::string glyph_ref(const char *action); // also for keyboard before a key was typed (help / key reference)
void update();     // main thread, per frame: pulls the latest game snapshot into the RmlUi data models
void sync_view();  // the view element on game_rect
void shutdown();
}

namespace input {
bool handle_event(SDL_Event &e);   // returns true when consumed by the UI
void update();                     // per frame (gamepad sticks)
void ui_action(const char *a);     // HUD / global actions (data-act)
std::string glyph_for(const char *action, bool always = false);
std::string pad_glyph(int code, bool ps);  // glyph of a gamepad button / trigger code (bindings.h)
std::string key_glyph(int key);            // keycap of a keyboard key
bool pad_is_ps();
bool keyboard_last();               // the last non-pad input was a key (not the mouse / touch)
void begin_capture(std::function<void(int dev, int code)> cb); // key mapping: the next key / pad button
bool capturing();
bool touch_input();
void vibrate(int ms);              // pad rumble / phone vibrator at the strength setting (0 stops)
bool pad_set_b();                  // gamepad: LB / L1 held (quickslot set B)
bool pad_input();                  // the last input came from a gamepad
bool vpad_stick(float *cx, float *cy, float *x, float *y); // V-Pad joystick (window px): true while held                // the last input came from a touch screen (no key / button prompts)
void test_command(const char *line);
}

// capture.cpp: draw interception of the original UI
void capture_on_frame();                                   // main thread, when a game frame is presented
const uint8_t *sprites_lookup(const char *id, int *w, int *h);
