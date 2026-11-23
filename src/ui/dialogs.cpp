// Modal dialogs: replaces the original's message / choice / count / death / linkstone forms (work/msg_spec.md 7-14).
// Answers are given through the forms' own key handlers so callbacks and script results fire exactly as in the
// original (cFormNotice / cFormChoice report results only from their key handlers).
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "cashshop.h"
#include "sprites.h"
#include <cstring>

namespace state {

// cFormNotice (1): only the plain / paged text kinds (0, 9) are drawn here; reward / quest / skill / title layouts
// keep the generic renderer. cFormPopup (0xF): count kinds 3/4/5/7/10.
static bool notice_text(uint32_t f) { int k = (int32_t)M32(f + 0x9c); return k == 0 || k == 9 || (k >= 1 && k <= 6) || k == 8; }

// ---- rich notices (cFormNotice kinds, Finalize @000ecff4 -> initLayout @000eb23c). The source structure is +0x114
// (ShowNoticePopup @0011f5dc userPtr); the working text +0xC8 is released after use, so the content is rebuilt from it.
static std::string lang(int id) { return g::rml_text(g::cstr(g::call(0x120aac, {(uint32_t)id}))); }
static std::string pool(uint32_t rm, uint32_t off) { return g::plain_text(g::cstr(M32(rm + 0x34) + off)); }
// a reward row as Finalize_ContentsReward @000ebf00 / Finalize_ContentsIndunReward @000ebae8 build them
static Dialog::Row reward_row(uint32_t rm, int type, int id, int grade, int count) {
    Dialog::Row r;
    if (type == 100) { // gold: icon by amount (frames 0x13 / 0x17 / 0x16)
        r.icon = sprites::image(0x6b, count < 20 ? 0x13 : count < 40 ? 0x17 : 0x16);
        char b[96]; snprintf(b, sizeof b, g::cstr(g::call(0x120aac, {0x94})), count); r.text = g::plain_text(b);
    } else if (type == 0x65) { r.icon = sprites::image(0x6b, 0x45); r.text = g::plain_text(g::cstr(g::call(0x120aac, {0x8f}))) + " " + std::to_string(count); }
    else if (type == 0x66) { // study book: cStudyBook::GetBookTitle @001319fc
        uint32_t sb = g::call(0x121c88, {});
        r.icon = sprites::image(0x6b, 8);
        if (sb) r.text = g::plain_text(g::cstr(g::call(0x1219fc, {sb, (uint32_t)id, 0})));
    } else {
        uint32_t info = g::call(0xcc3b4, {rm, (uint32_t)type, (uint32_t)id, (uint32_t)grade});
        if (info) {
            r.icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
            r.text = pool(rm, M32(info)) + (count > 1 ? " x" + std::to_string(count) : std::string());
        }
    }
    return r;
}
// the label texts of a container, in order (body columns of the quest / skill / blessing layouts)
static void labels_in(uint32_t e, std::string &out, int depth = 0) {
    if (!e || depth > 6 || (M32(e + 0x14) & 0x8000)) return;
    int ty = (M32(e + 0x14) >> 9) & 0x1f;
    if (ty == 2 || ty == 0x10) { std::string t = element_text(e); if (!t.empty() && t != "\xeb\x9d\xbc\xeb\xb2\xa8") out += (out.empty() ? "" : "<br/><br/>") + t; return; }
    if (ty == 7 || ty == 8 || ty == 9 || ty == 0xd || ty == 0xe || ty == 0x11) {
        uint32_t k = M32(e + 0x24);
        for (int i = 0; i < M8(e + 0x20) && k && i < 24; i++) labels_in(M32(k + 4 * i), out, depth + 1);
    }
}
static void capture_rich_notice(Dialog &d, uint32_t f, int kind) {
    uint32_t P = M32(f + 0x114), rm = g::resmgr();
    if (!P || !rm) return;
    if (kind == 1 || kind == 2) { // cQuest: title GetTitle @000d94a4, rewards +0x5C (8 bytes: u16 type, u16 a, u16 id), n u8 +9
        d.subtitle = g::plain_text(g::cstr(g::call(0xc94a4, {P})));
        int n = M8(P + 9);
        for (int j = 0; j < n && j < 6; j++) {
            uint32_t e = P + 0x5c + 8 * j;
            int ty = M16(e);
            if (ty == 100 || ty == 0x65 || ty == 0x66) d.rows.push_back(reward_row(rm, ty, (int32_t)M32(e + 4), -1, (int32_t)M32(e + 4)));
            else if (ty < 5) d.rows.push_back(reward_row(rm, ty, M16(e + 6), M16(e + 4), 1));
            else d.rows.push_back(reward_row(rm, ty, M16(e + 6), -1, M16(e + 4)));
        }
        d.rows_title = "\xeb\xb3\xb4\xec\x83\x81"; // 보상
        if (kind == 2) { // quest detail: description and objectives are the labels of the body (+0x94)
            d.body.clear();
            labels_in(M32(f + 0x94), d.body);
        }
    } else if (kind == 3) { // skill: cCharSkillInfo*, def = +8 (icon set +0x2D / frame +0x2E / palette +0x2F)
        uint32_t def = M32(P + 8);
        if (def) {
            d.icon = sprites::image(M8(def + 0x2d), M8(def + 0x2e), M8(def + 0x2f));
            d.subtitle = pool(rm, M32(def)) + Rml::CreateString("  Lv %d / %d", M8(P + 0x13), M8(def + 6));
        }
        d.body.clear();
        labels_in(M32(f + 0x94), d.body);
    } else if (kind == 4 || kind == 5) { // blessing record: +0 name, +4 description, materials +8 (s8 type, u8 n, s16 id)
        d.subtitle = pool(rm, M32(P));
        d.body = g::rml_text(g::cstr(M32(rm + 0x34) + M32(P + 4)));
        if (kind == 4) d.body += "<br/><br/>" + lang(0x1de) + " " + std::to_string(M16(P + 0x24)) + lang(0x119);
        else {
            for (int k = 0; k < 4; k++) {
                uint32_t m = P + 8 + 4 * k;
                int ty = (int8_t)M8(m), n = M8(m + 1);
                if (ty < 0 || n == 0) break;
                d.rows.push_back(reward_row(rm, ty, (int16_t)M16(m + 2), -1, n));
            }
            if (!d.rows.empty()) d.rows_title = lang(0x1df);
        }
    } else if (kind == 6) { // character title record: +4 name; the body text (description, progress, bonus) is kept at +0xD8
        d.subtitle = pool(rm, M32(P + 4));
        d.body = g::rml_text(g::cstr(M32(f + 0xd8)));
    } else if (kind == 8) { // dungeon / mission rewards: n s32 +0, entries +4 (12 bytes), item id by the player's class
        int n = (int32_t)M32(P);
        uint32_t pl = g::player();
        int cls = pl ? M16(pl + 6) : 0;
        for (int i = 0; i < n && i < 11; i++) {
            uint32_t e = P + 4 + 12 * i;
            int ty = M16(e);
            if (ty < 5) d.rows.push_back(reward_row(rm, ty, M16(e + 4 + 2 * (cls < 3 ? cls : 0)), cls, 1));
            else if (ty == 100 || ty == 0x65 || ty == 0x66) d.rows.push_back(reward_row(rm, ty, (int32_t)M32(e + 4), -1, (int32_t)M32(e + 4)));
            else d.rows.push_back(reward_row(rm, ty, (int32_t)M32(e + 4), -1, (int32_t)M32(e + 8)));
        }
        d.rows_title = "\xeb\xb3\xb4\xec\x83\x81";
        d.body.clear();
    }
}
// QBox draw (cFormPopup kind 8, OpenQbox @0010cf74): the reels of cFormPopup::QboxInit @000ef55c / QboxAnimate
// @000ef2bc are read and redrawn by qbox.cpp; the original keeps running them (Process @0x2703 of cFormPopup.c) and
// grants the reward with QboxApplyResultsAndSave @000eeed4 (slot +0x1EA of the 8-byte records at +0x1D0, count +0x1E8;
// type 100 gold x10, 0x65 exp x10, else cInventory::AddItem)
static std::string reward_icon(uint32_t rm, int type, int id, int grade, std::string *name) {
    if (type == 100) { if (name) *name = "\xea\xb3\xa8\xeb\x93\x9c"; return sprites::image(0x6b, 0x16); }   // 골드 (QboxFillSlot @000ef1a0)
    if (type == 0x65) { if (name) *name = "\xea\xb2\xbd\xed\x97\x98\xec\xb9\x98"; return sprites::image(0x6b, 0x45); } // 경험치
    uint32_t info = rm ? g::call(0xcc3b4, {rm, (uint32_t)type, (uint32_t)id, (uint32_t)grade}) : 0; // cResourceManager::GetItemInfo @000dc3b4
    if (!info) return "";
    if (name) *name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info)));
    return sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
}
static void capture_qbox(Snapshot &s, uint32_t f) {
    Snapshot::Qbox &q = s.qbox;
    uint32_t rm = g::resmgr();
    q.ptr = f;
    // the opened box (+0x2F8) is removed from the bag when the reward is granted (QboxApplyResultsAndSave
    // RemoveItem): its name / icon are read once while the reels turn
    static uint32_t box_of = 0;
    static std::string box, box_icon;
    uint32_t it = M32(f + 0x2f8);
    if (box_of != f && it && M8(f + 0x1c9) < 2) {
        box_of = f;
        box = g::plain_text(g::cstr(g::call(0xfed68, {it})));
        uint32_t info = M32(it + 0x38);
        box_icon = info ? sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12)) : "";
    }
    if (box_of == f) { q.box = box; q.box_icon = box_icon; }
    q.msg = element_text(M32(f + 0xac));
    q.cost = (int32_t)M32(f + 0x2fc);
    q.gold = M32(f + 0x300) == 1;
    q.state = M8(f + 0x1c9);
    q.result = (int32_t)M32(f + 0x1cc);
    q.win = (int8_t)M8(f + 0x1ea);
    q.count = (int16_t)M16(f + 0x1e8);
    for (int i = 0; i < 3; i++) {
        uint32_t r = f + 0x1d0 + 8 * i;
        q.phase[i] = M8(r + 5);
        q.icon[i] = reward_icon(rm, (int8_t)M8(r), (int16_t)M16(r + 2), (int8_t)M8(r + 1), &q.name[i]);
    }
    static std::vector<std::string> spin;   // QBoxRewardTemp: count @000dc4c8, record (u16 type, u16 id) @000dc4d0
    if (spin.size() < 4 && rm) {
        spin.clear();
        int n = (int)g::call(0xcc4c8, {rm});
        for (int i = 0; i < n && i < 48; i++) {
            uint32_t t = g::call(0xcc4d0, {rm, (uint32_t)i});
            if (!t) continue;
            std::string k = reward_icon(rm, (int16_t)M16(t), (int16_t)M16(t + 2), -1, nullptr);
            if (!k.empty()) spin.push_back(k);
        }
    }
    q.spin = spin;
}

