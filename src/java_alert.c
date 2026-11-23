/* GofManager.showSystemAlert__withTitle__withMsg__withUrl(type, title, msg, url) + GofManager.onClick
 * and Tar.onBackPressed (classes.dex):
 *  show: AlertDialog on the UI thread, SetNeedHookForRun(true) while it is up (type 1: "Ok", type 2: Ok/Cancel)
 *  click: SetNeedHookForRun(false); type 2 and not BUTTON_POSITIVE -> xxxQuitOnSysAlertOK = false, url = null;
 *         url -> openUrl; xxxQuitOnSysAlertOK -> requestQuit
 *  back:  no IBackListener -> xxxQuitOnSysAlertOK = true, type-2 alert "종료" / "게임을 종료하시겠습니까?" */
#include "java.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_quit_on_alert_ok;
static uint32_t g_native_SetNeedHookForRun;
typedef struct { int type; char title[256], msg[1024], url[512]; } Alert;

static int SDLCALL alert_thread(void *p) {
    Alert *al = p;
    Cpu *c = rc_new_cpu(256 << 10);
    uint32_t args[3] = {jni_env(), HPTR(g_gofmgr), 1};
    rc_call(c, g_native_SetNeedHookForRun, 3, args);
    SDL_MessageBoxButtonData btn[2] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, -1, "Ok"},
                                       {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -2, "Cancel"}};
    SDL_MessageBoxData md = {SDL_MESSAGEBOX_INFORMATION, NULL, al->title, al->msg, al->type == 2 ? 2 : 1, btn, NULL};
    int which = -1;
    if (getenv("EH_CTRL") || getenv("EH_SCRIPT") || getenv("EH_MONKEY") || getenv("EH_BOT")) which = al->type == 2 ? -2 : -1; /* automated runs never block */
    else SDL_ShowMessageBox(&md, &which);
    args[2] = 0;
    rc_call(c, g_native_SetNeedHookForRun, 3, args);
    if (al->type == 2 && which != -1) { g_quit_on_alert_ok = 0; al->url[0] = 0; }
    if (al->url[0]) host_log("[java] openUrl %s", al->url);
    if (g_quit_on_alert_ok) java_request_quit();
    free(al);
    return 0;
}

static void show(int type, const char *title, const char *msg, const char *url) {
    host_log("[java] showSystemAlert type=%d title=%s msg=%s url=%s", type, title ? title : "", msg ? msg : "", url ? url : "");
    if (type != 1 && type != 2) return;
    Alert *al = calloc(1, sizeof *al);
    al->type = type;
    snprintf(al->title, sizeof al->title, "%s", title ? title : "");
    snprintf(al->msg, sizeof al->msg, "%s", msg ? msg : "");
    snprintf(al->url, sizeof al->url, "%s", url ? url : "");
    SDL_DetachThread(SDL_CreateThread(alert_thread, "AlertDialog", al));
}

static uint64_t gm_showSystemAlert(Cpu *c, JObj *self, const uint64_t *a) {
    (void)c; (void)self;
    show((int)a[0], JSTR(a[1]), JSTR(a[2]), JSTR(a[3]));
    return 0;
}

void java_back_pressed(void) {
    g_quit_on_alert_ok = 1;
    show(2, "\xec\xa2\x85\xeb\xa3\x8c",
         "\xea\xb2\x8c\xec\x9e\x84\xec\x9d\x84 \xec\xa2\x85\xeb\xa3\x8c\xed\x95\x98\xec\x8b\x9c\xea\xb2\xa0\xec\x8a\xb5\xeb\x8b\x88\xea\xb9\x8c?", NULL);
}

void java_alert_register(void) {
    jni_register("com/ftt/gof2d/GofManager", "showSystemAlert__withTitle__withMsg__withUrl", NULL, gm_showSystemAlert);
    g_native_SetNeedHookForRun = rc_sym("Java_com_ftt_gof2d_GofManager_SetNeedHookForRun") & ~1u;
}
