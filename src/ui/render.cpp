// GL presenter: the original's RGB565 software framebuffer -> GL texture -> sharp-bilinear upscale into an FBO at
// window resolution. RmlUi draws that FBO as the bottom layer ("game-view" element, texture source "game:frame"),
// so UI effects such as backdrop blur see the game.
#include "render.h"
#include "ui_internal.h"
#include "gl_api.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace present {

static GLuint g_src_tex, g_fbo, g_fbo_tex, g_prog, g_vao, g_vbo;
static int g_src_w, g_src_h, g_fbo_w, g_fbo_h;
static std::vector<uint16_t> g_pixels;
static bool g_dirty;
static GLint u_src_size, u_rect, u_tex, u_sharp;
static bool g_sharp = true;
void set_sharp(bool on) { if (g_sharp != on) { g_sharp = on; g_dirty = true; } }

#if defined(__ANDROID__)
#define GLSL_HDR "#version 300 es\nprecision highp float;\n"
#else
#define GLSL_HDR "#version 330 core\n"
#endif

static const char *VS = GLSL_HDR
    "layout(location=0) in vec2 a_pos;\n"
    "uniform vec4 u_rect;\n" // x, y, w, h in clip space
    "out vec2 v_uv;\n"
    "void main(){ v_uv = a_pos; gl_Position = vec4(u_rect.xy + a_pos * u_rect.zw, 0.0, 1.0); }\n";
// sharp bilinear: nearest inside a source pixel, linear across the 1-output-pixel seam
static const char *FS = GLSL_HDR
    "in vec2 v_uv; out vec4 o;\n"
    "uniform sampler2D u_tex; uniform vec2 u_src_size;\n"
    "void main(){\n"
    "  vec2 p = v_uv * u_src_size;\n"
    "  vec2 seam = floor(p + 0.5);\n"
    "  vec2 d = p - seam;\n"
    "  vec2 fw = max(fwidth(p), vec2(1e-5));\n"
    "  d = clamp(d / fw, -0.5, 0.5);\n"
    "  o = vec4(texture(u_tex, (seam + d) / u_src_size).rgb, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        ui_log("[present] shader: %s", log);
    }
    return s;
}

bool init() {
    g_prog = glCreateProgram();
    glAttachShader(g_prog, compile(GL_VERTEX_SHADER, VS));
    glAttachShader(g_prog, compile(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(g_prog);
    u_src_size = glGetUniformLocation(g_prog, "u_src_size");
    u_rect = glGetUniformLocation(g_prog, "u_rect");
    u_tex = glGetUniformLocation(g_prog, "u_tex");
    u_sharp = glGetUniformLocation(g_prog, "u_sharp");
    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
    glGenTextures(1, &g_src_tex);
    glGenTextures(1, &g_fbo_tex);
    glGenFramebuffers(1, &g_fbo);
    return true;
}

void set_frame(const uint16_t *px, int w, int h) {
    g_pixels.assign(px, px + (size_t)w * h);
    g_src_w = w;
    g_src_h = h;
    g_dirty = true;
}

const uint16_t *frame_pixels(int *w, int *h) {
    *w = g_src_w;
    *h = g_src_h;
    return g_pixels.empty() ? nullptr : g_pixels.data();
}

// Renders the latest game frame into the FBO sized out_w x out_h (the on-screen game rect). Returns its texture.
unsigned int render_game(int out_w, int out_h) {
    if (out_w < 1 || out_h < 1) return g_fbo_tex;
    if (out_w != g_fbo_w || out_h != g_fbo_h) {
        glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, out_w, out_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_fbo_w = out_w;
        g_fbo_h = out_h;
        g_dirty = true;
    }
    if (!g_dirty || g_pixels.empty()) return g_fbo_tex;
    g_dirty = false;
    glBindTexture(GL_TEXTURE_2D, g_src_tex);
    static int tw, th;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (tw != g_src_w || th != g_src_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_src_w, g_src_h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, g_pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        tw = g_src_w;
        th = g_src_h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_src_w, g_src_h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, g_pixels.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    GLint prev_fbo = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, out_w, out_h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glUseProgram(g_prog);
    glUniform1i(u_tex, 0);
    glUniform1f(u_sharp, g_sharp ? 1.f : 0.f);
    glUniform2f(u_src_size, (float)g_src_w, (float)g_src_h);
    glUniform4f(u_rect, -1.f, -1.f, 2.f, 2.f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_src_tex);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    return g_fbo_tex;
}

unsigned int game_texture() { return g_fbo_tex; }

// live textures: small pixel regions captured from the original's frame (e.g. the minimap), updated every frame
struct Live { GLuint tex = 0; int w = 0, h = 0; };
static std::map<std::string, Live> g_live;
void live_update(const std::string &name, const uint8_t *rgba, int w, int h) {
    Live &l = g_live[name];
    if (!l.tex) glGenTextures(1, &l.tex);
    glBindTexture(GL_TEXTURE_2D, l.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (l.w != w || l.h != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        l.w = w; l.h = h;
    } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
unsigned int live_texture(const std::string &name, int *w, int *h) {
    auto it = g_live.find(name);
    if (it == g_live.end()) return 0;
    *w = it->second.w; *h = it->second.h;
    return it->second.tex;
}
bool is_live(unsigned int tex) { for (auto &kv : g_live) if (kv.second.tex == tex) return true; return false; }

bool save_backbuffer(const char *path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    SDL_Surface *sf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!sf) return false;
    for (int y = 0; y < h; y++) memcpy((uint8_t *)sf->pixels + (size_t)y * sf->pitch, px.data() + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    bool ok;
    std::string p = path;
    if (p.size() > 4 && p.substr(p.size() - 4) == ".png") ok = SDL_SavePNG(sf, path);
    else ok = SDL_SaveBMP(sf, path);
    SDL_DestroySurface(sf);
    return ok;
}

} // namespace present
