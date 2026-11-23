// Blacksmith / alchemist (replaces cFormSmithyAlchemy, form 0xB, and its refine popup cFormPopup kind 0).
// SetId @001183d0: mode +0xF8, sub +0xF4 (title lang: 0 reinforce 0x2A0, 1 refine 0x2A1, 2/0 repair 0x2A3, 2/1 restore
// 0x2A2, 3 melt 0x2A4, 4 scan 0x2A6, 5 remove gems 0x2A5). Item list: element +0xCC (image list: count s16 +0x30,
// selection s16 +0x32) over the model +0xB0 (cCustomItemImageList: cInvenItem* +0x14, count +0x20); modes 0/1/5 pick
// through the popup list +0x8C (shown unless flag 0x8000), modes 2-4 list +0x7C directly. Slots: element +0xC8 over
// the model +0xB4 (entries +0x14): mode 0 item / gem (+0x60), mode 1 the four luck scrolls (+0x68, item +0x64 and
// target +0xBC), mode 5 the item's gems (+0x84, item +0x80). Labels (LabelNum value +0x24): cost +0xA0, gold +0xA4,
// refine stones needed +0x6C / owned +0x70 / price +0x74.
// Driving (the original's own functions): open the picker for a slot = slot index (+0x32) + ShowCommonTv(form, slot)
// @001185d0 + refocus +0xEC; pick = cflUiImageList::Select @001557a0 + candidate +0xC0 + OnItemSelectedForDoAction
// @00118730; list modes = Select + somethingChange(form, i, i, list) @0011a4a8; luck scrolls = doScrollAction(form,
// 0x213 / 0x214 add, 0x212 remove) @001189f8; execute = doAction(form, 0 | 1 repair all) @0011ab00 (confirmations are
// cFormNotice, drawn by dialogs.cpp); close = key 0x3A (KeyPreview @001186dc: the picker first, then the form).
// Refine popup (cFormPopup::Process @000f0990, ShowRefineResult @000efc40): result title +0x58, stat rows in +0x60
// (child +0x24: [0] before / [1] after LabelNum, [2] the stat's word image, index s16 +0x22 of set 11), OK +0x10C.
// The original's purchase offers inside these notices (FUN_00118708 -> cash store 0x16) land on the gold shop page.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include "wordimg.h"
#include <mutex>

namespace state {
struct SmItem { uint32_t ptr = 0; std::string name, name_rml, icon, type_name; int refine = 0, cap = 0, dur = 0, dur_max = 0, req_lv = 0; bool unscanned = false; };
struct SmRow { std::string name; int old_v = 0, new_v = 0; };
struct Smithy {
    uint32_t ptr = 0;
    int mode = 0, sub = 0;
    std::string title, hint;
    int cost = 0, gold = 0;
    bool picker = false;
    int list_sel = -1;
    std::vector<SmItem> list;
    int nslots = 0, slot_sel = -1;
    std::vector<SmItem> slots;
    SmItem target;
    int stones_need = 0, stones_have = 0, scales_need = 0, scales_have = 0;
    int scroll_left[2] = {0, 0};
    std::string scroll_name[2], scroll_icon[2];
    // refine popup
    uint32_t rpop = 0;
    bool rdone = false, rask = false; // result shown / still asking (Yes / No)
    int routcome = 0;
    std::string rtitle;
    std::vector<SmRow> rrows;
};
static Smithy g_sm_pub;
static std::mutex g_sm_mx;

static int labelnum(uint32_t e) { return e ? (int32_t)M32(e + 0x24) : 0; }
static SmItem read_item(uint32_t it) {
    SmItem r;
    if (!it) return r;
    r.ptr = it;
    r.name = g::plain_text(g::cstr(g::call(0xfed68, {it})));
    r.name_rml = g::item_name_rml(it);
    r.type_name = g::plain_text(g::cstr(g::call(0xfed38, {it})));
    uint32_t info = M32(it + 0x38);
    r.unscanned = (M16(it + 4) & 0x100) != 0;
    if (info) { r.icon = sprites::image(0x6b, M8(info + 0x13), r.unscanned ? 0 : M8(info + 0x12)); r.req_lv = M8(info + 9); }
    if (M16(it + 6) < 5) { r.dur = M16(it + 0x20); r.dur_max = M16(it + 0x22); r.refine = M8(it + 0x24); r.cap = M8(it + 0x25); } // as menu_pages.cpp reads them
    return r;
}
static SmItem read_info(uint32_t rm, uint32_t info) { // tagITEM_INITINFO* (luck scrolls, gems)
    SmItem r;
    if (!info || !rm) return r;
    r.ptr = info;
    r.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info)));
    r.name_rml = g::rml_text(r.name.c_str(), false);
    r.icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
    return r;
}

