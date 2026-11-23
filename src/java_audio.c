/* com/ftt/gof2d/audio/* (classes.dex) on an SDL3 mixer.
 *  GofAudioBridge.newForMediaPlayer / newForSoundPool / newForAudioTrack -> GofAudioData(type, srcPath)
 *  GofAudioBridge.createClip(data, cQueueObjPtr) -> GofAudioClip{MediaPlayer,SoundPool,AudioTrack}
 *  clip.playClip(n) / stopClip / pauseClip / resumeClip / releaseClip / setVolume(0..100)
 * End-of-playback goes back to the natives GofAudioClip.audioPlaybackEnded(I),
 * GofAudioBridge.EndCallBack(I) and GofAudioBridge.EmulateEndCallBackAfterImpl(II).
 * res/raw/*.mp3 are decoded with Windows Media Foundation (system codec, like Android's). */
#define COBJMACROS
#include "java.h"
#include "apk.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#else
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_libs/dr_mp3.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIX_RATE 44100

/* ------------------------------------------------------------------ decoded sounds */
typedef struct Sound { char name[64]; int16_t *pcm; uint32_t frames; struct Sound *next; } Sound; /* stereo, MIX_RATE */
static Sound *g_sounds;
static SDL_Mutex *g_snd_lock;

#ifdef _WIN32
static Sound *decode_mp3(const char *res) {
    uint32_t off, size;
    if (!apk_find_raw(res, &off, &size)) { host_log("[audio] raw resource %s not found", res); return NULL; }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    void *bytes = apk_read(off, size);
    IStream *ms = SHCreateMemStream(bytes, size);
    free(bytes);
    if (!ms) return NULL;
    IMFByteStream *bs = NULL;
    IMFSourceReader *rd = NULL;
    Sound *s = NULL;
    if (FAILED(MFCreateMFByteStreamOnStream(ms, &bs))) goto done;
    IMFAttributes *attr = NULL;
    MFCreateAttributes(&attr, 1);
    if (FAILED(MFCreateSourceReaderFromByteStream(bs, attr, &rd))) { if (attr) IMFAttributes_Release(attr); goto done; }
    if (attr) IMFAttributes_Release(attr);
    IMFMediaType *mt = NULL;
    MFCreateMediaType(&mt);
    IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_NUM_CHANNELS, 2);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_SAMPLES_PER_SECOND, MIX_RATE);
    HRESULT hr = IMFSourceReader_SetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt);
    IMFMediaType_Release(mt);
    if (FAILED(hr)) { host_log("[audio] %s: PCM conversion unsupported (%08lx)", res, hr); goto done; }
    size_t cap = 1 << 20, len = 0;
    uint8_t *buf = malloc(cap);
    for (;;) {
        DWORD flags = 0;
        IMFSample *smp = NULL;
        if (FAILED(IMFSourceReader_ReadSample(rd, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, NULL, &smp))) break;
        if (smp) {
            IMFMediaBuffer *mb = NULL;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(smp, &mb))) {
                BYTE *p; DWORD n;
                IMFMediaBuffer_Lock(mb, &p, NULL, &n);
                if (len + n > cap) { while (len + n > cap) cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, p, n);
                len += n;
                IMFMediaBuffer_Unlock(mb);
                IMFMediaBuffer_Release(mb);
            }
            IMFSample_Release(smp);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    s = calloc(1, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", res);
    s->pcm = (int16_t *)buf;
    s->frames = (uint32_t)(len / 4);
    host_log("[audio] decoded %s: %u frames", res, s->frames);
done:
    if (rd) IMFSourceReader_Release(rd);
    if (bs) IMFByteStream_Release(bs);
    IStream_Release(ms);
    return s;
}

