// Shared between the modern-UI modules (C++)
#pragma once
#include <RmlUi/Core.h>
#include <cstdint>
#include <string>

struct SDL_Window;
extern "C" void host_log(const char *fmt, ...);
#define ui_log host_log

namespace ui {
struct ViewRect { float x, y, w, h; };
extern SDL_Window *window;
extern Rml::Context *context;
extern ViewRect game_rect;   // where the game framebuffer (world and the original's forms) is shown, window pixels
extern int win_w, win_h;     // drawable size in pixels
extern float dp;             // RmlUi dp ratio
// game framebuffer pixels -> window pixels for the original's interface (forms, captions), drawn over game_rect.
// Shared by every renderer of original layout and checked against the touch mapping by "ui geom" (screens.cpp).
struct FormMap {
    float x0, y0, sx, sy;
    float X(float x) const { return x0 + x * sx; }
    float Y(float y) const { return y0 + y * sy; }
};
FormMap form_map();
}
namespace input {
bool touch_target(float wx, float wy, int *gx, int *gy); // touch-down window px -> game px; false: not passed to the game
}
namespace ui {
std::string asset_path(const std::string &rel); // data/ui/<rel> on desktop, assets/ui/<rel> on Android

// sprites captured from the original (capture.cpp): returns an RmlUi texture source "sprite:<id>" or ""
}
