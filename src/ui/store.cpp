// NPC shop (replaces cFormStore, form 0xD; opened by town / NPC scripts through cGameUi::ShowUIWnd @0011eea8).
// List: cStoreItemList at form+0xB0 (items +0x14, count +0x20, per-item type +0x18[i], image list +0x30,
// category +0x98), mode form+0x84 (9 buy, 10 sell) (cFormStore::initContents @0011eaf0).
// Buy items are tagITEM_INITINFO* (name +0, class +8, level +9, price +0xC, palette +0x12, icon +0x13),
// sell items are cInvenItem*. Actions: select = image list s16 +0x32, then cFormStore::doReadyBuying @0011dfd8 /
// doReadySelling @0011dee4 (they open the count popup and confirmation, drawn by dialogs.cpp);
// category = cStoreItemList::OnCateChange(list, mode, cat) @0011e678 (cFormStore::fillFilterMenu @0011dd04 order).
// The tutorial store (cFormTutorialStore, form 0xF3) is a cFormStore subclass with the same fields: mode 0x1F lists one
// item (cStoreItemList::Load @0011e840), no categories (initLayout hides the filter), title lang 0x243, flags +0xE8 bit 0
// = the required item was bought (its doCompleteBuying); closing goes through its own key handler on the close button
// (cFormTutorialStore::somethingKeypress @0011e35c: until bought a notice, lang 0x2BD; then SetDead + the script's
// callback +0xE0), not key 0x3A (cFormStore::KeyPreview would close it without the callback).
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include <mutex>

namespace state {
struct StoreRow { std::string name, name_rml, icon, type_name, desc; int price = 0, req_lv = 0, cls = -1, type = 0, count = 0; };
struct Store { uint32_t ptr = 0; int mode = 9, cat = 0, sel = -1; bool tutorial = false, bought = false; std::string title; std::vector<StoreRow> rows; };
static Store g_store_pub;
static std::mutex g_store_mx;

void capture_store(const Snapshot &s) {
    Store st;
    const Form *sf = nullptr;
    for (auto &fm : s.forms) if (fm.id == 0xd || fm.id == 0xf3) sf = &fm;
    if (sf) {
        uint32_t f = sf->ptr;
        st.ptr = f;
        st.tutorial = sf->id == 0xf3;
        if (st.tutorial) {
            st.bought = M32(f + 0xe8) & 1;
            st.title = g::plain_text(g::cstr(g::call(0x120aac, {0x243})));
        }
        st.mode = (int32_t)M32(f + 0x84);
        uint32_t L = M32(f + 0xb0);
        uint32_t IL = L ? M32(L + 0x30) : 0;
        uint32_t rm = g::resmgr();
        if (L && IL && rm) {
            st.cat = (int32_t)M32(L + 0x98);
            st.sel = (int16_t)M16(IL + 0x32);
            int n = (int16_t)M16(IL + 0x30);
            uint32_t arr = M32(L + 0x14), types = M32(L + 0x18);
            for (int i = 0; i < n && i < 200 && arr; i++) {
                uint32_t p = M32(arr + 4 * i);
                if (!p) continue;
                StoreRow r;
                uint32_t info = p;
                if (st.mode == 10) { // sell: cInvenItem
                    info = M32(p + 0x38);
                    r.name = g::plain_text(g::cstr(g::call(0xfed68, {p})));
                    r.name_rml = g::item_name_rml(p);
                    r.type_name = g::plain_text(g::cstr(g::call(0xfed38, {p})));
                    r.type = M16(p + 6);
                    r.count = (int32_t)M32(p + 0x3c);
                    r.price = info ? M16(info + 0xa) : 0;
                    if (M16(p + 4) & 0x100) r.price = 1; // cFormStore::doReadySelling: bound items sell for 1
                } else {
                    r.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info)));
                    r.name_rml = g::rml_text(r.name.c_str(), false);
                    r.type = types ? M8(types + i) : 0;
                    r.price = (int32_t)M32(info + 0xc);
                    r.count = (int)g::call(0x11ff0c, {g::inv(), (uint32_t)r.type, M16(M32(L + 0x1c) + 2 * i)}); // owned
                    uint32_t cn = M32(IMG + 0x199268 + 4 * (r.type + 1)); // _all_item_cate_names_str
                    r.type_name = g::plain_text(g::cstr(cn));
                }
                if (!info) continue;
                r.icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
                r.cls = r.type < 5 ? M8(info + 8) : -1;
                r.req_lv = M8(info + 9);
                if (i == st.sel) {
                    uint32_t ds = g::call(0xcd0d8, {rm, 0, M32(info + 4)});
                    if (ds) r.desc = g::rml_text(g::cstr(ds));
                    if (r.desc == "0") r.desc.clear();
                }
                st.rows.push_back(std::move(r));
            }
        }
    }
    std::lock_guard<std::mutex> l(g_store_mx);
    g_store_pub = std::move(st);
}
static Store pull_store() { std::lock_guard<std::mutex> l(g_store_mx); return g_store_pub; }
}

