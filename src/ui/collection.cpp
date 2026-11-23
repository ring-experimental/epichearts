// Collection book (replaces cFormCollection, form 0xFF; script ShowUIWnd(0xFF, id 0x16..0x18) -> SetId @001153b4).
// idx +0xB0 = id - 0x16, item base +0xAC = {0x5B, 0x77, 0x49}[idx]; entries {24, 14, 11} = item type 8 id base + s
// (cCollectionItemList::Load @00115390); title lang 0x27A + idx (+ "(" lang 0x1A1 ")" when complete, initTitleArea
// @00115830). Registered bits u32 world +0x2A4 + 4 idx (bit 31 complete); owned = cInventory::GetItem(inv, 8, id, -1)
// @0012f9d0. Cell icon: item image 0x1E + idx (DrawIcon @00115c08). Register = DoRegItem @001158d4 (removes the item,
// sets the bit; a completed collection closes the form and runs script 0xDFDE + idx), unregister = DoUnRegItem
// @00115490; both act on the image list's selection (+0x60, cflUiImageList::Select @001557a0) and do not check, so the
// screen offers them only where cFormCollection::somethingChange @001154f4 would. Close: key 0x3A.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include <mutex>

namespace state {
struct CollEntry { std::string name, desc; bool reg = false, owned = false; };
struct Collection { uint32_t ptr = 0; int idx = 0, sel = 0; bool complete = false; std::string title, icon, l_reg, l_unreg; std::vector<CollEntry> e; };
static Collection g_col_pub;
static std::mutex g_col_mx;

void capture_collection(const Snapshot &s) {
    Collection c;
    uint32_t rm = g::resmgr(), w = g::world(), inv = g::inv();
    if (!s.forms.empty() && s.forms.back().id == 0xff && rm && w && inv) {
        uint32_t f = s.forms.back().ptr;
        c.ptr = f;
        c.idx = (int32_t)M32(f + 0xb0);
        if (c.idx < 0 || c.idx > 2) c.idx = 0;
        int base = (int32_t)M32(f + 0xac);
        static const int N[3] = {24, 14, 11};
        uint32_t bits = M32(w + 0x2a4 + 4 * c.idx);
        c.complete = (bits >> 31) & 1;
        c.title = element_text(M32(f + 0x58));
        c.icon = sprites::image(0x6b, 0x1e + c.idx);
        c.l_reg = g::plain_text(g::cstr(g::call(0x120aac, {0x24a})));
        c.l_unreg = g::plain_text(g::cstr(g::call(0x120aac, {0x231})));
        uint32_t il = M32(f + 0x60);
        c.sel = il ? (int16_t)M16(il + 0x32) : 0;
        for (int i = 0; i < N[c.idx]; i++) {
            CollEntry e;
            uint32_t info = g::call(0xcc3b4, {rm, 8, (uint32_t)(base + i), 0xffffffffu});
            if (info) {
                e.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info)));
                uint32_t ds = g::call(0xcd0d8, {rm, 0, M32(info + 4)});
                e.desc = ds ? g::rml_text(g::cstr(ds)) : "";
            }
            e.reg = (bits >> i) & 1;
            e.owned = g::call(0x11f9d0, {inv, 8, (uint32_t)(base + i), 0xffffffffu}) != 0;
            c.e.push_back(std::move(e));
        }
    }
    std::lock_guard<std::mutex> l(g_col_mx);
    g_col_pub = std::move(c);
}
static Collection pull_collection() { std::lock_guard<std::mutex> l(g_col_mx); return g_col_pub; }
}

namespace screens {

class CollectionScreen : public Screen {
    struct Session {
        int last = -2;           // selection the focus was placed for
        bool fresh = true;       // first update of this opening (the detail column was made anew on every opening)
    } ss;
    state::Collection c;         // per-frame copy of the captured form

