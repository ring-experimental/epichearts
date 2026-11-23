// Key / button mapping of the port's game actions (Options > 키 설정). Codes: keyboard = SDL_Keycode;
// gamepad = SDL_GamepadButton, 200 = left trigger, 201 = right trigger. Menu confirm / back / tabs are mappable too;
// arrows / Enter / Esc (keyboard) and the D-pad / Start (gamepad) always work as well, so a mapping can't lock the player out.
#pragma once
#include <string>
#include <vector>

namespace bind {
enum Dev { KB = 0, PAD = 1 };
enum { PAD_LT = 200, PAD_RT = 201 };
struct Action { const char *id; const char *label; int def; int group; }; // group 0 field, 1 menus / dialogue
const std::vector<Action> &actions(Dev d);
int get(Dev d, const char *id);              // bound code, -1 when unbound
std::string action_for(Dev d, int code, int group = 0); // the action of a group bound to a code, "" when none
void set(Dev d, const char *id, int code);   // a code already used by another action is swapped
void reset(Dev d);
bool load_line(const char *key, int value);  // settings file "kb.<id>" / "pad.<id>"
std::string save_lines();
}
