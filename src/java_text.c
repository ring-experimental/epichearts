/* com/ftt/gof2d/gofview/cAndroid_textView (classes.dex): the Android EditText overlay used for
 * text entry, on SDL3 text input. Behaviour per smali:
 *  <init>(peer)        str = str_in_tf = ""
 *  enable(true)        EditText created with str; its TextWatcher keeps str_in_tf (enable(false): no-op)
 *  afterTextChanged    UTF-8 byteCount > TF_GetMaxLen() -> TF_SetState(1) and revert, else str_in_tf = text
 *  IME action DONE     dismissKeyboardCommitting(true): str = str_in_tf, TF_SetState(3),
 *                      GofManager.SimulateKeyPress(0x30); then TF_keyboardIsDismissedSoRegardUrselfAsDisabled()
 *  getText()           UTF-8 of str in the shared 0x81-byte array; getBa_forJNI() returns that array
 *  setText(bytes)      str = decode(bytes); EditText (if shown) updated */
#include "java.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char str[0x81], str_in_tf[0x81];
    JObj *ba; /* Utf8.ba: byte[0x81] */
    int shown;
} TextView;

static JObj *g_active;
static uint32_t n_GetMaxLen, n_SetState, n_Dismissed, n_SimulateKey;
extern SDL_Window *g_window;

#define JM(name) static uint64_t name(Cpu *c, JObj *self, const uint64_t *a)
#define UNUSED (void)c; (void)self; (void)a

JM(tv_init) {
    UNUSED;
    TextView *t = calloc(1, sizeof *t);
    t->ba = jarr_new("[B", 1, 0x81);
    t->ba->global = 1;
    self->native = t;
    self->global = 1;
    jobj_field_set(self, "peer", (int64_t)(int32_t)a[0]);
    return 0;
}
JM(tv_enable) {
    UNUSED;
    TextView *t = self->native;
    if (!a[0]) return 0;
    snprintf(t->str_in_tf, sizeof t->str_in_tf, "%s", t->str);
    t->shown = 1;
    g_active = self;
    SDL_StartTextInput(g_window);
    return 0;
}
static uint64_t encode_into_ba(TextView *t, const char *s) {
    memset(t->ba->data, 0, 0x81);
    size_t n = strlen(s);
    if (n > 0x80) n = 0x80;
    memcpy(t->ba->data, s, n);
    return JRET_OBJ(t->ba);
}
JM(tv_getText) { UNUSED; TextView *t = self->native; return encode_into_ba(t, t->str); }
JM(tv_getBa) { UNUSED; TextView *t = self->native; return JRET_OBJ(t->ba); }
JM(tv_setText) {
    UNUSED;
    TextView *t = self->native;
    JObj *arr = JARR(a[0]);
    size_t n = 0;
    if (arr) while (n < arr->len && n < 0x80 && ((uint8_t *)arr->data)[n]) n++;
    memcpy(t->str, arr ? arr->data : "", n);
    t->str[n] = 0;
    if (t->shown) snprintf(t->str_in_tf, sizeof t->str_in_tf, "%s", t->str);
    return 0;
}

static Cpu *g_ui_cpu;
static void call_native(uint32_t fn, JObj *self, int nargs, uint32_t a2) {
    uint32_t args[3] = {jni_env(), HPTR(self), a2};
    rc_call(g_ui_cpu, fn, nargs, args);
}

static void dismiss(int commit) {
    JObj *o = g_active;
    if (!o) return;
    TextView *t = o->native;
    if (commit) {
        snprintf(t->str, sizeof t->str, "%s", t->str_in_tf);
        call_native(n_SetState, o, 3, 3);
        uint32_t args[3] = {jni_env(), HPTR(g_gofmgr), 0x30};
        rc_call(g_ui_cpu, n_SimulateKey, 3, args);
    }
    t->shown = 0;
    g_active = NULL;
    SDL_StopTextInput(g_window);
    call_native(n_Dismissed, o, 2, 0);
}

/* called by the main loop for every SDL event; returns 1 when consumed */
int java_text_event(Cpu *c, const SDL_Event *e) {
    g_ui_cpu = c;
    if (!g_active) return 0;
    TextView *t = g_active->native;
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        char cand[0x200];
        snprintf(cand, sizeof cand, "%s%s", t->str_in_tf, e->text.text);
        uint32_t args[2] = {jni_env(), HPTR(g_active)};
        int maxlen = (int)rc_call(c, n_GetMaxLen, 2, args);
        if ((int)strlen(cand) > maxlen || strlen(cand) > 0x80) call_native(n_SetState, g_active, 3, 1);
        else snprintf(t->str_in_tf, sizeof t->str_in_tf, "%s", cand);
        return 1;
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        if (e->key.key == SDLK_BACKSPACE) {
            size_t n = strlen(t->str_in_tf);
            while (n > 0 && ((uint8_t)t->str_in_tf[n - 1] & 0xc0) == 0x80) n--;
            if (n > 0) n--;
            t->str_in_tf[n] = 0;
        } else if (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) dismiss(1);
        else if (e->key.key == SDLK_ESCAPE) dismiss(0);
        return 1;
    }
    return 0;
}

/* the EditText overlay is drawn by the modern UI (ui/screens.cpp reads java_text_state) */
const char *java_text_state(void) {
    if (!g_active) return NULL;
    return ((TextView *)g_active->native)->str_in_tf;
}

void java_text_register(void) {
    const char *TV = "com/ftt/gof2d/gofview/cAndroid_textView";
    jni_register(TV, "<init>", NULL, tv_init);
    jni_register(TV, "enable", NULL, tv_enable);
    jni_register(TV, "getText", NULL, tv_getText);
    jni_register(TV, "getBa_forJNI", NULL, tv_getBa);
    jni_register(TV, "setText", NULL, tv_setText);
    n_GetMaxLen = rc_sym("Java_com_ftt_gof2d_gofview_cAndroid_1textView_TF_1GetMaxLen") & ~1u;
    n_SetState = rc_sym("Java_com_ftt_gof2d_gofview_cAndroid_1textView_TF_1SetState") & ~1u;
    n_Dismissed = rc_sym("Java_com_ftt_gof2d_gofview_cAndroid_1textView_TF_1keyboardIsDismissedSoRegardUrselfAsDisabled") & ~1u;
    n_SimulateKey = rc_sym("Java_com_ftt_gof2d_GofManager_SimulateKeyPress") & ~1u;
}