static bool popup_count(uint32_t f) { int k = (int32_t)M32(f + 0x168); return k == 3 || k == 4 || k == 5 || k == 7 || k == 10; }
// cFormResurrect (0x14): the death list (state 7); the sack-buy mode (paid) is never opened by the modern HUD
static bool resurrect_list(uint32_t f) { return (int32_t)M32(f + 0xbc) == 7; }

static void children_text(uint32_t cont, std::string &out) {
    if (!cont || (M32(cont + 0x14) & 0x8000)) return; // hidden container
    uint32_t kids = M32(cont + 0x24);
    if (!kids || !M8(cont + 0x20)) return;
    uint32_t e = M32(kids);
    if (!e || (M32(e + 0x14) & 0x8000)) return;
    out = element_text(e);
    if (out == "라벨") out.clear(); // UI-data placeholder of an unset label
}

void capture_dialog(Snapshot &s) {
    static bool reg = [] {
        set_ported_if(1, notice_text);
        set_ported(3);
        set_ported_if(0xf, [](uint32_t f) { int k = (int32_t)M32(f + 0x168); return popup_count(f) || k == 8 || k == 6 || k == 0 || k == 1 || k == 2; }); // + QBox draw (qbox.cpp), book page (skillbook.cpp), refine (smithy.cpp)
        set_ported_if(0x14, resurrect_list);
        set_ported(0x13);
        return true;
    }();
    (void)reg;
    if (s.forms.empty()) return;
    const Form &top = s.forms.back();
    if (top.id == 0xf && (int32_t)M32(top.ptr + 0x168) == 8) { capture_qbox(s, top.ptr); return; }
    if (top.id == 0x31) { // cFormTownFunc: registered container +0x50, cell i -> its child[3] is the function button
        Town &t = s.townsvc;
        t.ptr = top.ptr;
        uint32_t cont = M32(top.ptr + 0x50);
        uint32_t cells = cont ? M32(cont + 0x24) : 0;
        int n = cont ? M8(cont + 0x20) : 0;
        for (int i = 0; i < 6 && i < n && cells; i++) {
            uint32_t cell = M32(cells + 4 * i);
            uint32_t kids = cell ? M32(cell + 0x24) : 0;
            t.button[i] = kids ? M32(kids + 0xc) : 0;
            int x, y, w, h;
            if (element_rect(cell, &x, &y, &w, &h)) { t.rect[i][0] = x; t.rect[i][1] = y; t.rect[i][2] = w; t.rect[i][3] = h; }
        }
        return;
    }
    if (top.id == 0x27) { // cFormMakeChar: class buttons +0x5C/+0x58/+0x60 = class 0/1/2 (makingClassSelectHandler @000fcd60)
        CharSelect &c = s.charsel;
        c.ptr = top.ptr;
        c.selected = (int16_t)M16(top.ptr + 0x74);
        c.can_cancel = M16(top.ptr + 0x76) == 0;
        static const int btn[3] = {0x5c, 0x58, 0x60};
        for (int i = 0; i < 3; i++) {
            int x, y, w, h;
            if (element_rect(M32(top.ptr + btn[i]), &x, &y, &w, &h)) { c.rect[i][0] = x; c.rect[i][1] = y; c.rect[i][2] = w; c.rect[i][3] = h; }
            c.name[i] = g::plain_text(g::cstr(M32(IMG + 0x19920c + 4 * i)));                      // gCharNpcName
            c.desc[i] = g::rml_text(g::cstr(g::call(0x120aac, {(uint32_t)(0x2d9 + i)})));        // SelectCreateCharClass @000fcb24
        }
        return;
    }
    uint32_t f = top.ptr;
    Dialog &d = s.dialog;
    switch (top.id) {
    case 1: { // cFormNotice: title DockLabelStr +0x50, body +0xD8 (kind 9: pages at +0x114), buttons +0x68/+0x6C/+0x70, mode +0xA0
        if (!notice_text(f)) return;
        d.id = 1; d.ptr = f;
        d.kind = (int32_t)M32(f + 0x9c);
        d.mode = (int32_t)M32(f + 0xa0);
        d.title = element_text(M32(f + 0x50));
        uint32_t body = M32(f + 0xd8);
        if (d.kind == 9 && M32(f + 0x114)) {
            uint32_t pg = M32(f + 0x114);
            uint32_t arr = M32(pg), cnt = M32(pg + 4), cur = M32(pg + 8);
            if (arr && cur < cnt) body = M32(M32(arr + 4 * cur) + 0x10);
        }
        d.body = g::rml_text(g::cstr(body));
        // offline: the original's notice that a QBox draw needs the network (lang 0x25e etc.) is not true here, and
        // its offers of the cash store (form 0x16) for missing stones / scrolls / scales point at the gold shop of the menu
        // (cashshop.cpp); the store form itself is closed when it opens (menu.cpp)
        for (const char *cash : {"\xec\x9c\xa0\xeb\xa3\x8c\xec\x83\x81\xec\xa0\x90\xec\x9c\xbc\xeb\xa1\x9c \xec\x97\xb0\xea\xb2\xb0\xed\x95\x98\xec\x8b\x9c\xea\xb2\xa0\xec\x8a\xb5\xeb\x8b\x88\xea\xb9\x8c?"}) { // 유료상점으로 연결하시겠습니까?
            size_t at = d.body.find(cash);
            if (at != std::string::npos) d.body.replace(at, strlen(cash), " \xeb\xa9\x94\xeb\x89\xb4\xec\x9d\x98 \xec\x83\x81\xec\xa0\x90\xec\x97\x90\xec\x84\x9c \xea\xb3\xa8\xeb\x93\x9c\xeb\xa1\x9c \xea\xb5\xac\xec\x9e\x85\xed\x95\xa0 \xec\x88\x98 \xec\x9e\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4."); // 메뉴의 상점에서 골드로 구입할 수 있습니다.
        }
        for (const char *net : {"되며 네트워크에 접속이 필요합니다", " 네트워크에 접속이 필요합니다."}) {
            size_t at = d.body.find(net);
            if (at != std::string::npos) d.body.replace(at, strlen(net), net[0] == ' ' ? "" : "됩니다");
        }
        if (d.kind != 0 && d.kind != 9) capture_rich_notice(d, f, d.kind);
        static const int btn_off[3] = {0x68, 0x6c, 0x70};
        for (int i = 0; i < 3; i++) {
            uint32_t b = M32(f + btn_off[i]);
            bool used = d.mode == 0 ? i == 0 : d.mode == 1 ? i < 2 : d.mode == 2 ? i != 1 : false;
            d.buttons.push_back(used && b ? element_text(b) : "");
        }
        break;
    }
    case 3: { // cFormChoice: title child[0] of +0x50, desc child[0] of +0x54, entries +0x7C {next, text, flags}, list +0x5C
        d.id = 3; d.ptr = f;
        children_text(M32(f + 0x50), d.title);
        children_text(M32(f + 0x54), d.body);
        for (uint32_t e = M32(f + 0x7c), n = 0; e && n < 64; e = M32(e), n++) {
            d.items.push_back({g::rml_text(g::cstr(M32(e + 4))), (M32(e + 8) & 1) != 0});
        }
        uint32_t sc = M32(f + 0x5c);
        d.selected = sc ? M8(sc + 0x21) : 0;
        break;
    }
    case 0xf: { // cFormPopup count kinds: title child[0] of +0x50, body +0x58, slider child[0] of +0x7C value +0x24
        if (!popup_count(f)) return;
        d.id = 0xf; d.ptr = f;
        d.kind = (int32_t)M32(f + 0x168);
        d.mode = (int32_t)M32(f + 0x170);
        children_text(M32(f + 0x50), d.title);
        d.body = element_text(M32(f + 0x58));
        uint32_t sl = M32(f + 0x7c);
        uint32_t kids = sl ? M32(sl + 0x24) : 0;
        if (kids && M32(kids)) d.count = (int32_t)M32(M32(kids) + 0x24);
        d.count_max = (int32_t)M32(f + 0x30c);
        break;
    }
    case 0x14: { // cFormResurrect: options list +0x94 (sel u8 +0x21), revival items +0xB8
        if (!resurrect_list(f)) return;
        d.id = 0x14; d.ptr = f;
        uint32_t sc = M32(f + 0x94);
        d.selected = sc ? M8(sc + 0x21) : 0;
        d.extra = (int32_t)M32(f + 0xb8);
        // instant revival price: common settings record (resmgr+0x1E8) +0xAC Harz, shown by choice 0 of
        // cFormResurrect::choicesKeyPress @000fbd84 (resurrect_msgs[0] "하츠 %d개 사용"); the port charges gold
        { uint32_t rm = g::resmgr(), cfg = rm ? M32(rm + 0x1e8) : 0; d.count = cfg ? cash::harz_to_gold((int32_t)M32(cfg + 0xac)) : 0; }
        break;
    }
    case 0x13: { // cFormLinkstoneChoice: title +0x54, list +0x58, ids +0x74[]; text res+0x34 + LinkStoneInfo(id)[0]
        d.id = 0x13; d.ptr = f;
        d.title = element_text(M32(f + 0x54));
        uint32_t sc = M32(f + 0x58);
        int n = sc ? M8(sc + 0x20) : 0;
        d.selected = sc ? M8(sc + 0x21) : 0;
        // row label: the list cell's text labels (cFormLinkstoneChoice::initContents @000fd17c)
        uint32_t kids = sc ? M32(sc + 0x24) : 0;
        for (int i = 0; i < n && kids && i < 64; i++) {
            std::string t;
            uint32_t cell = M32(kids + 4 * i);
            // first text-bearing descendant
            std::function<void(uint32_t, int)> find = [&](uint32_t e, int depth) {
                if (!e || !t.empty() || depth > 4) return;
                int ty = (M32(e + 0x14) >> 9) & 0x1f;
                if (ty == 2 || ty == 0x10) { t = element_text(e); return; }
                uint32_t k = M32(e + 0x24);
                int nk = (ty == 8 || ty == 9 || ty == 0xd || ty == 0xe || ty == 0x11) ? M8(e + 0x20) : 0;
                for (int j = 0; j < nk && k; j++) find(M32(k + 4 * j), depth + 1);
            };
            find(cell, 0);
            d.items.push_back({t, false});
        }
        break;
    }
    }
}
}

