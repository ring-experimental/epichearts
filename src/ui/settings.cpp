#include "settings.h"
#include "bindings.h"
#include "render.h"
#include "ui_internal.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>

extern "C" char g_data_dir[1024];
extern "C" int g_logic512; // hooks.c: 512-px logic box on wider views
extern "C" int g_mul_exp, g_mul_gold, g_mul_life, g_mul_refine; // hooks.c: developer option multipliers (percent)

namespace settings {
static S g_s;
S &get() { return g_s; }
static int mul_clamp(int v) { return v < 100 ? 100 : v > MUL_MAX ? MUL_MAX : v; }
static std::string path() { return std::string(g_data_dir) + "/../ui_settings.ini"; }

void load() {
    FILE *f = fopen(path().c_str(), "r");
    if (!f) return;
    char k[64]; int v;
    while (fscanf(f, "%63[^=]=%d\n", k, &v) == 2) {
        if (!strcmp(k, "fullscreen")) g_s.fullscreen = v != 0;
        else if (!strcmp(k, "sharp")) g_s.sharp = v != 0;
        else if (!strcmp(k, "ui_scale")) g_s.ui_scale = v < 80 ? 80 : v > 130 ? 130 : v;
        else if (!strcmp(k, "vib_strength")) g_s.vib_strength = v < 0 ? 0 : v > 100 ? 100 : v;
        else if (!strcmp(k, "logic512")) g_s.logic512 = v != 0;
        else if (!strcmp(k, "vpad_x")) g_s.vpad_x = v < 0 ? 0 : v > 10 ? 10 : v;
        else if (!strcmp(k, "vpad_y")) g_s.vpad_y = v < 0 ? 0 : v > 10 ? 10 : v;
        else if (!strcmp(k, "dev")) g_s.dev = v != 0;
        else if (!strcmp(k, "mul_exp")) g_s.mul_exp = mul_clamp(v);
        else if (!strcmp(k, "mul_gold")) g_s.mul_gold = mul_clamp(v);
        else if (!strcmp(k, "mul_life")) g_s.mul_life = mul_clamp(v);
        else if (!strcmp(k, "mul_refine")) g_s.mul_refine = mul_clamp(v);
        else bind::load_line(k, v);
    }
    fclose(f);
}
void save() {
    FILE *f = fopen(path().c_str(), "w");
    if (!f) return;
    fprintf(f, "fullscreen=%d\nsharp=%d\nui_scale=%d\nvib_strength=%d\nlogic512=%d\nvpad_x=%d\nvpad_y=%d\n%s", g_s.fullscreen, g_s.sharp, g_s.ui_scale,
            g_s.vib_strength, g_s.logic512, g_s.vpad_x, g_s.vpad_y, "");
    fprintf(f, "dev=%d\nmul_exp=%d\nmul_gold=%d\nmul_life=%d\nmul_refine=%d\n%s", g_s.dev, g_s.mul_exp, g_s.mul_gold, g_s.mul_life, g_s.mul_refine,
            bind::save_lines().c_str());
    fclose(f);
}
void apply() {
    present::set_sharp(g_s.sharp);
    g_logic512 = g_s.logic512;
    // the multipliers count only while the developer options are unlocked
    g_mul_exp = g_s.dev ? g_s.mul_exp : 100;
    g_mul_gold = g_s.dev ? g_s.mul_gold : 100;
    g_mul_life = g_s.dev ? g_s.mul_life : 100;
    g_mul_refine = g_s.dev ? g_s.mul_refine : 100;
#ifndef __ANDROID__
    bool fs = (SDL_GetWindowFlags(ui::window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (fs != g_s.fullscreen) SDL_SetWindowFullscreen(ui::window, g_s.fullscreen);
#endif
}
}
