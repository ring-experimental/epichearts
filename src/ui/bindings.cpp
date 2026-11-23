#include "bindings.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <map>

namespace bind {

// defaults = the port's layout (input.cpp). group 0: the field; group 1: menus, the original's windows and the
// dialogue (confirm / back / tabs). The two groups are separate contexts and may share keys (A attacks and confirms).
static const std::vector<Action> KB_ACTIONS = {
    {"up", "\xec\x9c\x84\xeb\xa1\x9c \xec\x9d\xb4\xeb\x8f\x99", SDLK_W, 0},
    {"down", "\xec\x95\x84\xeb\x9e\x98\xeb\xa1\x9c \xec\x9d\xb4\xeb\x8f\x99", SDLK_S, 0},
    {"left", "\xec\x99\xbc\xec\xaa\xbd\xec\x9c\xbc\xeb\xa1\x9c \xec\x9d\xb4\xeb\x8f\x99", SDLK_A, 0},
    {"right", "\xec\x98\xa4\xeb\xa5\xb8\xec\xaa\xbd\xec\x9c\xbc\xeb\xa1\x9c \xec\x9d\xb4\xeb\x8f\x99", SDLK_D, 0},
    {"attack", "\xea\xb3\xb5\xea\xb2\xa9 \xc2\xb7 \xeb\x8c\x80\xed\x99\x94", SDLK_SPACE, 0},
    {"slot0", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf A1", SDLK_1, 0},
    {"slot1", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf A2", SDLK_2, 0},
    {"slot2", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf A3", SDLK_3, 0},
    {"slot3", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf A4", SDLK_4, 0},
    {"slot4", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf A5", SDLK_5, 0},
    {"slot5", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf B1", SDLK_6, 0},
    {"slot6", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf B2", SDLK_7, 0},
    {"slot7", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf B3", SDLK_8, 0},
    {"slot8", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf B4", SDLK_9, 0},
    {"slot9", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf B5", SDLK_0, 0},
    {"map", "\xec\xa7\x80\xeb\x8f\x84", SDLK_M, 0},
    {"quest", "\xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8", SDLK_J, 0},
    {"town", "\xeb\xa7\x88\xec\x9d\x84 \xec\x8b\x9c\xec\x84\xa4", SDLK_T, 0},
    {"save", "\xec\xa0\x80\xec\x9e\xa5", SDLK_F5, 0},
    {"fast", "\xeb\xb9\xa8\xeb\xa6\xac \xea\xb0\x90\xea\xb8\xb0", SDLK_F, 0},
    {"ui_confirm", "\xec\x84\xa0\xed\x83\x9d \xc2\xb7 \xed\x99\x95\xec\x9d\xb8", SDLK_SPACE, 1},
    {"ui_back", "\xeb\x92\xa4\xeb\xa1\x9c \xc2\xb7 \xeb\x8b\xab\xea\xb8\xb0", SDLK_BACKSPACE, 1},
    {"ui_tabl", "\xec\x9d\xb4\xec\xa0\x84 \xed\x83\xad \xc2\xb7 \xeb\x8c\x80\xed\x99\x94 \xea\xb8\xb0\xeb\xa1\x9d", SDLK_Q, 1},
    {"ui_tabr", "\xeb\x8b\xa4\xec\x9d\x8c \xed\x83\xad \xc2\xb7 \xec\x9e\x90\xeb\x8f\x99 \xec\xa7\x84\xed\x96\x89", SDLK_E, 1},
    {"ui_subl", "\xec\x9d\xb4\xec\xa0\x84 \xed\x95\x98\xec\x9c\x84 \xed\x83\xad", SDLK_Z, 1},
    {"ui_subr", "\xeb\x8b\xa4\xec\x9d\x8c \xed\x95\x98\xec\x9c\x84 \xed\x83\xad", SDLK_C, 1},
    {"ui_skip", "\xeb\x8c\x80\xed\x99\x94 \xea\xb1\xb4\xeb\x84\x88\xeb\x9b\xb0\xea\xb8\xb0", SDLK_F, 1},
    {"ui_ffwd", "\xeb\x8c\x80\xed\x99\x94 \xeb\xb9\xa8\xeb\xa6\xac \xea\xb0\x90\xea\xb8\xb0", SDLK_R, 1},
};
static const std::vector<Action> PAD_ACTIONS = {
    {"attack", "\xea\xb3\xb5\xea\xb2\xa9 \xc2\xb7 \xeb\x8c\x80\xed\x99\x94", SDL_GAMEPAD_BUTTON_SOUTH, 0},
    {"slot0", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf 1", SDL_GAMEPAD_BUTTON_WEST, 0},
    {"slot1", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf 2", SDL_GAMEPAD_BUTTON_NORTH, 0},
    {"slot2", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf 3", SDL_GAMEPAD_BUTTON_EAST, 0},
    {"slot3", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf 4", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0},
    {"slot4", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf 5", PAD_RT, 0},
    {"setb", "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xec\x84\xb8\xed\x8a\xb8 B (\xeb\x88\x84\xeb\xa5\xb8 \xec\xb1\x84\xeb\xa1\x9c)", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0},
    {"menu", "\xeb\xa9\x94\xeb\x89\xb4", SDL_GAMEPAD_BUTTON_START, 0},
    {"map2", "\xec\xa7\x80\xeb\x8f\x84", SDL_GAMEPAD_BUTTON_BACK, 0},
    {"fast", "\xeb\xb9\xa8\xeb\xa6\xac \xea\xb0\x90\xea\xb8\xb0", SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0},
    {"quest", "\xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8", SDL_GAMEPAD_BUTTON_DPAD_UP, 0},
    {"save", "\xec\xa0\x80\xec\x9e\xa5", SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0},
    {"map", "\xec\xa7\x80\xeb\x8f\x84 (\xeb\xb0\xa9\xed\x96\xa5\xed\x82\xa4)", SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0},
    {"town", "\xeb\xa7\x88\xec\x9d\x84 \xec\x8b\x9c\xec\x84\xa4", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0},
    {"ui_confirm", "\xec\x84\xa0\xed\x83\x9d \xc2\xb7 \xed\x99\x95\xec\x9d\xb8", SDL_GAMEPAD_BUTTON_SOUTH, 1},
    {"ui_back", "\xeb\x92\xa4\xeb\xa1\x9c \xc2\xb7 \xeb\x8b\xab\xea\xb8\xb0", SDL_GAMEPAD_BUTTON_EAST, 1},
    {"ui_tabl", "\xec\x9d\xb4\xec\xa0\x84 \xed\x83\xad \xc2\xb7 \xeb\x8c\x80\xed\x99\x94 \xea\xb8\xb0\xeb\xa1\x9d", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 1},
    {"ui_tabr", "\xeb\x8b\xa4\xec\x9d\x8c \xed\x83\xad \xc2\xb7 \xec\x9e\x90\xeb\x8f\x99 \xec\xa7\x84\xed\x96\x89", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 1},
    {"ui_subl", "\xec\x9d\xb4\xec\xa0\x84 \xed\x95\x98\xec\x9c\x84 \xed\x83\xad", PAD_LT, 1},
    {"ui_subr", "\xeb\x8b\xa4\xec\x9d\x8c \xed\x95\x98\xec\x9c\x84 \xed\x83\xad", PAD_RT, 1},
    {"ui_skip", "\xeb\x8c\x80\xed\x99\x94 \xea\xb1\xb4\xeb\x84\x88\xeb\x9b\xb0\xea\xb8\xb0", SDL_GAMEPAD_BUTTON_START, 1},
    {"ui_ffwd", "\xeb\x8c\x80\xed\x99\x94 \xeb\xb9\xa8\xeb\xa6\xac \xea\xb0\x90\xea\xb8\xb0", SDL_GAMEPAD_BUTTON_NORTH, 1},
};

static std::map<std::string, int> g_b[2];

const std::vector<Action> &actions(Dev d) { return d == KB ? KB_ACTIONS : PAD_ACTIONS; }

static void ensure(Dev d) {
    if (!g_b[d].empty()) return;
    for (auto &a : actions(d)) g_b[d][a.id] = a.def;
}

int get(Dev d, const char *id) {
    ensure(d);
    auto it = g_b[d].find(id);
    return it == g_b[d].end() ? -1 : it->second;
}

std::string action_for(Dev d, int code, int group) {
    ensure(d);
    if (code < 0) return "";
    for (auto &a : actions(d)) // table order: the first action wins if two share a code
        if (a.group == group && g_b[d][a.id] == code) return a.id;
    return "";
}

static int group_of(Dev d, const char *id) {
    for (auto &a : actions(d)) if (!strcmp(a.id, id)) return a.group;
    return 0;
}

void set(Dev d, const char *id, int code) {
    ensure(d);
    int old = g_b[d][id], grp = group_of(d, id);
    // swap with the action of the same context (field / menus) that had the code; the two contexts may share keys
    for (auto &kv : g_b[d])
        if (kv.first != id && kv.second == code && group_of(d, kv.first.c_str()) == grp) kv.second = old;
    g_b[d][id] = code;
}

void reset(Dev d) { g_b[d].clear(); ensure(d); }

// layout version of the saved bindings. 2: the pad's save moved off D-pad down (pressed by accident while browsing)
// to the right stick click, fast-forward took D-pad down. Older files list every binding, so their untouched
// old defaults are dropped in favour of the new ones.
static const int LAYOUT = 2;
static int g_file_layout = 1;
static bool old_default(Dev d, const char *id, int value) {
    if (d != PAD) return false;
    return (!strcmp(id, "save") && value == SDL_GAMEPAD_BUTTON_DPAD_DOWN) || (!strcmp(id, "fast") && value == SDL_GAMEPAD_BUTTON_RIGHT_STICK);
}

bool load_line(const char *key, int value) {
    if (!strcmp(key, "bind_layout")) { g_file_layout = value; return true; }
    Dev d;
    if (!strncmp(key, "kb.", 3)) d = KB;
    else if (!strncmp(key, "pad.", 4)) d = PAD;
    else return false;
    ensure(d);
    const char *id = strchr(key, '.') + 1;
    if (g_file_layout < LAYOUT && old_default(d, id, value)) return true;
    for (auto &a : actions(d))
        if (!strcmp(a.id, id)) g_b[d][a.id] = value;
    return true;
}

std::string save_lines() {
    std::string r = "bind_layout=" + std::to_string(LAYOUT) + "\n";
    for (int d = 0; d < 2; d++) {
        ensure((Dev)d);
        for (auto &a : actions((Dev)d)) r += std::string(d == KB ? "kb." : "pad.") + a.id + "=" + std::to_string(g_b[d][a.id]) + "\n";
    }
    return r;
}
}