namespace screens {

class DialogScreen : public Screen {
    struct Session {
        std::string key;         // content the items / buttons were built for
    } ss;
    uint32_t ptr = 0;            // the form answered (mirror of the snapshot; a new one refocuses)
    int id = 0;
    // data model "dialog" (dialog.rml), set in update() only. The original's texts go in through data-rml (rml_safe)
    struct Item {
        std::string act, rml;    // rml: number span + entry text (+ the resurrect price / count span)
        bool off = false;
        bool operator==(const Item &o) const { return act == o.act && rml == o.rml && off == o.off; }
    };
    struct Btn {
        std::string act, cap;
        bool operator==(const Btn &o) const { return act == o.act && cap == o.cap; }
    };
    struct RowV {
        std::string src, text;
        bool operator==(const RowV &o) const { return src == o.src && text == o.text; }
    };
    struct View {
        std::string kicker, title, body, sub, rows_title, hint;
        std::vector<RowV> rows;
        std::vector<Item> items;
        // buttons in the old build order (b2, b1, b0 / cancel): the plain ones, then the primary b0 (always the last)
        std::vector<Btn> btns;
        bool primary = false;
        std::string primary_cap;
        bool is_count = false;
        int count = 0, count_max = 0;
    } v;

public:
    DialogScreen() { z = 50; register_screen(this); }
    const char *file() override { return "dialog.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("dialog");
        if (!c) return;
        if (auto h = c.RegisterStruct<Item>()) { h.RegisterMember("act", &Item::act); h.RegisterMember("rml", &Item::rml); h.RegisterMember("off", &Item::off); }
        c.RegisterArray<std::vector<Item>>();
        if (auto h = c.RegisterStruct<Btn>()) { h.RegisterMember("act", &Btn::act); h.RegisterMember("cap", &Btn::cap); }
        c.RegisterArray<std::vector<Btn>>();
        if (auto h = c.RegisterStruct<RowV>()) { h.RegisterMember("src", &RowV::src); h.RegisterMember("text", &RowV::text); }
        c.RegisterArray<std::vector<RowV>>();
        c.Bind("kicker", &v.kicker); c.Bind("title", &v.title); c.Bind("body", &v.body); c.Bind("sub", &v.sub);
        c.Bind("rows_title", &v.rows_title); c.Bind("rows", &v.rows); c.Bind("items", &v.items); c.Bind("btns", &v.btns);
        c.Bind("primary", &v.primary); c.Bind("primary_cap", &v.primary_cap); c.Bind("hint", &v.hint);
        c.Bind("is_count", &v.is_count); c.Bind("count", &v.count); c.Bind("count_max", &v.count_max);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { return s.stripped && s.dialog.id != 0; }
    void on_show() override { ss = {}; }

    void update(const state::Snapshot &s) override {
        const state::Dialog &dg = s.dialog;
        bool fresh = dg.ptr != ptr;
        ptr = dg.ptr;
        id = dg.id;
        std::string k = std::to_string(dg.id) + dg.title + dg.body + std::to_string(dg.items.size()) + std::to_string(dg.mode) + std::to_string(dg.extra) +
                        (dg.id == 0x14 ? std::to_string(s.gold >= dg.count) : std::string());
        for (auto &b : dg.buttons) k += b;
        k += dg.subtitle + std::to_string(dg.rows.size());
        bool rebuild = k != ss.key;
        ss.key = k;
        static const char *KICK[] = {"", "\xec\x95\x8c\xeb\xa6\xbc", "", "\xec\x84\xa0\xed\x83\x9d"};
        // rich notices: kicker by kind (quest complete / quest / skill / blessing / title / dungeon clear)
        static const char *RK[9] = {"", "QUEST CLEAR", "QUEST", "SKILL", "BLESSING", "BLESSING", "TITLE", "", "REWARD"};
        std::string kicker = dg.id == 1 && dg.kind >= 1 && dg.kind <= 8 && RK[dg.kind][0] ? RK[dg.kind] : dg.id == 1 ? KICK[1] : dg.id == 3 ? KICK[3] : dg.id == 0xf ? "\xec\x88\x98\xeb\x9f\x89" : dg.id == 0x14 ? "\xec\xa0\x84\xed\x88\xac \xeb\xb6\x88\xeb\x8a\xa5" : "\xeb\xa7\x81\xed\x81\xac\xec\x8a\xa4\xed\x86\xa4";
        put(v.kicker, kicker, "kicker");
        std::string title = dg.title;
        if (dg.id == 0x14 && title.empty()) title = "\xec\xa7\x80\xea\xb8\x88 \xec\x93\xb0\xeb\x9f\xac\xec\xa1\x8c\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4"; // 지금 쓰러졌습니다
        put(v.title, rml_safe(title), "title");                  // shown when not empty (data-if)
        put(v.body, rml_safe(dg.body), "body");
        // subtitle (quest name, skill name and level, blessing name) with its icon, and reward / material rows
        std::string sub;
        if (!dg.subtitle.empty() || !dg.icon.empty())
            sub = (dg.icon.empty() ? std::string() : "<img src=\"" + sprite_src(dg.icon) + "\"/>") + "<span>" + rml_safe(dg.subtitle) + "</span>";
        put(v.sub, sub, "sub");
        std::vector<RowV> rows;
        for (auto &r : dg.rows) rows.push_back({sprite_src(r.icon), rml_safe(r.text)});
        put(v.rows, std::move(rows), "rows");
        put(v.rows_title, rml_safe(dg.rows_title), "rows_title");

        bool items_new = false, btns_new = false; // the item / button rows change in this frame's Context::Update
        if (rebuild) {
            std::vector<Item> items;
            std::vector<Btn> btns;
            bool primary = false;
            std::string pcap;
            if (dg.id == 3 || dg.id == 0x13) {
                for (size_t i = 0; i < dg.items.size(); i++)
                    items.push_back({Rml::CreateString("i%d", (int)i), Rml::CreateString("<span class=\"n\">%d</span>", (int)i + 1) + rml_safe(dg.items[i].text), dg.items[i].off});
            } else if (dg.id == 0x14) {
                // cFormResurrect options (choicesKeyPress @000fbd84): 1 revive with an item, 2 give up, 3 main menu.
                // Option 0 (revive with paid Harz, server request 0xA0) is replaced by a gold payment (rgold).
                bool afford = dg.count > 0 && s.gold >= dg.count;
                items.push_back({"rgold", Rml::CreateString("<span class=\"n\">0</span>\xec\xa6\x89\xec\x8b\x9c \xeb\xb6\x80\xed\x99\x9c<span class=\"s\">%s G</span>", num(dg.count).c_str()), !afford}); // 즉시 부활
                items.push_back({"i1", Rml::CreateString("<span class=\"n\">1</span>\xeb\xb6\x80\xed\x99\x9c \xec\x95\x84\xec\x9d\xb4\xed\x85\x9c \xec\x82\xac\xec\x9a\xa9<span class=\"s\">\xeb\xb3\xb4\xec\x9c\xa0 %d</span>", dg.extra), dg.extra <= 0});
                items.push_back({"i2", "<span class=\"n\">2</span>\xed\x8f\xac\xea\xb8\xb0\xed\x95\x98\xea\xb3\xa0 \xeb\x8f\x8c\xec\x95\x84\xea\xb0\x80\xea\xb8\xb0", false});
                items.push_back({"i3", "<span class=\"n\">3</span>\xed\x83\x80\xec\x9d\xb4\xed\x8b\x80 \xed\x99\x94\xeb\xa9\xb4\xec\x9c\xbc\xeb\xa1\x9c", false});
            }
            if (dg.id == 1) {
                // primary = first button (OK / Yes); buttons without a caption in the data get standard labels
                const char *def[3] = {dg.mode == 2 ? "\xec\x98\x88" : "\xed\x99\x95\xec\x9d\xb8", "\xec\xb7\xa8\xec\x86\x8c", "\xec\x95\x84\xeb\x8b\x88\xec\x98\xa4"};
                for (int i = 2; i >= 0; i--) {
                    if (i >= (int)dg.buttons.size()) continue;
                    bool used = dg.mode == 0 ? i == 0 : dg.mode == 1 ? i < 2 : dg.mode == 2 ? i != 1 : false;
                    if (!used) continue;
                    std::string cap = dg.buttons[i].empty() ? def[i] : rml_safe(dg.buttons[i]);
                    if (dg.kind == 2 && i == 2 && dg.buttons[i].empty()) cap = "\xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8 \xed\x8f\xac\xea\xb8\xb0"; // 퀘스트 포기
                    if (dg.kind == 2 && i == 0 && dg.buttons[i].empty()) cap = "\xed\x99\x95\xec\x9d\xb8";       // 확인
                    if (i == 0) { primary = true; pcap = cap; } else btns.push_back({Rml::CreateString("b%d", i), cap});
                }
            } else if (dg.id == 0xf) {
                btns.push_back({"b2", "\xec\xb7\xa8\xec\x86\x8c"});
                primary = true; pcap = "\xed\x99\x95\xec\x9d\xb8";
            } else if (dg.id == 3 || dg.id == 0x13) {
                btns.push_back({"cancel", "\xeb\x8b\xab\xea\xb8\xb0"});
            }
            items_new = put(v.items, std::move(items), "items");
            btns_new = put(v.btns, std::move(btns), "btns");
            put(v.primary, primary, "primary");
            put(v.primary_cap, pcap, "primary_cap");
            fresh = true;
        }
        put(v.is_count, dg.id == 0xf, "is_count");
        if (dg.id == 0xf) {
            put(v.count, dg.count, "count");
            put(v.count_max, dg.count_max, "count_max");
        }
        if (fresh) {
            nav.root = d.el("box");
            nav.set(nullptr);
            nav.want.clear();
            // focus: selected list entry, else the primary button (else the last button)
            std::string a;
            bool made = false;   // a row made or changed in this frame's Context::Update: focused on the next frame
            if (!v.items.empty()) {
                int n = (int)v.items.size();
                int sel = dg.id == 0x14 ? (dg.extra > 0 ? 1 : dg.count > 0 && s.gold >= dg.count ? 0 : 2) : std::min(dg.selected, n - 1);
                a = v.items[std::max(0, std::min(sel, n - 1))].act;
                made = items_new;
            } else if (v.primary) a = "b0";      // static: shown by data-if in this frame's Context::Update
            else if (!v.btns.empty()) { a = v.btns.back().act; made = btns_new; }
            Rml::Element *e = a.empty() || made ? nullptr : nav.find_act(a);
            if (e) nav.set(e);
            else if (!a.empty()) nav.want_act(a);
        }
        put(v.hint, "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("back") + " \xec\xb7\xa8\xec\x86\x8c</span>", "hint");
    }
    bool on_dir(int dir) override {
        if (id == 0xf && dir >= 2) { adjust(dir == 3 ? 1 : -1); return true; }
        return false;
    }
    void adjust(int diff) {
        uint32_t f = ptr;
        state::post([f, diff] { g::call(0xdd6a8, {f, (uint32_t)diff}); }); // cFormPopup::adjustSlidePosByDiff @000ed6a8
    }

    bool action(const std::string &a) override {
        uint32_t f = ptr;
        int fid = id;
        if (a.size() > 1 && a[0] == 'c' && (a[1] == '+' || a[1] == '-')) { adjust(atoi(a.c_str() + 1)); return true; }
        if (a.size() > 1 && a[0] == 'b' && isdigit((unsigned char)a[1])) {
            int i = a[1] - '0';
            if (fid == 1) {
                // cFormNotice::somethingKeypress(f, 0x30, 0, button) @000ecc08
                static const int off[3] = {0x68, 0x6c, 0x70};
                state::post([f, i] { g::call(0xdcc08, {f, 0x30, 0, M32(f + off[i])}); });
            } else if (fid == 0xf) {
                // cFormPopup::somethingKeypress(f, 0x30, 0, button) @000f2ed8: +0x10C confirm, +0x114 cancel
                state::post([f, i] { g::call(0xe2ed8, {f, 0x30, 0, M32(f + (i == 0 ? 0x10c : 0x114))}); });
            }
            return true;
        }
        if (a.size() > 1 && a[0] == 'i' && isdigit((unsigned char)a[1])) {
            int n = atoi(a.c_str() + 1);
            if (fid == 3) // cflUiSplitContainer::Select @00157158, cFormChoice::itemKeypress(f, 0x30, n) @000e790c
                state::post([f, n] { g::call(0x147158, {M32(f + 0x5c), (uint32_t)n, 0}); g::call(0xd790c, {f, 0x30, (uint32_t)n}); });
            else if (fid == 0x14) // Select(+0x94, n), cFormResurrect::choicesKeyPress(f, 0x30, 0) @000fbd84
                state::post([f, n] { g::call(0x147158, {M32(f + 0x94), (uint32_t)n, 0}); g::call(0xebd84, {f, 0x30, 0}); });
            else if (fid == 0x13) // Select(+0x58, n), cFormLinkstoneChoice::somethingKeypress(f, 0x30, 0, list) @000fd414
                state::post([f, n] { g::call(0x147158, {M32(f + 0x58), (uint32_t)n, 0}); g::call(0xed414, {f, 0x30, 0, M32(f + 0x58)}); });
            return true;
        }
        if (a == "rgold" && fid == 0x14) {
            // the success end of the Harz path without the server: cFormResurrect::OnLifeHerbPurchaseSuccess @000fb15c
            // revives through the form's result callback like the item path, goGame(form, 1) @000fb190
            // (cInventory::SubGold @0012fb44 refuses when the gold would go negative)
            state::post([f] {
                uint32_t inv = g::inv(), rm = g::resmgr(), cfg = rm ? M32(rm + 0x1e8) : 0;
                if (!inv || !cfg) return;
                int price = cash::harz_to_gold((int32_t)M32(cfg + 0xac));
                if (price <= 0 || !g::call(0x11fb44, {inv, (uint32_t)price})) return;
                g::call(0xeb190, {f, 1});
            });
            return true;
        }
        if (a == "cancel") { back(); return true; }
        return false;
    }
    void back() override {
        uint32_t f = ptr;
        int fid = id;
        if (fid == 1) state::post([f] { g::call(0xdcc08, {f, 0x3a, 0, M32(f + 0x68)}); });
        else if (fid == 3) state::post([f] { g::call(0xd790c, {f, 0x3a, 0}); });
        else if (fid == 0xf) state::post([f] { g::call(0xe2ed8, {f, 0x30, 0, M32(f + 0x114)}); });
        else if (fid == 0x13) state::post([] { state::key_tap(0x3a); });
        // cFormResurrect consumes and ignores 0x3A: no cancel
    }
};

static DialogScreen g_dialog;
}
