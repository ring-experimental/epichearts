// In-game menu (replaces cFormInvenMDI form 4 and its pages; work/menu_pages_spec.md).
// Tabs switch the original pages with cFormInvenMDI::tabsChangeImpl @0010baa8 so the original state follows;
// actions call the original functions (sources cited at each call).
#include "autoform.h"
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include "cashshop.h"

namespace state {
void capture_pages(Snapshot &s, uint32_t pl); // menu_pages.cpp
// ---- game thread
void capture_menu(Snapshot &s, uint32_t pl) {
    MenuData &m = s.menu;
    for (auto &f : s.forms) if (f.id == 4) m.mdi = f.ptr;
    if (!m.mdi) return;
    m.tab = (int32_t)M32(m.mdi + 0x90);
    { static int last = -2; if (getenv("EH_MENUDBG") && m.tab != last) { ui_log("[menu] frame %llu tab %d", (unsigned long long)s.frame, m.tab); last = m.tab; } }
    cash::capture(); // item shop page data
    for (int i = 0; i < 10; i++) m.page_form[i] = M32(m.mdi + 0xa4 + 4 * i); // MakeTabs @0010b808
    capture_pages(s, pl);
    // title: s16 pl+0x35e; < 0 -> GetLangData(0x26e) @00130aac, else str(GetCharTitleInfo(id) @000dc670 + 4)
    int16_t tid = (int16_t)M16(pl + 0x35e);
    uint32_t rm = g::resmgr();
    if (tid < 0) m.title = g::plain_text(g::cstr(g::call(0x120aac, {0x26e})));
    else if (rm) { uint32_t ti = g::call(0xcc670, {rm, (uint32_t)tid}); if (ti) m.title = g::plain_text(g::cstr(M32(rm + 0x34) + M32(ti + 4))); }
    // title list (cFormCharTitle::initContents @0011c630): a record under the level u16 +2 (vfunc 0x130) is "????";
    // learned = cCharacter::IsLearnTitle @000c0d78 (bit i of pl+0x354), equipped = pl+0x35e == i
    if (rm && m.tab == 0) {
        int n = (int32_t)M32(rm + 0x274), lv = (int)g::call((M32(M32(pl) + 0x130) - IMG) & ~1u, {pl});
        for (int i = 0; i < n && i < 256; i++) {
            uint32_t ti = g::call(0xcc670, {rm, (uint32_t)i});
            MenuData::TitleRow r;
            r.open = ti && M16(ti + 2) <= lv;
            if (r.open) {
                r.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(ti + 4)));
                // the description line of cFormNotice::Finalize_ContentsCharTitle @000eb4ec: GetSubDescString(0, +8) @000dd0d8
                r.desc = g::rml_text(g::cstr(g::call(0xcd0d8, {rm, 0, M32(ti + 8)})));
                r.learned = (M32(pl + 0x354 + 4 * (i >> 5)) >> (i & 31)) & 1;
                r.on = r.learned && tid == i;
            }
            m.titles.push_back(std::move(r));
        }
        m.l_equip = g::plain_text(g::cstr(g::call(0x120aac, {0x245})));
        m.l_info = g::plain_text(g::cstr(g::call(0x120aac, {0x22b})));
    }
    // stats (cFormCharStat::RefreshStat @0011c414, StaticRefreshGpStatus @0011b714, StaticRefresh2ndStat @0011b78c)
    for (int i = 0; i < 3; i++) { m.str[i] = (int32_t)M32(pl + 0x380 + 4 * i); m.invest[i] = (int16_t)M16(pl + 0x3c6 + 2 * i); }
    m.atk = (int32_t)M32(pl + 0x9c); m.def = (int32_t)M32(pl + 0xa0);
    m.hit = (int32_t)M32(pl + 0xac); m.eva = (int32_t)M32(pl + 0xb0); m.crit = (int32_t)M32(pl + 0xb4);
    // quests: cQuestManager +0 main, +0xc+4i sub (count u16 +0x90); cQuest::GetTitle @000d94a4, GetDesc @000d95e8
    uint32_t qm = g::questmgr();
    if (qm && m.tab == 4) {
        std::vector<uint32_t> qs;
        if (M32(qm)) qs.push_back(M32(qm));
        int ns = M16(qm + 0x90);
        for (int i = 0; i < ns && i < 32; i++) if (M32(qm + 0xc + 4 * i)) qs.push_back(M32(qm + 0xc + 4 * i));
        uint32_t inv = g::inv();
        for (uint32_t q : qs) {
            QuestInfo qi;
            qi.kind = M16(q + 6);
            uint32_t fl = M32(q + 0xc);
            qi.done = fl & 1;
            qi.failed = (fl & 8) != 0;
            qi.title = g::plain_text(g::cstr(g::call(0xc94a4, {q})));
            qi.desc = g::rml_text(g::cstr(g::call(0xc95e8, {q})));
            if ((int32_t)M32(q + 0x14) > 0 && !(fl & 8)) qi.timer = (int32_t)M32(q + 0x10) / 15;
            int nc = M8(q + 8);
            for (int c = 0; c < nc && c < 8; c++) {
                uint32_t cd = q + 0x1c + 8 * c;
                int type = (int32_t)M32(cd);
                if (type == 0 && inv) { // item: u8 type +4, u8 need +5, u16 id +6 (cInventory::GetDuplicateItemCount @0012ff0c)
                    int need = M8(cd + 5);
                    int have = (int)g::call(0x11ff0c, {inv, M8(cd + 4), M16(cd + 6)});
                    std::string nm;
                    uint32_t ii = rm ? g::call(0xcc3b4, {rm, M8(cd + 4), M16(cd + 6), 0}) : 0; // GetItemInfo @000dc3b4
                    if (ii) nm = g::plain_text(g::cstr(M32(rm + 0x34) + M32(ii)));
                    qi.conds.push_back((nm.empty() ? std::string("\xec\x95\x84\xec\x9d\xb4\xed\x85\x9c") : nm) + Rml::CreateString("  %d / %d", std::min(have, need), need));
                    qi.conds_ok.push_back(have >= need);
                } else if (type == 1) { // monster: u16 id +4, u16 need +6 (GetCaughtMonsterCountFor @000d93d0)
                    int need = M16(cd + 6);
                    int have = (int)g::call(0xc93d0, {qm, q, (uint32_t)c});
                    qi.conds.push_back(Rml::CreateString("\xed\x86\xa0\xeb\xb2\x8c  %d / %d", std::min(have, need), need));
                    qi.conds_ok.push_back(have >= need);
                }
            }
            m.quests.push_back(std::move(qi));
        }
    }
}
}