namespace screens {

class StoreScreen : public Screen {
    struct Session {             // selection, category and mode are the original's: only the focus starts per opening
        bool fresh = true;       // first update of this opening: on the first opening the lists are not made yet
    } ss;
    state::Store st;             // per-frame copy of the captured form

    // data model "store" (store.rml), set in update()
    struct Cat {
        std::string act, t;      // cat<c>, label
        bool on = false;
        bool operator==(const Cat &o) const { return act == o.act && t == o.t && on == o.on; }
    };
    struct Row {
        std::string act, src, nm, ty, pr, cnt; // r<i>, icon, name / type (markup), price, "x<n>" (sell only)
        bool sel = false, bad = false;
        bool operator==(const Row &o) const { return act == o.act && src == o.src && nm == o.nm && ty == o.ty && pr == o.pr && cnt == o.cnt && sel == o.sel && bad == o.bad; }
    };
    struct View {
        std::string title, kicker, gold, gl, gr, hint; // title, gl / gr (tab key glyphs), hint: markup
        std::vector<Cat> cats;
        std::vector<Row> rows;
        bool has = false;        // a row is selected: the detail column
        std::string icon, dn, dt, pk, price, ds, deal; // dn, dt, ds: markup
        int lv = 0, cnt = 0;
    } m;

public:
    StoreScreen() { z = 22; state::set_ported(0xd); state::set_ported(0xf3); register_screen(this); }
    const char *file() override { return "store.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("store");
        if (!c) return;
        if (auto h = c.RegisterStruct<Cat>()) { h.RegisterMember("act", &Cat::act); h.RegisterMember("t", &Cat::t); h.RegisterMember("on", &Cat::on); }
        c.RegisterArray<std::vector<Cat>>();
        if (auto h = c.RegisterStruct<Row>()) {
            h.RegisterMember("act", &Row::act); h.RegisterMember("src", &Row::src); h.RegisterMember("nm", &Row::nm); h.RegisterMember("ty", &Row::ty);
            h.RegisterMember("pr", &Row::pr); h.RegisterMember("cnt", &Row::cnt); h.RegisterMember("sel", &Row::sel); h.RegisterMember("bad", &Row::bad);
        }
        c.RegisterArray<std::vector<Row>>();
        c.Bind("title", &m.title); c.Bind("kicker", &m.kicker); c.Bind("gold", &m.gold); c.Bind("gl", &m.gl); c.Bind("gr", &m.gr); c.Bind("hint", &m.hint);
        c.Bind("cats", &m.cats); c.Bind("rows", &m.rows); c.Bind("has", &m.has); c.Bind("icon", &m.icon); c.Bind("dn", &m.dn); c.Bind("dt", &m.dt);
        c.Bind("pk", &m.pk); c.Bind("price", &m.price); c.Bind("ds", &m.ds); c.Bind("deal", &m.deal); c.Bind("lv", &m.lv); c.Bind("cnt", &m.cnt);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        st = state::pull_store();
        bool top = !s.forms.empty() && (s.forms.back().id == 0xd || s.forms.back().id == 0xf3);
        return s.stripped && st.ptr && (top || dialog_over(s)); // stays up under its count / confirm / notice popups
    }
    void on_show() override { ss = {}; nav.root = d.doc; nav.set(nullptr); }

