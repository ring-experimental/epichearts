#pragma once
#include <cstdint>
namespace capture { extern bool modern; extern bool force_strip; bool minimap(uint8_t *out40x40rgba, uint32_t *seq);
void cover_dump(const char *path); /* test: unpainted pixels of the last frame (EH_SENTINEL=1) */
namespace cover { extern bool on; } /* EH_SENTINEL=1: every frame starts as a sentinel colour (capture.cpp) */ }
