// Documents of the modern interface and their binding to the game snapshot (state.h)
#include "screens.h"
#include <RmlUi/Core/ElementText.h>
#include "capture_api.h"
#include "settings.h"
#include "render.h"
#include "ui.h"
#include "state.h"
#include "ui_internal.h"
#include "screen_util.h"
#include "autoform.h"
#include <RmlUi/Core.h>
#include <cmath>
#include <algorithm>
#include <map>

namespace screens {

// ------------------------------------------------------------------------------------------------ helpers
std::string num(int v) {
    std::string s = std::to_string(v < 0 ? -v : v), o;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) o += ',';
        o += s[i];
    }
    return v < 0 ? "-" + o : o;
}
std::string pct(int v, int mx) {
    float f = mx > 0 ? (float)v / mx : 0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return Rml::CreateString("%.2f%%", f * 100);
}
bool sprite_size(const std::string &key, int *w, int *h) {
    return !key.empty() && sprites_lookup(key.c_str(), w, h) != nullptr;
}

// ------------------------------------------------------------------------------------------------ game layer
static Doc g_game;
static Rml::Element *g_view;

// ------------------------------------------------------------------------------------------------ HUD
static Doc g_hud;
static int g_hp_ghost_from = -1;

// clicks: one listener per document in the capture phase (load_doc), so elements made later (SetInnerRML, data-for
// copies) need no listener of their own. The innermost element with an action handles the click; its ancestors'
// actions (a row around an arrow button) must not fire as well. Stopping in the capture phase also skips the target /
// bubble phases (EventDispatcher::DispatchEvent breaks once the event stops propagating).
class ActionListener : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &ev) override {
        Rml::Element *stop = ev.GetCurrentElement();
        for (Rml::Element *e = ev.GetTargetElement(); e && e != stop; e = e->GetParentNode()) {
            Rml::String act = e->GetAttribute<Rml::String>("data-act", "");
            if (act.empty()) continue;
            ev.StopPropagation();
            input::ui_action(act.c_str());
            return;
        }
    }
};
static ActionListener g_actions;

static void set_bar(Doc &d, const std::string &id, int v, int mx) {
    Rml::Element *b = d.el(id);
    if (!b) return;
    std::string w = pct(v, mx);
    for (int i = 0; i < b->GetNumChildren(); i++) {
        Rml::Element *c = b->GetChild(i);
        if (c->IsClassSet("fill") || c->IsClassSet("ghost")) d.prop(c, "width", w);
    }
}

