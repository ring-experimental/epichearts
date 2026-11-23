// Special books at the inn (replaces cFormSkillBook, form 0xE, opened by a script: ShowUIWnd(0xE) -> initContents
// @001182cc), and its page popup (cFormPopup kind 6: cFormSkillBook::doInvestOrb @001179d8 -> Finalize_Page @000f1190,
// FillPageDesc @000ede1c).
// Books: cStudyBook singleton (GetInstance @00131c88): +4 current book, +8[] current page per book, bitsets +0x48 owned /
// +0x50 mastered, book count +0x298, table +0x29C (stride 0x10: +0 title offset in the res+0x34 string pool, +4 summary
// sub-desc id (GetSubDescString(2, id)), u8 +10 page count); the current book's pages +0x2A4 (stride 0x18: +0 text id,
// +0x14 bit 1 text hidden (lang 0x20F), +0x16 orb kind, +0x17 orbs needed). Status text: lang 0x298 not owned / 0x297
// reading / 0x296 mastered (ilsItem_SelectionChanged @001180c0). The grid icon is item image 8 (cSkillBookItemList::
// DrawIcon @00118000). Orbs held: item type 8 id 0x1B / 0x1C (FillPageDesc).
// Driving: selection = image list s16 +0x32 of the list +0xAC/+0x30 and somethingChange @00117af4 (offers 0x801 for an
// owned book); open = doInvestOrb(form); page browse = cStudyBook::BrowsePage @001319a4 + cFormPopup::RefreshPage
// @000edff0; invest = the popup's main button +0x10C through cFormPopup::somethingKeypress @000f2ed8 (it checks and asks
// with a notice, then cFormSkillBook::doCompleteInvestingOrb @001174f4 grants the page); close = key 0x3A.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include <mutex>

