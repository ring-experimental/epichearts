// Snapshot of the original's state taken on the game thread at the end of every frame (cGlFbGraphics::FlushImpl),
// read by the modern UI on the main thread. Field meanings and sources: work/hud_spec.md, work/msg_spec.md,
// work/ui_spec.md, work/title_spec.md, work/menu_pages_spec.md.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Cpu;

namespace state {

struct Slot {
    int kind = -1;        // -1 empty, 0 skill, 1 item
    std::string icon;     // sprite key ("" while not rendered)
    std::string name;
    int count = 0;
    float cooldown = 0;   // remaining fraction 0..1
};

struct Buff { std::string icon; int ticks = 0; };

struct Talk {             // cTalkBallonWnd (type 4), cMessageWnd waiting for input, cGuideMessageWnd
    bool active = false;
    int kind = 0;         // 4 talk balloon, 0 message box, 7 guide notice, 100 mission popup
    std::string name;     // speaker
    std::string face;     // portrait sprite key
    bool face_right = false;
    std::string title;    // guide / mission title
    std::string text;     // RML markup of the current page
    int text_len = 0;     // visible characters (code points) of the page
    float reveal = 1;     // typewriter progress 0..1
    bool more = false;    // more pages follow
    bool waiting = false; // waits for the player
    uint32_t id = 0;      // changes when the box / page changes
};

// non-modal message boxes. kind: 0 map banner, 3 pick-up / result notice, 5 chapter card, 10 warning,
// 11 quest progress, 20 script caption (drawn where the original draws it: box rect in framebuffer px, flags, colour),
// 22 talk balloon that does not wait (over its speaker: box rect, tail point tx/ty, speaker name)
struct Toast {
    uint32_t id; int kind; std::string text;
    int x = 0, y = 0, w = 0, h = 0;
    uint32_t flags = 0, color = 0xffffff00;
    int tx = 0, ty = 0;
    std::string name;
    bool show_name = false; // the original draws the name (balloon type +0xEC != 2)
};

struct Form { int id; bool focused; uint32_t ptr; };

struct QuestInfo { int kind = 0; bool done = false, failed = false; std::string title, desc; std::vector<std::string> conds; std::vector<bool> conds_ok; int timer = -1; };

struct ItemInfo {             // cInvenItem (work/menu_pages_spec.md, menu_lists_spec.md 1)
    uint32_t ptr = 0;
    int type = 0, id = 0, count = 0, req_lv = 0, cls = 0, sell = 0, buy = 0, dur = 0, dur_max = 0, refine = 0;
    uint32_t flags = 0;
    bool hotslot = false;
    std::string name, icon, type_name, desc;
    std::string name_rml;      // name in the original's grade colour (g::item_name_rml)
};
struct SkillRow {             // cCharSkillInfo (menu_lists_spec.md 3)
    uint32_t ptr = 0;
    std::string name, icon;
    int rank = 0, max_rank = 0, req_lv = 0, sp_need = 0, type = 0, mp = 0;
    bool learned = false, can_up = false, hot_ok = false;
    std::string desc;          // effect text (GetSubDescString(2, def+0x24)), markup
    std::vector<std::pair<std::string, std::string>> stats; // the original's stat rows: MP, cool time, duration (cFormNotice::Finalize_ContentsSkill)
};
struct HotRef { int kind = -1; std::string icon, name, name_rml; };

struct MenuData {             // cFormInvenMDI (form 4) and its pages (work/menu_pages_spec.md)
    uint32_t mdi = 0;
    int tab = -1;
    uint32_t page_form[10] = {0};
    std::string title;        // character title (cCharacter +0x35e)
    // the title list of cFormCharTitle (form 0x10, initContents @0011c630): every record of resMgr+0x274
    struct TitleRow { bool open = false, learned = false, on = false; std::string name, desc; };
    std::vector<TitleRow> titles;
    std::string l_equip, l_info; // its row menu: 0x1001 lang 0x245, 0x1002 lang 0x22b (cInvenMenu::TransferItems @0010c5b0)
    int str[3] = {0}, invest[3] = {0};
    int atk = 0, def = 0, hit = 0, eva = 0, crit = 0;
    std::vector<QuestInfo> quests;
    HotRef hot[10];           // both hotslot pages (pl+0xb8 + s*8)
    // bag (form 7)
    std::vector<ItemInfo> items;
    int inv_sel = -1, inv_cat = 0, inv_cap = 0, inv_used = 0;
    // equipment (form 8)
    ItemInfo equipped[6];
    std::vector<ItemInfo> cands;
    int eq_slot = 0x10, cand_sel = -1;
    int delta[7] = {0};
    bool delta_on = false;
    // skills (form 9)
    int sk_tab = 0, sk_sel = -1;
    std::vector<SkillRow> skills;
    struct Life { std::string name, icon; int lv = 0, exp = 0, max = 1; } life[3];
};

struct Dialog {                // modal original form shown by the modern dialog (dialogs.cpp)
    int id = 0;               // form id
    uint32_t ptr = 0;
    int kind = 0, mode = 0;
    std::string title, body;
    std::vector<std::string> buttons;   // button captions in order
    struct Item { std::string text; bool off = false; };
    std::vector<Item> items;            // list entries (choice / linkstone), off: not selectable
    int selected = 0;
    int count = 0, count_max = 0;       // count popups
    int extra = 0;                      // resurrect: revival items owned
    // rich notices (cFormNotice kinds 1-6, 8): a subtitle, a big icon, rows of icon + text
    std::string subtitle, icon, rows_title;
    struct Row { std::string icon, text; };
    std::vector<Row> rows;
};

struct CharSelect {           // cFormMakeChar (form 0x27; work/title_spec.md 3)
    uint32_t ptr = 0;
    int selected = -1;        // s16 +0x74
    bool can_cancel = true;   // !(+0x76)
    int rect[3][4] = {};      // class buttons (framebuffer rect) for class 0, 1, 2
    std::string name[3], desc[3];
};

struct Town {                 // cFormTownFunc (form 0x31)
    uint32_t ptr = 0;
    uint32_t button[6] = {};  // function buttons 0..5 (cell i child[3], funcSelectHandler @000fc64c); 6 = trade (online) not shown
    int rect[6][4] = {};
};

struct SaveSlot { bool exists = false, data = false; int level = 0, seconds = 0, job = 0; std::string face, job_name; };

struct Snapshot {
    uint64_t frame = 0;
    int gm_state = -1;          // cGameManager FSM state (gm+0xc): 1 menu, 2 ready, 3 world, 4 UI
    bool in_field = false;
    bool script = false;        // gm+0x210
    bool hud_hidden = true;     // world flags hide the HUD (0x8000200), or no player
    int fade = 0;               // cFbGraphics fade level (s16 +0x10), range +-fade_max
    int fade_max = 1;