bool talkctl_shown(); // talkctl.cpp
static void update_hud(const state::Snapshot &s) {
    Doc &d = g_hud;
    bool show = s.in_field && !s.hud_hidden && s.stripped && s.forms.empty();
    d.cls("hud", "hidden", !show);
    if (!show) return;
    d.cls("hud", "talking", s.talk.active);
    d.attr(d.el("face"), "src", sprite_src(s.face));
    d.rml("lvl", std::to_string(s.level));
    static const char *jobs[3] = {"\xeb\x9d\xbc\xeb\xb0\x98", "\xec\x84\xb8\xeb\xa6\xb0", "\xeb\xa6\xac\xec\x95\x84\xeb\x82\x98"};
    (void)jobs;
    d.rml("name", s.name);
    set_bar(d, "hpbar", s.hp, s.maxhp);
    set_bar(d, "mpbar", s.mp, s.maxmp);
    d.rml("hpv", num(s.hp));
    d.rml("mpv", num(s.mp));
    set_bar(d, "xpbar", s.exp, s.expmax);
    d.rml("xptext", Rml::CreateString("EXP %s / %s", num(s.exp).c_str(), num(s.expmax).c_str()));
    d.rml("locname", s.field_name);
    d.rml("goldv", num(s.gold));

    // buffs
    std::string br;
    for (auto &b : s.buffs) br += "<img src=\"sprite:" + b.icon + "\"/>";
    d.rml("buffs", br);

    // buttons
    d.cls("b_save", "off", !s.can_save);
    d.cls("b_town", "off", !s.town);
    d.prop(d.el("b_town"), "display", s.town ? "inline-block" : "none");
    d.cls("b_map", "off", !s.minimap_ok);
    d.prop(d.el("b_map"), "display", s.minimap_ok ? "inline-block" : "none"); // no map in towns: no dead button
    d.cls("b_menu", "off", !(s.can_menu || s.voyage) || s.script);
    d.prop(d.el("qbadge"), "display", s.quest_ready ? "block" : "none");
    d.rml("k_menu", glyph("menu"));
    d.rml("k_map", glyph("map"));
    d.rml("k_quest", glyph("quest"));
    d.rml("k_town", glyph("town"));
    d.rml("k_save", glyph("save"));
    d.rml("k_fast", glyph("fast"));

    // minimap: the original's 40x40 map pixels (cMinimap::DrawMinimap @00104014), shown in a modern frame
    static uint8_t mm[40 * 40 * 4];
    static uint32_t mm_seq;
    static bool mm_ready;
    if (s.minimap_drawn && capture::minimap(mm, &mm_seq)) { present::live_update("minimap", mm, 40, 40); mm_ready = true; }
    bool mm_on = s.minimap_drawn && mm_ready;
    d.cls("mmap", "on", mm_on);
    if (mm_on) d.attr(d.el("mmimg"), "src", "live:minimap");
    d.rml("k_mmap", glyph("map"));
    d.prop(d.el("target"), "top", mm_on ? "300dp" : "150dp");

    // boss / target
    d.cls("boss", "on", s.boss);
    if (s.boss) {
        d.rml("bossname", "<span class=\"lv\">Lv " + std::to_string(s.boss_level) + "</span>" + s.boss_name);
        set_bar(d, "bossbar", s.boss_hp, s.boss_max);
    }
    d.cls("target", "on", s.target && !s.boss);
    if (s.target) {
        d.rml("tname", "<span class=\"lv\">Lv " + std::to_string(s.target_level) + "</span>" + s.target_name);
        set_bar(d, "tbar", s.target_hp, s.target_max);
    }

    // hotbar
    d.prop(d.el("hotbar"), "display", s.slots_hidden ? "none" : "block");
    bool kbm = !input::touch_input() && !input::pad_input(), padm = input::pad_input(), setb = padm && input::pad_set_b();
    if (d.doc) { d.doc->SetClass("kbm", kbm); d.doc->SetClass("pad", padm); }
    for (int i = 0; i < 10; i++) {
        Rml::Element *sl = d.el("s" + std::to_string(i));
        if (!sl) continue;
        // keyboard: A then B; gamepad: set A, or B while LB / L1 is held; touch: the page the original shows
        const state::Slot &q = kbm ? s.slots_all[i] : i >= 5 ? s.slots_all[i] : padm ? s.slots_all[(setb ? 5 : 0) + i] : s.slots[i];
        bool empty = q.kind < 0 || q.icon.empty();
        d.cls(sl, "empty", empty);
        Rml::Element *img = sl->GetChild(0), *cd = sl->GetChild(1), *cdt = sl->GetChild(2), *cnt = sl->GetChild(3), *key = sl->GetChild(4);
        d.attr(img, "src", empty ? "" : sprite_src(q.icon));
        if (q.cooldown > 0.001f) {
            float deg = q.cooldown * 360.f;
            d.prop(cd, "decorator", Rml::CreateString("conic-gradient(rgba(5,7,16,140) 0deg, rgba(5,7,16,140) %.0fdeg, rgba(5,7,16,0) %.0fdeg)", deg, deg));
        } else d.prop(cd, "decorator", "none");
        d.rml(cdt, "");
        d.rml(cnt, q.kind == 1 && q.count > 1 ? std::to_string(q.count) : "");
        d.rml(key, glyph(("slot" + std::to_string(i)).c_str()));
    }
    Rml::Element *page = d.el("page");
    if (page) {
        if (padm) d.rml(page->GetChild(1), "SET B");
        else d.rml(page->GetChild(1), s.slot_page == 0 ? "SET A" : "SET B");
        d.cls(page, "held", setb);
        d.rml(page->GetChild(2), glyph("page"));
    }

    // unspent points
    std::string pts;
    if (s.gp > 0) pts += "<span class=\"chip hit\" data-act=\"stats\" style=\"pointer-events:auto\"><span class=\"icon gold\">&#xe1e4;</span> GP " + std::to_string(s.gp) + "</span>";
    if (s.sp > 0) pts += "<span class=\"chip hit\" data-act=\"skills\" style=\"pointer-events:auto\"><span class=\"icon cyan\">&#xe416;</span> SP " + std::to_string(s.sp) + "</span>";
    Rml::Element *pe = d.el("points");
    if (pe && d.last[pe] != pts) d.rml(pe, pts); // clicks: the document's capture listener (data-act)

    // touch control: V-Pad joystick follows the finger (input.cpp), mode toggle shows the current mode
    if (d.doc) {
#ifdef __ANDROID__
        d.doc->SetClass("touch", true);
#else
        d.doc->SetClass("touch", input::touch_input());
#endif
        d.doc->SetClass("vpad", s.vpad);
    }
    d.rml("modelbl", s.vpad ? "V-Pad" : "Direct");
    {
        float cx, cy, x, y;
        bool held = input::vpad_stick(&cx, &cy, &x, &y);
        Rml::Element *pad = d.el("vpad"), *knob = d.el("vknob");
        d.cls(pad, "held", held);
        // the pad is fixed (hud.rml #vpad, the original's dpad place); the knob leans toward the thumb, at most 11vh
        float vh = d.doc ? d.doc->GetContext()->GetDimensions().y / 100.f : 1.f;
        // moved from there by the V-Pad position option (settings.h; input.cpp moves the stick centre the same way)
        d.prop(pad, "left", Rml::CreateString("%.2fvh", 6.69f + settings::vpad_dx_vh()));
        d.prop(pad, "bottom", Rml::CreateString("%.2fvh", 6.06f + settings::vpad_dy_vh()));
        float dx = held ? x - cx : 0, dy = held ? y - cy : 0, r = std::sqrt(dx * dx + dy * dy), m = 11 * vh;
        if (r > m) { dx *= m / r; dy *= m / r; }
        d.prop(knob, "left", Rml::CreateString("%.0fpx", 11 * vh + dx));
        d.prop(knob, "top", Rml::CreateString("%.0fpx", 11 * vh + dy));
    }
    d.cls("bagfull", "on", s.bag_full && s.in_field && s.forms.empty() && !s.talk.active);
    d.cls("b_fast", "on", s.fast_button && !talkctl_shown()); // the dialogue controls carry SKIP (talkctl.cpp)
    d.cls("b_fast", "active", s.fast_on);
}

// ------------------------------------------------------------------------------------------------ dialogue / notices
static Doc g_talk;
static uint32_t g_talk_id;
static double g_banner_until;
static uint32_t g_banner_src;
static std::string g_banner_txt; // with the id: boxes are re-allocated at the same address

