// Game-thread snapshot of the original's state for the modern UI. Every read cites the original function the offset
// comes from (Ghidra address); the specs in work/*.md collect the evidence.
#include "state.h"
#include "game.h"
#include <SDL3/SDL.h>
#include "sprites.h"
#include "autoform.h"
#include "ui_internal.h"
#include <cstring>
#include <deque>
#include <mutex>
#include <map>
#include <set>

extern "C" {
#include "jni.h"
#include "java.h"
}
extern "C" int g_game_w, g_game_h;

namespace g {
Cpu *cpu;
static SDL_ThreadID g_game_tid; // the thread that set cpu (game thread callbacks)
void check_thread(uint32_t va) {
    SDL_ThreadID t = SDL_GetCurrentThreadID();
    if (g_game_tid && t != g_game_tid) host_log("[ui] THREAD: guest call %06x off the game thread", va);
}
void set_cpu(Cpu *c) { cpu = c; g_game_tid = SDL_GetCurrentThreadID(); }

static void append_utf8(std::string &o, const char *&p) {
    unsigned char b = (unsigned char)*p;
    int n = (b & 0xF0) == 0xF0 ? 4 : (b & 0xE0) == 0xE0 ? 3 : (b & 0xC0) == 0xC0 ? 2 : 1;
    for (int i = 0; i < n && *p; i++) o += *p++;
}

// cflConstStr::CalculateLine @0016e750 / DrawLineString @0016ec94: 07 d colour, 0B reset, 08 centre, 09 tab,
// 0A newline, 0C a b timed pause, 0D c alignment; colours from the live table at vaddr 0x19ae30 (0xBBGGRRxx)
std::string rml_text(const char *s, bool keep_breaks) {
    std::string o;
    if (!s) return o;
    bool span = false;
    const char *p = s;
    while (*p) {
        unsigned char b = (unsigned char)*p;
        if (b == 0x07 && p[1]) {
            int d = p[1] - '0';
            p += 2;
            if (span) o += "</span>";
            uint32_t c = (d >= 0 && d < 10) ? M32(IMG + 0x19ae30 + 4 * d) : 0xffffff00;
            char buf[48];
            snprintf(buf, sizeof buf, "<span style=\"color: #%02x%02x%02x\">", (c >> 8) & 0xff, (c >> 16) & 0xff, (c >> 24) & 0xff);
            o += buf;
            span = true;
        } else if (b == 0x0b) { if (span) o += "</span>"; span = false; p++; }
        else if (b == 0x0a) { o += keep_breaks ? "<br/>" : " "; p++; }
        else if (b == 0x09) { o += "    "; p++; }
        else if (b == 0x0c) { p++; for (int i = 0; i < 2 && *p; i++) p++; }
        else if (b == 0x0d) { p++; if (*p) p++; }
        else if (b < 0x20) p++;
        else if (b == '<') { o += "&lt;"; p++; }
        else if (b == '>') { o += "&gt;"; p++; }
        else if (b == '&') { o += "&amp;"; p++; }
        else append_utf8(o, p);
    }
    if (span) o += "</span>";
    return o;
}

std::string plain_text(const char *s) {
    std::string o;
    if (!s) return o;
    const char *p = s;
    while (*p) {
        unsigned char b = (unsigned char)*p;
        if (b == 0x07 || b == 0x0d) { p++; if (*p) p++; }
        else if (b == 0x0c) { p++; for (int i = 0; i < 2 && *p; i++) p++; }
        else if (b == 0x0a) { o += ' '; p++; }
        else if (b < 0x20) p++;
        else append_utf8(o, p);
    }
    return o;
}
} // namespace g

