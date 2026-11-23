// RmlUi setup, documents and per-frame composition (game layer + modern interface).
#include "ui.h"
#include "ui_internal.h"
#include "render.h"
#include "screens.h"
#include "capture_api.h"
#include "settings.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include "gl_api.h"
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace ui {
SDL_Window *window;
Rml::Context *context;
ViewRect game_rect;
int win_w, win_h;
float dp = 1.f;
static SDL_GLContext g_gl;
static std::string g_base;
static std::string g_shot;

std::string asset_path(const std::string &rel) { return g_base + rel; }

// ---------------------------------------------------------------- file access through SDL (Android assets too)
class FileInterfaceSDL : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String &path) override {
        std::string p = path;
        SDL_IOStream *io = SDL_IOFromFile(p.c_str(), "rb");
        if (!io && p.rfind(g_base, 0) != 0) io = SDL_IOFromFile((g_base + p).c_str(), "rb");
        return (Rml::FileHandle)io;
    }
    void Close(Rml::FileHandle file) override { SDL_CloseIO((SDL_IOStream *)file); }
    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override { return SDL_ReadIO((SDL_IOStream *)file, buffer, size); }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        SDL_IOWhence w = origin == SEEK_SET ? SDL_IO_SEEK_SET : origin == SEEK_CUR ? SDL_IO_SEEK_CUR : SDL_IO_SEEK_END;
        return SDL_SeekIO((SDL_IOStream *)file, offset, w) >= 0;
    }
    size_t Tell(Rml::FileHandle file) override { return (size_t)SDL_TellIO((SDL_IOStream *)file); }
};

// ---------------------------------------------------------------- renderer with dynamic texture sources
// "game:frame"  -> the presenter's upscaled game layer (GL texture owned by render.cpp)
// "sprite:<id>" -> pixel-art captured from the original (capture.cpp), pre-upscaled with nearest neighbour
class Renderer : public RenderInterface_GL3 {
public:
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dims, const Rml::String &source) override {
        if (source == "game:frame") {
            dims = {game_tex_w, game_tex_h};
            return (Rml::TextureHandle)present::game_texture();
        }
        if (source.rfind("live:", 0) == 0) {
            int w = 1, h = 1;
            unsigned t = present::live_texture(source.substr(5), &w, &h);
            dims = {w, h};
            if (t) return (Rml::TextureHandle)t;
            static const uint8_t z[4] = {0, 0, 0, 0};
            return GenerateTexture({z, 4}, {1, 1});
        }
        if (source.rfind("sprite:", 0) == 0) {
            int w = 0, h = 0;
            const uint8_t *rgba = sprites_lookup(source.c_str() + 7, &w, &h);
            if (!rgba) { dims = {1, 1}; static const uint8_t z[4] = {0, 0, 0, 0}; return GenerateTexture({z, 4}, {1, 1}); }
            dims = {w, h};
            Rml::TextureHandle t = GenerateTexture({rgba, (size_t)w * h * 4}, {w, h});
            // the backend creates textures with GL_REPEAT: with linear filtering a sprite's opposite edge bleeds in
            // as a faint line along the image border
            glBindTexture(GL_TEXTURE_2D, (GLuint)t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            return t;
        }
        return RenderInterface_GL3::LoadTexture(dims, source);
    }
    void ReleaseTexture(Rml::TextureHandle h) override {
        if ((GLuint)h == present::game_texture() || present::is_live((GLuint)h)) return;
        RenderInterface_GL3::ReleaseTexture(h);
    }
    int game_tex_w = 1, game_tex_h = 1;
};

static Renderer *g_render;
// RmlUi's warnings and errors go to the port's log (epichearts.log / logcat) instead of the debug output
struct SystemInterface : SystemInterface_SDL {
    using SystemInterface_SDL::SystemInterface_SDL;
    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
        if (type <= Rml::Log::LT_WARNING) ui_log("[rml] %s", message.c_str());
        return true;
    }
};
static SystemInterface *g_system;
static FileInterfaceSDL g_files;

// x and y scale separately: layout_game_rect stretches the frame to the window when the fit is within 2%, and touches
// map back with each axis' own scale (ui_window_to_game). One scale from the width put forms and captions up to
// 2% off where their taps land (5 game px at the bottom of a 1600x770 window on a 676x320 frame).
FormMap form_map() {
    return {game_rect.x, game_rect.y, game_rect.w / (float)::g_game_w, game_rect.h / (float)::g_game_h};
}