// script caption (cMessageWnd from a script: sound effects, narration) at the original's box, in window pixels.
// Alignment from the original flags (0x20 left, 0x40 right, 0x800 / one-line default centre, cMessageWnd::Draw
// @00100800 DrawString 0x122); no backing when the script asked for no frame (0x10) or no border (0x2000);
// a tap-to-continue marker when it waits for a key (flag 4).
static std::string caption_rml(const state::Toast &to) {
    const ui::FormMap m = ui::form_map(); // the original's screen layout, not the zoomed world
    float x = m.X((float)to.x), y = m.Y((float)to.y), w = to.w * m.sx, h = to.h * m.sy;
    const char *align = (to.flags & 0x20) ? "left" : (to.flags & 0x40) ? "right" : "center";
    // colour 0xBBGGRRxx (talk-balloon name 0x6cb9e100 = #E1B96C)
    std::string col = Rml::CreateString("#%02x%02x%02x", (to.color >> 8) & 0xff, (to.color >> 16) & 0xff, (to.color >> 24) & 0xff);
    // short one-liners (sound effects, exclamations) are set large
    size_t chars = 0;
    for (size_t i = 0, tag = 0; i < to.text.size(); i++) {
        char c = to.text[i];
        if (c == '<') tag = 1; else if (c == '>') tag = 0; else if (!tag && ((unsigned char)c & 0xc0) != 0x80) chars++;
    }
    bool multi = to.text.find("<br") != std::string::npos;
    bool sfx = !multi && chars <= 8;
    bool frame = !(to.flags & 0x10) && !(to.flags & 0x2000) && !sfx;
    std::string cls = std::string("cap") + (sfx ? " sfx" : "") + (frame ? " framed" : "");
    float L = x, W = w;
    if (!strcmp(align, "center")) { L = x + w / 2 - 600; W = 1200; } // centred on the original box, free to be wider
    std::string r = Rml::CreateString("<div class=\"%s\" style=\"left:%.0fpx;top:%.0fpx;width:%.0fpx;min-height:%.0fpx;text-align:%s;color:%s\"><span class=\"cb\">",
                                      cls.c_str(), L, y, W, h, align, col.c_str()) + to.text;
    if (to.flags & 4) r += "<span class=\"wait\">&#xe06d;</span>";
    return r + "</span></div>";
}

// talk balloon that does not wait (toast kind 22, cTalkBallonWnd in the non-modal list): a speech bubble over its
// speaker, on the original box's side of the tail (flag 0x40000 = under, SetBalloonPos @000ff8b8). The bubble is
// centred on the box; near an edge of the view (the original clamps the box there) it is aligned to that edge.
// The name only on balloons the original names (Draw @000ffa9c skips DrawName for balloon type +0xEC == 2).
static std::string balloon_rml(const state::Toast &to) {
    const ui::FormMap m = ui::form_map();
    const float half = 170 * ui::dp; // wrapper 340dp, bubble at most 320dp
    float gl = ui::game_rect.x, gr = ui::game_rect.x + ui::game_rect.w;
    float cx = m.X(to.x + to.w / 2.f), tail = m.X((float)to.tx);
    float L = cx - half;
    const char *align = "center";
    if (L < gl) { L = std::max(gl, m.X((float)to.x)); align = "left"; }
    else if (cx + half > gr) { L = std::min(gr, m.X((float)(to.x + to.w))) - 2 * half; align = "right"; }
    bool below = (to.flags & 0x40000) != 0;
    std::string pos = below ? Rml::CreateString("top:%.0fpx", m.Y((float)to.y))
                            : Rml::CreateString("bottom:%.0fpx", ui::win_h - m.Y((float)(to.y + to.h)));
    std::string r = Rml::CreateString("<div class=\"bal%s\" style=\"left:%.0fpx;%s;text-align:%s\"><span class=\"bb\">", below ? " below" : "", L, pos.c_str(), align);
    if (to.show_name && !to.name.empty()) r += "<span class=\"bn\">" + rml_safe(to.name) + "</span><br/>";
    r += to.text + "</span>";
    // the tail points at the speaker (left: from the wrapper's left edge)
    float tl = std::max(10 * ui::dp, std::min(2 * half - 20 * ui::dp, tail - L - 5 * ui::dp));
    r += Rml::CreateString("<span class=\"bt\" data-bleed=\"1\" style=\"left:%.0fpx\"></span>", tl);
    return r + "</div>";
}

// drill minigame (cMbMinigame, state.cpp read_drill): the original's band, target and notes at its positions, drawn as
// chips with the direction / attack the note asks for (key table DAT_001828b0: commands 0-4 = left, right, up, down,
// attack, cMbMinigame::MbMgProcessKey @0014de20). The note in its hit window is lit; hits fill the pips.
static void update_drill(Doc &d, const state::Snapshot &s) {
    const state::Snapshot::Drill &g = s.drill;
    bool on = g.on && s.stripped;
    d.cls("drill", "on", on);
    if (!on) return;
    const ui::FormMap m = ui::form_map();
    // the original's band is at 1/6 of the view height, under the modern top bar: kept below the bar's chips
    float cx = m.X((float)g.cx), cy = std::max(m.Y((float)g.cy), 215 * ui::dp);
    d.prop(d.el("dband"), "left", Rml::CreateString("%.0fpx", m.X((float)g.band_x)));
    d.prop(d.el("dband"), "width", Rml::CreateString("%.0fpx", g.band_w * m.sx));
    d.prop(d.el("dband"), "top", Rml::CreateString("%.0fpx", cy));
    d.prop(d.el("dring"), "left", Rml::CreateString("%.0fpx", cx));
    d.prop(d.el("dring"), "top", Rml::CreateString("%.0fpx", cy));
    d.cls("drill", "good", g.good);
    d.cls("drill", "fail", g.fail);
    static const char *sym[5] = {"\xe2\x97\x80", "\xe2\x96\xb6", "\xe2\x96\xb2", "\xe2\x96\xbc", "&#xe2b4;"}; // left, right, up, down triangles; lucide sword
    std::string notes;
    for (auto &n : g.notes) {
        int c = n.cmd >= 0 && n.cmd < 5 ? n.cmd : 4;
        std::string cls = std::string("dn") + (c == 4 ? " atk" : "") + (n.idx < g.done ? " hit" : n.live ? " live" : "");
        notes += Rml::CreateString("<div class=\"%s\" style=\"left:%.0fpx;top:%.0fpx\">", cls.c_str(), m.X((float)n.x), cy) + sym[c] + "</div>";
    }
    d.rml("dnotes", notes);
    d.prop(d.el("dfx"), "left", Rml::CreateString("%.0fpx", cx));
    d.prop(d.el("dfx"), "top", Rml::CreateString("%.0fpx", cy + 40 * ui::dp));
    d.rml("dfx", g.fail ? "MISS" : g.good ? "GOOD" : "");
    std::string pips;
    for (int i = 0; i < g.total; i++) pips += i < g.done ? "<span class=\"on\"/>" : "<span/>";
    d.prop(d.el("dpips"), "left", Rml::CreateString("%.0fpx", cx));
    d.prop(d.el("dpips"), "top", Rml::CreateString("%.0fpx", cy - 56 * ui::dp));
    d.rml("dpips", pips);
}