namespace state {

static Snapshot g_pub, g_view;
static std::mutex g_mx;
static bool g_strip;
static std::deque<std::function<void()>> g_actions;
static std::mutex g_amx;

const Snapshot &get() { return g_view; }
void pull() { std::lock_guard<std::mutex> l(g_mx); g_view = g_pub; }
bool strip_original_ui() { return g_strip; }
} // namespace state
extern "C" int ui_hud_owns_input(void) { return state::strip_original_ui() ? 1 : 0; }
extern "C" int ui_forced_hit(void);
namespace state {
void post(std::function<void()> fn) { std::lock_guard<std::mutex> l(g_amx); g_actions.push_back(std::move(fn)); }

struct Later { int frames; std::function<void()> fn; };
static std::vector<Later> g_later;
void post_after(int frames, std::function<void()> fn) { std::lock_guard<std::mutex> l(g_amx); g_later.push_back({frames, std::move(fn)}); }
static std::deque<int> g_taps;
static int g_forced_hit, g_forced_frames;
} // namespace state
extern "C" int ui_forced_hit(void) { return state::g_forced_hit; }
namespace state {

void run_actions(Cpu *c) {
    g::set_cpu(c);
    std::deque<std::function<void()>> q;
    { std::lock_guard<std::mutex> l(g_amx); q.swap(g_actions); }
    // the posted actions drive the original forms as the original's own input does: cflUi::Process @00152d80 runs the
    // forms' handlers with ui flag 8 (+0xC) set, then clears it and calls AfterProcess (vfunc 0x34, @00153f5c), which
    // deletes the forms marked dead. cflUi::CloseUI @00153e14 only marks them dead under that flag; without it it frees
    // them at once (CloseUiImpl -> freeDeleteForms), under the caller: using a 워프포털 from the bag (cFormInvenItem::
    // DoItemAction @0010d344, action 0x41 -> item func 0x66, cFuncManager::ApplyFuncImpl_3 @00125278 -> CloseUI) freed
    // the bag form, and DoItemAction then reloaded its list +0x64 from freed memory
    for (auto &f : q) {
        uint32_t ui = g::gameui();
        bool in_ui = ui && M32(ui + 0x18) && !(M32(ui + 0xc) & 8); // Process' condition: an active form (+0x18)
        if (in_ui) M32(ui + 0xc) |= 8;
        f();
        if (in_ui) {
            M32(ui + 0xc) &= ~8u;
            g::call((M32(M32(ui) + 0x34) - IMG) & ~1u, {ui}); // cflUi::AfterProcess (vfunc 0x34)
        }
    }
    std::vector<std::function<void()>> due;
    {
        std::lock_guard<std::mutex> l(g_amx);
        for (size_t i = 0; i < g_later.size();)
            if (--g_later[i].frames <= 0) { due.push_back(std::move(g_later[i].fn)); g_later.erase(g_later.begin() + i); }
            else i++;
    }
    for (auto &f : due) f();
    // GofManager.SimulateKeyPress -> DAT 0x19b33c, applied by ApplyWaitingKeyPressIfAny at the end of procTouch:
    // one key per frame
    if (!g_taps.empty() && M32(IMG + 0x19b33c) == 0) { M32(IMG + 0x19b33c) = (uint32_t)g_taps.front(); g_taps.pop_front(); }
    // forced HUD hit: down (frame 0), up (frame 3), hit code cleared (frame 5)
    if (g_forced_frames > 0) {
        --g_forced_frames;
        if (g_forced_frames == 2) java_set_touch(2, g_game_w / 2, g_game_h / 2);
        if (g_forced_frames == 0) g_forced_hit = 0;
    }
}

// OK while a script drives the player (world&0x100000 and pl+4 == 0, no script running: hanging on the bungee rope,
// script 16041): cIdleKeyProcess::UpdateProcess @000d3c98 hands input to ScriptKeyProcess @000d3b40, which reads only
// key-down events and HUD hits, not SimulateKeyPress. On the phone this is a touch on the attack button (hit 0xB ->
// table entry 14 -> cCharacter::CallScriptKey(4) @000c119c -> the script set by op 0xBB, 16048 judges the stop).
static bool script_key_mode() {
    uint32_t w = g::world(), gm = g::gm(), pl = w ? g::player() : 0;
    return w && gm && pl && (M32(w) & 0x100000) && M8(pl + 4) == 0 && M32(gm + 0x210) == 0;
}

void key_tap(int code) {
    if (code == 0x30 && script_key_mode()) { hud_hit(0xb); return; }
    g_taps.push_back(code);
}

// a tap whose hit test answers `code` (cBattleUi::HitTestImpl @000f8834 hook in hooks.c), so the original's own
// handler runs (cGameWorld::PreProcKey @0012c35c: Town 0x13 -> ShowTownFunc, '?' 3 -> help script / quest tab)
void hud_hit(int code) {
    g_forced_hit = code;
    g_forced_frames = 5;
    java_set_touch(1, g_game_w / 2, g_game_h / 2);
}

bool element_rect(uint32_t e, int *x, int *y, int *w, int *h) {
    if (!e) return false;
    int ax = 0, ay = 0;
    for (uint32_t p = e, n = 0; p && n < 32; p = M32(p + 8), n++) {
        ax += (int16_t)M16(p + 0x18);
        ay += (int16_t)M16(p + 0x1a);
    }
    *x = ax; *y = ay;
    *w = (int16_t)M16(e + 0x1c);
    *h = (int16_t)M16(e + 0x1e);
    return true;
}

void tap_element(uint32_t e) {
    int x, y, w, h;
    if (!element_rect(e, &x, &y, &w, &h)) return;
    int cx = x + w / 2, cy = y + h / 2;
    java_set_touch(1, cx, cy);
    post_after(2, [cx, cy] { java_set_touch(2, cx, cy); });
}

// cGameWorld::SetAutoScpKeyMode(world, on) @0012afb4 (as PreProcKey does for the fast button)
// cFormOption::OnReflectTouchType @00110e34 (this unused): 1 = Direct (sys8 |= 8, iVirtualPad::SetUiType(bui, 1)),
// otherwise V-Pad (sys8 &= ~8, SetUiType(bui, 2))
void set_touch_type(bool direct) { g::call(0x100e34, {0, direct ? 1u : 2u}); }
// A quickslot of either set without changing the set the player chose: the page (pl+0x2F4, flipped by
// cCharacter::ToggleHotSlot @000c27dc, which only XORs it and plays a sound) is set for the slot key, which the
// original reads in its key handling (cCreature::GetSlotInfo @000c4098), and put back afterwards.
// UNVERIFIED: restore delay (the key is consumed within a frame or two of the tap)
void use_slot(int page, int i) {
    static const int KEYS[5] = {0x21, 0x23, 0x27, 0x29, 0x20}; // slot_key_map_list @0x19900c
    uint32_t pl = g::player();
    if (!pl || i < 0 || i > 4) return;
    uint32_t old = M32(pl + 0x2f4);
    M32(pl + 0x2f4) = (uint32_t)(page & 1);
    key_tap(KEYS[i]);
    post_after(4, [pl, old] { if (g::player() == pl) M32(pl + 0x2f4) = old; });
}
void toggle_fast() {
    uint32_t w = g::world();
    if (w && M32(w + 0x768)) g::call(0x11afb4, {w, M32(w + 0x76c) == 0 ? 1u : 0u});
}

// cGameManager::OpenUI(gm, 0) @00126b34 + cGameUi::ShowMainMenu(ui, 0, tab) @0011f7d8 + world |= 0x80 (PreProcKey)
// HUD buttons the original handles in cGameWorld::PreProcKey @0012c35c (hit codes from cBattleUi::HitTestWithScript),
// called directly so they do not depend on a forced touch reaching HitTestImpl (lost while the player moves).
// code 3 quests: nothing while bui+0x180 == 2; a pending quest script (world+0x5F8 > 0) is fired
// (cGameManager::FireScript(int) @00127858); otherwise the main menu opens on tab 4.
// bui+0x180 is the direct-touch phase (2 = a finger holds to walk, work/hud_spec.md). The original only resets it
// on a touch release, so after walking with keys / a stick it stays 2: the gate applies to touch input only.
void quest_button(bool touch) {
    uint32_t bu = g::bui(), w = g::world(), f = g::field();
    if (!bu || !w || (touch && M32(bu + 0x180) == 2)) return;
    int scp = (int32_t)M32(w + 0x5f8);
    if (scp > 0) { g::call(0x117858, {g::gm(), (uint32_t)scp}); return; }
    if (f && M32(f + 0x70)) { hud_hit(3); return; } // the original asks a choice first (PreProcKey @0012c4b0 path): keep its own flow
    open_menu_tab(4);
}
// code 0x13 town services: nothing while bui+0x180 == 2, else cGameUi::ShowTownFunc(ui, 0, 0, 0) @0011f4b8
void town_button(bool touch) {
    uint32_t bu = g::bui(), ui = g::gameui();
    if (!bu || !ui || (touch && M32(bu + 0x180) == 2)) return;
    g::call(0x10f4b8, {ui, 0, 0, 0});
}
void open_menu_tab(int tab) {
    uint32_t gm = g::gm(), w = g::world(), ui = g::gameui();
    if (!gm || !w || !ui || M32(gm + 0x210) || M32(gm + 0xc) != 3) return;
    g::call(0x116b34, {gm, 0});
    g::call(0x10f7d8, {ui, 0, (uint32_t)tab});
    M32(w) |= 0x80;
}
// cAndInput::OnKeyPressed @00163a10 / OnKeyReleased @001639fc (input = *(gp_giInstance)+4)
void key_down(int code) {
    uint32_t gi = M32(IMG + 0x19af28);
    if (gi) g::call(0x153a10, {M32(gi + 4), (uint32_t)code});
}
void key_up(int code) {
    uint32_t gi = M32(IMG + 0x19af28);
    if (gi) g::call(0x1539fc, {M32(gi + 4), (uint32_t)code});
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- HUD (work/hud_spec.md 2, cBattleUi::DrawUiPlayInfo @000f9018)
static void read_player(Snapshot &s, uint32_t pl) {
    s.hp = (int32_t)M32(pl + 0x94);
    s.maxhp = (int32_t)M32(pl + 0x98);
    s.mp = (int32_t)M32(pl + 0x328);
    s.maxmp = (int32_t)M32(pl + 0x32c);
    s.level = (int32_t)M32(pl + 0x90);
    s.exp = (int32_t)M32(pl + 0x334);
    s.expmax = (int32_t)M32(pl + 0x338);
    s.gp = (int32_t)M32(pl + 0x344);
    s.sp = (int32_t)M32(pl + 0x34c);
    s.job = (int16_t)M16(pl + 6);
    s.name = g::plain_text(g::cstr(M32(pl + 0x30)));
    uint32_t inv = g::inv();
    s.gold = inv ? (int32_t)M32(inv + 0x48) : 0; // cInventory::AddGold @0012facc
    // MP is shown divided by a scale (cCharacter::AddMp @000c0c04): u16 *(resMgr+0x1e8)+0x7c
    uint32_t rm = g::resmgr();
    int scale = rm && M32(rm + 0x1e8) ? M16(M32(rm + 0x1e8) + 0x7c) : 1;
    if (scale > 1) { s.mp /= scale; s.maxmp /= scale; }
    // portrait: sprite set 0xa1, frame = job (cFormCharStat::OnConstruct @0011b9ac)
    s.face = sprites::image(0xa1, s.job);

    // skill buffs: count s16 pl+0x1ec, entries pl+0x1f0+0x18*i {+4 u16 set, +6 u8 image, +7 u8 palette, +8 s16 ticks}
    int nb = (int16_t)M16(pl + 0x1ec);
    for (int i = 0; i < nb && i < 4; i++) {
        uint32_t e = pl + 0x1f0 + 0x18 * i;
        Buff b;
        b.icon = sprites::image(M16(e + 4), M8(e + 6), M8(e + 7));
        b.ticks = (int16_t)M16(e + 8);
        if (!b.icon.empty()) s.buffs.push_back(b);
    }

    // quickslots: cCreature::GetSlotInfo @000c4098 = pl+0xb8+(page*5+i)*8 {s32 type; void* data}, page pl+0x2f4
    s.slot_page = (int)(M32(pl + 0x2f4) & 1);
    for (int k = 0; k < 10; k++) {
        uint32_t e = pl + 0xb8 + (uint32_t)k * 8;
        int type = (int32_t)M32(e);
        uint32_t data = M32(e + 4);
        Slot &sl = s.slots_all[k];
        if (!data) continue;
        if (type == 0) { // skill: cCharSkillInfo; def = +8 (+0x2d set, +0x2e image, +0x2f palette), name str(def+0)
            uint32_t def = M32(data + 8);
            if (!def) continue;
            sl.kind = 0;
            sl.icon = sprites::image(M8(def + 0x2d), M8(def + 0x2e), M8(def + 0x2f));
            if (rm) sl.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(def)));
            int maxcd = (int32_t)M32(data + 0x18), el = (int32_t)M32(data + 0x1c); // cSkillInfo::IncreCoolTime @00130db0
            if (maxcd > 0 && el < maxcd) sl.cooldown = 1.f - (float)el / maxcd;
        } else { // item: cInvenItem; info = +0x38 (+0x12 palette, +0x13 frame), set 0x6b (cItemImageList::DrawIcon @000e9818)
            uint32_t info = M32(data + 0x38);
            if (!info) continue;
            sl.kind = 1;
            sl.icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
            sl.count = (int32_t)M32(data + 0x3c);
            if (rm) sl.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info)));
            // cInventory::GetNowCoolTime @0012f960 / GetMaxCoolTime @0012f974 for consumables (type 6), group info+8
            if (M16(data + 6) == 6 && inv) {
                int grp = M8(info + 8);
                int now = (int32_t)M32(inv + 0x4c + 8 * grp), mx = (int32_t)M32(inv + 0x50 + 8 * grp);
                if (mx > 0 && now < mx) sl.cooldown = 1.f - (float)now / mx;
            }
        }
    }
    for (int i = 0; i < 5; i++) s.slots[i] = s.slots_all[s.slot_page * 5 + i];
}