namespace screens {

static const int TABS[] = {0, 1, 2, 3, 4, 6, 8}; // 6: item shop (cashshop.cpp), a modern page only
static const char *TAB_NAMES[] = {"\xec\xba\x90\xeb\xa6\xad\xed\x84\xb0", "\xea\xb0\x80\xeb\xb0\xa9", "\xec\x9e\xa5\xeb\xb9\x84",
                                  "\xec\x8a\xa4\xed\x82\xac", "\xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8", "", "\xec\x83\x81\xec\xa0\x90", "", "\xec\x8b\x9c\xec\x8a\xa4\xed\x85\x9c"};

class MenuScreen : public Screen {
    struct Session {
        int qsel = 0;
        int confirm = 0;         // 1 return to title, 2 buy in the shop
        std::string toast;       // the menu toast (action() and update() both toast: update() puts it in the model)
        double toast_until = 0;
        int cur_tab = -1;        // the page drawn (-1: rebuild the rail and the focus)
        bool shop = false;       // the item shop page is shown (the original MDI stays on its current tab)
        int picker = 0;          // quickslot picker over the page: 1 item, 2 skill
        uint32_t picker_obj = 0;
        bool ttl = false;        // title list over the character page (replaces cFormCharTitle, form 0x10)
        int ttl_sel = 0;
        std::string focus_act;   // data-act of the last page action: its element takes the focus when its list changes
        unsigned drawn = 0;      // pages drawn in this opening (bit per tab): the first draw scrolls their detail text up
        Pending<int> tab_req;    // tab asked of the original (tabsChangeImpl), not switched yet
        bool pending_save = false;
        volatile int *save_result = nullptr;
    } ss;
    uint32_t mdi = 0;            // per-frame mirror of the snapshot (the original's menu form)
    int shop_sel = 0;            // remembered: the shop page opens on the last product chosen
    uint32_t shop_seen = 0;      // purchase results already toasted (cash::result sequence): an old one is not shown again
    // data model "menu" (menu.rml), set in update() only
    struct Quest {               // p4 row: kind / state labels are ours, nm the original's title (data-rml, rml_safe)
        std::string act, kind, nm, st;
        bool sel = false, sub = false, done = false;
        bool operator==(const Quest &o) const { return act == o.act && kind == o.kind && nm == o.nm && st == o.st && sel == o.sel && sub == o.sub && done == o.done; }
    };
    struct Cell {                // icon grid cell (shop products, bag items, equipment candidates)
        std::string act, src, n, pr; // n: count badge ("" none), pr: shop price pill
        bool sel = false, free = false, hs = false, bad = false; // hs: on a quickslot, bad: candidate not usable
        bool operator==(const Cell &o) const { return act == o.act && src == o.src && n == o.n && pr == o.pr && sel == o.sel && free == o.free && hs == o.hs && bad == o.bad; }
    };
    struct Slot {                // p2 equipment slot: the item's icon, or the slot's placeholder glyph
        std::string act, src, ph, sl, nm; // ph: lucide entity (data-rml), sl: slot word, nm: item name (data-rml) / 비어 있음
        bool sel = false, has = false;
        bool operator==(const Slot &o) const { return act == o.act && src == o.src && ph == o.ph && sl == o.sl && nm == o.nm && sel == o.sel && has == o.has; }
    };
    struct Skill {               // p3 skill row: nm the original's name, rk the rank pips and req the level / SP line (data-rml)
        std::string act, src, nm, rk, req;
        bool sel = false, locked = false;
        bool operator==(const Skill &o) const { return act == o.act && src == o.src && nm == o.nm && rk == o.rk && req == o.req && sel == o.sel && locked == o.locked; }
    };
    struct Life {                // p3 life skill row (gathering / mining / fishing): no action
        std::string src, nm, w, req; // nm: name + level (data-rml), w: exp bar width
        bool operator==(const Life &o) const { return src == o.src && nm == o.nm && w == o.w && req == o.req; }
    };
    struct PkCell {              // quickslot picker slot: icon or placeholder, key glyph (markup)
        std::string act, src, k;
        bool has = false;
        bool operator==(const PkCell &o) const { return act == o.act && src == o.src && k == o.k && has == o.has; }
    };
    struct PkRow {               // picker page A / B
        std::string pg;
        std::vector<PkCell> cells;
        bool operator==(const PkRow &o) const { return pg == o.pg && cells == o.cells; }
    };
    struct Ttl {                 // title row: nm the original's name or "????" (data-rml)
        std::string act, nm;
        bool sel = false, on = false, learned = false, locked = false;
        bool operator==(const Ttl &o) const { return act == o.act && nm == o.nm && sel == o.sel && on == o.on && learned == o.learned && locked == o.locked; }
    };
    struct View {
        int tab = -1;            // the page drawn: rail sel / page on
        std::string ptitle, gold, bumpl, bumpr, foot, c_t, c_d;
        bool touch = false, dot0 = false, dot3 = false, confirm = false, can_save = true;
        // p0 character (name and title are the original's text: data-rml, rml_safe)
        std::string face, cvname, cvtitle, cvlvt, cvxp, gpleft, st[3], spv[3], sec[7];
        std::string xpw = "auto"; // xp bar width (data-style; "" would be an inline syntax error before the first update)
        bool gp_on = false;
        std::string sysinfo;     // p8 right column (glyph markup; the original's name / field name go through rml_safe)
        // p4 quests: list, summary counts, detail (markup made here: the destination is parsed out of the description)
        std::vector<Quest> quests;
        int q_main = 0, q_sub = 0, q_ready = 0;
        std::string qdetail;
        // p6 shop: products (cash::pull, outside the snapshot), detail markup
        std::vector<Cell> shop;
        std::string shopdet;
        std::string gl, gr;      // sub-tab key hints at the ends of the bag / skill chip rows ("" hides them)
        // p1 bag: category, capacity, items, detail markup
        int inv_cat = 0, inv_used = 0, inv_cap = 0;
        bool inv_full = false;
        std::vector<Cell> items;
        std::string invdet;
        // p2 equipment: slots, candidates header (markup), candidates, detail markup
        std::vector<Slot> eqslots;
        std::string eqhead;
        std::vector<Cell> cands;
        std::string eqdet;
        // p3 skills: chip, SP, skill rows or life rows, detail markup
        int sk_tab = 0, sp = 0;
        bool sk_empty = false; // sk_empty: skill tab with no skills ("스킬이 없습니다")
        std::vector<Skill> skills;
        std::vector<Life> life;
        std::string skdet;
        // quickslot picker over the page
        bool picker = false;
        std::vector<PkRow> pk_rows;
        // title list over the character page
        bool ttl = false;
        std::vector<Ttl> ttl_rows;
        std::string ttl_det, l_eq, l_in;
        std::string toast;
        bool toast_on = false;
    } v;

public:
    MenuScreen() {
        z = 20;
        for (int id : {4, 5, 7, 8, 9, 10, 0x11}) state::set_ported(id);
        register_screen(this);
    }
    const char *file() override { return "menu.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("menu");
        if (!c) return;
        c.Bind("tab", &v.tab); c.Bind("ptitle", &v.ptitle); c.Bind("gold", &v.gold); c.Bind("bumpl", &v.bumpl); c.Bind("bumpr", &v.bumpr);
        c.Bind("foot", &v.foot); c.Bind("c_t", &v.c_t); c.Bind("c_d", &v.c_d);
        c.Bind("touch", &v.touch); c.Bind("dot0", &v.dot0); c.Bind("dot3", &v.dot3); c.Bind("confirm", &v.confirm); c.Bind("can_save", &v.can_save);
        c.Bind("face", &v.face); c.Bind("cvname", &v.cvname); c.Bind("cvtitle", &v.cvtitle); c.Bind("cvlvt", &v.cvlvt); c.Bind("cvxp", &v.cvxp);
        c.Bind("xpw", &v.xpw); c.Bind("gpleft", &v.gpleft); c.Bind("gp_on", &v.gp_on);
        for (int i = 0; i < 3; i++) { c.Bind("st" + std::to_string(i), &v.st[i]); c.Bind("sp" + std::to_string(i), &v.spv[i]); }
        for (int i = 0; i < 7; i++) c.Bind("sec" + std::to_string(i), &v.sec[i]);
        c.Bind("sysinfo", &v.sysinfo);
        if (auto h = c.RegisterStruct<Quest>()) {
            h.RegisterMember("act", &Quest::act); h.RegisterMember("kind", &Quest::kind); h.RegisterMember("nm", &Quest::nm); h.RegisterMember("st", &Quest::st);
            h.RegisterMember("sel", &Quest::sel); h.RegisterMember("sub", &Quest::sub); h.RegisterMember("done", &Quest::done);
        }
        c.RegisterArray<std::vector<Quest>>();
        c.Bind("quests", &v.quests); c.Bind("q_main", &v.q_main); c.Bind("q_sub", &v.q_sub); c.Bind("q_ready", &v.q_ready); c.Bind("qdetail", &v.qdetail);
        if (auto h = c.RegisterStruct<Cell>()) {
            h.RegisterMember("act", &Cell::act); h.RegisterMember("src", &Cell::src); h.RegisterMember("n", &Cell::n); h.RegisterMember("pr", &Cell::pr);
            h.RegisterMember("sel", &Cell::sel); h.RegisterMember("free", &Cell::free); h.RegisterMember("hs", &Cell::hs); h.RegisterMember("bad", &Cell::bad);
        }
        c.RegisterArray<std::vector<Cell>>();
        c.Bind("shop", &v.shop); c.Bind("shopdet", &v.shopdet);
        c.Bind("gl", &v.gl); c.Bind("gr", &v.gr);
        c.Bind("inv_cat", &v.inv_cat); c.Bind("inv_used", &v.inv_used); c.Bind("inv_cap", &v.inv_cap); c.Bind("inv_full", &v.inv_full);
        c.Bind("items", &v.items); c.Bind("invdet", &v.invdet);
        if (auto h = c.RegisterStruct<Slot>()) {
            h.RegisterMember("act", &Slot::act); h.RegisterMember("src", &Slot::src); h.RegisterMember("ph", &Slot::ph); h.RegisterMember("sl", &Slot::sl);
            h.RegisterMember("nm", &Slot::nm); h.RegisterMember("sel", &Slot::sel); h.RegisterMember("has", &Slot::has);
        }
        c.RegisterArray<std::vector<Slot>>();
        c.Bind("eqslots", &v.eqslots); c.Bind("eqhead", &v.eqhead); c.Bind("cands", &v.cands); c.Bind("eqdet", &v.eqdet);
        if (auto h = c.RegisterStruct<Skill>()) {
            h.RegisterMember("act", &Skill::act); h.RegisterMember("src", &Skill::src); h.RegisterMember("nm", &Skill::nm); h.RegisterMember("rk", &Skill::rk);
            h.RegisterMember("req", &Skill::req); h.RegisterMember("sel", &Skill::sel); h.RegisterMember("locked", &Skill::locked);
        }
        c.RegisterArray<std::vector<Skill>>();
        if (auto h = c.RegisterStruct<Life>()) {
            h.RegisterMember("src", &Life::src); h.RegisterMember("nm", &Life::nm); h.RegisterMember("w", &Life::w); h.RegisterMember("req", &Life::req);
        }
        c.RegisterArray<std::vector<Life>>();
        c.Bind("sk_tab", &v.sk_tab); c.Bind("sp", &v.sp); c.Bind("sk_empty", &v.sk_empty); c.Bind("skills", &v.skills); c.Bind("life", &v.life); c.Bind("skdet", &v.skdet);
        if (auto h = c.RegisterStruct<PkCell>()) {
            h.RegisterMember("act", &PkCell::act); h.RegisterMember("src", &PkCell::src); h.RegisterMember("k", &PkCell::k); h.RegisterMember("has", &PkCell::has);
        }
        c.RegisterArray<std::vector<PkCell>>();
        if (auto h = c.RegisterStruct<PkRow>()) { h.RegisterMember("pg", &PkRow::pg); h.RegisterMember("cells", &PkRow::cells); }
        c.RegisterArray<std::vector<PkRow>>();
        c.Bind("picker", &v.picker); c.Bind("pk_rows", &v.pk_rows);
        if (auto h = c.RegisterStruct<Ttl>()) {
            h.RegisterMember("act", &Ttl::act); h.RegisterMember("nm", &Ttl::nm); h.RegisterMember("sel", &Ttl::sel); h.RegisterMember("on", &Ttl::on);
            h.RegisterMember("learned", &Ttl::learned); h.RegisterMember("locked", &Ttl::locked);
        }
        c.RegisterArray<std::vector<Ttl>>();
        c.Bind("ttl", &v.ttl); c.Bind("ttl_rows", &v.ttl_rows); c.Bind("ttl_det", &v.ttl_det); c.Bind("l_eq", &v.l_eq); c.Bind("l_in", &v.l_in);
        c.Bind("toast", &v.toast); c.Bind("toast_on", &v.toast_on);
        model = c.GetModelHandle();
    }
    // an original form the generic renderer draws on top (QBox draw popup, ...): it takes the keys
    bool yield = false;
    bool modal() override { return !yield; }
    bool want(const state::Snapshot &s) override {
        yield = false;
        if (!s.forms.empty())
            for (auto &f : recorded().forms) if (f.ptr == s.forms.back().ptr && f.generic) yield = true;
        // the original's cash store (form 0x16: QBox scales short, tag 0x4000) is the gold shop page here
        for (auto &f : s.forms)
            if (f.id == 0x16) { uint32_t p = f.ptr; state::post([p] { g::call(0x147760, {p, 1}); }); switch_tab(6); break; }
        if (!s.stripped || !s.menu.mdi) return false;
        // visible while the menu or one of its pages is the top form; dialogs above it (options, popups) keep it underneath
        return true;
    }
    void on_show() override { ss = {}; }
    void on_hide() override { ss.tab_req.clear(); }