void capture_smithy(const Snapshot &s) {
    Smithy m;
    uint32_t f = 0;
    for (auto &fm : s.forms) if (fm.id == 0xb) f = fm.ptr;
    uint32_t rm = g::resmgr(), inv = g::inv();
    if (f && rm && inv) {
        m.ptr = f;
        m.mode = (int32_t)M32(f + 0xf8);
        m.sub = (int32_t)M32(f + 0xf4);
        m.title = element_text(M32(f + 0x58));
        uint32_t det = M32(f + 0x98), dk = det ? M32(det + 0x24) : 0;
        uint32_t c0 = dk ? M32(dk) : 0, ck = c0 ? M32(c0 + 0x24) : 0;
        m.hint = ck ? element_text(M32(ck)) : "";
        m.cost = labelnum(M32(f + 0xa0));
        m.gold = (int32_t)M32(inv + 0x48);
        uint32_t pk = M32(f + 0x8c);
        m.picker = pk && !(M32(pk + 0x14) & 0x8000);
        uint32_t il = M32(f + 0xcc), model = M32(f + 0xb0);
        bool list_live = m.mode >= 2 && m.mode <= 4 ? true : m.picker;
        if (il && model && list_live) {
            m.list_sel = (int16_t)M16(il + 0x32);
            int n = (int32_t)M32(model + 0x20);
            uint32_t arr = M32(model + 0x14);
            for (int i = 0; i < n && i < 120 && arr; i++) m.list.push_back(read_item(M32(arr + 4 * i)));
        }
        uint32_t sl = M32(f + 0xc8), sm = M32(f + 0xb4);
        if (sl && sm && m.mode != 2 && m.mode != 3 && m.mode != 4) {
            m.nslots = (int16_t)M16(sl + 0x30);
            m.slot_sel = (int16_t)M16(sl + 0x32);
            uint32_t arr = M32(sm + 0x14);
            for (int k = 0; k < m.nslots && k < 8 && arr; k++) {
                uint32_t e = M32(arr + 4 * k);
                m.slots.push_back(m.mode == 0 ? read_item(e) : read_info(rm, e));
            }
        }
        m.target = read_item(M32(f + 0xbc));
        if (m.mode == 1) {
            m.stones_need = labelnum(M32(f + 0x6c));
            m.stones_have = labelnum(M32(f + 0x70));
            m.cost = labelnum(M32(f + 0x74));
            m.scroll_left[0] = (int32_t)M32(f + 0x124);
            m.scroll_left[1] = (int32_t)M32(f + 0x128);
            static const int SID[2] = {0x17, 0x1a};
            for (int k = 0; k < 2; k++) {
                SmItem si = read_info(rm, g::call(0xcc3b4, {rm, 8, (uint32_t)SID[k], 0}));
                m.scroll_name[k] = si.name; m.scroll_icon[k] = si.icon;
            }
        }
        if (m.mode == 5 || (m.mode == 2 && m.sub == 1)) { // dragon scales (item 8/9 + 8/10)
            m.scales_have = (int32_t)g::call(0x11ff0c, {inv, 8, 9}) + (int32_t)g::call(0x11ff0c, {inv, 8, 10});
            uint32_t it = m.mode == 5 ? M32(f + 0xbc) : (m.list_sel >= 0 && m.list_sel < (int)m.list.size() ? m.list[m.list_sel].ptr : 0);
            if (it) m.scales_need = (int32_t)g::call(m.mode == 5 ? 0xfe97c : 0xfe9a4, {it}); // cItem::GetRemovePStoneDragonScale / GetRepiarRefineDragonScale
        }
        // the refine popup on top
        if (!s.forms.empty() && s.forms.back().id == 0xf && (int32_t)M32(s.forms.back().ptr + 0x168) == 0) {
            uint32_t p = s.forms.back().ptr;
            m.rpop = p;
            auto shown = [](uint32_t e) { return e && !(M32(e + 0x14) & 0x8000); };
            bool prog = shown(M32(p + 0x5c));                       // progress area (Process @000f0990), already up while asking
            m.rask = shown(M32(p + 0x114));                         // the question: Yes +0x10C / No +0x114
            m.rdone = !m.rask && !prog && shown(M32(p + 0x10c));    // ShowRefineResult: progress hidden, OK shown
            if (m.rask) m.rtitle = element_text(M32(p + 0x58));
            m.routcome = (int32_t)M32(p + 0x1cc);
            if (m.rdone) {
                m.rtitle = element_text(M32(p + 0x58));
                uint32_t rows = M32(p + 0x60);
                int n = rows && !(M32(rows + 0x14) & 0x8000) ? M8(rows + 0x20) : 0;
                uint32_t kids = rows ? M32(rows + 0x24) : 0;
                for (int i = 0; i < n && i < 10 && kids; i++) {
                    uint32_t row = M32(kids + 4 * i), rk = row ? M32(row + 0x24) : 0;
                    if (!rk || (M32(row + 0x14) & 0x8000)) continue;
                    SmRow r;
                    r.old_v = labelnum(M32(rk));      // ShowRefineResult: [0] the value before (its argument), [1] the item's now
                    r.new_v = labelnum(M32(rk + 4));
                    uint32_t img = M32(rk + 8);
                    const char *w = img ? wordimg::word(11, (int16_t)M16(img + 0x22)) : nullptr;
                    r.name = w ? w : "";
                    m.rrows.push_back(r);
                }
            }
        }
    }
    std::lock_guard<std::mutex> l(g_sm_mx);
    g_sm_pub = std::move(m);
}
static Smithy pull_smithy() { std::lock_guard<std::mutex> l(g_sm_mx); return g_sm_pub; }
}