    // player
    int hp = 0, maxhp = 0, mp = 0, maxmp = 0, level = 0;
    int exp = 0, expmax = 1, gold = 0, gp = 0, sp = 0;
    int job = 0;
    std::string name;           // character name (cActiveObj name, +0x30)
    std::string face;           // player portrait sprite key
    std::vector<Buff> buffs;
    int slot_page = 0;
    bool slots_hidden = false;
    Slot slots[5];              // the page the original shows (touch HUD)
    Slot slots_all[10];         // set A (page 0) then set B (page 1): keyboard / gamepad HUD

    // field
    std::string field_name;
    int field_group = 0;
    int map_w = 0, map_h = 0;   // field size in pixels (cField +0x7C/+0x80)
    bool town = false;          // field group 0x1A..0x21
    bool can_save = false;
    bool can_menu = true;
    bool voyage = false;        // sky-sea voyage on the puffer (cField +0x70, extend field): the menu key ends it
    bool quest_ready = false;   // cQuestManager::HasCompletable
    bool bag_full = false;
    int bag_cap = 0, bag_max = 0;   // backpack slots inv+0x1C / limit inv+0x18 (cBackPack::IncreaseSlot @0012f7a0)
    bool minimap_ok = false;
    bool minimap_drawn = false; // cGameWorld::Draw draws cMinimap::DrawMinimap this frame
    bool vpad = false;          // touch control V-Pad (systemInfo+8 bit 0x8 clear) instead of Direct
    bool fast_button = false, fast_on = false;
    bool fast_locked = false;   // gm+0x1B4 & 0x4000: the scene vetoes the fast button (cGameWorld::ShowFastButton @00129558)

    // target / boss
    bool target = false;
    std::string target_name;
    int target_level = 0, target_hp = 0, target_max = 1;
    bool boss = false;
    std::string boss_name;
    int boss_level = 0, boss_hp = 0, boss_max = 1;

    // messages
    Talk talk;
    std::vector<Toast> toasts;
    std::string banner;          // map-name banner text
    uint32_t banner_id = 0;
    std::string countdown;
    // drill minigame (cMbMinigame, script cmd 0xEF), framebuffer pixels; read_drill in state.cpp
    struct Drill {
        bool on = false;
        int band_x = 0, band_y = 0, band_w = 0, band_h = 0, cx = 0, cy = 0;
        struct Note { int idx, cmd, x; bool live; }; // cmd 0 left, 1 right, 2 up, 3 down, 4 attack
        std::vector<Note> notes;
        int done = 0, total = 0;
        bool good = false, fail = false;
    } drill;
    // gathering / fishing gauge (cCollectGaugeWnd at mwm+8), framebuffer pixels; read_gather in state.cpp
    struct Gather {
        bool on = false;
        int x = 0, y = 0;   // the original's 0x78 x 0x28 box (top left)
        float progress = 0; // 0..1
        std::string name, icon;
    } gather;