static void layout_game_rect() {
    SDL_GetWindowSizeInPixels(window, &win_w, &win_h);
    float gw = (float)::g_game_w, gh = (float)::g_game_h;
    float s = std::min(win_w / gw, win_h / gh);
    game_rect.w = std::round(gw * s);
    game_rect.h = std::round(gh * s);
    game_rect.x = std::floor((win_w - game_rect.w) / 2);
    game_rect.y = std::floor((win_h - game_rect.h) / 2);
    // the framebuffer width is rounded to an even number of pixels: when the fit is that close to the window,
    // fill it exactly instead of leaving a one-pixel bar (the non-uniform scale is well under 1%)
    if (std::fabs(game_rect.w - win_w) <= win_w * 0.02f && std::fabs(game_rect.h - win_h) <= win_h * 0.02f) game_rect = {0, 0, (float)win_w, (float)win_h};
    // one pixel scale for every map: a map smaller than the view stays centred in black margins, as the original
    // shows it (cField::AdjustToInner @00122568 centres it, cGameWorld::Draw @0012e574 clears around it)
    // RmlUi dp: layouts are authored for a 1280x720 reference; phones get a slightly larger UI
#ifdef __ANDROID__
    dp = std::max(0.5f, win_h / 600.f);
#else
    dp = std::max(0.5f, win_h / 720.f);
#endif
    dp *= settings::get().ui_scale / 100.f;
    // the screens need at least 600 dp of height: a larger interface scale never shrinks the usable height below
    // that (on phones the base already uses all of it, so scales above 100% have no effect there)
    dp = std::min(dp, std::max(0.5f, win_h / 600.f));
    if (context) {
        context->SetDimensions({win_w, win_h});
        context->SetDensityIndependentPixelRatio(dp);
    }
    g_render->SetViewport(win_w, win_h);
    int lw, lh;
    SDL_GetWindowSize(window, &lw, &lh);
    ui_log("[ui] layout: window %dx%d (pixels %dx%d), dp %.2f, game rect %.0f,%.0f %.0fx%.0f", lw, lh, win_w, win_h, dp, game_rect.x, game_rect.y, game_rect.w, game_rect.h);
}

static void find_base() {
#ifdef __ANDROID__
    g_base = "ui/";
#else
    const char *cands[] = {"data/ui/", "../data/ui/"};
    std::string exe = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    std::vector<std::string> all = {exe + "data/ui/", exe + "../data/ui/", cands[0], cands[1]};
    g_base = cands[0];
    for (auto &c : all) {
        SDL_PathInfo pi;
        if (SDL_GetPathInfo((c + "fonts").c_str(), &pi)) { g_base = c; break; }
    }
#endif
    for (auto &ch : g_base) if (ch == '\\') ch = '/';
}

} // namespace ui

using namespace ui;

extern "C" // the interface's text font is the original game's own: res/raw/fttcustom of the original APK (NanumGothic OTF Bold,
// all 11,172 Hangul syllables; the font GofFontBridge renders the game's text with, java_font.c), registered as family
// "EpicHearts" and as the fallback face. One weight: every font-weight resolves to it.
extern "C" int apk_find_raw(const char *resname, uint32_t *off, uint32_t *size);
extern "C" void *apk_read(uint32_t off, uint32_t size);
// The font's vertical metrics (ascender 800 / descender -300 of 1000) put its glyphs low in the line: the ink of a
// Hangul syllable spans -158..791, centred 317 above the baseline, while the line box is centred at 250. The copy given
// to the font engine gets ascender 867 / descender -233 (same 1100 line height, centre 317), so text sits in the middle
// of its line box; the icon font is shifted to the same centre (tools/subset_fonts.py). The file in the APK is unchanged.
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)((uint16_t)v >> 8); p[1] = (uint8_t)v; }
static void center_metrics(std::vector<uint8_t> &f, int asc, int desc) {
    if (f.size() < 12) return;
    int n = be16(&f[4]);
    for (int i = 0; i < n && 12 + 16 * (size_t)i + 16 <= f.size(); i++) {
        const uint8_t *e = &f[12 + 16 * i];
        uint32_t off = (uint32_t)e[8] << 24 | e[9] << 16 | e[10] << 8 | e[11];
        if (!memcmp(e, "hhea", 4) && off + 8 <= f.size()) { put16(&f[off + 4], asc); put16(&f[off + 6], desc); }
        if (!memcmp(e, "OS/2", 4) && off + 78 <= f.size()) {
            put16(&f[off + 68], asc); put16(&f[off + 70], desc);    // sTypoAscender / sTypoDescender
            put16(&f[off + 74], asc); put16(&f[off + 76], -desc);   // usWinAscent / usWinDescent
        }
    }
}
static bool load_game_font() {
    uint32_t off, size;
    if (!apk_find_raw("fttcustom", &off, &size)) return false;
    const uint8_t *src = (const uint8_t *)apk_read(off, size);
    static std::vector<uint8_t> font;                     // kept for the font engine's lifetime
    font.assign(src, src + size);
    center_metrics(font, 867, -233);
    return Rml::LoadFontFace({(const Rml::byte *)font.data(), font.size()}, "EpicHearts", Rml::Style::FontStyle::Normal,
                             Rml::Style::FontWeight::Auto, true);
}


// the largest interface scale (%) that keeps the 600 dp the screens need (layout() caps dp the same way)
int ui_max_scale() {
#ifdef __ANDROID__
    return 100;          // dp base = height / 600
#else
    return 120;          // dp base = height / 720
#endif
}