// gathering / fishing gauge (cCollectGaugeWnd, state.cpp read_gather): a card with the item's icon, name and progress,
// centred over the original's box above the gatherer
static void update_gather(Doc &d, const state::Snapshot &s) {
    const state::Snapshot::Gather &g = s.gather;
    bool on = g.on && s.stripped;
    d.cls("gather", "on", on);
    if (!on) return;
    const ui::FormMap m = ui::form_map();
    float cx = m.X(g.x + 0x78 / 2.f), mid = m.Y(g.y + 0x28 / 2.f);
    d.prop(d.el("gather"), "left", Rml::CreateString("%.0fpx", cx));
    d.prop(d.el("gather"), "top", Rml::CreateString("%.0fpx", std::max(mid - 29 * ui::dp, 8 * ui::dp)));
    d.attr(d.el("gimg"), "src", g.icon.empty() ? "" : sprite_src(g.icon));
    d.rml("gname", rml_safe(g.name));
    d.prop(d.el("gfill"), "width", Rml::CreateString("%.1f%%", g.progress * 100));
    d.rml("gpct", Rml::CreateString("%d%%", (int)(g.progress * 100 + 0.5f)));
}

static void update_talk(const state::Snapshot &s) {
    Doc &d = g_talk;
    // a full-screen help picture (cFormHelpFull 0x33) covers the tutorial's messages
    bool help = false;
    for (auto &f : s.forms) help |= f.id == 0x33;
    if (d.doc) d.doc->SetProperty("visibility", help ? "hidden" : "visible");
    const state::Talk &t = s.talk;
    bool box = t.active && (t.kind == 4 || t.kind == 0) && s.stripped && (!t.text.empty() || !t.name.empty());
    bool notice = t.active && (t.kind == 7 || t.kind == 100) && s.stripped;
    d.cls("talk", "on", box);
    d.cls("talk", "hit", box);
    d.cls("notice", "on", notice);
    if (box) {
        bool face = !t.face.empty();
        d.cls("talk", "face", face);
        if (!face) { d.prop(d.el("tpanel"), "padding-left", "34dp"); d.prop(d.el("tname"), "left", "30dp"); }
        d.attr(d.el("tfaceimg"), "src", face ? sprite_src(t.face) : "");
        int fw, fh;
        if (face && sprite_size(t.face, &fw, &fh) && fh > 0) {
            // bust standing on the panel's lower edge, at most 260dp tall
            float h = std::min(230.f, fh / 3.f * 2.0f), w = h * fw / fh;
            if (w > 200) { h = h * 200 / w; w = 200; }
            d.prop(d.el("tfaceimg"), "height", Rml::CreateString("%.0fdp", h));
            d.prop(d.el("tfaceimg"), "width", Rml::CreateString("%.0fdp", w));
            d.prop(d.el("tpanel"), "padding-left", Rml::CreateString("%.0fdp", w + 40));
            d.prop(d.el("tname"), "left", Rml::CreateString("%.0fdp", w + 30));
        }
        d.prop(d.el("tfaceimg"), "transform", t.face_right ? "scaleX(-1)" : "none");
        d.rml("tname", t.name);
        d.cls("tname", "none", t.name.empty());
        // typewriter: reveal a prefix of the page markup by visible characters
        int vis = (int)std::floor(t.reveal * t.text_len + 0.5f);
        std::string out;
        int count = 0;
        bool in_tag = false;
        for (size_t i = 0; i < t.text.size(); i++) {
            char ch = t.text[i];
            if (ch == '<') in_tag = true;
            if (in_tag) { out += ch; if (ch == '>') in_tag = false; continue; }
            bool lead = ((unsigned char)ch & 0xC0) != 0x80;
            if (lead && ch != '&') { if (count >= vis) break; count++; }
            if (ch == '&') { size_t e = t.text.find(';', i); if (count >= vis) break; count++; out += t.text.substr(i, e - i + 1); i = e; continue; }
            out += ch;
        }
        // keep the layout stable while typing: the rest is laid out invisibly
        std::string rest = t.text.size() > out.size() ? t.text.substr(out.size()) : "";
        // the invisible part keeps only its line breaks: a colour span inside it (07 d colour codes) would
        // override the transparency and flash the coloured words ahead of the typewriter
        std::string hid;
        for (size_t i = 0; i < rest.size(); i++) {
            if (rest[i] != '<') { hid += rest[i]; continue; }
            size_t e = rest.find('>', i);
            if (e == std::string::npos) break;
            if (!rest.compare(i, 3, "<br")) hid += rest.substr(i, e - i + 1);
            i = e;
        }
        // close the colour spans still open in the revealed part
        int open = 0;
        for (size_t p = 0; (p = out.find("<span", p)) != std::string::npos; p += 5) open++;
        for (size_t p = 0; (p = out.find("</span", p)) != std::string::npos; p += 6) open--;
        for (; open > 0; open--) out += "</span>";
        d.rml("ttext", out + "<span style=\"color: rgba(0,0,0,0)\">" + hid + "</span>");
        bool done = t.reveal >= 0.999f;
        d.cls("tnext", "on", done && t.waiting);
        d.rml("tnextlabel", ""); // no key prompt on the continue marker (user: confirm / cancel prompts break immersion)
    }
    if (notice) {
        d.rml("nkicker", t.kind == 100 ? "MISSION" : "NOTICE");
        d.rml("ntitle", t.title);
        d.rml("ntext", t.text);
        d.rml("nfoot", glyph("advance") + " \xea\xb3\x84\xec\x86\x8d");
    }

    // area banner (cGameWorld::ShowMapName: non-modal cMessageWnd kind 0, centred) and toasts
    double now = Rml::GetSystemInterface()->GetElapsedTime();
    std::string toasts, captions;
    for (auto &to : s.toasts) {
        bool shown = to.id == g_banner_src && to.text == g_banner_txt;
        if (to.kind == 0 && !shown && s.stripped) {
            // single-line centred message with the field name = map banner
            if (to.text.find(s.field_name) != std::string::npos && !s.field_name.empty()) {
                g_banner_src = to.id; g_banner_txt = to.text;
                g_banner_until = now + 2.6;
                d.rml("bannertext", to.text);
                d.rml("bannerkick", "AREA");
                d.cls("banner", "chapter", false);
                continue;
            }
        }
        if (shown) continue;
        if (to.kind == 5 && s.stripped) { // cChapterNameWnd: chapter title card, shown like the area banner
            {
                g_banner_src = to.id; g_banner_txt = to.text; g_banner_until = now + 3.0; d.rml("bannertext", to.text);
                d.rml("bannerkick", "CHAPTER");
                d.cls("banner", "chapter", true); // chapter title card: centred, large (it plays over the black intro)
            }
            continue;
        }
        if (to.kind == 20) { captions += caption_rml(to); continue; }
        if (to.kind == 22) { captions += balloon_rml(to); continue; }
        if (to.kind == 21) { // ticker: a guide strip at the bottom (the original scrolls it across a bottom band)
            std::string col = Rml::CreateString("#%02x%02x%02x", (to.color >> 8) & 0xff, (to.color >> 16) & 0xff, (to.color >> 24) & 0xff);
            if (col == "#000000") col = "#ffffff";
            captions += Rml::CreateString("<div class=\"ticker\"><span class=\"tk\" style=\"color:%s\"><span class=\"ti\">&#xe0f9;</span>", col.c_str()) + to.text + "</span></div>";
            continue;
        }
        const char *k = to.kind == 10 ? "toast glass warn" : to.kind == 11 ? "toast glass quest" : to.kind == 5 ? "toast glass chapter" : "toast glass";
        // icon by what the notice is: warning, quest progress, save result, title, item obtained, other results
        const char *ic = "&#xe226;";
        if (to.kind == 10) ic = "&#xe193;";
        else if (to.kind == 11) ic = "&#xe2ed;";
        else if (to.text.find("\xec\xa0\x80\xec\x9e\xa5") != std::string::npos) ic = "&#xe14d;";
        else if (to.text.find("\xec\xb9\xad\xed\x98\xb8") != std::string::npos) ic = "&#xe04f;";
        else if (to.text.find("\xed\x9a\x8d\xeb\x93\x9d") != std::string::npos) ic = "&#xe2c8;";
        toasts += Rml::CreateString("<div class=\"%s\" id=\"t%08x\"><span class=\"ti\">%s</span>", k, to.id, ic) + to.text + "</div>";
    }
    d.cls("banner", "on", now < g_banner_until && s.stripped);
    d.rml("toasts", s.stripped ? toasts : "");
    d.rml("captions", s.stripped ? captions : "");
    d.cls("countdown", "on", !s.countdown.empty() && s.stripped);
    d.rml("countdown", s.countdown);
    update_drill(d, s);
    update_gather(d, s);
}