// ---- message windows (work/msg_spec.md 2-3)
static std::string page_text(uint32_t m, int lines, int wrapw, bool *more, std::vector<std::string> *rows = nullptr) {
    // current page = from base(+0x1C)+offset(+0x28) up to the end of `lines` wrapped lines: cflConstStr::SkipLine
    // @0016ea60 on a copy of the string state with pauses ignored (+2 |= 0x100)
    static uint8_t *copy; // the cflConstStr copy is passed to guest code: guest memory
    if (!copy) copy = (uint8_t *)gas_calloc(1, 0x20);
    memcpy(copy, GP(m + 0x10), 0x1c);
    uint32_t cp = HPTR(copy);
    M16(cp + 2) |= 0x100;
    const char *base = (const char *)GP(M32(m + 0x1c));
    if (!M32(m + 0x1c)) { *more = false; return ""; }
    const char *start = base + (int32_t)M32(m + 0x28);
    const char *at = start;
    for (int i = 0; i < lines; i++) {
        g::call(0x15ea60, {cp, (uint32_t)wrapw});
        const char *e = (const char *)GP(M32(cp + 0xc)) + (int32_t)M32(cp + 0x18);
        if (rows && e > at && e <= at + 4096) { rows->emplace_back(at, e); at = e; }
    }
    const char *end = (const char *)GP(M32(cp + 0xc)) + (int32_t)M32(cp + 0x18);
    if (end < start || end > start + 4096) end = start + strlen(start);
    std::string pg(start, end);
    *more = *end != 0;
    return pg;
}