namespace state {
struct Book { std::string name, summary; int pages = 0; bool owned = false, mastered = false, current = false; };
struct BookPage { int page = 0, pages = 0, invested = 0, need = 0, kind = 0, have0 = 0, have1 = 0; bool mastered = false; std::string text, orb, orb_icon, name0, name1; };
struct SkillBook {
    uint32_t form = 0, popup = 0;
    int sel = -1;
    std::string icon, st_none, st_read, st_done;
    std::vector<Book> books;
    BookPage pg;
};
static SkillBook g_sb_pub;
static std::mutex g_sb_mx;

void capture_skillbook(const Snapshot &s) {
    SkillBook b;
    for (auto &f : s.forms) if (f.id == 0xe) b.form = f.ptr;
    if (b.form && !s.forms.empty() && s.forms.back().id == 0xf && (int32_t)M32(s.forms.back().ptr + 0x168) == 6) b.popup = s.forms.back().ptr;
    uint32_t sb = b.form ? g::call(0x121c88, {}) : 0; // cStudyBook::GetInstance @00131c88
    uint32_t rm = g::resmgr();
    if (sb && rm) {
        static std::string st[3];
        if (st[0].empty())
            for (int i = 0; i < 3; i++) st[i] = g::plain_text(g::cstr(g::call(0x120aac, {(uint32_t)(0x298 - i)}))); // GetLangData
        b.st_none = st[0]; b.st_read = st[1]; b.st_done = st[2];
        b.icon = sprites::image(0x6b, 8);
        int n = (int32_t)M32(sb + 0x298);
        uint32_t tab = M32(sb + 0x29c);
        for (int i = 0; i < n && i < 64 && tab; i++) {
            Book k;
            uint32_t e = tab + 0x10 * i;
            k.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(e)));
            k.summary = g::rml_text(g::cstr(g::call(0xcd0d8, {rm, 2, M32(e + 4)})));
            k.pages = M8(e + 10);
            k.owned = (M32(sb + 0x48 + 4 * (i >> 5)) >> (i & 31)) & 1;
            k.mastered = (M32(sb + 0x50 + 4 * (i >> 5)) >> (i & 31)) & 1;
            k.current = (int32_t)M32(sb + 4) == i;
            b.books.push_back(std::move(k));
        }
        uint32_t L = M32(b.form + 0xac), IL = L ? M32(L + 0x30) : 0;
        b.sel = IL ? (int16_t)M16(IL + 0x32) : -1;
        if (b.popup) {
            int cur = (int32_t)M32(sb + 4);
            BookPage &p = b.pg;
            p.page = cur >= 0 ? M8(sb + 8 + cur) : 0;
            p.pages = (int32_t)M32(sb + 0x2a0);
            uint32_t rec = M32(sb + 0x2a4) ? M32(sb + 0x2a4) + 0x18 * p.page : 0;
            p.mastered = g::call(0x121a28, {sb}) != 0;               // cStudyBook::BeenCurPageMastered @00131a28
            p.invested = (int32_t)g::call(0x121ac8, {sb});           // GetCurrentPageInvestOrbCount @00131ac8
            if (rec) {
                p.text = (M8(rec + 0x14) & 2) ? g::rml_text(g::cstr(g::call(0x120aac, {0x20f}))) : g::rml_text(g::cstr(g::call(0xcd0d8, {rm, 2, M32(rec)})));
                p.kind = M8(rec + 0x16);
                p.need = M8(rec + 0x17);
                // UNVERIFIED: the orb of a page = item type 8 id 0x1B + kind (FillPageDesc counts ids 0x1B / 0x1C)
                uint32_t info = g::call(0xcc3b4, {rm, 8, (uint32_t)(0x1b + p.kind), 0});
                if (info) { p.orb = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info))); p.orb_icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12)); }
            }
            uint32_t inv = g::inv();
            p.have0 = inv ? (int32_t)g::call(0x11ff0c, {inv, 8, 0x1b}) : 0; // cInventory::GetDuplicateItemCount @0012ff0c
            p.have1 = inv ? (int32_t)g::call(0x11ff0c, {inv, 8, 0x1c}) : 0;
            for (int k = 0; k < 2; k++) {
                uint32_t oi = g::call(0xcc3b4, {rm, 8, (uint32_t)(0x1b + k), 0});
                (k ? p.name1 : p.name0) = oi ? g::plain_text(g::cstr(M32(rm + 0x34) + M32(oi))) : "";
            }
        }
    }
    std::lock_guard<std::mutex> l(g_sb_mx);
    g_sb_pub = std::move(b);
}
static SkillBook pull_skillbook() { std::lock_guard<std::mutex> l(g_sb_mx); return g_sb_pub; }
}

namespace screens {

class SkillBookScreen : public Screen {
    struct Session {
        int last_sel = -2;       // selection / page the focus was placed for
        bool last_page = false;
        bool fresh = true;       // first update of this opening (the detail column was made anew on every opening)
    } ss;
    state::SkillBook sb;         // per-frame copy of the captured form

