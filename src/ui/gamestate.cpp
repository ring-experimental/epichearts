// game-thread entry from GofManager.procTouch (src/java.c)
#include "state.h"
#include "ui.h"
extern "C" void ui_game_tick(struct Cpu *c) { state::run_actions(c); }