#else
static Sound *decode_mp3(const char *res) {
    /* non-Windows hosts: dr_mp3 decodes, SDL converts to the mixer format (stereo s16 MIX_RATE) */
    uint32_t off, size;
    if (!apk_find_raw(res, &off, &size)) { host_log("[audio] raw resource %s not found", res); return NULL; }
    void *bytes = apk_read(off, size);
    drmp3_config cfg;
    drmp3_uint64 nfr = 0;
    drmp3_int16 *pcm = drmp3_open_memory_and_read_pcm_frames_s16(bytes, size, &cfg, &nfr, NULL);
    free(bytes);
    if (!pcm) { host_log("[audio] %s: decode failed", res); return NULL; }
    SDL_AudioSpec src = {SDL_AUDIO_S16, (int)cfg.channels, (int)cfg.sampleRate}, dst = {SDL_AUDIO_S16, 2, MIX_RATE};
    Uint8 *out = NULL; int outlen = 0;
    int ok = SDL_ConvertAudioSamples(&src, (const Uint8 *)pcm, (int)(nfr * cfg.channels * 2), &dst, &out, &outlen);
    drmp3_free(pcm, NULL);
    if (!ok) return NULL;
    Sound *s = calloc(1, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", res);
    s->pcm = (int16_t *)out;
    s->frames = (uint32_t)(outlen / 4);
    host_log("[audio] decoded %s: %u frames", res, s->frames);
    return s;
}

#endif
/* GofFileBridge.getResId: name before the first '.', lower case */
static void res_name(const char *src, char *out, size_t n) {
    size_t i = 0;
    for (; src[i] && src[i] != '.' && i + 1 < n; i++) out[i] = (char)tolower((unsigned char)src[i]);
    out[i] = 0;
}

static Sound *get_sound(const char *src) {
    char res[64];
    res_name(src, res, sizeof res);
    SDL_LockMutex(g_snd_lock);
    for (Sound *s = g_sounds; s; s = s->next) if (!strcmp(s->name, res)) { SDL_UnlockMutex(g_snd_lock); return s; }
    SDL_UnlockMutex(g_snd_lock);
    Sound *s = decode_mp3(res);
    if (!s) return NULL;
    SDL_LockMutex(g_snd_lock);
    s->next = g_sounds;
    g_sounds = s;
    SDL_UnlockMutex(g_snd_lock);
    return s;
}

/* ------------------------------------------------------------------ mixer voices */
#define MAX_VOICES 32
typedef struct {
    int active, paused;
    Sound *snd;
    uint32_t pos;
    int loops_left; /* -1 infinite, else number of extra passes */
    float vol;
    int id;
    void (*on_end)(int id);
} Voice;
static Voice g_voice[MAX_VOICES];
static int g_next_id = 1;
static SDL_AudioStream *g_stream;

static void SDLCALL mix_cb(void *ud, SDL_AudioStream *st, int additional, int total) {
    (void)ud; (void)total;
    int frames = additional / 8;
    if (frames <= 0) return;
    float *out = SDL_calloc(frames * 2, sizeof(float));
    int ended[MAX_VOICES], nended = 0;
    SDL_LockMutex(g_snd_lock);
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vo = &g_voice[v];
        if (!vo->active || vo->paused || !vo->snd) continue;
        for (int i = 0; i < frames; i++) {
            if (vo->pos >= vo->snd->frames) {
                if (vo->loops_left != 0) { if (vo->loops_left > 0) vo->loops_left--; vo->pos = 0; if (!vo->snd->frames) break; }
                else { vo->active = 0; ended[nended++] = v; break; }
            }
            out[2 * i] += vo->snd->pcm[2 * vo->pos] * (vo->vol / 32768.0f);
            out[2 * i + 1] += vo->snd->pcm[2 * vo->pos + 1] * (vo->vol / 32768.0f);
            vo->pos++;
        }
    }
    SDL_UnlockMutex(g_snd_lock);
    SDL_PutAudioStreamData(st, out, frames * 8);
    {   /* EH_WAVDUMP=<file>: raw float32 stereo copy of the mix (test only) */
        static FILE *dump; static int checked;
        if (!checked) { checked = 1; if (getenv("EH_WAVDUMP")) dump = fopen(getenv("EH_WAVDUMP"), "wb"); }
        if (dump) { fwrite(out, 8, frames, dump); fflush(dump); }
    }
    SDL_free(out);
    for (int k = 0; k < nended; k++) if (g_voice[ended[k]].on_end) g_voice[ended[k]].on_end(g_voice[ended[k]].id);
}

