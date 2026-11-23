// Full-screen help picture (replaces the chrome of cFormHelpFull, form 0x33; opened by the tutorial help script
// fired from the HUD "?" button, cGameWorld::PreProcKey code 3 with world+0x5F8, and by cflScript).
// The picture is the original's: cflUiCustom draw handlers onDrawControlHelp @0010a5fc / onDrawQuestHelp @0010a594 /
// onDrawBattleUiHelp @0010a528 tile sprite set 0x4A (spriteSets +0x128) images into the form; autoform.cpp takes those
// canvas pixels from the frame. The original's only control is a small button (first child of form+0x50) that closes
// the form: cFormHelpFull::somethingKeypress @0010a664 (key 0x30 / 0x25 on that button -> SetDead, then callback
// form+0x60(form+0x64, 1, 0, form)). Here the whole picture, a "next" button, Space / Enter and pad A all do that.
#include "autoform.h"
#include "game.h"
#include "render.h"
#include "screen_util.h"
#include "sprites.h"
#include <mutex>
#include "screens.h"
#include "ui.h"
#include "ui_internal.h"
#include <algorithm>
#include <cmath>

namespace screens {

class HelpFullScreen : public Screen {
    uint32_t ptr = 0;

public:
    HelpFullScreen() { z = 34; state::set_ported(0x33); register_screen(this); }
    const char *file() override { return "helpfull.rml"; }
    bool want(const state::Snapshot &s) override {
        ptr = 0;
        for (auto &f : s.forms) if (f.id == 0x33) ptr = f.ptr;
        return s.stripped && ptr;
    }
    void on_show() override { ss = {}; { std::lock_guard<std::mutex> l(g_mx); g_key.clear(); } nav.root = d.doc; nav.set(d.el("next")); }

    // which picture: the draw handler of the form's cflUiCustom element (+0x24, cflUiCustom::SetEventHandler
    // @00155a68): onDrawControlHelp @0010a5fc tiles images 0.., onDrawQuestHelp @0010a594 0xF0.., onDrawBattleUiHelp
    // @0010a528 0x1E0.., 16 x 15 tiles of 16 px from sprite set 0x4A (spriteSets +0x128)
    // the control guide (picture type 0) teaches touch gestures: on keyboard / gamepad it is replaced by a card of
    // the controls of the device in use; other pictures (quest / battle HUD) and touch keep the original picture
    struct Session {
        int base = -1;           // first tile of the picture asked of the game thread (-1: not yet)
        std::string key;         // its sprite key once built
        std::string card_key;    // the control card drawn
    } ss;
    static inline std::string g_key; // written on the game thread (cleared on show)
    static inline std::mutex g_mx;
    bool show_card() { return M32(ptr + 0x5c) == 0 && !input::touch_input(); }
    void update_card() {
        auto row = [](const std::string &keys, const char *what) {
            return "<div class=\"cr\"><span class=\"ck\">" + keys + "</span><span class=\"cw\">" + what + "</span></div>";
        };
        bool pad = glyph_ref("stick").find("bumper") != std::string::npos; // the stick glyph is a pad button only on a gamepad
        std::string c = "<div class=\"ch\">\xec\xa1\xb0\xec\x9e\x91 \xeb\xb0\xa9\xeb\xb2\x95</div><div class=\"cols\"><div class=\"col\">";
        c += row(pad ? glyph_ref("stick") : glyph_ref("stick") + " <span class=\"glyph\">&#8592;&#8593;&#8594;&#8595;</span>", "\xec\x9d\xb4\xeb\x8f\x99");
        c += row(glyph_ref("attack"), "\xea\xb3\xb5\xea\xb2\xa9 \xc2\xb7 \xeb\x8c\x80\xed\x99\x94 \xc2\xb7 \xec\xa1\xb0\xec\x82\xac");
        std::string a5 = glyph_ref("slot0") + glyph_ref("slot1") + glyph_ref("slot2") + glyph_ref("slot3") + glyph_ref("slot4");
        c += row(a5, "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xec\x84\xb8\xed\x8a\xb8 A");
        if (pad) c += row(glyph_ref("page") + "<span class=\"plus\">+</span>", "\xeb\x88\x84\xeb\xa5\xb8 \xec\xb1\x84\xeb\xa1\x9c: \xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xec\x84\xb8\xed\x8a\xb8 B");
        else c += row(glyph_ref("slot5") + glyph_ref("slot6") + glyph_ref("slot7") + glyph_ref("slot8") + glyph_ref("slot9"), "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xec\x84\xb8\xed\x8a\xb8 B");
        c += "</div><div class=\"col\">";
        c += row(glyph_ref("menu"), "\xeb\xa9\x94\xeb\x89\xb4 (\xec\xba\x90\xeb\xa6\xad\xed\x84\xb0 \xc2\xb7 \xea\xb0\x80\xeb\xb0\xa9 \xc2\xb7 \xec\x8a\xa4\xed\x82\xac)");
        c += row(glyph_ref("map"), "\xec\xa7\x80\xeb\x8f\x84");
        c += row(glyph_ref("quest"), "\xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8");
        c += row(glyph_ref("town") + glyph_ref("save"), "\xeb\xa7\x88\xec\x9d\x84 \xec\x8b\x9c\xec\x84\xa4 \xc2\xb7 \xec\xa0\x80\xec\x9e\xa5");
        c += "</div></div>";
        if (c != ss.card_key) { ss.card_key = c; d.rml("card", c); }
    }

