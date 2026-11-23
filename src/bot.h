/* Autoplay bot for verification runs (EH_BOT=1). Reads the original game's memory and injects
 * touches through the same path as the mouse (GofManager.setTouch). Run with EH_TESTBUFF=1. */
#pragma once
#include "host.h"

void bot_init(int surf_w, int surf_h);
/* called on the game thread at the start of GofManager.procTouch */
void bot_tick(Cpu *c);
int bot_enabled(void);
