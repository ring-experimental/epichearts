/* Modern presentation layer: GL presenter for the original software framebuffer + RmlUi interface.
 * C API used by the C host (main.c, gl.c, java.c). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
union SDL_Event;
struct SDL_Window;
struct Cpu;

extern int g_game_w, g_game_h; /* software framebuffer size the original renders (width follows the display aspect) */

int ui_init(struct SDL_Window *win);
void ui_shutdown(void);
/* main thread, inside MyRenderer.render: this frame's back buffer (RGB565) */
void ui_present_game_frame(const uint16_t *px, int w, int h);
/* returns 1 when the modern UI consumed the event */
int ui_handle_event(union SDL_Event *e);
/* main thread: called every frame before render */
void ui_update(void);
/* draws game layer + UI and swaps */
void ui_render_and_swap(void);
void ui_request_shot(const char *path);
/* window pixel -> game framebuffer coordinates; returns 0 when outside the game view */
int ui_window_to_game(float wx, float wy, int *gx, int *gy);
void ui_toggle_fullscreen(void);
void ui_vibrate(int ms);            /* original playVib(ms) / stopVib (0): pad rumble or phone vibrator */

/* game thread, from GofManager.procTouch (before the queued touches are delivered) */
void ui_game_tick(struct Cpu *c);
/* test driver commands for the UI (EH_CTRL "ui ..." lines); main thread */
void ui_test_command(const char *line);

#ifdef __cplusplus
}
#endif
