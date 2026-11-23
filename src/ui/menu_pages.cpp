// Data of the bag / equipment / skill pages, read from the original page forms (game thread).
// Offsets and functions: work/menu_lists_spec.md (Ghidra addresses; calls use ELF vaddr = -0x10000).
#include "game.h"
#include "sprites.h"
#include "state.h"
#include "ui_internal.h"

namespace state {

// cItem::GetName @0010ed68, cItem::GetTypeName @0010ed38, info: +4 desc id (GetSubDescString(res,0,id) @000dd0d8),
// +8 class, +9 req level, +0xa sell, +0xc buy, +0x12 palette, +0x13 icon frame (set 0x6b, cItemImageList::DrawIcon @000e9818)
static ItemInfo item_info(uint32_t it, bool want_desc) {
    ItemInfo r;
    if (!it) return r;
    r.ptr = it;
    r.flags = M16(it + 4);
    r.type = M16(it + 6);
    r.id = (int16_t)M16(it + 8);
    r.count = (int32_t)M32(it + 0x3c);
    uint32_t info = M32(it + 0x38);
    r.name = g::plain_text(g::cstr(g::call(0xfed68, {it})));
    r.name_rml = g::item_name_rml(it);
    r.type_name = g::plain_text(g::cstr(g::call(0xfed38, {it})));
    if (info) {
        r.icon = sprites::image(0x6b, M8(info + 0x13), (r.flags & 0x100) ? 0 : M8(info + 0x12));
        r.cls = M8(info + 8);
        r.req_lv = M8(info + 9);
        r.sell = M16(info + 0xa);
        r.buy = (int32_t)M32(info + 0xc);
        if (want_desc) {
            uint32_t rm = g::resmgr();
            uint32_t ds = rm ? g::call(0xcd0d8, {rm, 0, M32(info + 4)}) : 0;
            if (ds) r.desc = g::rml_text(g::cstr(ds));
            if (r.desc == "0") r.desc.clear();
        }
    }
    if (r.type < 5) { r.dur = M16(it + 0x20); r.dur_max = M16(it + 0x22); r.refine = M8(it + 0x24); }
    return r;
}

static void read_hot(MenuData &m, uint32_t pl) {
    uint32_t rm = g::resmgr();
    for (int s = 0; s < 10; s++) {
        uint32_t e = pl + 0xb8 + s * 8; // cCreature::GetSlotInfo @000c40bc
        int kind = (int32_t)M32(e);
        uint32_t data = M32(e + 4);
        HotRef &h = m.hot[s];
        if (!data) continue;
        if (kind == 0) {
            uint32_t def = M32(data + 8);
            if (!def) continue;
            h.kind = 0;
            h.icon = sprites::image(M8(def + 0x2d), M8(def + 0x2e), M8(def + 0x2f));
            if (rm) h.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(def)));
            h.name_rml = g::rml_text(h.name.c_str(), false);
        } else {
            uint32_t info = M32(data + 0x38);
            if (!info) continue;
            h.kind = 1;
            h.icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
            h.name = g::plain_text(g::cstr(g::call(0xfed68, {data})));
            h.name_rml = g::item_name_rml(data);
        }
    }
}