static size_t utf8_len(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}

// typewriter progress as the original draws it (cTalkBallonWnd::Draw @000ffa9c): line +0x78 is revealed up to clip
// width +0x84, which grows 0x14 px per frame up to that line's own width (cflString::TextRenderW), then the next line
// starts; at the last line +0x78 jumps to +0x40. So the visible characters are the earlier lines plus the share of
// the current line's width, measured per line with cflGraphics::TextRenderWEx(char const*) @001619b4
static float talk_reveal(const std::vector<std::string> &rows, int line, int px, int lines, int total) {
    if (total <= 0 || line >= lines || line >= (int)rows.size()) return 1.f;
    static std::string key;
    static std::vector<int> chars, widths;
    std::string k;
    for (auto &r : rows) { k += r; k += '\n'; }
    if (k != key) { // measured once per page
        key = k;
        chars.clear(); widths.clear();
        static char *buf;
        if (!buf) buf = (char *)gas_calloc(1, 4100);
        for (auto &r : rows) {
            chars.push_back((int)utf8_len(g::plain_text(r.c_str())));
            size_t n = std::min<size_t>(r.size(), 4096);
            memcpy(buf, r.data(), n); buf[n] = 0;
            widths.push_back(g::gfx() ? (int)g::call(0x1519b4, {g::gfx(), HPTR(buf)}) : 0);
        }
    }
    int vis = 0;
    for (int i = 0; i < line && i < (int)chars.size(); i++) vis += chars[i];
    if (line >= 0 && line < (int)chars.size()) {
        int w = widths[line];
        float f = w > 0 ? clampi(px, 0, w) / (float)w : 0.f;
        vis += (int)(chars[line] * f + 0.5f);
    }
    float r = vis / (float)total;
    return r < 0 ? 0 : r > 1 ? 1 : r;
}

// drill minigame cMbMinigame (vtable 0x1953a8; ctor @0014e288, Draw @0014db14, MbMgProcessKey @0014de20,
// ProcMsgBoxImpl @0014df78): beat length B +0xC8 (frames, gp_TarSystem vfunc +0x1C, at least 2), clock T +0xCC (from -1),
// command table +0xD0 (8-byte entries, byte 0 = command), count N +0xD8, presses so far +0xDC, failed +0xF0.
// Draw: band (box.x, h/6 for the fixed tables / (h-17)/2 for the random one, box.w, 17), target at (box.x + w/2, band
// y + 8); note u shows from T = B*u and slides from 0.4w out (right for even u, left for odd) to the target at
// T = B*u + 2B - 1; the first one drawn is (T-B)/B. A press counts in T - (u+1)B >= 0.8B, and T >= (u+2)B fails.
static void read_drill(Snapshot &s, uint32_t mg) {
    Snapshot::Drill &d = s.drill;
    uint32_t gm = g::gm();
    if (!gm) return;
    int bx = (int32_t)M32(gm + 0x1dc), bw = (int32_t)M32(gm + 0x1e4), bh = (int32_t)M32(gm + 0x1e8); // GetWorldViewBox @00126ed0
    int B = (int32_t)M32(mg + 0xc8), T = (int32_t)M32(mg + 0xcc), N = (int32_t)M32(mg + 0xd8), dc = (int32_t)M32(mg + 0xdc);
    uint32_t tab = M32(mg + 0xd0);
    if (B < 2 || !tab || N <= 0 || N > 16) return;
    bool fixed = tab == IMG + 0x19a114; // DAT_001aa114: the script's fixed sequences (types 1-3)
    d.on = true;
    d.band_x = bx; d.band_w = bw; d.band_h = 0x11;
    d.band_y = fixed ? bh / 6 : (bh - 0x11) / 2;
    d.cx = bw / 2 + bx; d.cy = d.band_y + 8;
    d.total = N; d.done = dc;
    d.fail = M32(mg + 0xf0) != 0;
    int cur = T >= B ? (T - B) / B : 0;
    d.good = !d.fail && (T < B ? dc >= 1 : dc > cur);
    if (d.fail) return;
    for (int u = cur; u < N && B * u <= T; u++) {
        int off = 2 * bw * (B * u + 2 * B - 1 - T) / (5 * (2 * B - 1));
        if (off < 0) off = 0;
        if (u & 1) off = -off;
        d.notes.push_back({u, (int)MS8(tab + 8 * u), d.cx + off, T - (u + 1) * B >= B * 80 / 100});
    }
    // test: EH_DRILLAUTO presses the lit note's key through the player's key path (game key codes LEFT 0x2d, RIGHT 0x2c, UP 0x2f, DOWN 0x2e, OK 0x30)
    static const int KEYS[5] = {0x2d, 0x2c, 0x2f, 0x2e, 0x30};
    static const bool autoplay = getenv("EH_DRILLAUTO") != nullptr;
    if (autoplay) for (auto &n : d.notes) if (n.live && n.idx == dc && n.cmd >= 0 && n.cmd < 5) { ui_log("[drill] B %d T %d press %d", B, T, n.idx); key_tap(KEYS[n.cmd]); }
}