// ------------------------------------------------------------------------------------------------ lifecycle
Rml::ElementDocument *load_doc(Doc &d, const char *file) {
    d.doc = ui::context->LoadDocument(ui::asset_path(file));
    if (!d.doc) { ui_log("[ui] cannot load %s", file); return nullptr; }
    d.doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    d.doc->AddEventListener(Rml::EventId::Click, &g_actions, true);
    return d.doc;
}
static Rml::ElementDocument *load(Doc &d, const char *file) { return load_doc(d, file); }

static std::vector<Screen *> &registry() { static std::vector<Screen *> r; return r; }
void register_screen(Screen *s) { registry().push_back(s); }
static Screen *g_active; // topmost visible modal screen

void init() {
    if (load(g_game, "game.rml")) {
        g_game.doc->PushToBack();
        g_view = g_game.doc->GetElementById("view");
    }
    load(g_hud, "hud.rml");
    load(g_talk, "talk.rml");
    for (Screen *sc : registry()) {
        sc->make_model(ui::context);
        load_doc(sc->d, sc->file());
        if (sc->d.doc) sc->nav.root = sc->d.doc;
    }
    std::vector<Screen *> order = registry();
    std::stable_sort(order.begin(), order.end(), [](Screen *a, Screen *b) { return a->z < b->z; });
    for (Screen *sc : order) if (sc->d.doc) sc->d.doc->PullToFront();
}

static autoform::Frame g_rec_frame;
const autoform::Frame &recorded() { return g_rec_frame; }

// the view element on game_rect (every map at the same pixel scale: no small-map zoom, maps narrower than the view
// keep the original's black margins)
void sync_view() {
    if (g_view) {
        const ui::ViewRect &r = ui::game_rect;
        g_view->SetProperty("left", Rml::CreateString("%gpx", r.x));
        g_view->SetProperty("top", Rml::CreateString("%gpx", r.y));
        g_view->SetProperty("width", Rml::CreateString("%gpx", r.w));
        g_view->SetProperty("height", Rml::CreateString("%gpx", r.h));
    }
}

