#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace sprites {
// game thread: render (once) and return the cache key, "" if the sprite set is not loaded yet
std::string image(int set, int idx, int pal = 0, int flip = 0);
std::string image_fx(int set, int idx, int pal, int flip, const uint32_t fx[4]);
std::string frame(int set, int frm, int pal = 0, int dy = 40);
// drawn at buffer centre + (0, dy)
std::string grid(int set, int base, int cols, int rows, int tile); // tiled picture from consecutive images
std::string capture_insitu(int x, int y, int w, int h, const std::function<void()> &draw);
// any thread
const uint8_t *lookup(const char *key, int *w, int *h);
bool ready(const std::string &key);
// placement of a rendered sprite: bbox origin relative to the draw point, size in source pixels
bool info(const std::string &key, int *ox, int *oy, int *sw, int *sh);
}