// gathering gauge cCollectGaugeWnd (cCollectProcess puts it at mwm+8 while gathering, cMessageWndManager draws it last):
// +4 actor, +8 item number, +0xC now (int64), +0x14 duration, +0x18 start (int64) (Initialize @000fe40c,
// SetNowTime @000fe67c). Draw @00100380: box 0x78 x 0x28 at actor (x - 0x32, y - 0x3C) minus the field camera
// (field +4 / +8), kept inside the world view box; bar width x (now - start) / duration; name GetItemInfo(8, no)
// text, icon set 0x6B image info+0x13.
static void read_gather(Snapshot &s, uint32_t mw) {
    uint32_t gw = M32(mw + 8), f = g::field(), gm = g::gm(), rm = g::resmgr();
    if (!gw || !f || !gm) return;
    uint32_t act = M32(gw + 4);
    if (!act) return;
    Snapshot::Gather &g = s.gather;
    int bx = (int32_t)M32(gm + 0x1dc), by = (int32_t)M32(gm + 0x1e0), bw = (int32_t)M32(gm + 0x1e4), bh = (int32_t)M32(gm + 0x1e8);
    int x = (int32_t)M32(act + 0x14) - 0x32 - (int32_t)M32(f + 4), y = (int32_t)M32(act + 0x18) - 0x3c - (int32_t)M32(f + 8);
    if (x < bx) x = bx; else if (bx + bw <= x + 0x77) x = bx + bw - 0x78;
    if (y < by) y = by; else if (by + bh <= y + 0x27) y = by + bh - 0x28;
    g.on = true;
    g.x = x; g.y = y;
    uint64_t now = (uint64_t)M32(gw + 0xc) | (uint64_t)M32(gw + 0x10) << 32, start = (uint64_t)M32(gw + 0x18) | (uint64_t)M32(gw + 0x1c) << 32;
    uint32_t dur = M32(gw + 0x14);
    g.progress = dur ? (float)(double)(now - start) / (float)dur : 1.f;
    if (g.progress < 0) g.progress = 0; else if (g.progress > 1) g.progress = 1;
    static int last_no = -1;
    static std::string name, icon;
    int no = (int32_t)M32(gw + 8);
    if (no != last_no && rm) {
        uint32_t info = g::call(0xcc3b4, {rm, 8, (uint32_t)no, 0xffffffffu}); // cResourceManager::GetItemInfo @000dc3b4
        name = info ? g::plain_text(g::cstr(M32(rm + 0x34) + M32(info))) : "";
        icon = info ? sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12)) : "";
        last_no = no;
    }
    g.name = name; g.icon = icon;
}

