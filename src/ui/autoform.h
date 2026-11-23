// Generic modern rendering of original forms that have no dedicated modern screen.
// The original's draw calls inside cflUi::Draw are recorded (text runs, sprite images / frames, numbers), the
// form's element tree gives panels and buttons; the modern UI redraws these crisply in its own style.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Cpu;

namespace autoform {

struct Cmd {
    int kind;            // 0 sprite image, 1 sprite frame, 2 text run, 3 number
    int form;            // index into Frame::forms
    int x, y;            // framebuffer position of the draw point
    std::string key;     // sprite key (kind 0/1)
    std::string text;    // RML-escaped text (kind 2/3)
    uint32_t color;      // 0xBBGGRRxx text colour
    int font_h;          // glyph height (px)
    int align;           // number alignment (DrawNumber param: 1 centre, 2 right)
    int clip[4];         // clip box in framebuffer coordinates
    int set = -1, idx = -1; // sprite set number and image / frame index (kind 0/1)
    bool frm = false;       // idx is a DrawFrame frame
    int iw = 0, ih = 0;     // the drawn image's own size (kind 0), before the screen edge clips the capture
};

struct Box {
    int form;
    int kind;            // 0 form root, 1 panel, 2 button, 3 list slot, 4 scroll track, 5 map canvas (pixels in Frame::canvases[canvas]),
                         // 6 any element of a map form (rect of element `el`, for dedicated map screens)
    int x, y, w, h;
    bool focused, pressed, disabled;
    int canvas = -1;
    uint32_t el = 0;
};

// pixels of an element the original draws with primitives the recorder does not see (map canvases)
struct Canvas { std::string name; int w, h; std::vector<uint8_t> rgba; };

struct FormInfo { int id; uint32_t ptr; int x, y, w, h; bool focused; bool dim_parent; bool generic; }; // generic: drawn by the generic renderer

struct Frame {
    uint64_t seq = 0;
    std::vector<FormInfo> forms;
    std::vector<Cmd> cmds;
    std::vector<Box> boxes;
    std::vector<Canvas> canvases;
};

// game thread
void begin_ui_draw();               // cflUi::Draw entry
void end_ui_draw();
void end_frame(Cpu *c, const std::vector<int> &unported_ids);  // FlushImpl: render sprites, publish
// main thread
void pull(Frame &out);
}