// test: "ui geom <file>": the frame / interface / touch geometry with invariant checks, one "ok" / "FAIL" line each and
// a last line "geom: <n> fail". Window, game and form mappings must invert each other, the view element must sit on
// game_rect, and a touch-down in the window area outside the drawn frame must not reach the game.
void dump_geom(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    int lw = 0, lh = 0;
    SDL_GetWindowSize(ui::window, &lw, &lh);
    const int W = g_game_w, H = g_game_h;
    const ui::ViewRect &g = ui::game_rect;
    const ui::FormMap m = ui::form_map();
    fprintf(f, "window %dx%d px (logical %dx%d, density %.3f)\n", ui::win_w, ui::win_h, lw, lh, SDL_GetWindowPixelDensity(ui::window));
    fprintf(f, "game %dx%d dp %.3f forms %d\n", W, H, ui::dp, (int)state::get().forms.size());
    fprintf(f, "game_rect %.1f,%.1f %.1fx%.1f (sx %.4f sy %.4f)\n", g.x, g.y, g.w, g.h, g.w / W, g.h / H);
    fprintf(f, "form_map %.1f,%.1f sx %.4f sy %.4f\n", m.x0, m.y0, m.sx, m.sy);
    int fails = 0;
    auto check = [&](bool ok, const char *name, const std::string &detail) {
        fprintf(f, "%s %s %s\n", ok ? "ok  " : "FAIL", name, detail.c_str());
        fails += !ok;
    };
    // the whole frame is shown (nothing crops it, e.g. a zoom) and lies inside the window
    check(g.x >= -1 && g.y >= -1 && g.x + g.w <= ui::win_w + 1 && g.y + g.h <= ui::win_h + 1, "frame_inside_window",
          Rml::CreateString("%.1f,%.1f %.1fx%.1f", g.x, g.y, g.w, g.h));
    // game px centre -> window (form_map: forms and captions are drawn with it) -> game px (touch mapping), 9x9 grid
    {
        int worst = 0, wx0 = 0, wy0 = 0, ex = 0, ey = 0;
        for (int j = 0; j < 9; j++)
            for (int i = 0; i < 9; i++) {
                int x = i * (W - 1) / 8, y = j * (H - 1) / 8, gx, gy;
                ui_window_to_game(m.X(x + 0.5f), m.Y(y + 0.5f), &gx, &gy);
                int e = std::abs(gx - x) + std::abs(gy - y);
                if (e > worst) { worst = e; wx0 = x; wy0 = y; ex = gx; ey = gy; }
            }
        check(worst == 0, "form_roundtrip", worst ? Rml::CreateString("worst %d px: game %d,%d -> %d,%d", worst, wx0, wy0, ex, ey) : std::string());
    }
    if (g_view) {
        Rml::Vector2f o = g_view->GetAbsoluteOffset(Rml::BoxArea::Border), s = g_view->GetBox().GetSize(Rml::BoxArea::Border);
        bool ok = std::fabs(o.x - g.x) <= 1 && std::fabs(o.y - g.y) <= 1 && std::fabs(s.x - g.w) <= 1 && std::fabs(s.y - g.h) <= 1;
        check(ok, "view_element_on_game_rect", Rml::CreateString("%.1f,%.1f %.1fx%.1f", o.x, o.y, s.x, s.y));
    }
    // the drawn frame inside the window; each bar around it at least 2 px wide gets three touch-downs
    float fx0 = std::max(0.f, g.x), fy0 = std::max(0.f, g.y);
    float fx1 = std::min((float)ui::win_w, g.x + g.w), fy1 = std::min((float)ui::win_h, g.y + g.h);
    struct Bar { const char *name; float x0, y0, x1, y1; } bars[] = {
        {"left", 0, 0, fx0, (float)ui::win_h}, {"right", fx1, 0, (float)ui::win_w, (float)ui::win_h},
        {"top", fx0, 0, fx1, fy0}, {"bottom", fx0, fy1, fx1, (float)ui::win_h}};
    int nbars = 0;
    for (auto &b : bars) {
        if (b.x1 - b.x0 < 2 || b.y1 - b.y0 < 2) continue;
        nbars++;
        int passed = 0, gx = 0, gy = 0;
        for (int k = 1; k <= 3; k++) {
            float wx = b.x0 + (b.x1 - b.x0) * (b.x1 - b.x0 > b.y1 - b.y0 ? k / 4.f : 0.5f);
            float wy = b.y0 + (b.y1 - b.y0) * (b.x1 - b.x0 > b.y1 - b.y0 ? 0.5f : k / 4.f);
            passed += input::touch_target(wx, wy, &gx, &gy);
        }
        check(passed == 0, (std::string("bar_touch_") + b.name).c_str(),
              Rml::CreateString("%.0f,%.0f-%.0f,%.0f: %d/3 touches reach the game (last at %d,%d)", b.x0, b.y0, b.x1, b.y1, passed, gx, gy));
    }
    if (!nbars) fprintf(f, "bars none\n");
    fprintf(f, "geom: %d fail\n", fails);
    fclose(f);
}