    void switch_tab(int tab) {
        // a page action does not outlive its page: the Q / E tab keys come here without page_action's clear, and the
        // page's lists change on the way back in (the original fills them a frame or two later), which would pull the
        // focus back to an old chip / cell
        ss.focus_act.clear();
        if (tab == 6) { ss.shop = true; ss.cur_tab = -1; ss.tab_req.clear(); return; }
        ss.shop = false;
        ss.cur_tab = -1;
        // the original switches on the game thread a frame or two later: until then the rail shows the asked tab,
        // not the original's previous one (from the shop that is an older tab, which flashed for a frame)
        if (tab != 9) ss.tab_req.ask(tab, 30); // 9 closes the menu: nothing to show
        uint32_t m = mdi;
        state::post([m, tab] {
            if (!m) return;
            // container that would have produced this tab (cFormInvenMDI::pnlTabs_Change @0010bd14)
            uint32_t cont = M32(m + 0x88) ? M32(m + ((tab & 1) ? 0x64 : 0x60)) : M32(m + 0x54);
            g::call(0xfbaa8, {m, (uint32_t)tab, (uint32_t)(tab >> 1), (uint32_t)(tab >> 1), cont}); // tabsChangeImpl @0010baa8
        });
    }

    void toast(const std::string &t) {
        ss.toast = t;
        ss.toast_until = Rml::GetSystemInterface()->GetElapsedTime() + 2.2;
    }