static void read_messages(Snapshot &s) {
    uint32_t mw = g::mwm();
    if (!mw) return;
    read_gather(s, mw);
    uint32_t rm = g::resmgr();
    (void)rm;
    // modal box (+4)
    uint32_t m = M32(mw + 4);
    if (m && M32(m) == IMG + 0x1953a8) { read_drill(s, m); m = 0; }
    if (m) {
        int type = (int32_t)M32(m + 4);
        uint32_t flags = M32(m + 0xc);
        Talk &t = s.talk;
        if (type == 4) { // cTalkBallonWnd: name +0xDC, face sprite +0x6C (frame +0xF0), lines +0x40, wrap +0xB4, typewriter +0x78/+0x84
            t.active = true;
            t.kind = 4;
            t.waiting = (flags & 4) != 0;
            t.name = g::plain_text(g::cstr(M32(m + 0xdc)));
            int lines = (int32_t)M32(m + 0x40);
            int wrap = (int32_t)M32(m + 0xb4);
            std::vector<std::string> rows;
            std::string pg = page_text(m, lines > 0 ? lines : 3, wrap > 0 ? wrap : 200, &t.more, &rows);
            t.text = g::rml_text(pg.c_str(), false);
            t.text_len = (int)utf8_len(g::plain_text(pg.c_str()));
            t.reveal = talk_reveal(rows, (int32_t)M32(m + 0x78), (int32_t)M32(m + 0x84), lines, t.text_len);
            uint32_t face = M32(m + 0x6c);
            if (face && (int32_t)M32(m + 0xec) != 2) {
                // cflSprite: +6 frame, +0x14 cflSpriteSetInfo*. Find the set number for the sprite key.
                uint32_t info = M32(face + 0x14), sets = g::sprite_sets();
                int frm = (int32_t)M32(m + 0xf0);
                for (int k = 0; sets && k < 0x200; k++)
                    if (M32(sets + 4 * k) == info) { t.face = sprites::frame(k, frm); break; }
                t.face_right = (flags & 0x40000) != 0;
            }
            t.id = m ^ (uint32_t)M32(m + 0x28);
        } else if (type == 7) { // cGuideMessageWnd: title cflString +0x10 (ptr +0x20), body +0x30 (ptr +0x40)
            t.active = true;
            t.kind = 7;
            t.waiting = true;
            t.title = g::rml_text(g::cstr(M32(m + 0x20)));
            t.text = g::rml_text(g::cstr(M32(m + 0x40)));
            t.id = m;
        } else if (type == 0 && (flags & 4)) { // cMessageWnd waiting for a key: text +0x1C + +0x28, lines +0x40, wrap +0x38-8
            t.active = true;
            t.kind = 0;
            t.waiting = true;
            int lines = (int32_t)M32(m + 0x40);
            std::string pg = page_text(m, lines > 0 ? lines : 1, (int32_t)M32(m + 0x38) - 8, &t.more);
            t.text = g::rml_text(pg.c_str());
            // no typewriter here (reveal stays 1), but the panel reveals text_len characters: without it the whole
            // page was laid out transparent (script op 0x5B lines such as the bungee result, 16047 / 16045)
            t.text_len = (int)utf8_len(g::plain_text(pg.c_str()));
            t.id = m ^ (uint32_t)M32(m + 0x28);
        }
    }
    // non-modal list (+0): banners, warnings, pick-up toasts (cflLinkedList: s16 +0xE count, +0x10 head, node {next,data})
    // field name for map-banner detection, straight from the field (cField::GetName @001220e0): the snapshot's
    // field_name is only set once the player is in the field, after ShowMapName already created the banner
    static std::string last_field;
    if (uint32_t f = g::field()) { std::string fn = g::plain_text(g::cstr(g::call(0x1120e0, {f}))); if (!fn.empty()) last_field = fn; }
    uint32_t lst = M32(mw);
    if (lst) {
        int n = (int16_t)M16(lst + 0xe);
        uint32_t node = M32(lst + 0x10);
        for (int i = 0; i < n && node && i < 32; i++, node = M32(node)) {
            uint32_t b = M32(node + 4);
            if (!b) continue;
            int type = (int32_t)M32(b + 4);
            if (getenv("EH_MSGDBG") && type != 0) ui_log("[msg] box type %d", type);
            if (M32(b) == IMG + 0x1953a8) { read_drill(s, b); continue; }
            if (type == 4) { // cTalkBallonWnd that does not wait: InitType @00100f10 sets flag 2 for param_4 == 0, and
                // AddMessageBox @001019e0 lists those here instead of the modal slot. Several can be up at once: script
                // balloons without the wait byte (cflScript::MakeTalkBallon @000e0dd0, lifetime from the command) and
                // monster shouts (cMonster, lifetime 1000). Draw @000ffa9c skips the box without flag 1 and re-runs
                // SetBalloonRePos -> SetBalloonPos @000ff8b8 every frame: box +0x9C/+0xA0/+0xA4/+0xA8, tail point
                // +0x94/+0x98 (speaker in framebuffer px), flag 0x40000 = box under the tail
                uint32_t fl = M32(b + 0xc);
                if (!(fl & 1)) continue;
                int lines = (int32_t)M32(b + 0x40);
                int wrap = (int32_t)M32(b + 0xb4);
                bool more = false;
                std::string pg = page_text(b, lines > 0 ? lines : 3, wrap > 0 ? wrap : 200, &more);
                std::string tx = g::rml_text(pg.c_str(), false);
                if (tx.empty()) continue;
                Toast t{b, 22, tx};
                t.x = (int32_t)M32(b + 0x9c); t.y = (int32_t)M32(b + 0xa0); t.w = (int32_t)M32(b + 0xa4); t.h = (int32_t)M32(b + 0xa8);
                t.tx = (int32_t)M32(b + 0x94); t.ty = (int32_t)M32(b + 0x98);
                t.flags = fl;
                t.name = g::plain_text(g::cstr(M32(b + 0xdc)));
                t.show_name = (int32_t)M32(b + 0xec) != 2;
                if (getenv("EH_MSGDBG")) ui_log("[msg] balloon flags %08x box %d,%d %dx%d tail %d,%d '%s': %s", fl, t.x, t.y, t.w, t.h, t.tx, t.ty, t.name.c_str(), g::plain_text(pg.c_str()).c_str());
                s.toasts.push_back(t);
            } else if (type == 3) { // cPickUpItemWnd: displayed line cflString* +0x60 (text ptr +0x10)
                uint32_t ln = M32(b + 0x60);
                std::string tx = ln ? g::rml_text(g::cstr(M32(ln + 0x10))) : "";
                if (!tx.empty()) s.toasts.push_back(Toast{b, 3, tx});
            } else if (type == 0) { // cMessageWnd (work/msg_spec.md 3.1), cGameWorld::CreateMessageBox @0012c838 callers:
                // map banner (ShowMapName @0012c9f4), warnings (red border, InitType extra 0x4000), quest progress
                // (cQuestManager::MakeCondiWnd @000d9ae0: text starts with \b; capture count "%s x %d/%d") and script
                // messages (cflScript::MakeMessageBox @000e102c, script cmd 0x110): those are scene captions (sound
                // effects, narration) placed by the script, so they are drawn at the original's box rect
                // (+0x30..+0x3C, set by cMessageWnd::Start @00100a5c), with its flags and text colour (+0x48, lerped
                // by colour effects) - not in the notification stack
                uint32_t raw = M32(b + 0x1c) ? M32(b + 0x1c) + M32(b + 0x28) : 0;
                if (!raw) continue;
                uint32_t fl = M32(b + 0xc);
                int lines = (int32_t)M32(b + 0x40);
                bool more = false;
                std::string tx = lines > 1 ? g::rml_text(page_text(b, lines, (int32_t)M32(b + 0x38) - 8, &more).c_str()) : g::rml_text(g::cstr(raw));
                if (tx.empty()) continue;
                Toast t{b, 20, tx};
                t.x = (int32_t)M32(b + 0x30); t.y = (int32_t)M32(b + 0x34); t.w = (int32_t)M32(b + 0x38); t.h = (int32_t)M32(b + 0x3c);
                t.flags = fl; t.color = M32(b + 0x48);
                std::string plain = g::plain_text(g::cstr(raw));
                bool progress = M8(raw) == 8; // "\b%s %d/%d"
                if (!progress) { // "... 3/5" capture count
                    size_t sl = plain.rfind('/');
                    progress = sl != std::string::npos && sl > 0 && sl + 1 < plain.size() && isdigit((unsigned char)plain[sl - 1]) && isdigit((unsigned char)plain[sl + 1]) &&
                               plain.find_first_not_of("0123456789 )", sl + 1) == std::string::npos;
                }
                if (M32(b + 0x44) == 0xc000) t.kind = 10;
                else if (!last_field.empty() && plain.find(last_field) != std::string::npos) t.kind = 0; // map banner (the field name can be empty while the field loads)
                else if (progress) t.kind = 11;
                if (getenv("EH_MSGDBG")) ui_log("[msg] kind %d flags %08x rect %d,%d %dx%d col %08x first %02x: %s", t.kind, fl, t.x, t.y, t.w, t.h, t.color, M8(raw), plain.c_str());
                s.toasts.push_back(t);
            } else if (type == 8) { // cTickableMsgWnd (CreateTickableBox @0012c7a0): text cflString +0x10 (ptr +0x20), colour +0x38;
                // the original scrolls it in a strip at the bottom of the view (Draw @000fe910) until its passes are done
                std::string tx = g::rml_text(g::cstr(M32(b + 0x20)));
                if (!tx.empty()) { Toast t{b, 21, tx}; t.color = M32(b + 0x38); s.toasts.push_back(t); }
            } else if (type == 2) { // cMotionWnd (CreateCharHeadUpInfoBox @0012c304): line cflString* +0x60, rect +0x64..+0x70 above the player
                uint32_t ln = M32(b + 0x60);
                std::string tx = ln ? g::rml_text(g::cstr(M32(ln + 0x10))) : "";
                if (!tx.empty()) {
                    Toast t{b, 20, tx};
                    t.x = (int32_t)M32(b + 0x64); t.y = (int32_t)M32(b + 0x68); t.w = (int32_t)M32(b + 0x6c); t.h = (int32_t)M32(b + 0x70);
                    t.flags = 0x10; // outlined text, no box (Draw @000fe9d4)
                    s.toasts.push_back(t);
                }
            } else if (type == 6) { // cCountdownWnd: ticks +0x7C (15 per s)
                int t = (int32_t)M32(b + 0x7c) / 15;
                char buf[16];
                snprintf(buf, sizeof buf, "%02d:%02d", t / 60, t % 60);
                s.countdown = buf;
            } else if (type == 5) { // cChapterNameWnd: text +0x20
                std::string tx = g::rml_text(g::cstr(M32(b + 0x20)));
                if (!tx.empty()) s.toasts.push_back(Toast{b, 5, tx});
            }
        }
    }
    // mission popup: gm+0x1FC pending, singleton 0x19af34 (+0xC cQuest*); title GetLangData(0x2DC) @00130aac,
    // body cQuest::GetPopupDesc @000d95c4 (cShowPendingMission::ShowMission @00127278)
    uint32_t gm = g::gm();
    uint32_t sm = M32(IMG + 0x19af34);
    if (gm && M32(gm + 0x1fc) && sm && !s.talk.active) {
        uint32_t q = M32(sm + 0xc);
        Talk &t = s.talk;
        t.active = true;
        t.kind = 100;
        t.waiting = true;
        t.title = g::rml_text(g::cstr(g::call(0x120aac, {0x2dc})));
        if (q) t.text = g::rml_text(g::cstr(g::call(0xc95c4, {q})));
        t.id = sm;
    }
}