    // data model "skillbook" (skillbook.rml), set in update(). The page buttons' `off` stays with Doc::cls: the focus
    // placed in the same update() (nav.ensure) must see it at once
    struct Cell {
        std::string act, src;    // b<i>, the book icon
        bool sel = false, no = false, done = false, cur = false; // no: not owned; tags: mastered / the current book
        bool operator==(const Cell &o) const { return act == o.act && src == o.src && sel == o.sel && no == o.no && done == o.done && cur == o.cur; }
    };
    struct View {
        std::vector<Cell> grid;
        bool page = false;       // the page popup is up
        bool has = false;        // a book is selected: the detail column
        std::string icon, dn, st, pc, ds; // dn, st, ds: markup
        int st_k = 0;            // 0 s-no, 1 s-done, 2 s-read
        bool owned = false;
        std::string ptitle, ptext, porb, hint; // markup
    } m;

public:
    SkillBookScreen() {
        z = 23;
        state::set_ported(0xe);
        register_screen(this);
    }
    const char *file() override { return "skillbook.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("skillbook");
        if (!c) return;
        if (auto h = c.RegisterStruct<Cell>()) {
            h.RegisterMember("act", &Cell::act); h.RegisterMember("src", &Cell::src); h.RegisterMember("sel", &Cell::sel);
            h.RegisterMember("no", &Cell::no); h.RegisterMember("done", &Cell::done); h.RegisterMember("cur", &Cell::cur);
        }
        c.RegisterArray<std::vector<Cell>>();
        c.Bind("grid", &m.grid); c.Bind("page", &m.page); c.Bind("has", &m.has); c.Bind("icon", &m.icon); c.Bind("dn", &m.dn);
        c.Bind("st", &m.st); c.Bind("pc", &m.pc); c.Bind("ds", &m.ds); c.Bind("st_k", &m.st_k); c.Bind("owned", &m.owned);
        c.Bind("ptitle", &m.ptitle); c.Bind("ptext", &m.ptext); c.Bind("porb", &m.porb); c.Bind("hint", &m.hint);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        sb = state::pull_skillbook();
        bool top = !s.forms.empty() && (s.forms.back().id == 0xe || s.forms.back().ptr == sb.popup || dialog_over(s));
        return s.stripped && sb.form && top;
    }
    void on_show() override { ss = {}; nav.root = d.el("main"); nav.set(nullptr); }