    void update(const state::Snapshot &s) override {
        bool buy = st.mode != 10;
        put(m.title, st.tutorial ? rml_safe(st.title) : std::string(buy ? "구매" : "판매"), "title");
        put(m.kicker, std::string(st.tutorial ? (st.bought ? "TUTORIAL · 완료" : "TUTORIAL · 구매해 보세요") : buy ? "SHOP · BUY" : "SHOP · SELL"), "kicker");
        put(m.gold, num(s.gold), "gold");
        static const char *CB[4] = {"전체", "소비", "장비", "재료"}, *CS[4] = {"전체", "장비", "소비", "재료"};
        // the tab key glyphs around the categories (none when the device shows no key for them, or the tutorial)
        put(m.gl, st.tutorial ? std::string() : glyph("tabl"), "gl");
        put(m.gr, st.tutorial ? std::string() : glyph("tabr"), "gr");
        std::vector<Cat> cats;
        for (int c = 0; c < 4 && !st.tutorial; c++) cats.push_back({Rml::CreateString("cat%d", c), buy ? CB[c] : CS[c], c == st.cat});
        put(m.cats, std::move(cats), "cats");
        std::vector<Row> rows;
        for (size_t i = 0; i < st.rows.size(); i++) {
            const auto &r = st.rows[i];
            bool bad = (r.cls >= 0 && r.cls != s.job) || r.req_lv > s.level;
            rows.push_back({Rml::CreateString("r%d", (int)i), sprite_src(r.icon), r.name_rml,
                            rml_safe(r.type_name) + (r.req_lv > 1 ? Rml::CreateString(" · Lv %d", r.req_lv) : std::string()), num(r.price),
                            !buy && r.count > 1 ? Rml::CreateString("x%d", r.count) : std::string(), (int)i == st.sel, bad && buy});
        }
        // rows going away this frame (Context::Update) must not keep the focus, nor get it from ensure() below
        bool shrink = rows.size() < m.rows.size();
        if (shrink && nav.focus) {
            std::string a = nav.focus->GetAttribute<Rml::String>("data-act", "");
            if (a.size() > 1 && a[0] == 'r' && isdigit((unsigned char)a[1]) && atoi(a.c_str() + 1) >= (int)rows.size()) nav.clear();
        }
        put(m.rows, std::move(rows), "rows");
        bool has = st.sel >= 0 && st.sel < (int)st.rows.size();
        // the detail column was rebuilt (its text box back at the top) whenever anything in it changed
        bool det = put(m.has, has, "has");
        if (has) {
            const auto &r = st.rows[st.sel];
            det |= put(m.icon, sprite_src(r.icon), "icon");
            det |= put(m.dn, r.name_rml, "dn");
            det |= put(m.dt, rml_safe(r.type_name), "dt");
            det |= put(m.pk, std::string(buy ? "가격" : "판매가"), "pk");
            det |= put(m.price, num(r.price), "price");
            det |= put(m.lv, r.req_lv, "lv");
            det |= put(m.cnt, r.count, "cnt");
            det |= put(m.ds, rml_safe(r.desc), "ds");
            det |= put(m.deal, std::string(buy ? "구매하기" : "판매하기"), "deal");
        }
        if ((det || ss.fresh) && d.el("det")) if (Rml::Element *ds = d.el("det")->QuerySelector(".ds")) ds->SetScrollTop(0);
        put(m.hint, "<span>" + glyph("advance") + " 선택</span><span>" + glyph("back") + " 나가기</span>", "hint");
        // the first focusable item once this frame's Context::Update has made the lists
        if (!ss.fresh && !shrink) nav.ensure();
        ss.fresh = false;
    }

    bool action(const std::string &a) override {
        uint32_t f = st.ptr;
        int mode = st.mode;
        if (a.rfind("cat", 0) == 0) {
            int c = atoi(a.c_str() + 3);
            state::post([f, mode, c] { uint32_t L = M32(f + 0xb0); if (L) g::call(0x10e678, {L, (uint32_t)mode, (uint32_t)c}); });
            return true;
        }
        if (a[0] == 'r' && isdigit((unsigned char)a[1])) {
            int i = atoi(a.c_str() + 1);
            if (i == st.sel) return action("deal"); // second click on the selected row = buy / sell
            state::post([f, i] { uint32_t L = M32(f + 0xb0), IL = L ? M32(L + 0x30) : 0; if (IL && i < (int16_t)M16(IL + 0x30)) M16(IL + 0x32) = (uint16_t)i; });
            return true;
        }
        if (a == "deal") {
            state::post([f, mode] {
                uint32_t L = M32(f + 0xb0), IL = L ? M32(L + 0x30) : 0;
                if (!IL) return;
                int sel = (int16_t)M16(IL + 0x32), n = (int16_t)M16(IL + 0x30);
                if (sel < 0 || sel >= n) return;
                g::call(mode == 10 ? 0x10dee4 : 0x10dfd8, {f});
            });
            return true;
        }
        if (a == "close") { back(); return true; }
        return false;
    }
    // LB / RB (the mapped tab keys): the category row (cStoreItemList::OnCateChange as a click on it)
    void tab(int dir) override { if (st.tutorial) return; action(Rml::CreateString("cat%d", (st.cat + dir + 4) % 4)); }
    // closing: key 0x3A on the focused store form (cFormStore::somethingKeypress @0011e3fc / decideClosing @0011de9c)
    void back() override {
        uint32_t f = st.ptr;
        if (st.tutorial) { state::post([f] { g::call(0x10e35c, {f, 0x30, 0, M32(f + 0x50)}); }); return; } // the gated close
        state::post([] { state::key_tap(0x3a); });
    }
};

static StoreScreen g_store;
}