static int voice_start(Sound *s, int loops, float vol, void (*on_end)(int)) {
    SDL_LockMutex(g_snd_lock);
    int slot = -1;
    for (int v = 0; v < MAX_VOICES; v++) if (!g_voice[v].active) { slot = v; break; }
    if (slot < 0) { SDL_UnlockMutex(g_snd_lock); return 0; }
    Voice *vo = &g_voice[slot];
    memset(vo, 0, sizeof *vo);
    vo->active = 1; vo->snd = s; vo->loops_left = loops; vo->vol = vol; vo->on_end = on_end;
    vo->id = g_next_id++;
    int id = vo->id;
    SDL_UnlockMutex(g_snd_lock);
    return id;
}
static Voice *voice_find(int id) {
    if (!id) return NULL;
    for (int v = 0; v < MAX_VOICES; v++) if (g_voice[v].active && g_voice[v].id == id) return &g_voice[v];
    return NULL;
}
static void voice_stop(int id) { SDL_LockMutex(g_snd_lock); Voice *v = voice_find(id); if (v) v->active = 0; SDL_UnlockMutex(g_snd_lock); }
static void voice_pause(int id, int p) { SDL_LockMutex(g_snd_lock); Voice *v = voice_find(id); if (v) v->paused = p; SDL_UnlockMutex(g_snd_lock); }
static void voice_volume(int id, float vol) { SDL_LockMutex(g_snd_lock); Voice *v = voice_find(id); if (v) v->vol = vol; SDL_UnlockMutex(g_snd_lock); }

/* ------------------------------------------------------------------ callbacks to natives (Java event thread) */
typedef struct { int kind; uint32_t ptr, arg; uint32_t obj; } AEvent; /* 0 audioPlaybackEnded, 1 EndCallBack, 2 EmulateEndCallBackAfterImpl */
#define MAX_EV 256
static AEvent g_ev[MAX_EV];
static int g_ev_head, g_ev_tail;
static SDL_Mutex *g_ev_lock;
static SDL_Condition *g_ev_cond;
static uint32_t g_n_playbackEnded, g_n_EndCallBack, g_n_EmulateEnd;

static void post_event(int kind, uint32_t obj, uint32_t ptr, uint32_t arg) {
    SDL_LockMutex(g_ev_lock);
    int nt = (g_ev_tail + 1) % MAX_EV;
    if (nt != g_ev_head) { g_ev[g_ev_tail] = (AEvent){kind, ptr, arg, obj}; g_ev_tail = nt; }
    SDL_SignalCondition(g_ev_cond);
    SDL_UnlockMutex(g_ev_lock);
}
static int SDLCALL event_thread(void *p) {
    (void)p;
    Cpu *c = rc_new_cpu(512 << 10);
    for (;;) {
        SDL_LockMutex(g_ev_lock);
        while (g_ev_head == g_ev_tail) SDL_WaitCondition(g_ev_cond, g_ev_lock);
        AEvent e = g_ev[g_ev_head];
        g_ev_head = (g_ev_head + 1) % MAX_EV;
        SDL_UnlockMutex(g_ev_lock);
        uint32_t args[4] = {jni_env(), e.obj, e.ptr, e.arg};
        uint32_t fn = e.kind == 0 ? g_n_playbackEnded : e.kind == 1 ? g_n_EndCallBack : g_n_EmulateEnd;
        rc_call(c, fn, e.kind == 2 ? 4 : 3, args);
    }
    return 0;
}

/* ------------------------------------------------------------------ Java objects */
enum { T_MEDIAPLAYER = 0, T_SOUNDPOOL = 1, T_AUDIOTRACK = 2 };
typedef struct { int type; char src[128]; Sound *snd; int runtime; int soundPoolId; } AData;
typedef struct {
    AData *data;
    uint32_t qptr;      /* mCQueueObjPtr */
    JObj *self;
    int voice;          /* playing voice */
    int streamID;       /* SoundPool: looping stream */
    int sidForSetVolume;
    int loop_max, loop_cur, looping;
    float vol;
    int released;
} AClip;

#define JM(name) static uint64_t name(Cpu *c, JObj *self, const uint64_t *a)
#define UNUSED (void)c; (void)self; (void)a