// ---- forms (work/ui_spec.md 1.5, 7)
static void read_forms(Snapshot &s) {
    uint32_t ui = g::gameui();
    if (!ui) return;
    uint32_t foc = M32(ui + 0x18);
    for (uint32_t f = M32(ui + 0x14), n = 0; f && n < 32; f = M32(f + 0x40), n++) {
        uint32_t fl = M32(f + 0x14);
        if (fl & 0x1800000) continue; // closed / dead
        if (fl & 0x8000) continue;    // hidden
        s.forms.push_back({(int16_t)M16(f + 0x38), f == foc, f});
        s.top_form = (int16_t)M16(f + 0x38);
    }
}

// ---- title: cMenuMgr (vaddr 0x19b244) state +8, cFormMainMenu at +0x20 (cMenuMgr::OnTitle @00146f6c);
// save headers systemInfo+0x10+slot*0x30 (work/title_spec.md 2)
static void read_title(Snapshot &s) {
    uint32_t mm = M32(IMG + 0x19b244);
    s.menu_state = mm ? (int32_t)M32(mm + 8) : -1;
    s.title_form = mm ? M32(mm + 0x20) : 0;
    if (s.gm_state != 1) return;
    if (s.title_form) s.title_delete_mode = M32(s.title_form + 0x168) != 0;
    for (int i = 0; i < 3; i++) {
        uint32_t h = g::sysinfo() + 0x10 + 0x30 * i;
        SaveSlot &sv = s.saves[i];
        sv.exists = M8(h) != 0;
        sv.data = M8(h + 1) != 0;
        sv.level = M16(h + 2);
        sv.seconds = (int32_t)M32(h + 4);
        sv.job = M8(h + 0xa);
        if (sv.exists) {
            sv.face = sprites::image(0xa1, sv.job);
            if (sv.job < 3) sv.job_name = g::plain_text(g::cstr(M32(IMG + 0x19920c + 4 * sv.job))); // gCharNpcName
        }
    }
    s.keyart = sprites::frame(20, 0x12, 0, 0);
}

// forms whose interface the modern UI draws itself (others keep the original look)
static std::set<int> &ported() { static std::set<int> p; return p; }
static std::map<int, std::function<bool(uint32_t)>> &ported_if() { static std::map<int, std::function<bool(uint32_t)>> p; return p; }
void set_ported(int id) { ported().insert(id); }
void set_ported_if(int id, std::function<bool(uint32_t)> pred) { ported_if()[id] = std::move(pred); }
static bool is_ported(const Form &f) {
    auto it = ported_if().find(f.id);
    if (it != ported_if().end()) return it->second(f.ptr);
    return ported().count(f.id) != 0;
}

// cflUiLabelStr::GetText @00154fd8: owned cflString (+0x3c -> +0x10), raw char* (+0x2c when flag 0x80000) or the
// UI-data string table (ui+0x44 + offset); buttons keep theirs at +0x28 (cflUiButton::GetText @00155548)
std::string element_text(uint32_t e) {
    if (!e) return "";
    uint32_t fl = M32(e + 0x14);
    int type = (fl >> 9) & 0x1f;
    uint32_t ui = g::gameui();
    uint32_t p = 0;
    if (type == 4) {
        uint32_t t = M32(e + 0x28);
        p = (fl & 0x80000) ? t : (ui && M32(ui + 0x44) && t ? M32(ui + 0x44) + t : 0);
    } else if (type == 2 || type == 0x10) {
        uint32_t own = M32(e + 0x3c);
        if (own) p = M32(own + 0x10);
        else if (fl & 0x80000) p = M32(e + 0x2c);
        else if (ui && M32(ui + 0x44)) p = M32(ui + 0x44) + M32(e + 0x2c);
    }
    return p ? g::rml_text((const char *)GP(p)) : "";
}