    void update(const state::Snapshot &s) override {
        if (getenv("EH_LAYOUTDBG")) {
            static int n; if (n++ % 120 == 0) for (const char *id : {"rail", "main", "scrim"}) { Rml::Element *e = d.el(id); if (!e) continue;
                Rml::Vector2f o = e->GetAbsoluteOffset(Rml::BoxArea::Border); Rml::Vector2f sz = e->GetBox().GetSize(Rml::BoxArea::Border);
                ui_log("[dbg] %s %.0f,%.0f %.0fx%.0f body %.0fx%.0f", id, o.x, o.y, sz.x, sz.y, d.doc->GetBox().GetSize().x, d.doc->GetBox().GetSize().y); }
        }
        on_frame();
        const state::MenuData &m = s.menu;
        mdi = m.mdi;
        int tab = ss.shop ? 6 : m.tab < 0 ? 0 : m.tab;
        if (!ss.shop && ss.tab_req.settle(m.tab)) tab = ss.tab_req.want;
        if (tab != ss.cur_tab) {
            ss.cur_tab = tab;
            put(v.tab, tab, "tab"); // rail tab sel / page on (data-class: a newly shown page is not visible to ensure() below
                                    // in this frame either way, IsVisible follows the class in the Context::Update)
            nav.root = d.el("p" + std::to_string(tab));
            nav.set(nullptr);
            nav.ensure();
            if (!nav.focus) nav.set(d.el("tab" + std::to_string(tab)));
        }
        put(v.ptitle, std::string(tab < 9 ? TAB_NAMES[tab] : ""), "ptitle");
        put(v.gold, num(s.gold), "gold");
        put(v.bumpl, glyph("tabl"), "bumpl");
        put(v.bumpr, glyph("tabr"), "bumpr");
        put(v.touch, input::touch_input(), "touch"); // touch shows no tab keys: the rail drops their room
        put(v.dot0, s.gp > 0, "dot0");
        put(v.dot3, s.sp > 0, "dot3");

        if (tab == 0) update_char(s);
        if (tab == 4) update_quests(s);
        if (tab == 1) update_inv(s);
        if (tab == 2) update_eq(s);
        if (tab == 3) update_sk(s);
        if (tab == 6) update_shop(s);
        // the old code rebuilt every box of a page on its first draw in an opening (d.invalidate on show), so a detail
        // text started at the top; the boxes keep their elements across openings now: scroll it up instead
        if (tab >= 0 && tab < 32 && !(ss.drawn & (1u << tab))) {
            ss.drawn |= 1u << tab;
            Rml::ElementList ds;
            if (Rml::Element *pg = d.el("p" + std::to_string(tab))) pg->QuerySelectorAll(ds, ".ds");
            for (Rml::Element *e : ds) e->SetScrollTop(0);
        }
        int code;
        if (cash::result(&shop_seen, &code))
            toast(code == 0 ? "\xea\xb5\xac\xeb\xa7\xa4\xed\x96\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4" : code == 1 ? "\xea\xb3\xa8\xeb\x93\x9c\xea\xb0\x80 \xeb\xb6\x80\xec\xa1\xb1\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4" : code == 2 ? "\xea\xb0\x80\xeb\xb0\xa9 \xea\xb3\xb5\xea\xb0\x84\xec\x9d\xb4 \xeb\xb6\x80\xec\xa1\xb1\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4" : "\xea\xb5\xac\xeb\xa7\xa4\xed\x95\xa0 \xec\x88\x98 \xec\x97\x86\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4");
        // #mtoast: text and `on` (its toastup animation runs when the class comes on, as before)
        put(v.toast, ss.toast, "toast");
        put(v.toast_on, Rml::GetSystemInterface()->GetElapsedTime() <= ss.toast_until, "toast_on");
        update_picker(s);
        update_titles(s);
        if (tab == 8) { put(v.can_save, s.can_save, "can_save"); update_sysinfo(s); }

        put(v.confirm, ss.confirm != 0, "confirm");
        if (ss.confirm == 2) {
            auto pv = cash::pull();
            if (shop_sel >= 0 && shop_sel < (int)pv.size()) {
                const auto &p = pv[shop_sel];
                put(v.c_t, std::string("\xea\xb5\xac\xeb\xa7\xa4 \xed\x99\x95\xec\x9d\xb8"), "c_t");
                int g = state::get().gold;
                put(v.c_d, "<img src=\"" + sprite_src(p.icon) + "\"/>" + rml_safe(p.name) + (p.count > 1 ? Rml::CreateString(" x%d", p.count) : std::string()) + "<br/>" +
                               price_label(p.price) + "<br/><span class=\"after\">\xeb\xb3\xb4\xec\x9c\xa0 \xea\xb3\xa8\xeb\x93\x9c " + num(g) + " G &#8594; " + num(g - p.price) + " G</span>", "c_d");
            }
        }
        if (ss.confirm == 1) {
            put(v.c_t, std::string("\xed\x83\x80\xec\x9d\xb4\xed\x8b\x80\xeb\xa1\x9c \xeb\x8f\x8c\xec\x95\x84\xea\xb0\x80\xea\xb8\xb0"), "c_t");
            put(v.c_d, std::string("\xec\xa0\x80\xec\x9e\xa5\xed\x95\x98\xec\xa7\x80 \xec\x95\x8a\xec\x9d\x80 \xec\xa7\x84\xed\x96\x89\xec\x9d\x80 \xec\x82\xac\xeb\x9d\xbc\xec\xa7\x91\xeb\x8b\x88\xeb\x8b\xa4."), "c_d");
        }
        put(v.foot, "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("back") + " \xeb\x8b\xab\xea\xb8\xb0</span>", "foot");
    }


    // ------------------------------------------------------------------------------------------- pages

    static std::string esc_num(int v) { return num(v); }
    std::string item_props(const state::ItemInfo &it) {
        std::string r;
        auto row = [&](const char *k, const std::string &val) { r += std::string("<div><span class=\"k\">") + k + "</span>" + val + "</div>"; };
        if (it.req_lv > 0) row("필요 레벨", "Lv " + std::to_string(it.req_lv));
        if (it.type < 6 && it.dur_max > 0) row("내구도", std::to_string(it.dur) + " / " + std::to_string(it.dur_max));
        if (it.type < 6 && it.refine > 0) row("제련", "+" + std::to_string(it.refine));
        if (it.count > 1) row("보유", std::to_string(it.count) + "개");
        if (it.sell > 0) row("판매가", esc_num(it.sell) + " G");
        return r;
    }

    static std::string price_label(int g) { return g > 0 ? num(g) + " G" : std::string("\xeb\xac\xb4\xeb\xa3\x8c"); }

    // a grid / list changed. The old code rebuilt it (SetInnerRML) and keep_focus put the focus on the element of the
    // last page action (ss.focus_act) found in it. Rows are reused by index now, so a focus elsewhere in the list stays;
    // a focus on a row going away is dropped, and the acted element still takes the focus (now when its row is there,
    // else on the next frame once made). keep = false: a list the old code rebuilt without keep_focus.
    template <class T> bool rows(std::vector<T> &var, std::vector<T> &&val, const char *name, const char *list, bool keep) {
        if (var == val) return false;
        std::vector<std::string> acts;
        for (auto &r : val) acts.push_back(r.act);
        Rml::Element *l = d.el(list);
        nav.drop_if_gone(l, acts);
        put(var, std::move(val), name);
        if (keep) refocus(l, acts);
        return true;
    }
    void refocus(Rml::Element *l, const std::vector<std::string> &acts) {
        if (ss.focus_act.empty() || std::find(acts.begin(), acts.end(), ss.focus_act) == acts.end()) return;
        if (Rml::Element *e = nav.find_act(ss.focus_act, l)) nav.set(e); else nav.want_act(ss.focus_act, l);
    }
    // a category chip row: fixed chips <prefix>0..n-1 in menu.rml (`on` by data-class) between the sub-tab key hints.
    // A changed selection rebuilt the chip row in the old code, and the page's list / detail box when their markup changed
    // with it: a focus in a rebuilt box was lost (the list's keep_focus looks for ss.focus_act, a chip act, among the
    // list's rows), a focus elsewhere (the rail, an unchanged box) stayed. Where the old code lost it the acted chip takes
    // it now (keep_focus's rule for the row it missed; tab3_skt1: the focus on sk1, skt1 acted): chip_focus once the
    // list's / detail's change is known. Returns where the focus was when the selection changed (FOCUS_*).
    enum { FOCUS_NONE, FOCUS_ROW, FOCUS_LIST, FOCUS_DET };
    int chips(int &var, int sel, const char *name, const char *row, const char *list, const char *det) {
        put(v.gl, glyph("ui_subl"), "gl");
        put(v.gr, glyph("ui_subr"), "gr");
        if (!put(var, sel, name)) return FOCUS_NONE;
        Rml::Element *r = d.el(row), *l = d.el(list), *dt = d.el(det);
        for (Rml::Element *p = nav.focus.get(); p; p = p->GetParentNode()) {
            if (p == r) return FOCUS_ROW;
            if (p == l) return FOCUS_LIST;
            if (p == dt) return FOCUS_DET;
        }
        return FOCUS_NONE;
    }
    void chip_focus(const char *list, int n, const char *prefix) {
        std::vector<std::string> acts;
        for (int i = 0; i < n; i++) acts.push_back(prefix + std::to_string(i));
        refocus(d.el(list), acts);
    }
    // a detail box whose inner markup is made here (data-rml): a focus inside goes with its old elements when the markup
    // changes and is taken again by its data-act in the new markup on the next frame (smithy's #slots / #det; the old
    // code's SetInnerRML lost it). Not the button just pressed (use, equip, level up, drop: ss.focus_act): its action is
    // what changed the box, and the box's new button may act on something else (after equipping the original selects
    // candidate 0); the focus is dropped there, as the old rebuild did
    void inner(std::string &var, std::string &&val, const char *name) {
        if (var == val) return;
        Rml::Element *box = d.el(name), *f = nav.focus.get(), *p = f;
        while (p && p != box) p = p->GetParentNode();
        if (p && box) {
            std::string a = f->GetAttribute<Rml::String>("data-act", "");
            if (a == ss.focus_act) nav.clear(); else nav.want_act(a, box);
        }
        put(var, std::move(val), name);
    }

    void update_shop(const state::Snapshot &s) {
        auto pv = cash::pull();
        if (shop_sel >= (int)pv.size()) shop_sel = 0;
        // #shophead: a fixed note in menu.rml; an empty list shows "상품을 불러오는 중" (data-if)
        std::vector<Cell> cells;
        for (size_t i = 0; i < pv.size(); i++) {
            const auto &p = pv[i];
            Cell c;
            c.act = Rml::CreateString("sh%d", (int)i);
            c.src = sprite_src(p.icon);
            if (p.count > 1) c.n = std::to_string(p.count);
            c.pr = p.price > 0 ? num(p.price) : std::string("\xeb\xac\xb4\xeb\xa3\x8c"); // 무료
            c.free = p.price <= 0;
            c.sel = (int)i == shop_sel;
            cells.push_back(std::move(c));
        }
        rows(v.shop, std::move(cells), "shop", "shopgrid", true);
        std::string det;
        if (shop_sel >= 0 && shop_sel < (int)pv.size()) {
            const auto &p = pv[shop_sel];
            bool afford = s.gold >= p.price;
            det = "<div class=\"bigic\"><img src=\"" + sprite_src(p.icon) + "\"/></div><div class=\"nm\">" + rml_safe(p.name) + "</div><div class=\"ty\">" + rml_safe(p.type_name) + "</div>";
            det += "<div class=\"props\">";
            det += Rml::CreateString("<div><span class=\"k\">\xec\x88\x98\xeb\x9f\x89</span>%d\xea\xb0\x9c</div>", p.count);
            det += "<div><span class=\"k\">\xea\xb0\x80\xea\xb2\xa9</span>" + price_label(p.price) + "</div>";
            det += Rml::CreateString("<div><span class=\"k\">\xeb\xb3\xb4\xec\x9c\xa0</span>%d\xea\xb0\x9c</div>", p.owned);
            det += "</div><div class=\"ds\">" + rml_safe(p.desc) + "</div><div class=\"acts\">";
            det += std::string("<span class=\"btn primary") + (afford ? "" : " off") + "\" data-act=\"sh_buy\">\xea\xb5\xac\xeb\xa7\xa4\xed\x95\x98\xea\xb8\xb0</span>";
            if (!afford) det += Rml::CreateString("<span class=\"why\">%s G \xeb\xb6\x80\xec\xa1\xb1</span>", num(p.price - s.gold).c_str());
            det += "</div>";
        } else det = "<div class=\"none\">\xec\x83\x81\xed\x92\x88\xec\x9d\x84 \xec\x84\xa0\xed\x83\x9d\xed\x95\x98\xec\x84\xb8\xec\x9a\x94</div>";
        inner(v.shopdet, std::move(det), "shopdet");
    }

    void update_inv(const state::Snapshot &s) {
        const state::MenuData &m = s.menu;
        // #invcats: fixed chips 전체 장비 소비 재료 기타 (cat0-4) in menu.rml between the sub-tab key hints, then the capacity
        int cat = chips(v.inv_cat, m.inv_cat, "inv_cat", "invcats", "invgrid", "invdet");
        put(v.inv_used, m.inv_used, "inv_used");
        put(v.inv_cap, m.inv_cap, "inv_cap");
        put(v.inv_full, m.inv_used > m.inv_cap, "inv_full");
        std::vector<Cell> cells;
        for (size_t i = 0; i < m.items.size(); i++) {
            const auto &it = m.items[i];
            Cell c;
            c.act = Rml::CreateString("it%d", (int)i);
            c.src = sprite_src(it.icon);
            if (it.count > 1) c.n = std::to_string(it.count);
            c.hs = it.hotslot;
            c.sel = (int)i == m.inv_sel;
            cells.push_back(std::move(c));
        }
        bool grid = rows(v.items, std::move(cells), "items", "invgrid", true); // empty: "비어 있습니다" (data-if)
        if (cat == FOCUS_ROW || (cat == FOCUS_LIST && grid)) chip_focus("invcats", 5, "cat");
        std::string det;
        if (m.inv_sel >= 0 && m.inv_sel < (int)m.items.size()) {
            const auto &it = m.items[m.inv_sel];
            det = "<div class=\"bigic\"><img src=\"" + sprite_src(it.icon) + "\"/></div><div class=\"nm\">" + it.name_rml + "</div><div class=\"ty\">" + rml_safe(it.type_name) + "</div>";
            det += "<div class=\"props\">" + item_props(it) + "</div><div class=\"ds\">" + rml_safe(it.desc) + "</div><div class=\"acts\">";
            // offered actions: DAT_00176ba0 table (menu_lists_spec 1.3); a QBox (type 7, info+8 == 1) opens with
            // action 0x46 (cFormInvenItem::DoItemAction @0010d344 -> OpenQbox @0010cf74: the draw is local in this APK)
            bool qbox = it.type == 7 && it.cls == 1;
            if (qbox) det += "<span class=\"btn primary\" data-act=\"inv_open\">열기</span>";
            bool use = it.type == 6, hot = it.type == 6, drop = !(it.flags & 0x1000) && it.type != 9, piece = it.type == 8;
            if (use) det += "<span class=\"btn primary\" data-act=\"inv_use\">사용</span>";
            if (hot) det += "<span class=\"btn\" data-act=\"inv_hot\">단축키 등록</span>";
            if (piece) det += "<span class=\"btn\" data-act=\"inv_piece\">조각 모으기</span>";
            if (drop) det += "<span class=\"btn\" data-act=\"inv_drop\">버리기</span>";
            det += "</div>";
        } else det = "<div class=\"none\">아이템을 선택하세요</div>";
        if (cat == FOCUS_DET && det != v.invdet) chip_focus("invcats", 5, "cat");
        inner(v.invdet, std::move(det), "invdet");
    }

    void update_eq(const state::Snapshot &s) {
        const state::MenuData &m = s.menu;
        static const char *SLOT[6] = {"무기", "투구", "갑옷", "글로브", "부츠", "액세서리"};
        // empty-slot icons (lucide): sword, hard-hat, shirt, hand, footprints, gem
        static const char *PH[6] = {"&#xe2b3;", "&#xe0ee;", "&#xe1ca;", "&#xe1d7;", "&#xe3bd;", "&#xe242;"};
        // #eqslots: the six slots (rows reused; the slot words are ours, the item name the original's: data-rml)
        std::vector<Slot> slots;
        for (int k = 0; k < 6; k++) {
            const auto &it = m.equipped[k];
            Slot e;
            e.act = Rml::CreateString("eqs%d", k);
            e.has = it.ptr != 0;
            if (e.has) e.src = sprite_src(it.icon);
            e.ph = PH[k];
            e.sl = SLOT[k];
            e.nm = it.ptr ? it.name_rml : std::string("비어 있음");
            e.sel = m.eq_slot == k;
            slots.push_back(std::move(e));
        }
        rows(v.eqslots, std::move(slots), "eqslots", "eqslots", true);
        put(v.eqhead, m.eq_slot >= 0 && m.eq_slot < 6 ? std::string("교체할 ") + SLOT[m.eq_slot] + " <span style=\"color:#ffb547\">" + std::to_string(m.cands.size()) + "</span>"
                                                       : "가방의 장비 <span style=\"color:#ffb547\">" + std::to_string(m.cands.size()) + "</span>", "eqhead");
        std::vector<Cell> cells;
        for (size_t i = 0; i < m.cands.size(); i++) {
            const auto &it = m.cands[i];
            Cell c;
            c.act = Rml::CreateString("eqc%d", (int)i);
            c.src = sprite_src(it.icon);
            c.bad = (it.type < 5 && it.cls != s.job) || it.req_lv > s.level;
            c.sel = (int)i == m.cand_sel;
            cells.push_back(std::move(c));
        }
        rows(v.cands, std::move(cells), "cands", "eqcands", true); // empty: "교체할 장비가 없습니다" (data-if)
        std::string det;
        const state::ItemInfo *cur = nullptr;
        bool cand = false;
        if (m.cand_sel >= 0 && m.cand_sel < (int)m.cands.size()) { cur = &m.cands[m.cand_sel]; cand = true; }
        else if (m.eq_slot >= 0 && m.eq_slot < 6 && m.equipped[m.eq_slot].ptr) cur = &m.equipped[m.eq_slot];
        if (cur) {
            det = "<div class=\"bigic\"><img src=\"" + sprite_src(cur->icon) + "\"/></div><div class=\"nm\">" + rml_safe(cur->name) + "</div><div class=\"ty\">" + rml_safe(cur->type_name) + "</div>";
            det += "<div class=\"props\">" + item_props(*cur) + "</div>";
            if (cand && m.delta_on) {
                static const char *ST[7] = {"공격", "방어", "최대 HP", "최대 MP", "명중", "회피", "치명"};
                det += "<div class=\"ds delta\" style=\"top: 200dp;\">";
                for (int i = 0; i < 7; i++) {
                    int dv = m.delta[i];
                    det += std::string("<div><span class=\"k\">") + ST[i] + "</span>" +
                           (dv > 0 ? "<span class=\"up\">▲ " + num(dv) + "</span>" : dv < 0 ? "<span class=\"dn\">▼ " + num(-dv) + "</span>" : std::string("<span>—</span>")) + "</div>";
                }
                det += "</div>";
            } else det += "<div class=\"ds\">" + rml_safe(cur->desc) + "</div>";
            det += "<div class=\"acts\">";
            if (cand) det += "<span class=\"btn primary\" data-act=\"eq_equip\">장착</span>";
            else if (m.eq_slot >= 0 && m.eq_slot < 6) det += "<span class=\"btn\" data-act=\"eq_off\">해제</span>";
            det += "</div>";
        } else det = "<div class=\"none\">장비 슬롯을 선택하세요</div>";
        inner(v.eqdet, std::move(det), "eqdet");
    }

    void update_sk(const state::Snapshot &s) {
        const state::MenuData &m = s.menu;
        // #sktabs: fixed chips 액티브 패시브 생활 (skt0-2) in menu.rml between the sub-tab key hints, then the SP
        int tabc = chips(v.sk_tab, m.sk_tab, "sk_tab", "sktabs", "sklist", "skdet");
        put(v.sp, s.sp, "sp");
        // #sklist: skill rows (tabs 0 / 1) or the three life skill rows (tab 2, no data-act); no skills: "스킬이 없습니다"
        std::vector<Skill> sk;
        std::vector<Life> life;
        std::string det;
        if (m.sk_tab <= 1) {
            for (size_t i = 0; i < m.skills.size(); i++) {
                const auto &r = m.skills[i];
                Skill row;
                row.act = Rml::CreateString("sk%d", (int)i);
                row.src = sprite_src(r.icon);
                row.nm = rml_safe(r.name);
                for (int k = 0; k < r.max_rank; k++) row.rk += k < r.rank ? "<span class=\"on\"/>" : "<span/>"; // rank pips
                row.req = Rml::CreateString("Lv %d · SP %d%s", r.req_lv, r.sp_need, r.can_up ? "<span class=\"up\">배울 수 있음</span>" : "");
                row.sel = (int)i == m.sk_sel;
                row.locked = !r.learned;
                sk.push_back(std::move(row));
            }
            if (m.sk_sel >= 0 && m.sk_sel < (int)m.skills.size()) {
                const auto &r = m.skills[m.sk_sel];
                det = "<div class=\"bigic\"><img src=\"" + sprite_src(r.icon) + "\"/></div><div class=\"nm\">" + rml_safe(r.name) + "</div>";
                // .skb: the stat rows and the effect text scroll together (passives have long effect texts)
                det += Rml::CreateString("<div class=\"ty\">%s · Rank %d / %d</div><div class=\"skb\"><div class=\"props\">", r.type <= 1 ? "액티브" : "패시브", r.rank, r.max_rank);
                if (r.rank < r.max_rank) {
                    det += Rml::CreateString("<div><span class=\"k\">필요 레벨</span>Lv %d</div><div><span class=\"k\">필요 SP</span>%d</div>", r.req_lv, r.sp_need);
                } else det += "<div><span class=\"k\">상태</span>마스터</div>";
                for (const auto &kv : r.stats) det += "<div><span class=\"k\">" + rml_safe(kv.first) + "</span>" + rml_safe(kv.second) + "</div>";
                det += "</div>";
                if (!r.desc.empty()) det += "<div class=\"ds\">" + rml_safe(r.desc) + "</div>";
                det += "</div><div class=\"acts\">";
                if (r.rank < r.max_rank)
                    det += std::string("<span class=\"btn primary") + (r.can_up ? "" : " off") + "\" data-act=\"sk_up\">" + (r.learned ? "레벨 업" : "배우기") + "</span>";
                if (r.hot_ok) det += "<span class=\"btn\" data-act=\"sk_hot\">단축키 등록</span>";
                det += "</div>";
            } else det = "<div class=\"none\">스킬을 선택하세요</div>";
        } else {
            for (int i = 0; i < 3; i++) {
                const auto &lf = m.life[i];
                Life row;
                row.src = sprite_src(lf.icon);
                row.nm = rml_safe(lf.name) + Rml::CreateString(" <span style=\"color:#ffd166\">Lv %d</span>", lf.lv);
                row.w = Rml::CreateString("%.0fdp", 280.f * std::min(1.f, (float)lf.exp / lf.max)); // exp bar in a 300dp track
                row.req = num(lf.exp) + " / " + num(lf.max);
                life.push_back(std::move(row));
            }
            det = "<div class=\"none\">채집 · 채광 · 낚시로 오르는 생활 스킬입니다</div>";
        }
        put(v.sk_empty, m.sk_tab <= 1 && m.skills.empty(), "sk_empty");
        bool list = rows(v.skills, std::move(sk), "skills", "sklist", true);
        if (tabc == FOCUS_ROW || (tabc == FOCUS_LIST && (list || !(life == v.life)))) chip_focus("sktabs", 3, "skt");
        put(v.life, std::move(life), "life");
        if (tabc == FOCUS_DET && det != v.skdet) chip_focus("sktabs", 3, "skt");
        inner(v.skdet, std::move(det), "skdet");
    }

    void update_picker(const state::Snapshot &s) {
        put(v.picker, ss.picker != 0, "picker"); // a new opening starts with it closed (ss = {})
        if (!ss.picker) return;
        const state::MenuData &m = s.menu;
        // #pk_d: a fixed line in menu.rml. #pk_rows: the two hotslot pages A / B of five slots (nested data-for)
        std::vector<PkRow> pr;
        for (int pg = 0; pg < 2; pg++) {
            PkRow row;
            row.pg = pg ? "B" : "A";
            for (int c = 0; c < 5; c++) {
                const auto &h = m.hot[pg * 5 + c];
                // an <img> without a texture renders as a white quad: empty slots get a placeholder instead
                PkCell cell;
                cell.act = Rml::CreateString("pk%d", pg * 5 + c);
                cell.has = h.kind >= 0 && !h.icon.empty();
                if (cell.has) cell.src = sprite_src(h.icon);
                cell.k = glyph(Rml::CreateString("slot%d", c).c_str());
                row.cells.push_back(std::move(cell));
            }
            pr.push_back(std::move(row));
        }
        put(v.pk_rows, std::move(pr), "pk_rows");
    }

    void update_titles(const state::Snapshot &s) {
        const state::MenuData &m = s.menu;
        put(v.ttl, ss.ttl, "ttl");
        if (!ss.ttl) return;
        std::vector<Ttl> rows;
        for (size_t i = 0; i < m.titles.size(); i++) {
            const auto &t = m.titles[i];
            Ttl r;
            r.act = "tt" + std::to_string(i);
            r.nm = t.open ? rml_safe(t.name) : std::string("????"); // initContents @0011c630
            r.sel = (int)i == ss.ttl_sel; r.on = t.on; r.learned = t.learned; r.locked = !t.open;
            rows.push_back(std::move(r));
        }
        put(v.ttl_rows, std::move(rows), "ttl_rows");
        std::string det;
        if (ss.ttl_sel >= 0 && ss.ttl_sel < (int)m.titles.size() && m.titles[ss.ttl_sel].open) {
            const auto &t = m.titles[ss.ttl_sel];
            det = "<div class=\"nm\">" + rml_safe(t.name) + "</div><div class=\"ds\">" + t.desc + "</div>";
        }
        put(v.ttl_det, det, "ttl_det");
        put(v.l_eq, m.l_equip, "l_eq");
        put(v.l_in, m.l_info, "l_in");
    }
    // cFormCharTitle::OnMessageFromChild @0011c8d4 (0x1001 equip, 0x1002 detail) and somethingKeypress @0011cbbc (a
    // record under the level: notice GetConstMsg 0xba); notices are the original's (cGameUi::ShowNoticePopup @0011f5dc)
    static void title_do(int i, bool equip) {
        state::post([i, equip] {
            uint32_t pl = g::player(), rm = g::resmgr(), ui = g::gameui();
            if (!pl || !rm || !ui || i < 0 || i >= (int32_t)M32(rm + 0x274)) return;
            uint32_t ti = g::call(0xcc670, {rm, (uint32_t)i}); // GetCharTitleInfo @000dc670
            if (!ti) return;
            auto notice = [ui](uint32_t body, uint32_t title) { g::call(0x10f5dc, {ui, 0, 0, body, 0, 0, title, 0, 0, 0}); };
            uint32_t err = g::call(0x121cd4, {0x102}); // cGameError::GetConstMsg @00131cd4
            int lv = (int)g::call((M32(M32(pl) + 0x130) - IMG) & ~1u, {pl});
            if (M16(ti + 2) > lv) { notice(g::call(0x121cd4, {0xba}), err); return; }
            if (!equip) { g::call(0x10f5dc, {ui, 6, 0, 0, 0, 0, 0, 0, ti, 0}); return; }
            if (!g::call(0xb0d78, {pl, (uint32_t)i})) { notice(g::call(0x121cd4, {0xb8}), err); return; } // IsLearnTitle @000c0d78
            if ((int16_t)M16(pl + 0x35e) == i) { notice(g::call(0x121cd4, {0xb9}), err); return; }
            static char *buf = (char *)gas_calloc(100, 1); // the original's 100-byte stack buffer
            snprintf(buf, 100, g::cstr(g::call(0x120aac, {0x26f})), g::cstr(M32(rm + 0x34) + M32(ti + 4)));
            g::call(0xb295c, {pl, (uint32_t)i, 0}); // cCharacter::SelectTitle @000c295c
            notice((uint32_t)HPTR(buf), 0);
        });
    }

    bool page_action(const std::string &a) {
        const state::Snapshot &s = state::get();
        const state::MenuData &m = s.menu;
        uint32_t fi = m.page_form[1], fe = m.page_form[2], fs = m.page_form[3];
        ss.focus_act = a;
        auto num_after = [&](size_t n) { return atoi(a.c_str() + n); };
        if (a.rfind("cat", 0) == 0) { // cInvenItemList::OnCateChange(L, c) @0010dab0
            int c = num_after(3);
            state::post([fi, c] { uint32_t L = M32(fi + 0x90); if (L) g::call(0xfdab0, {L, (uint32_t)c}); });
            return true;
        }
        if (a.rfind("it", 0) == 0) { // bag selection: image list s16 +0x32 (menu_lists_spec 0.3)
            int i = num_after(2);
            state::post([fi, i] { uint32_t IL = M32(fi + 0x64); if (IL && i < (int16_t)M16(IL + 0x30)) M16(IL + 0x32) = (uint16_t)i; });
            return true;
        }
        auto do_item = [&](int id) { // cFormInvenItem::DoItemAction(form, id) @0010d344, only with a valid selection
            state::post([fi, id] {
                uint32_t IL = M32(fi + 0x64);
                if (!IL) return;
                int sel = (int16_t)M16(IL + 0x32), n = (int16_t)M16(IL + 0x30);
                if (sel >= 0 && sel < n) g::call(0xfd344, {fi, (uint32_t)id});
            });
        };
        if (a == "inv_use") { do_item(0x41); return true; }
        if (a == "inv_open") { do_item(0x46); return true; }
        if (a == "bag_expand") { open_bagbuy(); return true; }
        if (a == "inv_drop") { do_item(0x44); return true; }
        if (a == "inv_piece") { do_item(0x47); return true; }
        if (a == "inv_hot") {
            if (m.inv_sel >= 0 && m.inv_sel < (int)m.items.size()) { ss.picker = 1; ss.picker_obj = m.items[m.inv_sel].ptr; nav.root = d.el("picker"); nav.set(nullptr); nav.ensure(); }
            return true;
        }
        if (a.rfind("eqs", 0) == 0) { // slot k: form+0x94/+0x98 = k, RefreshEquipInven(form, 1) @00108ec4
            int k = num_after(3);
            state::post([fe, k] { M32(fe + 0x94) = (uint32_t)k; M32(fe + 0x98) = (uint32_t)k; g::call(0xf8ec4, {fe, 1}); g::call(0xf8ec4, {fe, 0}); });
            return true;
        }
        if (a.rfind("eqc", 0) == 0) { // candidate i: cflUiImageList::Select @001557a0, then the preview RefreshEquipInven(form, 0)
            int i = num_after(3);
            state::post([fe, i] { uint32_t IL = M32(fe + 0x64); if (IL && i < (int16_t)M16(IL + 0x30)) { g::call(0x1457a0, {IL, (uint32_t)i}); g::call(0xf8ec4, {fe, 0}); } });
            return true;
        }
        if (a == "eq_equip") { // cFormInvenEquip::EquipItem @00109644 + candidate list reload (RefreshEquipInven(form, 1))
            state::post([fe] {
                uint32_t IL = M32(fe + 0x64);
                if (!IL) return;
                int sel = (int16_t)M16(IL + 0x32), n = (int16_t)M16(IL + 0x30);
                if (sel < 0 || sel >= n) return;
                g::call(0xf9644, {fe});
                g::call(0xf8ec4, {fe, 1});
                g::call(0xf8ec4, {fe, 0});
            });
            return true;
        }
        if (a == "eq_off") { // cFormInvenEquip::UnequipItem @00109600 (slot = EL+0x94, synced by RefreshEquipInven(1))
            int k = m.eq_slot;
            if (k < 0 || k >= 6 || !m.equipped[k].ptr) return true;
            state::post([fe, k] { M32(fe + 0x94) = (uint32_t)k; g::call(0xf8ec4, {fe, 1}); g::call(0xf9600, {fe}); g::call(0xf8ec4, {fe, 1}); });
            return true;
        }
        if (a.rfind("skt", 0) == 0) { // tab: cFormInvenSkill::InitContents(form, t, 1) @00112c8c + tab container +0x21
            int t = num_after(3);
            state::post([fs, t] { uint32_t sc = M32(fs + 0x50); if (sc) M8(sc + 0x21) = (uint8_t)t; g::call(0x102c8c, {fs, (uint32_t)t, 1}); });
            return true;
        }
        if (a.rfind("sk", 0) == 0 && isdigit((unsigned char)a[2])) { // row: list u8 +0x21
            int i = num_after(2);
            state::post([fs, i] { uint32_t sc = M32(fs + 0x60); if (sc && i < (int32_t)M32(fs + 0x88)) M8(sc + 0x21) = (uint8_t)i; });
            return true;
        }
        if (a == "sk_up") { // cFormInvenSkill::LevelUpSkill @00112890 (learn and rank up)
            state::post([fs] { uint32_t sc = M32(fs + 0x60); if (sc && M8(sc + 0x21) < (int32_t)M32(fs + 0x88)) g::call(0x102890, {fs}); });
            return true;
        }
        if (a == "sk_hot") {
            if (m.sk_sel >= 0 && m.sk_sel < (int)m.skills.size()) { ss.picker = 2; ss.picker_obj = m.skills[m.sk_sel].ptr; nav.root = d.el("picker"); nav.set(nullptr); nav.ensure(); }
            return true;
        }
        if (a.rfind("pk", 0) == 0 && isdigit((unsigned char)a[2])) {
            // cCreature::SetSlotInfo(P, s/5, s%5, kind, obj) @000c4148 (what the hotslot popup does on confirm, menu_lists_spec 3.5)
            int sidx = num_after(2);
            int kind = ss.picker == 2 ? 0 : 1;
            uint32_t obj = ss.picker_obj;
            state::post([sidx, kind, obj, fi] {
                uint32_t pl = g::player();
                if (!pl || !obj) return;
                g::call(0xb4148, {pl, (uint32_t)(sidx / 5), (uint32_t)(sidx % 5), (uint32_t)kind, obj});
                uint32_t IL = fi ? M32(fi + 0x64) : 0;
                if (kind == 1 && IL) { g::call(0x145658, {IL}); M32(IL + 0x14) |= 0x200000; } // cflUiImageList::Load @00155658
            });
            ss.picker = 0;
            toast("단축키에 등록했습니다");
            nav.root = d.el("p" + std::to_string(ss.cur_tab));
            return true;
        }
        if (a == "ttl") { ss.ttl = true; ss.ttl_sel = 0; { const auto &t = state::get().menu.titles; for (size_t i = 0; i < t.size(); i++) if (t[i].on) ss.ttl_sel = (int)i; } nav.root = d.el("titles"); nav.set(nullptr); ss.focus_act = "tt" + std::to_string(ss.ttl_sel); return true; }
        if (a.size() > 2 && a[0] == 't' && a[1] == 't' && isdigit((unsigned char)a[2])) {
            int i = atoi(a.c_str() + 2);
            if (i == ss.ttl_sel) title_do(i, true); // second press on the chosen row = the row's first menu entry
            ss.ttl_sel = i;
            return true;
        }
        if (a == "tt_eq" || a == "tt_info") { title_do(ss.ttl_sel, a == "tt_eq"); ss.focus_act = a; return true; }
        if (a == "tt_close") { ss.ttl = false; nav.root = d.el("p" + std::to_string(ss.cur_tab)); nav.set(d.el("cvtitle")); return true; }
        if (a == "pk_close") { ss.picker = 0; nav.root = d.el("p" + std::to_string(ss.cur_tab)); return true; }
        ss.focus_act.clear();
        return false;
    }


    void update_char(const state::Snapshot &s) {
        const state::MenuData &m = s.menu;
        put(v.face, sprite_src(s.face), "face");
        put(v.cvname, rml_safe(s.name), "cvname");
        put(v.cvtitle, rml_safe(m.title), "cvtitle");
        put(v.cvlvt, "Lv " + std::to_string(s.level), "cvlvt");
        put(v.cvxp, num(s.exp) + " / " + num(s.expmax), "cvxp");
        put(v.xpw, pct(s.exp, s.expmax), "xpw");
        put(v.gpleft, s.gp > 0 ? Rml::CreateString("\xeb\x82\xa8\xec\x9d\x80 GP %d", s.gp) : std::string(), "gpleft");
        put(v.gp_on, s.gp > 0, "gp_on");
        for (int i = 0; i < 3; i++) {
            put(v.st[i], num(m.str[i]), ("st" + std::to_string(i)).c_str());
            put(v.spv[i], m.invest[i] ? Rml::CreateString("GP +%d", m.invest[i]) : std::string(), ("sp" + std::to_string(i)).c_str());
        }
        // #sec: HP MP 공격 방어 명중 회피 치명 (fixed cells in menu.rml)
        const std::string sec[7] = {num(s.hp) + " / " + num(s.maxhp), num(s.mp) + " / " + num(s.maxmp), num(m.atk), num(m.def), num(m.hit), num(m.eva), num(m.crit)};
        for (int i = 0; i < 7; i++) put(v.sec[i], sec[i], ("sec" + std::to_string(i)).c_str());
    }

    void update_sysinfo(const state::Snapshot &s) {
        auto row = [](const char *k, const std::string &val) { return std::string("<div class=\"row\"><span class=\"k\">") + k + "</span>" + val + "</div>"; };
        auto kr = [](const std::string &keys, const char *what) { return "<div class=\"kr\"><span class=\"ks\">" + keys + "</span>" + what + "</div>"; };
        std::string r = "<div class=\"card\"><h2>\xed\x94\x8c\xeb\xa0\x88\xec\x9d\xb4 \xec\xa0\x95\xeb\xb3\xb4</h2>";
        r += row("\xec\xba\x90\xeb\xa6\xad\xed\x84\xb0", rml_safe(s.name) + Rml::CreateString("  Lv %d", s.level));
        r += row("\xed\x98\x84\xec\x9e\xac \xec\x9c\x84\xec\xb9\x98", s.field_name.empty() ? std::string("-") : rml_safe(s.field_name));
        r += row("\xeb\xb3\xb4\xec\x9c\xa0 \xea\xb3\xa8\xeb\x93\x9c", num(s.gold) + " G");
        r += "</div><div class=\"card\"><h2>\xec\xa1\xb0\xec\x9e\x91</h2>";
        bool pad = glyph_ref("stick").find("bumper") != std::string::npos;
        if (glyph_ref("advance").empty()) r += "<div class=\"kr\">\xed\x84\xb0\xec\xb9\x98: \xed\x99\x94\xeb\xa9\xb4 \xec\x99\xbc\xec\xaa\xbd HUD\xec\x9d\x98 \xeb\xb2\x84\xed\x8a\xbc\xec\x9c\xbc\xeb\xa1\x9c Direct / V-Pad \xec\xa1\xb0\xec\x9e\x91\xec\x9d\x84 \xeb\xb0\x94\xea\xbf\x80 \xec\x88\x98 \xec\x9e\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4</div>";
        else {
            r += kr(glyph_ref("stick"), "\xec\x9d\xb4\xeb\x8f\x99") + kr(glyph_ref("attack"), "\xea\xb3\xb5\xea\xb2\xa9 \xc2\xb7 \xeb\x8c\x80\xed\x99\x94");
            r += kr(glyph_ref("slot0") + glyph_ref("slot1") + glyph_ref("slot2") + glyph_ref("slot3") + glyph_ref("slot4"), "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xec\x84\xb8\xed\x8a\xb8 A");
            r += pad ? kr(glyph_ref("page") + " +", "\xeb\x88\x84\xeb\xa5\xb8 \xec\xb1\x84\xeb\xa1\x9c \xec\x84\xb8\xed\x8a\xb8 B") : kr(glyph_ref("slot5") + glyph_ref("slot6") + glyph_ref("slot7") + glyph_ref("slot8") + glyph_ref("slot9"), "\xed\x80\xb5\xec\x8a\xac\xeb\xa1\xaf \xec\x84\xb8\xed\x8a\xb8 B");
            r += kr(glyph_ref("menu") + glyph_ref("map") + glyph_ref("quest"), "\xeb\xa9\x94\xeb\x89\xb4 \xc2\xb7 \xec\xa7\x80\xeb\x8f\x84 \xc2\xb7 \xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8");
        }
        r += "</div><div class=\"ver\">v1.0 \xc2\xb7 \xec\x9b\x90\xec\x9e\x91 EpicHearts (2012) \xec\x9e\xac\xea\xb5\xac\xed\x98\x84</div>";
        put(v.sysinfo, r, "sysinfo");
    }

    void update_quests(const state::Snapshot &s) {
        const auto &qs = s.menu.quests;
        if (ss.qsel >= (int)qs.size()) ss.qsel = 0;
        // rows: 메인 퀘스트 / 서브 퀘스트, 완료 가능 / 진행중 (the old code rebuilt the list without keep_focus: a focus on
        // a row stays now, on a row going away it is dropped)
        std::vector<Quest> ql;
        for (size_t i = 0; i < qs.size(); i++) {
            const auto &q = qs[i];
            ql.push_back({Rml::CreateString("q%d", (int)i), q.kind ? "\xec\x84\x9c\xeb\xb8\x8c \xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8" : "\xeb\xa9\x94\xec\x9d\xb8 \xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8", rml_safe(q.title),
                          q.done ? "\xec\x99\x84\xeb\xa3\x8c \xea\xb0\x80\xeb\x8a\xa5" : "\xec\xa7\x84\xed\x96\x89\xec\xa4\x91", (int)i == ss.qsel, q.kind != 0, q.done});
        }
        rows(v.quests, std::move(ql), "quests", "qlist", false);
        {
            int mains = 0, subs = 0, ready = 0;
            for (auto &q : qs) { (q.kind ? subs : mains)++; ready += q.done; }
            put(v.q_main, mains, "q_main");
            put(v.q_sub, subs, "q_sub");
            put(v.q_ready, ready, "q_ready");
        }
        std::string det;
        if (!qs.empty()) {
            const auto &q = qs[ss.qsel];
            // the original ends a quest description with the destination on its own line starting with '*'
            // (e.g. "*island. area. place"): shown as a destination card
            std::string body, dest;
            {
                size_t pos = 0;
                std::string sep = "<br/>";
                while (pos <= q.desc.size()) {
                    size_t e = q.desc.find(sep, pos);
                    std::string part = q.desc.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
                    std::string plain;
                    for (size_t k = 0, tag = 0; k < part.size(); k++) { if (part[k] == '<') tag = 1; else if (part[k] == '>') tag = 0; else if (!tag) plain += part[k]; }
                    size_t f = plain.find_first_not_of(" ");
                    if (f != std::string::npos && plain[f] == '*') dest = plain.substr(f + 1);
                    else body += (body.empty() ? "" : sep) + part;
                    if (e == std::string::npos) break;
                    pos = e + sep.size();
                }
            }
            det = std::string("<div class=\"meta\">") + (q.kind ? "SUB QUEST" : "MAIN QUEST") + "</div><h2>" + rml_safe(q.title) + "</h2><div class=\"desc\">" + rml_safe(body) + "</div>";
            if (!dest.empty()) det += "<div class=\"sec\">\xeb\xaa\xa9\xec\xa0\x81\xec\xa7\x80</div><div class=\"dest\"><span class=\"i\">&#xe111;</span>" + rml_safe(dest) + "</div>";
            if (!q.conds.empty() || q.timer >= 0) det += "<div class=\"sec\">\xeb\xaa\xa9\xed\x91\x9c</div>";
            det += "<div class=\"cond\">";
            for (size_t c = 0; c < q.conds.size(); c++) det += std::string("<div") + (q.conds_ok[c] ? " class=\"ok\"" : "") + ">" + rml_safe(q.conds[c]) + "</div>";
            if (q.timer >= 0) det += Rml::CreateString("<div>\xeb\x82\xa8\xec\x9d\x80 \xec\x8b\x9c\xea\xb0\x84  %02d:%02d:%02d</div>", q.timer / 3600, q.timer / 60 % 60, q.timer % 60);
            det += "</div>";
        }
        put(v.qdetail, det, "qdetail"); // the box scrolls itself: as before, its scroll position stays
    }

    bool action(const std::string &a) override {
        const state::Snapshot &s = state::get();
        if (page_action(a)) return true;
        if (a.rfind("tab", 0) == 0) { switch_tab(atoi(a.c_str() + 3)); return true; }
        if (a.rfind("sh", 0) == 0 && isdigit((unsigned char)a[2])) {
            int i = atoi(a.c_str() + 2);
            if (i == shop_sel) return action("sh_buy"); // second press on the selected product = buy
            shop_sel = i;
            ss.focus_act = a;
            return true;
        }
        if (a == "sh_buy") {
            auto pv = cash::pull();
            if ((size_t)shop_sel < pv.size() && pv[shop_sel].price > state::get().gold) { toast("\xea\xb3\xa8\xeb\x93\x9c\xea\xb0\x80 \xeb\xb6\x80\xec\xa1\xb1\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4"); return true; }
            ss.confirm = 2; nav.root = d.el("confirm"); nav.set(d.el("c_yes")); return true;
        }
        if (a == "close") { switch_tab(9); return true; }
        if (a.rfind("gp", 0) == 0) {
            int i = a[2] - '0';
            // cFormGpInvest::DoInvest @0011cf40 + commit (OnMessageFromChild @0011cfd4): invested s16 pl+0x3c6+2i,
            // unspent GP pl+0x344, then the stat recompute vfunc 0x138 (cFormCharStat::RefreshStat @0011c414)
            state::post([i] {
                uint32_t pl = g::player();
                if (!pl || (int32_t)M32(pl + 0x344) <= 0) return;
                M16(pl + 0x3c6 + 2 * i) = (uint16_t)(M16(pl + 0x3c6 + 2 * i) + 1);
                M32(pl + 0x344) = M32(pl + 0x344) - 1;
                uint32_t fn = M32(M32(pl) + 0x138);
                g::call(fn - IMG, {pl});
            });
            return true;
        }
        if (a.size() > 1 && a[0] == 'q' && isdigit((unsigned char)a[1])) { ss.qsel = atoi(a.c_str() + 1); return true; }
        if (a == "sys_save") {
            if (!s.can_save) { toast("\xec\xa7\x80\xea\xb8\x88\xec\x9d\x80 \xec\xa0\x80\xec\x9e\xa5\xed\x95\xa0 \xec\x88\x98 \xec\x97\x86\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4"); return true; }
            // cFormInvenSystem::DoSystem @001139a0 save path: CanSaveGameChk @0014285c, then SaveGameWithCheck @001452e4
            static volatile int result = -1;
            result = -1;
            state::post([] {
                uint32_t gm = g::gm();
                if (g::call(0x13285c, {}) != 0) { result = 2; return; }
                result = (int)g::call(0x1352e4, {gm});
            });
            ss.pending_save = true;
            ss.save_result = &result;
            return true;
        }
        if (a == "sys_help") { state::post([] { g::call(0x10f564, {g::gameui(), 0}); }); return true; } // cGameUi::ShowHelp @0011f564
        if (a == "sys_opt") {
            uint32_t pf = s.menu.page_form[8];
            state::post([pf] { g::call(0x10f5a0, {g::gameui(), pf}); }); // cGameUi::ShowOption @0011f5a0
            return true;
        }
        if (a == "sys_title") { ss.confirm = 1; nav.root = d.el("confirm"); nav.set(d.el("c_no")); return true; }
        if (a == "c_no") { bool sh = ss.confirm == 2; ss.confirm = 0; nav.root = d.el(sh ? "p6" : "p8"); nav.set(sh ? nullptr : d.el("sy_title")); return true; }
        if (a == "c_yes" && ss.confirm == 2) {
            { auto pv = cash::pull(); cash::buy((size_t)shop_sel < pv.size() ? pv[shop_sel].index : -1); }
            ss.confirm = 0;
            nav.root = d.el("p6");
            ss.focus_act = "sh" + std::to_string(shop_sel); // focus returns to the purchased product
            nav.set(nullptr);
            if (Rml::Element *e = nav.find_act(ss.focus_act, d.el("shopgrid"))) nav.set(e); // the grid's cells stay (data-for)
            return true;
        }
        if (a == "c_yes") {
            if (ss.confirm == 1) {
                uint32_t m = mdi;
                // cFormInvenSystem::OnMessageFromChild @001138d4 (popup 3, yes): SetDead(menu), ReturnMainMenu(gm, 0) @00127778
                state::post([m] { g::call(0x147760, {m, 1}); g::call(0x117778, {g::gm(), 0}); });
            }
            ss.confirm = 0;
            return true;
        }
        return false;
    }

    void back() override {
        if (ss.picker) { action("pk_close"); return; }
        if (ss.ttl) { action("tt_close"); return; }
        if (ss.confirm) { action("c_no"); return; }
        switch_tab(9);
    }
    // second tab row: bag categories (page 1), skill tabs (page 3)
    void subtab(int dir) override {
        if (ss.picker || ss.ttl || ss.confirm || ss.shop) return;
        const state::MenuData &m = state::get().menu;
        if (ss.cur_tab == 1) action(Rml::CreateString("cat%d", (m.inv_cat + dir + 5) % 5));
        else if (ss.cur_tab == 3) action(Rml::CreateString("skt%d", (m.sk_tab + dir + 3) % 3));
    }
    void tab(int dir) override {
        int i = 0;
        const int n = (int)(sizeof TABS / sizeof TABS[0]);
        for (int k = 0; k < n; k++) if (TABS[k] == (ss.shop ? 6 : ss.cur_tab)) i = k;
        i = (i + dir + n) % n;
        switch_tab(TABS[i]);
    }
    void on_frame() {
        if (ss.pending_save && ss.save_result && *ss.save_result >= 0) {
            ss.pending_save = false;
            toast(*ss.save_result == 0 ? "\xec\xa0\x80\xec\x9e\xa5\xed\x96\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4" : "\xec\xa0\x80\xec\x9e\xa5\xec\x97\x90 \xec\x8b\xa4\xed\x8c\xa8\xed\x96\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4");
        }
    }
};

static MenuScreen g_menu;
}