    // forms
    std::vector<Form> forms;
    int top_form = -1;
    bool stripped = false;      // the original interface is hidden this frame (modern UI owns the screen)

    // title (cMenuMgr state +8, cFormMainMenu at menu+0x20; work/title_spec.md)
    int menu_state = -1;
    uint32_t title_form = 0;
    bool title_delete_mode = false;
    SaveSlot saves[3];
    std::string keyart;         // sprite key of the title key art (cMenuMgr::DrawTitleBg: set 20 frame 0x12)

    MenuData menu;
    Dialog dialog;
    struct Qbox {                   // QBox draw: cFormPopup kind 8 (qbox.cpp)
        uint32_t ptr = 0;
        std::string box, box_icon, msg;  // the opened box (+0x2F8), the popup's message label (+0xAC)
        int cost = 0; bool gold = false; // +0x2FC, +0x300 == 1: gold, else dragon scales (0 = free)
        int state = 0;                   // u8 +0x1C9: < 2 the reels turn, 2 done
        int result = 0;                  // +0x1CC: 0 granted, 1 failed (GetConstMsg 0xB5)
        int win = -1;                    // s8 +0x1EA
        int count = 0;                   // s16 +0x1E8
        int phase[3] = {0, 0, 0};        // reel stop stage u8 +0x1D5 + 8i (2 stopping, 3 stopped)
        std::string icon[3], name[3];    // reel rewards +0x1D0 + 8i (type, grade, s16 id)
        std::vector<std::string> spin;   // icons of the fake rewards the reels show while turning (QBoxRewardTemp)
    } qbox;
    CharSelect charsel;
    Town townsvc;
};

void capture_menu(Snapshot &s, uint32_t player); // menu.cpp (game thread)

// main thread
const Snapshot &get();      // latest published snapshot (copied once per UI frame)
void pull();                 // copy the latest snapshot for this UI frame

// game thread
void capture_frame(Cpu *c);  // FlushImpl hook
bool strip_original_ui();    // decided in capture_frame: original interface hidden this frame
void set_ported(int form_id); // forms the modern UI draws itself
// per-instance decision for a form id (game thread): true = the modern UI draws this form
void set_ported_if(int form_id, std::function<bool(uint32_t form)> pred);
void capture_dialog(Snapshot &s);   // dialogs.cpp (game thread)
void capture_store(const Snapshot &s); // store.cpp (game thread)
void capture_map(const Snapshot &s);   // map.cpp (game thread)
void capture_smithy(const Snapshot &s);    // smithy.cpp (game thread)
void capture_farm(const Snapshot &s);      // farm.cpp
void capture_skillbook(const Snapshot &s); // skillbook.cpp
void capture_detail(const Snapshot &s);    // detail.cpp
void capture_collection(const Snapshot &s); // collection.cpp
std::string element_text(uint32_t elem);  // LabelStr / Button text (cflUiLabelStr::GetText @00154fd8)
void run_actions(Cpu *c);    // procTouch: executes queued UI actions

// any thread: queue work for the game thread (executed at the next procTouch)
void post(std::function<void()> fn);
void post_after(int frames, std::function<void()> fn);
// game thread: tap an original cflUiElement at its centre (framebuffer coords from the parent chain,
// work/ui_spec.md 4.1: absolute = view origin + sum of ancestors' x,y)
void tap_element(uint32_t elem);
bool element_rect(uint32_t elem, int *x, int *y, int *w, int *h);

// key injection (game thread): one-shot (GofManager.SimulateKeyPress) / hold
void key_tap(int code);       // queued: one SimulateKeyPress per game frame
void hud_hit(int code);       // one tap on an original HUD button (cBattleUi::HitTestImpl code, hud_spec 5.1)
void set_touch_type(bool direct);
void use_slot(int page, int i); // quickslot i of set A (0) / B (1) regardless of the shown page, game thread // cFormOption::OnReflectTouchType, game thread
void toggle_fast();           // cGameWorld::SetAutoScpKeyMode
void open_menu_tab(int tab);  // cGameUi::ShowMainMenu(tab) + OpenUI
void quest_button(bool touch);   // HUD quest button (cGameWorld::PreProcKey code 3), game thread
void town_button(bool touch);    // HUD town button (code 0x13), game thread
void key_down(int code);
void key_up(int code);
}