static JObj *new_data(int type, const char *src) {
    AData *d = calloc(1, sizeof *d);
    d->type = type;
    snprintf(d->src, sizeof d->src, "%s", src ? src : "");
    d->snd = get_sound(d->src);
    JObj *o = jobj_new("com/ftt/gof2d/audio/GofAudioData", JK_INSTANCE);
    o->native = d;
    o->global = 1;
    return o;
}
JM(ab_newForMediaPlayer) { UNUSED; return JRET_OBJ(new_data(T_MEDIAPLAYER, JSTR(a[0]))); }
JM(ab_newForAudioTrack) { UNUSED; return JRET_OBJ(new_data(T_AUDIOTRACK, JSTR(a[0]))); }
/* newForSoundPool(src, runtime): prepareForSoundPool; null when SoundPool.load failed (id 0) */
JM(ab_newForSoundPool) {
    UNUSED;
    JObj *o = new_data(T_SOUNDPOOL, JSTR(a[0]));
    AData *d = o->native;
    d->runtime = (int)a[1];
    d->soundPoolId = d->snd ? 1 : 0;
    if (!d->soundPoolId) return 0;
    return JRET_OBJ(o);
}
/* GofAudioData.clone(): Object.clone() field copy */
JM(ad_clone) {
    UNUSED;
    AData *d = malloc(sizeof *d);
    *d = *(AData *)self->native;
    JObj *o = jobj_new(self->cls, JK_INSTANCE);
    o->native = d;
    o->global = 1;
    return JRET_OBJ(o);
}
JM(ab_createClip) {
    UNUSED;
    JObj *dobj = GP((uint32_t)a[0]);
    if (!dobj) return 0;
    AClip *k = calloc(1, sizeof *k);
    k->data = dobj->native;
    k->qptr = (uint32_t)a[1];
    k->vol = 1.0f;
    static const char *cls[] = {"com/ftt/gof2d/audio/GofAudioClipMediaPlayer", "com/ftt/gof2d/audio/GofAudioClipSoundPool", "com/ftt/gof2d/audio/GofAudioClipAudioTrack"};
    JObj *o = jobj_new(cls[k->data->type], JK_INSTANCE);
    o->native = k;
    o->global = 1;
    k->self = o;
    return JRET_OBJ(o);
}
/* vibration: cAndMediaMgr::PlayVibJNI @00164248 calls playVib(int) (the original's Java: android.os.Vibrator.vibrate),
 * StopVibJNI @0016421c stopVib(); the option toggle gates it (cAndMediaMgr::SetVibOnOff @001640e8). The port sends it
 * to the gamepad in use (rumble) or the phone's vibrator, scaled by the vibration strength setting (ui/input.cpp). */
void ui_vibrate(int ms);
JM(ab_playVib) { UNUSED; ui_vibrate((int)a[0]); return 0; }
JM(ab_stopVib) { UNUSED; ui_vibrate(0); return 0; }

/* map voice id -> clip for completion handling */
static AClip *g_vclip[4096];
static void mp_on_end(int id) {
    /* MediaPlayer.onCompletion (only reached when not looping): play_loop_current++ == max -> stopClip, audioPlaybackEnded */
    AClip *k = g_vclip[id & 4095];
    if (!k || k->released) return;
    k->voice = 0;
    if (k->loop_max > 0 && ++k->loop_cur == k->loop_max) post_event(0, HPTR(k->self), k->qptr, 0);
}

static int is_mp(AClip *k) { return k->data->type != T_SOUNDPOOL; }

