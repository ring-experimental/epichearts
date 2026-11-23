#pragma once
#include "jni.h"

/* AndroidManifest.xml package of the original APK */
#define JAVA_PACKAGE "com.ftt.ankrepicheartslite"

extern JObj *g_gofmgr, *g_filebr, *g_audiobr, *g_fontbr, *g_devbr, *g_glview, *g_applock, *g_eventlock;
extern volatile int g_thread_state;

void java_set_touch(int type, int x, int y);
void java_request_quit(void);
void java_font_register(void);
void java_audio_register(void);
void java_audio_shutdown(void); /* quit: the mixer device stops (nothing plays after the window is gone) */
void java_net_register(void);
void java_text_register(void);
union SDL_Event;
int java_text_event(Cpu *c, const union SDL_Event *e);
const char *java_text_state(void); /* text being typed into the EditText overlay, NULL when hidden */
void java_back_pressed(void);
void java_set_turbo(int on, int (*keep)(void)); /* unpaced game frames (dialogue SKIP) while keep() (game thread) */
int java_turbo(void);
void java_alert_register(void);