    // data model "collection" (collection.rml), set in update()
    struct Cell {
        std::string act, src;    // c<i>, the medal icon
        bool sel = false, reg = false, own = false; // own: owned, not registered
        bool operator==(const Cell &o) const { return act == o.act && src == o.src && sel == o.sel && reg == o.reg && own == o.own; }
    };
    struct View {
        std::vector<Cell> grid;
        std::string title, prog, pfill = "0%", hint; // title, hint: markup
        bool has = false;        // a medal is selected: the detail column
        std::string icon, dn, st, ds, l_unreg, l_reg; // dn, ds, the button labels: markup
        int st_k = 0;            // 0 s-no, 1 s-own, 2 s-reg
        bool unreg = false, reg = false;
    } m;

public:
    CollectionScreen() { z = 23; state::set_ported(0xff); register_screen(this); }
    const char *file() override { return "collection.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor k = ctx->CreateDataModel("collection");
        if (!k) return;
        if (auto h = k.RegisterStruct<Cell>()) {
            h.RegisterMember("act", &Cell::act); h.RegisterMember("src", &Cell::src);
            h.RegisterMember("sel", &Cell::sel); h.RegisterMember("reg", &Cell::reg); h.RegisterMember("own", &Cell::own);
        }
        k.RegisterArray<std::vector<Cell>>();
        k.Bind("grid", &m.grid); k.Bind("title", &m.title); k.Bind("prog", &m.prog); k.Bind("pfill", &m.pfill); k.Bind("hint", &m.hint);
        k.Bind("has", &m.has); k.Bind("icon", &m.icon); k.Bind("dn", &m.dn); k.Bind("st", &m.st); k.Bind("ds", &m.ds);
        k.Bind("l_unreg", &m.l_unreg); k.Bind("l_reg", &m.l_reg); k.Bind("st_k", &m.st_k); k.Bind("unreg", &m.unreg); k.Bind("reg", &m.reg);
        model = k.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        c = state::pull_collection();
        return s.stripped && c.ptr;
    }
    void on_show() override { ss = {}; }
    void update(const state::Snapshot &s) override {
        (void)s;
        put(m.title, rml_safe(c.title), "title");
        int got = 0;
        for (auto &e : c.e) got += e.reg;
        put(m.prog, Rml::CreateString("%d / %d", got, (int)c.e.size()), "prog");
        put(m.pfill, Rml::CreateString("%d%%", c.e.empty() ? 0 : got * 100 / (int)c.e.size()), "pfill");
        std::vector<Cell> g;
        for (size_t i = 0; i < c.e.size(); i++) {
            const auto &e = c.e[i];
            g.push_back({Rml::CreateString("c%d", (int)i), sprite_src(c.icon), (int)i == c.sel, e.reg, !e.reg && e.owned});
        }
        put(m.grid, std::move(g), "grid");
        bool has = c.sel >= 0 && c.sel < (int)c.e.size();
        // the detail column was rebuilt (its text box back at the top) whenever anything in it changed
        bool det = put(m.has, has, "has");
        if (has) {
            const auto &e = c.e[c.sel];
            det |= put(m.icon, sprite_src(c.icon), "icon");
            det |= put(m.dn, rml_safe(e.name), "dn");
            det |= put(m.st_k, e.reg ? 2 : e.owned ? 1 : 0, "st_k");
            det |= put(m.st, std::string(e.reg ? "\xeb\x93\xb1\xeb\xa1\x9d\xeb\x90\xa8" : e.owned ? "\xeb\xb3\xb4\xec\x9c\xa0 \xec\xa4\x91 \xc2\xb7 \xeb\x93\xb1\xeb\xa1\x9d \xea\xb0\x80\xeb\x8a\xa5" : "\xeb\xaf\xb8\xeb\xb3\xb4\xec\x9c\xa0"), "st"); // 등록됨 / 보유 중 · 등록 가능 / 미보유
            det |= put(m.ds, rml_safe(e.desc), "ds");
            det |= put(m.unreg, !c.complete && e.reg, "unreg");
            det |= put(m.reg, !c.complete && !e.reg && e.owned, "reg");
            det |= put(m.l_unreg, rml_safe(c.l_unreg), "l_unreg");
            det |= put(m.l_reg, rml_safe(c.l_reg), "l_reg");
        }
        if ((det || ss.fresh) && d.el("det")) if (Rml::Element *ds = d.el("det")->QuerySelector(".ds")) ds->SetScrollTop(0);
        ss.fresh = false;
        if (c.sel != ss.last) {
            ss.last = c.sel;
            nav.root = d.el("main");
            nav.set(nullptr);
            // the cells are made in this frame's Context::Update on the first opening
            if (has) { std::string a = "c" + std::to_string(c.sel); if (Rml::Element *e = nav.find_act(a)) nav.set(e); else nav.want_act(a); }
            else if (!c.e.empty() && !nav.find_act("c0")) nav.want_act("c0"); // what ensure() takes once the cells exist
            if (nav.want.empty()) nav.ensure();
        }
        put(m.hint, "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("back") + " \xeb\x82\x98\xea\xb0\x80\xea\xb8\xb0</span>", "hint");
    }
    bool action(const std::string &a) override {
        uint32_t f = c.ptr;
        if (a.size() > 1 && a[0] == 'c' && isdigit((unsigned char)a[1])) {
            int i = atoi(a.c_str() + 1);
            if (i == c.sel && i < (int)c.e.size() && !c.complete && (c.e[i].owned || c.e[i].reg)) return action(c.e[i].reg ? "unreg" : "reg");
            state::post([f, i] { uint32_t il = M32(f + 0x60); if (il && i < (int16_t)M16(il + 0x30)) { g::call(0x1457a0, {il, (uint32_t)i}); g::call(0x1054f4, {f}); } });
            return true;
        }
        int s = c.sel;
        bool ok = s >= 0 && s < (int)c.e.size() && !c.complete;
        if (a == "reg" && ok && !c.e[s].reg && c.e[s].owned) { state::post([f] { g::call(0x1058d4, {f}); }); return true; }
        if (a == "unreg" && ok && c.e[s].reg) { state::post([f] { g::call(0x105490, {f}); }); return true; }
        if (a == "close") { back(); return true; }
        return false;
    }
    void back() override { state::post([] { state::key_tap(0x3a); }); }
};

static CollectionScreen g_collection;
}