JM(cl_playClip) {
    UNUSED;
    AClip *k = self->native;
    int n = (int)a[0];
    if (!k->data->snd) return (uint32_t)-1;
    if (is_mp(k)) {
        /* setLoopCount(n): n == 1 -> setLooping(false), else setLooping(true); then start() */
        if (k->voice) voice_stop(k->voice);
        k->loop_max = n; k->loop_cur = 0; k->looping = n != 1;
        k->voice = voice_start(k->data->snd, k->looping ? -1 : 0, k->vol, mp_on_end);
        g_vclip[k->voice & 4095] = k;
        return 0;
    }
    /* SoundPool.play(id, 1, 1, 1, n - 1, 1): loop -1 = infinite */
    int loop = n - 1;
    /* a skipped scene runs its frames unpaced (java_set_turbo): its one-shot effects play silent (loops keep their volume), the clip logic is unchanged */
    int sid = voice_start(k->data->snd, loop < 0 ? -1 : loop, java_turbo() && loop >= 0 ? 0.f : k->vol, NULL);
    k->sidForSetVolume = sid;
    if (loop == -1) k->streamID = sid;
    else {
        k->streamID = 0;
        post_event(2, HPTR(g_audiobr), k->qptr, (uint32_t)k->data->runtime); /* EmulateEndCallBackAfter(this, data.runtime) */
    }
    return sid ? (uint32_t)sid : (uint32_t)-1;
}
JM(cl_stopClip) {
    UNUSED;
    AClip *k = self->native;
    if (is_mp(k)) { if (k->voice) { voice_stop(k->voice); k->voice = 0; } return 0; }
    if (k->streamID) {
        voice_stop(k->streamID);
        k->streamID = 0;
        post_event(1, HPTR(g_audiobr), k->qptr, 0); /* EndCallBack(mCQueueObjPtr) */
    }
    k->sidForSetVolume = 0;
    return 0;
}
JM(cl_pauseClip) {
    UNUSED;
    AClip *k = self->native;
    if (is_mp(k)) {
        /* MediaPlayer: pause() if looping, else stopClip() */
        if (k->voice) { if (k->looping) voice_pause(k->voice, 1); else { voice_stop(k->voice); k->voice = 0; } }
        return 0;
    }
    if (k->streamID) voice_pause(k->streamID, 1);
    return 0;
}
JM(cl_resumeClip) {
    UNUSED;
    AClip *k = self->native;
    if (is_mp(k)) {
        if (k->voice) voice_pause(k->voice, 0);
        else if (k->data->snd) { k->voice = voice_start(k->data->snd, k->looping ? -1 : 0, k->vol, mp_on_end); g_vclip[k->voice & 4095] = k; }
        return 0;
    }
    if (k->streamID) voice_pause(k->streamID, 0);
    return 0;
}
JM(cl_releaseClip) {
    UNUSED;
    AClip *k = self->native;
    if (is_mp(k)) { if (k->voice) voice_stop(k->voice); k->voice = 0; k->released = 1; }
    return 0;
}
JM(cl_setVolume) {
    UNUSED;
    AClip *k = self->native;
    int v = (int)a[0];
    if (v < 0) v = 0;
    k->vol = v / 100.0f;
    if (is_mp(k)) { if (k->voice) voice_volume(k->voice, k->vol); }
    else if (k->sidForSetVolume) voice_volume(k->sidForSetVolume, k->vol);
    return 0;
}
JM(cl_isPlaying) { UNUSED; AClip *k = self->native; return is_mp(k) ? (k->voice != 0) : 1; }

void java_audio_register(void) {
    g_snd_lock = SDL_CreateMutex();
    g_ev_lock = SDL_CreateMutex();
    g_ev_cond = SDL_CreateCondition();
#ifdef _WIN32
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
#endif
    const char *AB = "com/ftt/gof2d/audio/GofAudioBridge";
    jni_register(AB, "newForMediaPlayer", NULL, ab_newForMediaPlayer);
    jni_register(AB, "newForSoundPool", NULL, ab_newForSoundPool);
    jni_register(AB, "newForAudioTrack", NULL, ab_newForAudioTrack);
    jni_register(AB, "createClip", NULL, ab_createClip);
    jni_register("com/ftt/gof2d/audio/GofAudioData", "clone", NULL, ad_clone);
    jni_register(AB, "playVib", NULL, ab_playVib);
    jni_register(AB, "stopVib", NULL, ab_stopVib);
    jni_register(NULL, "playClip", NULL, cl_playClip);
    jni_register(NULL, "stopClip", NULL, cl_stopClip);
    jni_register(NULL, "pauseClip", NULL, cl_pauseClip);
    jni_register(NULL, "resumeClip", NULL, cl_resumeClip);
    jni_register(NULL, "releaseClip", NULL, cl_releaseClip);
    jni_register(NULL, "setVolume", NULL, cl_setVolume);
    jni_register(NULL, "isPlaying", NULL, cl_isPlaying);
    g_n_playbackEnded = rc_sym("Java_com_ftt_gof2d_audio_GofAudioClip_audioPlaybackEnded") & ~1u;
    g_n_EndCallBack = rc_sym("Java_com_ftt_gof2d_audio_GofAudioBridge_EndCallBack") & ~1u;
    g_n_EmulateEnd = rc_sym("Java_com_ftt_gof2d_audio_GofAudioBridge_EmulateEndCallBackAfterImpl") & ~1u;
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, MIX_RATE};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix_cb, NULL);
    if (g_stream) SDL_ResumeAudioStreamDevice(g_stream);
    else host_log("[audio] no audio device: %s", SDL_GetError());
    SDL_DetachThread(SDL_CreateThread(event_thread, "JavaAudioEvents", NULL));
}

void java_audio_shutdown(void) {
    if (!g_stream) return;
    SDL_PauseAudioStreamDevice(g_stream);
    SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
}
