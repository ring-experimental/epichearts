#pragma once
#include <cstdint>
#include <string>
namespace present {
bool init();
void set_frame(const uint16_t *px, int w, int h);
const uint16_t *frame_pixels(int *w, int *h);
unsigned int render_game(int out_w, int out_h);
unsigned int game_texture();
void set_sharp(bool on);
void live_update(const std::string &name, const uint8_t *rgba, int w, int h);
unsigned int live_texture(const std::string &name, int *w, int *h);
bool is_live(unsigned int tex);
bool save_backbuffer(const char *path, int w, int h);
}