    void update(const state::Snapshot &s) override {
        (void)s;
        bool card = show_card();
        d.prop(d.el("frame"), "display", card ? "none" : "block");
        d.prop(d.el("card"), "display", card ? "block" : "none");
        if (card) {
            update_card();
            float dp = d.doc->GetContext()->GetDensityIndependentPixelRatio();
            Rml::Element *c = d.el("card");
            float h = c->GetOffsetHeight(), w = 820 * dp;
            d.prop(c, "left", Rml::CreateString("%.0fpx", (ui::win_w - w) / 2));
            d.prop(c, "top", Rml::CreateString("%.0fpx", (ui::win_h - h) / 2 - 30 * dp));
            d.prop(d.el("next"), "left", Rml::CreateString("%.0fpx", (ui::win_w + w) / 2 - d.el("next")->GetOffsetWidth()));
            d.prop(d.el("next"), "top", Rml::CreateString("%.0fpx", (ui::win_h + h) / 2 - 30 * dp + 16 * dp));
            return;
        }
        if (ss.base < 0) {
            // picture type form+0x5C (0..2, cFormHelpFull::Finalize @0010a2f0 installs the draw handler through thunks
            // 0x10a65c -> onDrawControlHelp, 0x10a5f4 -> onDrawQuestHelp, 0x10a58c -> onDrawBattleUiHelp)
            uint32_t t = M32(ptr + 0x5c);
            ss.base = t == 1 ? 0xf0 : t == 2 ? 0x1e0 : 0;
            if (ss.base < 0) return;
            int bs = ss.base;
            state::post([bs] { std::string k = sprites::grid(0x4a, bs, 16, 15, 16); std::lock_guard<std::mutex> l(g_mx); g_key = k; });
        }
        if (ss.key.empty()) { std::lock_guard<std::mutex> l(g_mx); ss.key = g_key; }
        int ox, oy, sw, sh;
        if (ss.key.empty() || !sprites::info(ss.key, &ox, &oy, &sw, &sh)) return;
        Rml::Element *img = d.el("pic");
        d.attr(img, "src", sprite_src(ss.key));
        // the picture is 256 x 240: as large as fits under the header, integer scale when possible (pixel art)
        float dp = d.doc->GetContext()->GetDensityIndependentPixelRatio();
        float W = 256, H = 240;
        float k = std::min((ui::win_w - 80 * dp) / W, (ui::win_h - 150 * dp) / H);
        if (k >= 2.f) k = std::floor(k);
        // the captured image is trimmed to its pixels: place it inside the 256 x 240 picture
        Rml::Element *box = d.el("frame");
        d.prop(box, "width", Rml::CreateString("%.0fpx", W * k));
        d.prop(box, "height", Rml::CreateString("%.0fpx", H * k));
        d.prop(img, "left", Rml::CreateString("%.0fpx", 10 * dp + ox * k)); // inside #frame padding
        d.prop(img, "top", Rml::CreateString("%.0fpx", 10 * dp + oy * k));
        d.prop(img, "width", Rml::CreateString("%.0fpx", sw * k));
        d.prop(img, "height", Rml::CreateString("%.0fpx", sh * k));
        float pad = 11 * dp, top = 30 * dp, avail = ui::win_h - top - 90 * dp;
        d.prop(box, "left", Rml::CreateString("%.0fpx", (ui::win_w - W * k) / 2 - pad));
        d.prop(box, "top", Rml::CreateString("%.0fpx", top + (avail - H * k) / 2 - pad));
        // "next" sits under the picture, right-aligned with it
        d.prop(d.el("next"), "left", Rml::CreateString("%.0fpx", (ui::win_w + W * k) / 2 + pad - d.el("next")->GetOffsetWidth()));
        d.prop(d.el("next"), "top", Rml::CreateString("%.0fpx", top + (avail + H * k) / 2 + pad + 14 * dp));
    }

    void advance() {
        uint32_t f = ptr;
        state::post([f] {
            uint32_t c = M32(f + 0x50), kids = c ? M32(c + 0x24) : 0;
            if (kids && M32(kids)) g::call(0xfa664, {f, 0x30, 0, M32(kids)}); // cFormHelpFull::somethingKeypress @0010a664
        });
    }
    bool action(const std::string &a) override {
        if (a == "next") { advance(); return true; }
        return false;
    }
    void back() override { advance(); }
};

static HelpFullScreen g_helpfull;
}