void capture_pages(Snapshot &s, uint32_t pl) {
    MenuData &m = s.menu;
    read_hot(m, pl);
    uint32_t inv = g::inv();
    if (m.tab == 1 && m.page_form[1]) { // bag: L = form+0x90 (items +0x14, count +0x20, cat +0x94, hotslot flag +0x18[i]), IL = form+0x64
        uint32_t f = m.page_form[1], L = M32(f + 0x90), IL = M32(f + 0x64);
        if (L && IL) {
            int n = (int16_t)M16(IL + 0x30);
            uint32_t arr = M32(L + 0x14), hf = M32(L + 0x18);
            m.inv_sel = (int16_t)M16(IL + 0x32);
            m.inv_cat = (int32_t)M32(L + 0x94);
            for (int i = 0; i < n && i < 300 && arr; i++) {
                ItemInfo it = item_info(M32(arr + 4 * i), i == m.inv_sel);
                it.hotslot = hf && M8(hf + i) == 1;
                m.items.push_back(std::move(it));
            }
        }
        if (inv) { m.inv_cap = (int32_t)M32(inv + 0x1c); m.inv_used = (int)g::call(0x11f824, {inv}); } // cBackPack::GetBackpackItemCount @0012f824
    }
    if (m.tab == 2 && m.page_form[2]) { // equipment: slots pl+0x39c+4k, slot +0x94, EL +0x78, IL +0x64, preview (+0x70).child[1]
        uint32_t f = m.page_form[2], EL = M32(f + 0x78), IL = M32(f + 0x64);
        for (int k = 0; k < 6; k++) m.equipped[k] = item_info(M32(pl + 0x39c + 4 * k), true); // GetEquipmentItem @000c0b54
        m.eq_slot = (int32_t)M32(f + 0x94);
        if (EL && IL) {
            int n = (int16_t)M16(IL + 0x30);
            uint32_t arr = M32(EL + 0x14);
            m.cand_sel = (int16_t)M16(IL + 0x32);
            for (int i = 0; i < n && i < 300 && arr; i++) m.cands.push_back(item_info(M32(arr + 4 * i), i == m.cand_sel));
        }
        uint32_t S = M32(f + 0x70);
        uint32_t kids = S ? M32(S + 0x24) : 0;
        uint32_t D = kids ? M32(kids + 4) : 0;
        if (D && !(M32(D + 0x14) & 0x8000)) {
            m.delta_on = true;
            uint32_t dk = M32(D + 0x24);
            for (int i = 0; i < 7 && dk; i++) {
                uint32_t lb = M32(dk + 4 * i);
                int v = lb ? (int32_t)M32(lb + 0x24) : 0;
                m.delta[i] = (lb && M16(lb + 0x2c) == 0xc204) ? -v : v;
            }
        }
    }
    if (m.tab == 3 && m.page_form[3]) { // skills: tab (+0x50).+0x21, rows +0x84[] count +0x88, selected (+0x60).+0x21
        uint32_t f = m.page_form[3];
        uint32_t tabsc = M32(f + 0x50), list = M32(f + 0x60);
        m.sk_tab = tabsc ? M8(tabsc + 0x21) : 0;
        m.sk_sel = list ? M8(list + 0x21) : -1;
        int n = (int32_t)M32(f + 0x88);
        int lv = (int32_t)M32(pl + 0x90);
        int sp = (int32_t)M32(pl + 0x34c);
        uint32_t rm = g::resmgr();
        int scale = rm && M32(rm + 0x1e8) ? M16(M32(rm + 0x1e8) + 0x7c) : 1;
        if (m.sk_tab <= 1)
            for (int i = 0; i < n && i < 40; i++) {
                uint32_t sk = M32(f + 0x84 + 0 * 4) ? M32(M32(f + 0x84) + 4 * i) : 0;
                if (!sk) continue;
                SkillRow r;
                r.ptr = sk;
                uint32_t def = M32(sk + 8), lvr = M32(sk + 0xc);
                r.name = g::plain_text(g::cstr(g::call(0x120dd0, {sk}))); // cSkillInfo::GetName @00130dd0
                if (def) {
                    r.icon = sprites::image(M8(def + 0x2d), M8(def + 0x2e), M8(def + 0x2f));
                    r.max_rank = M8(def + 6);
                    r.type = M8(def + 0xd);
                    r.hot_ok = r.type != 2 && r.type != 6 && (int8_t)M8(def + 0x29) < 0;
                }
                r.learned = M8(sk + 0x12) != 0;
                r.rank = M8(sk + 0x13);
                r.req_lv = (int)g::call(0x120e60, {sk});                          // GetLevelUpCharLv @00130e60
                r.sp_need = r.learned && lvr ? M8(lvr + 0x3e) : 1;               // CheckLevelUpSkillPoint @00130e74
                if (lvr) r.mp = (int32_t)M32(lvr + 4) / (scale > 0 ? scale : 1);
                // the skill detail of the original (ShowDetail @00112504 -> cFormNotice::Finalize_ContentsSkill @000ec70c):
                // effect text GetSubDescString(2, def+0x24); types other than 2 / 3 add lang 0xC4 (MP) lvr+4 / scale,
                // lang 0x1E0 cool time vfunc +0x14 / 15 and lang 0x119, and for types 1 / 4 lang 0x1E0 duration u16 lvr+2
                if (def && rm) r.desc = g::rml_text(g::cstr(g::call(0xcd0d8, {rm, 2, M32(def + 0x24)})));
                if (def && lvr && r.type != 2 && r.type != 3) {
                    auto L = [](uint32_t id) { // GetLangData; the label column drops the original's trailing ':' (lang 0xC4 "소모MP:")
                        std::string t = g::plain_text(g::cstr(g::call(0x120aac, {id})));
                        while (!t.empty() && (t.back() == ':' || t.back() == ' ')) t.pop_back();
                        return t;
                    };
                    std::string unit = L(0x119);
                    int cd = (int32_t)g::call((M32(M32(sk) + 0x14) - IMG) & ~1u, {sk}) / 15;
                    r.stats.push_back({L(0xc4), std::to_string(r.mp)});
                    r.stats.push_back({L(0x1e0), std::to_string(cd) + unit});
                    if (r.type == 1 || r.type == 4) r.stats.push_back({L(0x1e1), std::to_string((int)M16(lvr + 2)) + unit});
                }
                r.can_up = r.rank < r.max_rank && lv >= r.req_lv && sp >= r.sp_need;
                r.hot_ok = r.hot_ok && r.learned;
                m.skills.push_back(std::move(r));
            }
        // life skills (SettingCellDatasForLivingSkill @00112b94): lang 0x34..0x36, icons set 0x6b frames 0x24..0x22
        for (int i = 0; i < 3; i++) {
            m.life[i].name = g::plain_text(g::cstr(g::call(0x120aac, {(uint32_t)(0x34 + i)})));
            m.life[i].icon = sprites::image(0x6b, 0x24 - i, 0);
            m.life[i].lv = M8(pl + 0x360 + i);
            m.life[i].exp = (int32_t)M32(pl + 0x364 + 4 * i);
            m.life[i].max = std::max(1, (int32_t)M32(pl + 0x370 + 4 * i));
        }
    }
}
}