namespace screens {

class SmithyScreen : public Screen {
    struct Session {
        int shown_frames = 0;
        std::string fkey;        // stage the focus was placed for
        bool ensure_next = false; // the stage's focus is the first item once this frame's model changes are in
    } ss;
    state::Smithy m;             // per-frame copy of the captured form
    uint32_t opened_for = 0;     // the picker opens once per original form, not again when the screen comes back
    // data model "smithy" (smithy.rml), set in update() only. #slots / #det differ by mode: their inner markup is made
    // here and goes in whole (data-rml)
    struct Row {
        std::string act, src, nm, ty; // nm / ty: original text (data-rml, rml_safe)
        bool sel = false;
        bool operator==(const Row &o) const { return act == o.act && src == o.src && nm == o.nm && ty == o.ty && sel == o.sel; }
    };
    struct View {
        std::string kicker, title, gold, hint;
        std::vector<Row> rows;
        int list_mode = 0;       // with no rows: 1 "no item" text, 2 the "choose other equipment" button (picker closed)
        std::string slots, det, rbody;
        bool noslots = false, refining = false, rdone = false, rask = false;
    } v;

    // (markup for data-rml: the original's names go through rml_safe)
    static std::string item_line(const state::SmItem &it) {
        std::string s = rml_safe(it.type_name);
        if (it.unscanned) s += " \xc2\xb7 \xeb\xaf\xb8\xec\x8b\x9d\xeb\xb3\x84"; // · 미식별
        else {
            if (it.refine > 0) s += Rml::CreateString(" \xc2\xb7 +%d", it.refine);
            if (it.dur_max > 0) s += Rml::CreateString(" \xc2\xb7 \xeb\x82\xb4\xea\xb5\xac %d/%d", it.dur, it.dur_max); // 내구
        }
        return s;
    }
    static std::string slot_html(const state::SmItem &it, const char *ph, const char *act, bool sel) {
        std::string r = Rml::CreateString("<div class=\"slot%s%s\" data-act=\"%s\">", sel ? " sel" : "", it.ptr ? "" : " empty", act);
        r += it.ptr ? "<img src=\"" + sprite_src(it.icon) + "\"/>" : std::string("<span class=\"ph\">&#xe13d;</span>");
        r += std::string("<div class=\"sl\">") + (it.ptr ? it.name_rml : std::string(ph)) + "</div></div>";
        return r;
    }

public:
    SmithyScreen() {
        z = 23;
        state::set_ported(0xb);
        register_screen(this);
    }
    const char *file() override { return "smithy.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("smithy");
        if (!c) return;
        if (auto h = c.RegisterStruct<Row>()) {
            h.RegisterMember("act", &Row::act); h.RegisterMember("src", &Row::src); h.RegisterMember("nm", &Row::nm);
            h.RegisterMember("ty", &Row::ty); h.RegisterMember("sel", &Row::sel);
        }
        c.RegisterArray<std::vector<Row>>();
        c.Bind("kicker", &v.kicker); c.Bind("title", &v.title); c.Bind("gold", &v.gold); c.Bind("hint", &v.hint);
        c.Bind("rows", &v.rows); c.Bind("list_mode", &v.list_mode);
        c.Bind("slots", &v.slots); c.Bind("det", &v.det); c.Bind("rbody", &v.rbody);
        c.Bind("noslots", &v.noslots); c.Bind("refining", &v.refining); c.Bind("rdone", &v.rdone); c.Bind("rask", &v.rask);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        m = state::pull_smithy();
        bool top = !s.forms.empty() && (s.forms.back().id == 0xb || (m.rpop && s.forms.back().ptr == m.rpop) || dialog_over(s));
        return s.stripped && m.ptr && top;
    }
    void on_show() override { ss = {}; }

