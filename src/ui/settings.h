// Port-only settings (not part of the original's systemInfo): window mode, pixel filter, interface scale
#pragma once
namespace settings {
// vib_strength 0..100 %; logic512: the original's 512-px box for the few gameplay decisions made from the view (hooks.c)
// vpad_x / vpad_y: V-Pad position steps 0..10 from the original's place (right / up, VPAD_STEP_X / _Y vh per step)
// dev: hidden developer options unlocked (options: tap the version row 7 times); mul_*: gameplay multipliers in percent
// (100 = the original), applied by hooks.c (g_mul_*): monster EXP, field gold, gathering / mining / fishing, refine success
struct S {
    bool fullscreen = false; bool sharp = true; int ui_scale = 100; int vib_strength = 70; bool logic512 = true; int vpad_x = 0, vpad_y = 0;
    bool dev = false; int mul_exp = 100, mul_gold = 100, mul_life = 100, mul_refine = 100;
};
constexpr int MUL_STEPS[] = {100, 150, 200, 300, 500, 1000, 2000, 3000, 5000, 10000}; // the values the developer options offer (up to x100)
constexpr int MUL_N = sizeof(MUL_STEPS) / sizeof(MUL_STEPS[0]);
constexpr int MUL_MAX = MUL_STEPS[MUL_N - 1];
S &get();
constexpr float VPAD_STEP_X = 4.f, VPAD_STEP_Y = 3.f; // vh
inline float vpad_dx_vh() { return get().vpad_x * VPAD_STEP_X; }
inline float vpad_dy_vh() { return get().vpad_y * VPAD_STEP_Y; }
void load();
void save();
void apply();   // pushes the values to the window / presenter / UI scale
}