void update() {
    state::pull();
    autoform::pull(g_rec_frame);
    const state::Snapshot &s = state::get();
    {   // touch: key / button prompt bars are hidden (theme.rcss body.touchin)
        bool t = input::touch_input();
        for (Screen *sc : registry()) {
            if (sc->d.doc) sc->d.doc->SetClass("touchin", t);
            if (sc->nav.focus) sc->nav.focus->SetClass("focus", !t); // the focus cursor only for keys / pad
        }
        if (g_talk.doc) g_talk.doc->SetClass("touchin", t);
    }
    sync_view();
    update_hud(s);
    update_talk(s);
    g_active = nullptr;
    std::vector<Screen *> order = registry();
    std::stable_sort(order.begin(), order.end(), [](Screen *a, Screen *b) { return a->z < b->z; });
    for (Screen *sc : order) {
        bool w = sc->d.doc && sc->want(s);
        if (w != sc->shown) {
            sc->shown = w;
            if (sc->d.doc) sc->d.doc->SetClass("on", w);
            if (w) { sc->d.invalidate(); sc->on_show(); if (sc->model) sc->model.DirtyAllVariables(); } else { sc->nav.clear(); sc->nav.want.clear(); sc->on_hide(); }
        }
        if (w) {
            sc->nav.settle();
            sc->update(s);
            if (sc->modal()) g_active = sc;
        }
    }
    // RmlUi brings a clicked document to the front: keep the screens in their z order
    for (Screen *sc : order)
        if (sc->shown && sc->d.doc) sc->d.doc->PullToFront();
    (void)g_hp_ghost_from;
    (void)g_talk_id;
}

void action(const char *name) {
    // topmost first: a screen opened over another (help over the menu) shares action names with it (c_no / c_yes)
    std::vector<Screen *> order = registry();
    std::stable_sort(order.begin(), order.end(), [](Screen *a, Screen *b) { return a->z > b->z; });
    for (Screen *sc : order)
        if (sc->shown && sc->action(name)) return;
    ui_log("[ui] unhandled action %s", name);
}
bool menu_active() { return g_active != nullptr; }
void pad_button(int b, bool down) {
    if (!g_active || !down) return;
    g_active->nav.settle_input(); // a focus asked for the next frame (want_act) is taken before the button acts
    if (b == SDL_GAMEPAD_BUTTON_SOUTH) { g_active->nav.ensure(); g_active->nav.activate(); }
    else if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_START) g_active->back();
    else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || b == 100) g_active->tab(-1);
    else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER || b == 101) g_active->tab(1);
    else if (b == 102) g_active->subtab(-1); // the mapped sub-tab keys (LT / RT, Z / C by default)
    else if (b == 103) g_active->subtab(1);
    else if (b >= SDL_GAMEPAD_BUTTON_DPAD_UP && b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) pad_dir(b - SDL_GAMEPAD_BUTTON_DPAD_UP);
}
void pad_dir(int dir) {
    if (!g_active || dir < 0) return;
    g_active->nav.settle_input();
    if (g_active->on_dir(dir)) return;
    // nothing further up / down: scroll the box instead (help texts, notices, long lists)
    if (!g_active->nav.move(dir) && dir < 2 && g_active->d.doc)
        g_active->nav.scroll((dir == 0 ? -90.f : 90.f) * g_active->d.doc->GetContext()->GetDensityIndependentPixelRatio());
}
void scroll_active(float dp) {
    if (getenv("EH_SCROLLDBG")) ui_log("[scroll] %.2f active %p", dp, (void *)g_active);
    if (!g_active || !g_active->d.doc) return;
    g_active->nav.scroll(dp * g_active->d.doc->GetContext()->GetDensityIndependentPixelRatio(), true); // right stick: text first
}

bool talk_active() {
    const state::Snapshot &s = state::get();
    return s.talk.active && s.stripped;
}