    void update(const state::Snapshot &s) override {
        (void)s;
        // modes 0 / 1 / 5 choose their item in the original's picker: open it once for the first slot
        bool picks = m.mode == 0 || m.mode == 1 || m.mode == 5;
        // (a few frames in: the form finishes its own set-up first)
        if (++ss.shown_frames > 4 && picks && opened_for != m.ptr && !m.picker && !m.target.ptr && (m.slots.empty() || !m.slots[0].ptr)) { opened_for = m.ptr; open_picker(0); }

        // the focus asked by the stage change of the last frame: the first item, now that the rows / slots are in
        if (ss.ensure_next) { ss.ensure_next = false; nav.ensure(); }

        static const char *KICK[7] = {"REINFORCE", "REFINE", "REPAIR", "MELT", "SCAN", "GEMS", "SMITHY"};
        put(v.kicker, std::string(m.mode == 2 && m.sub == 1 ? "RESTORE" : KICK[m.mode >= 0 && m.mode < 7 ? m.mode : 6]), "kicker");
        put(v.title, rml_safe(m.title), "title");
        put(v.gold, num(m.gold), "gold");

        // left: the list (always for modes 2-4, while the picker is open for 0 / 1 / 5)
        bool list_on = !m.list.empty() || !picks || m.picker;
        std::vector<Row> rows;
        for (size_t i = 0; i < m.list.size(); i++) {
            const auto &it = m.list[i];
            rows.push_back({Rml::CreateString("l%d", (int)i), sprite_src(it.icon), it.name_rml, item_line(it), (int)i == m.list_sel});
        }
        put(v.rows, std::move(rows), "rows");
        put(v.list_mode, !m.list.empty() ? 0 : list_on ? 1 : 2, "list_mode"); // 1 대상 아이템이 없습니다, 2 다른 장비 선택

        // a focus inside #slots / #det goes with their old elements when the markup changes (data-rml makes new ones):
        // it is taken again by its data-act in the new markup on the next frame (the old code's SetInnerRML lost it)
        auto inner = [&](std::string &var, std::string &&val, const char *name) {
            if (var == val) return;
            Rml::Element *box = d.el(name), *f = nav.focus.get();
            Rml::Element *p = f;
            while (p && p != box) p = p->GetParentNode();
            if (p && box) nav.want_act(f->GetAttribute<Rml::String>("data-act", ""), box);
            put(var, std::move(val), name);
        };

        // middle: slots
        std::string sl;
        if (m.mode == 0) {
            sl += slot_html(m.slots.size() > 0 ? m.slots[0] : state::SmItem(), "\xec\x9e\xa5\xeb\xb9\x84", "pick0", m.picker && m.slot_sel == 0);         // 장비
            sl += "<span class=\"plus\">&#xe13d;</span>";
            sl += slot_html(m.slots.size() > 1 ? m.slots[1] : state::SmItem(), "\xea\xb0\x95\xed\x99\x94\xec\x84\x9d", "pick1", m.picker && m.slot_sel == 1); // 강화석
        } else if (m.mode == 1) {
            sl += slot_html(m.target, "\xec\x9e\xa5\xeb\xb9\x84", "pick0", m.picker);
            sl += "<div class=\"scrolls\">";
            for (int k = 0; k < 4; k++) {
                const state::SmItem it = k < (int)m.slots.size() ? m.slots[k] : state::SmItem();
                sl += Rml::CreateString("<div class=\"sc%s%s\" data-act=\"sc%d\">", it.ptr ? "" : " empty", m.slot_sel == k ? " sel" : "", k) +
                      (it.ptr ? "<img src=\"" + sprite_src(it.icon) + "\"/>" : std::string("")) + "</div>";
            }
            sl += "</div><div class=\"addsc\">";
            for (int k = 0; k < 2; k++)
                sl += Rml::CreateString("<span class=\"btn small%s\" data-act=\"add%d\"><img src=\"%s\"/>%s <span class=\"n\">%d</span></span>", m.scroll_left[k] > 0 && m.target.ptr ? "" : " off", k,
                                        sprite_src(m.scroll_icon[k]).c_str(), rml_safe(m.scroll_name[k]).c_str(), m.scroll_left[k]);
            sl += "<span class=\"btn small\" data-act=\"rmsc\">&#xe1b2;</span></div>";
        } else if (m.mode == 5) {
            sl += slot_html(m.target, "\xec\x9e\xa5\xeb\xb9\x84", "pick0", m.picker);
            sl += "<div class=\"scrolls\">";
            for (int k = 0; k < m.nslots && k < 4; k++) {
                const auto &it = m.slots[k];
                sl += Rml::CreateString("<div class=\"sc%s%s\" data-act=\"gem%d\">", it.ptr ? "" : " empty", m.slot_sel == k ? " sel" : "", k) +
                      (it.ptr ? "<img src=\"" + sprite_src(it.icon) + "\"/>" : std::string("")) + "</div>";
            }
            sl += "</div>";
        }
        put(v.noslots, sl.empty(), "noslots");
        inner(v.slots, std::move(sl), "slots");

        // right: the chosen item, costs, the action
        const state::SmItem *cur = m.mode == 0 ? (m.slots.empty() ? nullptr : &m.slots[0]) : (m.mode == 1 || m.mode == 5) ? &m.target
                                 : (m.list_sel >= 0 && m.list_sel < (int)m.list.size() ? &m.list[m.list_sel] : nullptr);
        std::string det;
        if (cur && cur->ptr) det = "<div class=\"bigic\"><img src=\"" + sprite_src(cur->icon) + "\"/></div><div class=\"dn\">" + rml_safe(cur->name) + "</div><div class=\"dt\">" + item_line(*cur) + "</div>";
        det += "<div class=\"hn\">" + rml_safe(m.hint) + "</div><div class=\"props\">";
        auto prop = [&](const char *k, const std::string &val, bool bad = false) { det += std::string("<div><span class=\"k\">") + k + "</span><span class=\"" + (bad ? "bad" : "v") + "\">" + val + "</span></div>"; };
        if (m.mode == 1) {
            prop("\xec\xa0\x9c\xeb\xa0\xa8\xec\x84\x9d", Rml::CreateString("%d / %d", m.stones_have, m.stones_need), m.stones_have < m.stones_need); // 제련석
            if (cur && cur->ptr) prop("\xec\xa0\x9c\xeb\xa0\xa8 \xeb\x8b\xa8\xea\xb3\x84", Rml::CreateString("+%d / +%d", cur->refine, cur->cap)); // 제련 단계
        }
        if (m.mode == 5 || (m.mode == 2 && m.sub == 1)) prop("\xec\x9a\xa9\xeb\xb9\x84\xeb\x8a\x98", Rml::CreateString("%d / %d", m.scales_have, m.scales_need), m.scales_have < m.scales_need); // 용비늘
        if (m.mode != 3 && !(m.mode == 2 && m.sub == 1) && m.mode != 5) prop("\xeb\xb9\x84\xec\x9a\xa9", num(m.cost) + " G", m.cost > m.gold); // 비용
        det += "</div><div class=\"acts\">";
        static const char *DO[7] = {"\xea\xb0\x95\xed\x99\x94\xed\x95\x98\xea\xb8\xb0", "\xec\xa0\x9c\xeb\xa0\xa8\xed\x95\x98\xea\xb8\xb0", "\xec\x88\x98\xeb\xa6\xac\xed\x95\x98\xea\xb8\xb0",
                                    "\xeb\x85\xb9\xec\x9d\xb4\xea\xb8\xb0", "\xec\x8a\xa4\xec\xba\x94\xed\x95\x98\xea\xb8\xb0", "\xea\xb0\x95\xed\x99\x94\xec\x84\x9d \xec\xa0\x9c\xea\xb1\xb0", "\xec\x8b\xa4\xed\x96\x89"};
        // 강화하기 제련하기 수리하기 녹이기 스캔하기 강화석 제거 실행
        const char *label = m.mode == 2 && m.sub == 1 ? "\xeb\xb3\xb5\xea\xb5\xac\xed\x95\x98\xea\xb8\xb0" : DO[m.mode >= 0 && m.mode < 7 ? m.mode : 6]; // 복구하기
        if (m.mode == 2 && m.sub == 0) det += "<span class=\"btn\" data-act=\"doall\">\xec\xa0\x84\xec\xb2\xb4 \xec\x88\x98\xeb\xa6\xac</span>"; // 전체 수리
        det += std::string("<span class=\"btn primary\" data-act=\"do\">") + label + "</span></div>";
        inner(v.det, std::move(det), "det");

        // refine popup: progress, then the result (its stage classes and body change only while it is up)
        put(v.refining, m.rpop != 0, "refining");
        if (m.rpop) {
            put(v.rdone, m.rdone, "rdone");
            put(v.rask, m.rask, "rask");
            std::string r;
            if (m.rask) r = "<div class=\"rq\">" + rml_safe(m.rtitle) + "</div>";
            else if (!m.rdone) r = "<div class=\"rt\">\xec\xa0\x9c\xeb\xa0\xa8 \xec\xa4\x91\xe2\x80\xa6</div><div class=\"rbar\"><div class=\"rfill\"/></div>"; // 제련 중…
            else {
                static const char *CLS[5] = {"ok", "bad", "warn", "bad", "warn"};
                r = std::string("<div class=\"rt ") + CLS[m.routcome >= 0 && m.routcome < 5 ? m.routcome : 1] + "\">" + rml_safe(m.rtitle) + "</div>";
                for (auto &row : m.rrows)
                    r += Rml::CreateString("<div class=\"rr\"><span class=\"k\">%s</span><span class=\"o\">%d</span><span class=\"ar\">&#xe06f;</span><span class=\"n\">%d</span></div>", rml_safe(row.name).c_str(), row.old_v, row.new_v);
            }
            put(v.rbody, r, "rbody");
        }

        std::string fk = std::to_string(m.mode) + "/" + std::to_string(m.picker) + "/" + std::to_string(m.list.size()) + "/" + std::to_string(m.rpop) + std::to_string(m.rdone) + std::to_string(m.rask);
        popup_focus();
        if (fk != ss.fkey) {
            ss.fkey = fk;
            nav.root = d.el(m.rpop ? "rbox" : "main");
            nav.set(nullptr);
            nav.want.clear();   // the stage decides (not a slot / detail refocus asked above)
            ss.ensure_next = false;
            if (m.rpop) { nav.set(d.el(m.rask ? "ryes" : "rok")); nav.ensure(); }
            else if (!m.list.empty()) {
                // the selected row (rows are reused by index: there now, or made in this frame's Context::Update)
                std::string a = Rml::CreateString("l%d", std::max(0, std::min(m.list_sel, (int)m.list.size() - 1)));
                if (Rml::Element *e = nav.find_act(a, d.el("list"))) { nav.set(e); nav.ensure(); }
                else { nav.want_act(a, d.el("list")); ss.ensure_next = true; }
            } else ss.ensure_next = true; // no rows: the first item ("다른 장비 선택", a slot, the action) once shown
        }
        put(v.hint, "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("back") + " \xeb\x82\x98\xea\xb0\x80\xea\xb8\xb0</span>", "hint");
    }

    // the popup's buttons appear with its stage (their styles apply a frame later): keep its default button focused
    void popup_focus() {
        if (!m.rpop || !(m.rask || m.rdone)) return;
        Rml::Element *want = d.el(m.rask ? "ryes" : "rok");
        if (nav.focus.get() != want && !(m.rask && nav.focus == d.el("rno"))) nav.set(want);
    }

    void open_picker(int slot) {
        uint32_t f = m.ptr;
        int mode = m.mode;
        state::post([f, mode, slot] {
            // the slot element whose picker opens: mode 0 slot list +0x60, mode 1 item image +0x64, mode 5 item image +0x80
            uint32_t el = M32(f + (mode == 0 ? 0x60 : mode == 1 ? 0x64 : 0x80));
            // mode 0 with no item in slot 0 (model +0xB4 entries +0x14 [0] null): a slot other than 0 shows the notice
            // ShowNoticePopup(ui +4, 0, 0, GetLangData(0x2A8), 0, 0, GetConstMsg(0x102), 0, 0, 0) instead of the picker
            // (ShowCommonTv would read that item's +6) -- somethingKeypress @0011b318, slot list +0xC8 select key
            if (mode == 0 && slot != 0) {
                uint32_t sm = M32(f + 0xb4);
                if (!sm || !M32(M32(sm + 0x14))) {
                    g::call(0x10f5dc, {M32(f + 4), 0, 0, g::call(0x120aac, {0x2a8}), 0, 0, // cGameUi::ShowNoticePopup @0011f5dc, GetLangData @00130aac
                                       g::call(0x121cd4, {0x102}), 0, 0, 0});              // cGameError::GetConstMsg @00131cd4
                    return;
                }
            }
            if (mode == 0 && el) M16(el + 0x32) = (uint16_t)slot;
            g::call(0x1085d0, {f, (uint32_t)(mode == 0 ? slot : 0)}); // cFormSmithyAlchemy::ShowCommonTv @001185d0
            M32(f + 0xec) = el;
        });
    }

    bool action(const std::string &a) override {
        uint32_t f = m.ptr;
        int mode = m.mode;
        if (a.size() > 1 && a[0] == 'l' && isdigit((unsigned char)a[1])) {
            int i = atoi(a.c_str() + 1);
            bool picks = mode == 0 || mode == 1 || mode == 5;
            state::post([f, i, picks] {
                uint32_t il = M32(f + 0xcc), model = M32(f + 0xb0);
                if (!il || !model || i >= (int32_t)M32(model + 0x20)) return;
                g::call(0x1457a0, {il, (uint32_t)i});                     // cflUiImageList::Select @001557a0
                if (picks) {
                    M32(f + 0xc0) = M32(M32(model + 0x14) + 4 * i);
                    g::call(0x108730, {f});                               // OnItemSelectedForDoAction @00118730
                } else {
                    g::call(0x10a4a8, {f, (uint32_t)i, (uint32_t)i, il}); // somethingChange @0011a4a8
                }
            });
            return true;
        }
        if (a == "pick0" || a == "pick1") { open_picker(a[4] - '0'); return true; }
        if (a.rfind("sc", 0) == 0 || a.rfind("gem", 0) == 0) { // select a scroll / gem slot
            int k = atoi(a.c_str() + (a[0] == 's' ? 2 : 3));
            int off = a[0] == 's' ? 0x68 : 0x84;
            state::post([f, k, off] { uint32_t el = M32(f + off); if (el && k < (int16_t)M16(el + 0x30)) M16(el + 0x32) = (uint16_t)k; });
            return true;
        }
        if (a == "add0" || a == "add1" || a == "rmsc") {
            uint32_t op = a == "add0" ? 0x213 : a == "add1" ? 0x214 : 0x212;
            state::post([f, op] { g::call(0x1089f8, {f, op}); });         // doScrollAction @001189f8
            return true;
        }
        if (a == "do" || a == "doall") {
            uint32_t all = a == "doall";
            state::post([f, all] {
                // the original runs the action with the focus on its button +0xAC (somethingKeypress @0011b318: OK on
                // +0xAC -> doAction(form, 0)). procOnFocusChange @0011a97c keeps
                // the last focused element at +0xC4 and the confirmation's Yes (OnMessageFromChild @0011aff8) gives it the
                // focus again after doReinforce / doMelt / ...: left on the item list +0xCC (where this screen's picks
                // leave it), that refocus shows the list's selected entry (somethingChange -> ShowItemSummary), e.g. the
                // reinforce stone stack just used up, and cItem::GetName read freed memory. cflUiElement::SetFocus @00154c6c
                uint32_t btn = M32(f + 0xac);
                if (btn) g::call(0x144c6c, {btn});
                g::call(0x10ab00, {f, all});                              // doAction @0011ab00
            });
            return true;
        }
        if (a == "ryes" || a == "rno") {
            uint32_t p = m.rpop;
            int off = a == "ryes" ? 0x10c : 0x114;
            // custom data +0x314 selects the server refine (Yes: cTarNet::OpenNet, then Process -> DecideRefineResult3);
            // offline the popup takes its local path (Process -> DecideRefineResult @000f0054) as when it is 0
            if (p && m.rask) state::post([p, off] { if (off == 0x10c) M32(p + 0x314) = 0; g::call(0xe2ed8, {p, 0x30, 0, M32(p + off)}); });
            return true;
        }
        if (a == "rok") { uint32_t p = m.rpop; if (p && m.rdone) state::post([p] { g::call(0xe2ed8, {p, 0x30, 0, M32(p + 0x10c)}); }); return true; }
        if (a == "close") { back(); return true; }
        return false;
    }
    void back() override {
        if (m.rpop) { if (m.rdone) action("rok"); else if (m.rask) action("rno"); return; }
        uint32_t f = m.ptr;
        bool picker = m.picker;
        state::post([f, picker] {
            if (picker) g::call(0x108570, {f}); // ExitIlsItemList @00118570 (KeyPreview closes the picker first)
            state::key_tap(0x3a);
        });
    }
};

static SmithyScreen g_smithy;
}