int ui_init(SDL_Window *win) {
    window = win;
    g_gl = SDL_GL_CreateContext(win);
    if (!g_gl) { ui_log("GL context: %s", SDL_GetError()); return 0; }
    SDL_GL_MakeCurrent(win, g_gl);
    SDL_GL_SetSwapInterval(1);
    Rml::String msg;
    if (!RmlGL3::Initialize(&msg)) { ui_log("GL load failed: %s", msg.c_str()); return 0; }
    ui_log("[ui] %s", msg.c_str());
    present::init();
    capture::modern = getenv("EH_CLASSIC") == nullptr;
    capture::force_strip = getenv("EH_STRIP") != nullptr;
    capture::cover::on = getenv("EH_SENTINEL") != nullptr;
    find_base();
    ui_log("[ui] assets at %s", g_base.c_str());

    g_render = new Renderer();
    if (!*g_render) { ui_log("[ui] RmlUi GL3 renderer failed"); return 0; }
    g_system = new SystemInterface(win);
    Rml::SetRenderInterface(g_render);
    Rml::SetSystemInterface(g_system);
    Rml::SetFileInterface(&g_files);
    Rml::Initialise();
    struct { const char *f; bool fallback; } fonts[] = {
        {"fonts/lucide.ttf", false}, // icons (subset by tools/subset_fonts.py)
    };
    if (!load_game_font()) ui_log("[ui] the original's font (fttcustom) failed");
    for (auto &f : fonts)
        if (!Rml::LoadFontFace(asset_path(f.f), f.fallback)) ui_log("[ui] font %s failed", f.f);

    SDL_GetWindowSizeInPixels(win, &win_w, &win_h);
    context = Rml::CreateContext("main", {win_w, win_h});
    if (!context) return 0;
    layout_game_rect();
    settings::load();
    if (getenv("EH_WINSIZE")) settings::get().fullscreen = false; // test runs keep their window
    settings::apply();
    layout_game_rect();
    screens::init();
    return 1;
}

extern "C" void ui_shutdown(void) {
    screens::shutdown();
    Rml::Shutdown();
    delete g_render;
    delete g_system;
    RmlGL3::Shutdown();
    if (g_gl) SDL_GL_DestroyContext(g_gl);
}

extern "C" void ui_present_game_frame(const uint16_t *px, int w, int h) {
    present::set_frame(px, w, h);
    capture_on_frame(); // pairs the original's UI draw list with this frame (capture.cpp)
}

// window pixels -> framebuffer pixels: the world and the original's own forms are both shown through game_rect
extern "C" int ui_window_to_game(float wx, float wy, int *gx, int *gy) {
    if (game_rect.w < 1 || game_rect.h < 1) { *gx = *gy = 0; return 0; } // minimized window
    float x = (wx - game_rect.x) * ::g_game_w / game_rect.w;
    float y = (wy - game_rect.y) * ::g_game_h / game_rect.h;
    *gx = (int)std::floor(x);
    *gy = (int)std::floor(y);
    return x >= 0 && y >= 0 && x < ::g_game_w && y < ::g_game_h;
}

extern "C" void ui_toggle_fullscreen(void) {
    settings::get().fullscreen = !settings::get().fullscreen;
    settings::apply();
    settings::save();
}
void ui_relayout() { layout_game_rect(); }

extern "C" void ui_update(void) {
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w != win_w || h != win_h) layout_game_rect();
    // RmlUi's SDL platform sets the dp ratio to the OS display scale on window events; the port scales the
    // interface with the window height instead (layouts are authored for 720 lines)
    if (context->GetDensityIndependentPixelRatio() != dp) context->SetDensityIndependentPixelRatio(dp);
    input::update();
    // test: EH_UIPERF logs the mean time of the screens' update and of RmlUi's update (data models, layout) every 300 frames
    static const bool perf = getenv("EH_UIPERF") != nullptr;
    static double t_scr, t_ctx;
    static int frames;
    uint64_t t0 = perf ? SDL_GetPerformanceCounter() : 0;
    screens::update();
    uint64_t t1 = perf ? SDL_GetPerformanceCounter() : 0;
    context->Update();
    if (perf) {
        double f = 1000.0 / SDL_GetPerformanceFrequency();
        t_scr += (t1 - t0) * f;
        t_ctx += (SDL_GetPerformanceCounter() - t1) * f;
        if (++frames == 300) {
            ui_log("[perf] screens::update %.3f ms, context Update %.3f ms (mean of 300 frames)", t_scr / frames, t_ctx / frames);
            t_scr = t_ctx = 0;
            frames = 0;
        }
    }
}

extern "C" void ui_render_and_swap(void) {
    present::render_game((int)game_rect.w, (int)game_rect.h);
    g_render->game_tex_w = (int)game_rect.w;
    g_render->game_tex_h = (int)game_rect.h;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glClearColor(0.02f, 0.02f, 0.035f, 1.f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    g_render->BeginFrame();
    context->Render();
    g_render->EndFrame();
    if (!g_shot.empty()) {
        present::save_backbuffer(g_shot.c_str(), win_w, win_h);
        g_shot.clear();
    }
    SDL_GL_SwapWindow(window);
}

extern "C" void ui_request_shot(const char *path) { g_shot = path; }

extern "C" int ui_handle_event(SDL_Event *e) { return input::handle_event(*e); }

extern "C" void ui_test_command(const char *line) { input::test_command(line); }