    void update(const state::Snapshot &s) override {
        (void)s;
        bool page = sb.popup != 0;
        put(m.page, page, "page");
        std::vector<Cell> g;
        for (size_t i = 0; i < sb.books.size(); i++) {
            const auto &k = sb.books[i];
            g.push_back({Rml::CreateString("b%d", (int)i), sprite_src(sb.icon), (int)i == sb.sel, !k.owned, k.mastered, !k.mastered && k.current && k.owned});
        }
        put(m.grid, std::move(g), "grid");
        bool has = sb.sel >= 0 && sb.sel < (int)sb.books.size();
        // the detail column was rebuilt (its text box back at the top) whenever anything in it changed
        bool det = put(m.has, has, "has");
        if (has) {
            const auto &k = sb.books[sb.sel];
            const std::string &st = !k.owned ? sb.st_none : k.mastered ? sb.st_done : sb.st_read;
            det |= put(m.icon, sprite_src(sb.icon), "icon");
            det |= put(m.dn, rml_safe(k.name), "dn");
            det |= put(m.st_k, !k.owned ? 0 : k.mastered ? 1 : 2, "st_k");
            det |= put(m.st, rml_safe(st), "st");
            det |= put(m.pc, Rml::CreateString("%d\xec\x9e\xa5", k.pages), "pc"); // N장
            det |= put(m.ds, rml_safe(k.summary), "ds");
            det |= put(m.owned, k.owned, "owned");
        }
        if ((det || ss.fresh) && d.el("det")) if (Rml::Element *ds = d.el("det")->QuerySelector(".ds")) ds->SetScrollTop(0);
        ss.fresh = false;

        if (page) {
            const auto &p = sb.pg;
            std::string pt = Rml::CreateString("<span class=\"pn\">%d / %d</span>", p.page + 1, p.pages);
            if (sb.sel >= 0 && sb.sel < (int)sb.books.size()) pt = rml_safe(sb.books[sb.sel].name) + pt;
            put(m.ptitle, pt, "ptitle");
            put(m.ptext, rml_safe(p.text), "ptext");
            std::string o;
            if (p.mastered) o = "<div class=\"mastered\"><span class=\"ic\">&#xe06c;</span> \xec\x9d\xb4 \xec\x9e\xa5\xec\x9d\x84 \xeb\xaa\xa8\xeb\x91\x90 \xec\x9d\xbd\xec\x97\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4</div>"; // 이 장을 모두 읽었습니다
            else {
                o = "<div class=\"orb\"><img src=\"" + sprite_src(p.orb_icon) + "\"/><span class=\"on\">" + rml_safe(p.orb) + "</span>";
                o += Rml::CreateString("<span class=\"prog\"><span class=\"v\">%d</span> / %d</span></div>", p.invested, p.need);
                int pct = p.need > 0 ? std::min(100, p.invested * 100 / p.need) : 0;
                o += Rml::CreateString("<div class=\"bar\"><div class=\"fill\" style=\"width:%d%%\"/></div>", pct);
            }
            o += Rml::CreateString("<div class=\"have\">\xeb\xb3\xb4\xec\x9c\xa0 %s %d \xc2\xb7 %s %d</div>", rml_safe(p.name0).c_str(), p.have0, rml_safe(p.name1).c_str(), p.have1); // 보유
            put(m.porb, o, "porb");
            d.cls("pprev", "off", p.page <= 0);
            d.cls("pnext", "off", p.page + 1 >= p.pages);
            d.cls("pinv", "off", p.mastered);
        }
        int key = page ? 1000 + sb.pg.page : sb.sel;
        if (key != ss.last_sel || page != ss.last_page) {
            ss.last_sel = key; ss.last_page = page;
            nav.root = d.el(page ? "pbox" : "main");
            nav.set(nullptr);
            if (page) nav.set(d.el(sb.pg.mastered ? "pnext" : "pinv"));
            else if (has) {
                // the cells are made in this frame's Context::Update on the first opening
                std::string a = "b" + std::to_string(sb.sel);
                if (Rml::Element *e = nav.find_act(a)) nav.set(e); else nav.want_act(a);
            } else if (!sb.books.empty() && !nav.find_act("b0")) nav.want_act("b0"); // what ensure() takes once the cells exist
            if (nav.want.empty()) nav.ensure();
        }
        put(m.hint, "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("back") + " \xeb\x8b\xab\xea\xb8\xb0</span>", "hint");
    }

    bool action(const std::string &a) override {
        uint32_t f = sb.form, pp = sb.popup;
        if (a.size() > 1 && a[0] == 'b' && isdigit((unsigned char)a[1])) {
            int i = atoi(a.c_str() + 1);
            if (i == sb.sel && i < (int)sb.books.size() && sb.books[i].owned) return action("open");
            state::post([f, i] {
                uint32_t L = M32(f + 0xac), IL = L ? M32(L + 0x30) : 0;
                if (!IL || i >= (int16_t)M16(IL + 0x30)) return;
                M16(IL + 0x32) = (uint16_t)i;
                g::call(0x107af4, {f});                            // cFormSkillBook::somethingChange @00117af4
            });
            return true;
        }
        if (a == "open") { state::post([f] { g::call(0x1079d8, {f}); }); return true; } // doInvestOrb @001179d8
        if (!pp) return a == "close" ? (back(), true) : false;
        if (a == "pprev" || a == "pnext") {
            int dir = a == "pnext" ? 1 : -1;
            state::post([pp, dir] {
                uint32_t sbk = g::call(0x121c88, {});
                if (!sbk) return;
                g::call(0x1219a4, {sbk, (uint32_t)dir});           // cStudyBook::BrowsePage @001319a4
                g::call(0xddff0, {pp});                            // cFormPopup::RefreshPage @000edff0
            });
            return true;
        }
        // invest = key 0x30 on the popup's main button +0x10C: somethingKeypress @000f2ed8 -> +0x168 != 0, kind +0x16C == 3
        // -> 0xf367c (orb count, GetCurrentPageInvestOrbCount, confirm notice 0x20B -> doCompleteInvestingOrb). +0x98 is not it
        if (a == "pinv") { state::post([pp] { g::call(0xe2ed8, {pp, 0x30, 0, M32(pp + 0x10c)}); }); return true; }
        if (a == "pclose") { back(); return true; }
        return false;
    }
    void back() override { state::post([] { state::key_tap(0x3a); }); } // page popup / book list: key 0x3A closes (KeyPreview)
    bool on_dir(int dir) override {
        if (sb.popup && dir >= 2) { action(dir == 2 ? "pprev" : "pnext"); return true; }
        return false;
    }
};

static SkillBookScreen g_skillbook;
}