void capture_frame(Cpu *c) {
    g::set_cpu(c);
    Snapshot s;
    static uint64_t frame;
    s.frame = ++frame;
    uint32_t gm = g::gm();
    s.gm_state = gm ? (int32_t)M32(gm + 0xc) : -1;
    s.script = gm && M32(gm + 0x210) != 0;
    uint32_t gx = g::gfx();
    if (gx) { s.fade = (int16_t)M16(gx + 0x10); s.fade_max = (int32_t)M32(gx + 0x14); if (s.fade_max <= 0) s.fade_max = 1; }
    uint32_t w = g::world();
    uint32_t pl = (s.gm_state == 3 || s.gm_state == 4) ? g::player() : 0;
    // field size (cField +0x7C/+0x80, cField::AdjustToInner @00122568) also while the field changes (gm state 5):
    // the new map is drawn, fading in, before the player exists, and the small-map zoom must already fit it
    if (w && g::field() && (s.gm_state == 3 || s.gm_state == 4 || s.gm_state == 5)) {
        uint32_t f = g::field();
        s.map_w = (int32_t)M32(f + 0x7c);
        s.map_h = (int32_t)M32(f + 0x80);
    }
    if (w && pl) {
        s.in_field = true;
        uint32_t wf = M32(w);
        s.hud_hidden = (wf & 0x8000200) != 0; // cGameWorld::Draw @0012e574 skips cBattleUi::Draw
        s.slots_hidden = M32(w + 0x770) != 0; // DrawUiPlayInfo: quickslots / attack hidden
        read_player(s, pl);
        s.field_group = (int32_t)M32(w + 8);
        s.town = s.field_group >= 0x1a && s.field_group <= 0x21;
        uint32_t f = g::field();
        if (f) s.field_name = g::plain_text(g::cstr(g::call(0x1120e0, {f}))); // cField::GetName @001220e0
        { // field changes in the log (group world+8, sub world+0xC): reproducing a place seen on a device
            static int lg = -1, ls = -1;
            int sub = (int32_t)M32(w + 0xc);
            if (s.field_group != lg || sub != ls) { lg = s.field_group; ls = sub; ui_log("[field] %d/%d '%s' map %dx%d", lg, ls, s.field_name.c_str(), s.map_w, s.map_h); }
        }
        uint32_t sys8 = M32(g::sysinfo() + 8);
        s.can_save = (sys8 & 1) && !(wf & 0x60000) && M32(pl + 0x43c) == 0; // Save button gate (DrawUiPlayInfo @f9528)
        s.can_menu = !(wf & 0x1000);
        // the voyage map (cField::SetExtendFieldFlag @00124608 sets cField +0x70 and world flags 0x21000): there the
        // original's menu key / bag button opens the end-voyage choice instead of the menu (cGameWorld::PreProcKey
        // @0012c35c, field +0x70 != 0 -> ShowChoicePopup, lang 0x83/0x6f/0x84)
        s.voyage = f && M32(f + 0x70) != 0;
        s.minimap_ok = !(wf & 0x41000) && !s.town;
        s.vpad = !(M32(g::sysinfo() + 8) & 8); // Direct touch control when set (work/hud_spec.md, sys8 flags)
        s.minimap_drawn = !(wf & 0x8000200) && !(wf & 0x1000) && !s.town; // cGameWorld::Draw @0012e574 -> cMinimap::DrawMinimap @00104014
        uint32_t qm = g::questmgr();
        s.quest_ready = qm && g::call(0xc9280, {qm}) != 0; // cQuestManager::HasCompletable @000d9280
        uint32_t inv = g::inv();
        s.bag_full = inv && g::call(0x11f910, {inv}) != 0; // cBackPack::IsBackPackFull @0012f910
        if (inv) { s.bag_cap = (int32_t)M32(inv + 0x1c); s.bag_max = (int32_t)M32(inv + 0x18); }
        s.fast_button = M32(w + 0x768) != 0;
        s.fast_on = M32(w + 0x76c) == 1;
        s.fast_locked = gm && (M32(gm + 0x1b4) & 0x4000) != 0; // HideFastButton(1) @00129578 sets it
        // boss gauge: world flag 0x40000000, creature at bui+0x128 (cGameWorld::SetBossGauge @0012b698)
        uint32_t bu = g::bui();
        uint32_t boss = bu ? M32(bu + 0x128) : 0;
        if ((wf & 0x40000000) && boss) {
            s.boss = true;
            s.boss_name = g::plain_text(g::cstr(M32(boss + 0x30)));
            s.boss_hp = (int32_t)M32(boss + 0x94);
            s.boss_max = std::max(1, (int32_t)M32(boss + 0x98));
            s.boss_level = (int32_t)M32(boss + 0x90);
        }
        // target gauge: bui+0xD4 active, +0xD8 name, +0xDC level, +0xE0 hp, +0xE4 max, visible frames +0x5A
        if (bu && M32(bu + 0xd4) && (int16_t)M16(bu + 0x5a) > 0) {
            s.target = true;
            s.target_name = g::plain_text(g::cstr(M32(bu + 0xd8)));
            s.target_level = (int32_t)M32(bu + 0xdc);
            s.target_hp = (int32_t)M32(bu + 0xe0);
            s.target_max = std::max(1, (int32_t)M32(bu + 0xe4));
        }
    }
    read_messages(s);
    read_forms(s);
    read_title(s);
    if (pl) capture_menu(s, pl);
    capture_dialog(s);
    capture_store(s);
    capture_smithy(s);
    capture_farm(s);
    capture_skillbook(s);
    capture_detail(s);
    capture_collection(s);
    if (getenv("EH_ERRDBG")) { // cGameError singleton (DAT_001ab164, GetInstance @001326d8): watch the message pool
        uint32_t ge = M32(IMG + 0x19b164);
        static uint32_t last[9];
        if (ge) {
            uint32_t now[9];
            for (int i = 0; i < 4; i++) { now[2 * i] = M32(ge + 4 + 0x20 * i + 0x14); now[2 * i + 1] = M32(ge + 4 + 0x20 * i + 0x10); }
            now[8] = M8(ge + 0x84) | (M32(M32(now[0]) + 0xc) << 8);
            if (memcmp(now, last, sizeof now)) {
                memcpy(last, now, sizeof now);
                ui_log("[err] frame %llu pool alloc %08x %08x %08x %08x buf %08x %08x %08x %08x flags+vt %08x", (unsigned long long)s.frame, now[0], now[2], now[4], now[6], now[1], now[3], now[5], now[7], now[8]);
            }
        }
    }
    capture_map(s);

    // the original interface is always hidden: forms without a dedicated modern screen are redrawn by the
    // generic renderer (autoform.cpp) from the original's recorded draw calls
    std::vector<int> unported;
    for (auto &f : s.forms)
        if (!is_ported(f)) unported.push_back(f.id);
    autoform::end_frame(c, unported);
    g_strip = true;
    s.stripped = g_strip;

    std::lock_guard<std::mutex> l(g_mx);
    g_pub = std::move(s);
}

} // namespace state