void shutdown() {}
// ---- test introspection (EH_CTRL "ui dumpui <file>"): the visible element tree of every shown document as text,
// with layout problems marked, so a screen can be checked without reading a screenshot:
//   !OFF      the element's box lies (partly) outside the window
//   !OVERFLOW the content is wider / taller than the box and the box does not scroll (text cut or spilling)
//   !OUTSIDE  the box sticks out of a parent that clips (overflow hidden / auto)
//   *FOCUS    the active screen's keyboard / gamepad focus
//   !OVERLAP  (per document) two texts covering each other
// An element marked data-bleed (art meant to run past the edges or be cut by its frame) is exempt from !OFF / !OUTSIDE.
static std::string plain(const Rml::String &rml) {
    std::string o;
    bool tag = false;
    for (char c : rml) {
        if (c == '<') tag = true;
        else if (c == '>') { tag = false; o += ' '; }
        else if (!tag) o += c == '\n' ? ' ' : c;
    }
    size_t a = o.find_first_not_of(' '), b = o.find_last_not_of(' ');
    o = a == std::string::npos ? "" : o.substr(a, b - a + 1);
    std::string r;
    for (char c : o) if (!(c == ' ' && !r.empty() && r.back() == ' ')) r += c;
    if (r.size() > 90) { size_t cut = 90; while (cut > 0 && ((unsigned char)r[cut] & 0xC0) == 0x80) cut--; r = r.substr(0, cut) + "..."; }
    return r;
}
struct DumpCtx {
    FILE *f; int issues; float W, H; Rml::Element *focus;
    struct T { Rml::Element *e; float x, y, w, h; std::string text; };
    std::vector<T> texts; // text leaves of the current document (overlap check)
};
static void dump_el(DumpCtx &c, Rml::Element *e, int depth, Rml::Element *clip) {
    if (!e || !e->IsVisible()) return;
    if (e->GetTagName() == "#text") return;
    Rml::Vector2f o = e->GetAbsoluteOffset(Rml::BoxArea::Border), sz = e->GetBox().GetSize(Rml::BoxArea::Border);
    std::string line = std::string(depth * 2, ' ') + e->GetTagName();
    if (!e->GetId().empty()) line += "#" + e->GetId();
    std::string cls = e->GetClassNames();
    for (char &ch : cls) if (ch == ' ') ch = '.';
    if (!cls.empty()) line += "." + cls;
    Rml::String act = e->GetAttribute<Rml::String>("data-act", "");
    if (!act.empty()) line += " [" + act + "]";
    line += Rml::CreateString(" (%.0f,%.0f %.0fx%.0f)", o.x, o.y, sz.x, sz.y);
    if (e->GetScrollHeight() > e->GetClientHeight() + 1) // a scrolling box: position / range
        line += Rml::CreateString(" {scroll %.0f/%.0f}", e->GetScrollTop(), e->GetScrollHeight() - e->GetClientHeight());
    bool leaf = true;
    for (int i = 0; i < e->GetNumChildren(); i++) if (e->GetChild(i)->GetTagName() != "#text") leaf = false;
    if (leaf) {
        std::string t = plain(e->GetInnerRML());
        if (!t.empty()) {
            line += " \"" + t + "\"";
            if (sz.x > 0 && sz.y > 0 && e->GetComputedValues().font_family() != "lucide") {
                // the text's own horizontal extent (its line geometry), not the possibly full-width block box
                float x0 = o.x, x1 = o.x + sz.x;
                for (int k = 0; k < e->GetNumChildren(); k++)
                    if (auto *et = rmlui_dynamic_cast<Rml::ElementText *>(e->GetChild(k))) {
                        float base = et->GetAbsoluteOffset(Rml::BoxArea::Border).x, lx0 = 1e9f, lx1 = -1e9f;
                        for (auto &ln : et->GetLines()) { lx0 = std::min(lx0, base + ln.position.x); lx1 = std::max(lx1, base + ln.position.x + ln.width); }
                        if (lx1 > lx0) { x0 = lx0; x1 = lx1; }
                    }
                c.texts.push_back({e, x0, o.y, x1 - x0, sz.y, t});
            }
        }
    }
    if (e->GetComputedValues().font_family() == "lucide") line += " {icon}"; // tools/align_check.py measures these
    std::string flags;
    bool bleed = e->HasAttribute("data-bleed");
    // (inside a clipping / scrolling box the box's own check below applies instead)
    if (!bleed && !clip && sz.x > 0 && sz.y > 0 && (o.x < -1 || o.y < -1 || o.x + sz.x > c.W + 1 || o.y + sz.y > c.H + 1)) flags += " !OFF";
    const auto &ov = e->GetComputedValues();
    bool scrolls = ov.overflow_x() == Rml::Style::Overflow::Auto || ov.overflow_x() == Rml::Style::Overflow::Scroll ||
                   ov.overflow_y() == Rml::Style::Overflow::Auto || ov.overflow_y() == Rml::Style::Overflow::Scroll;
    if (!scrolls && sz.x > 0 && (e->GetScrollWidth() > e->GetClientWidth() + 2 || (leaf && e->GetScrollHeight() > e->GetClientHeight() + 2))) flags += " !OVERFLOW";
    if (clip && !bleed && sz.x > 0 && sz.y > 0) {
        Rml::Vector2f co = clip->GetAbsoluteOffset(Rml::BoxArea::Padding), cs = clip->GetBox().GetSize(Rml::BoxArea::Padding);
        if (o.x < co.x - 1 || o.y < co.y - 1 || o.x + sz.x > co.x + cs.x + 1 || o.y + sz.y > co.y + cs.y + 1) {
            bool scrolled = clip->GetScrollHeight() > clip->GetClientHeight() + 1 || clip->GetScrollWidth() > clip->GetClientWidth() + 1;
            if (!scrolled) flags += " !OUTSIDE"; // inside a scrolling box it is just scrolled out of view
        }
    }
    if (e == c.focus) flags += " *FOCUS";
    if (flags.find('!') != std::string::npos) c.issues++;
    fprintf(c.f, "%s%s\n", line.c_str(), flags.c_str());
    Rml::Element *nclip = (ov.overflow_x() != Rml::Style::Overflow::Visible || ov.overflow_y() != Rml::Style::Overflow::Visible) ? e : clip;
    for (int i = 0; i < e->GetNumChildren(); i++) dump_el(c, e->GetChild(i), depth + 1, nclip);
}
void dump_ui(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    DumpCtx c{f, 0, (float)ui::win_w, (float)ui::win_h, g_active ? g_active->nav.focus.get() : nullptr};
    fprintf(f, "window %dx%d, active screen %s\n", ui::win_w, ui::win_h, g_active ? g_active->file() : "-");
    Rml::Context *ctx = ui::context;
    for (int i = 0; ctx && i < ctx->GetNumDocuments(); i++) {
        Rml::ElementDocument *d = ctx->GetDocument(i);
        if (!d || !d->IsVisible()) continue;
        bool body_on = true;
        const auto &ov = d->GetComputedValues();
        if (ov.display() == Rml::Style::Display::None) body_on = false;
        if (!body_on) continue;
        Screen *owner = nullptr;
        for (Screen *sc : registry()) if (sc->d.doc == d) owner = sc;
        if (owner && !owner->shown) continue;
        fprintf(f, "== document %s%s\n", d->GetSourceURL().c_str(), owner == g_active && owner ? " (active)" : "");
        c.texts.clear();
        for (int k = 0; k < d->GetNumChildren(); k++) dump_el(c, d->GetChild(k), 1, nullptr);
        //   !OVERLAP  two texts of the document cover each other (more than 30% of the smaller box)
        for (size_t a = 0; a < c.texts.size(); a++)
            for (size_t b = a + 1; b < c.texts.size(); b++) {
                const auto &A = c.texts[a], &B = c.texts[b];
                float ix = std::min(A.x + A.w, B.x + B.w) - std::max(A.x, B.x), iy = std::min(A.y + A.h, B.y + B.h) - std::max(A.y, B.y);
                if (ix <= 0 || iy <= 0) continue;
                if (ix * iy < 0.3f * std::min(A.w * A.h, B.w * B.h)) continue;
                fprintf(f, "  !OVERLAP \"%s\" (%.0f,%.0f %.0fx%.0f) and \"%s\" (%.0f,%.0f %.0fx%.0f)\n", A.text.c_str(), A.x, A.y, A.w, A.h, B.text.c_str(), B.x, B.y, B.w, B.h);
                c.issues++;
            }
    }
    fprintf(f, "issues %d\n", c.issues);
    fclose(f);
}

} // namespace screens
