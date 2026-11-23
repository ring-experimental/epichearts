// Monster farm (replaces cFormMonsterFarm, form 0x1F; town function 5 -> script 205 -> ShowUIWnd(0x1F, mode)).
// Mode +0x13C: 0x1A capture status, 0x1B feeding, 0x1C hatching (title lang 0x26C + mode, RefreshInfo @00116b74);
// visible entries +0x140 (Finalize @001170fc: table entries whose unlock value +6 <= u16 world+0x14).
// Table: cResourceManager::GetCaptureInitInfo(i) @000dc704 (stride 0x1C: u16 +0 captures needed, u16 +2 food needed,
// u8 +7 / u16 +8 food item type / id, u16 +0xA hatch units, s16 +0xC monster ids); per-slot state world+0x4AC + 0x10 i
// (u8 +0 egg, u8 +1 fed, u16 +2 captured, s32 +0xC hatch timer in ticks, shown / 15 as seconds).
// Cell text as cFormMonsterFarm::fillInfo @00116628 builds it (lang 0x28A..0x28F); icons: set 0x50 frame
// {9,6,0,1,2,4,5,3,7,8}[i] (modes 0x1A/0x1B), item set 0x6B frame 0x6F egg / 0x7C (mode 0x1C).
// Actions go through the form's own key handler: select the list entry (cflUiSplitContainer::Select @00157158) and
// somethingKeypress(form, 0x30, 0, list) @00116f78 -> ShowMenu (feed: choice popup -> RemoveItem + FeedingCaptureMonster;
// hatch: yes/no notice -> the APK's offline hatch at 0x19c1d0 -> AddItem + ProcHatchEgg). Close: key 0x3A.
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"
#include <mutex>

namespace state {
struct FarmRow { std::string name, icon, status, food_icon; int captured = 0, need = 0, fed = 0, food = 0, timer = 0; bool egg = false; };
struct Farm { uint32_t ptr = 0; int mode = 0, sel = 0; std::string title, empty; std::vector<FarmRow> rows; };
static Farm g_farm_pub;
static std::mutex g_farm_mx;

static std::string lang(int id) { return g::plain_text(g::cstr(g::call(0x120aac, {(uint32_t)id}))); } // GetLangData @00130aac

void capture_farm(const Snapshot &s) {
    Farm fm;
    uint32_t f = 0;
    for (auto &fr : s.forms) if (fr.id == 0x1f) f = fr.ptr;
    if (f) {
        uint32_t rm = g::resmgr(), w = g::world();
        fm.ptr = f;
        fm.mode = (int32_t)M32(f + 0x13c);
        fm.title = lang(0x26c + fm.mode);
        fm.empty = lang(0x289);
        uint32_t list = M32(f + 0x9c);
        fm.sel = list ? M8(list + 0x21) : 0;
        int n = (int32_t)M32(f + 0x140);
        static const int FRAME[10] = {9, 6, 0, 1, 2, 4, 5, 3, 7, 8};
        for (int i = 0; i < n && i < 10 && rm && w; i++) {
            uint32_t info = g::call(0xcc704, {rm, (uint32_t)i});
            if (!info) continue;
            uint32_t sl = w + 0x4ac + 0x10 * i;
            FarmRow r;
            r.need = M16(info);
            r.food = M16(info + 2);
            r.captured = M16(sl + 2);
            r.fed = M8(sl + 1);
            r.egg = M8(sl) != 0;
            r.timer = (int32_t)M32(sl + 0xc) / 15;
            uint32_t mon = g::call(0xcc278, {rm, (uint32_t)(int16_t)M16(info + 0xc)}); // cResourceManager::GetMonInfo @000dc278
            if (mon) r.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(mon)));
            r.icon = fm.mode == 0x1c ? sprites::image(0x6b, r.egg ? 0x6f : 0x7c) : sprites::image(0x50, FRAME[i]);
            uint32_t food = g::call(0xcc3b4, {rm, M8(info + 7), M16(info + 8), 0xffffffffu});
            std::string food_name = food ? g::plain_text(g::cstr(M32(rm + 0x34) + M32(food))) : "";
            if (food) r.food_icon = sprites::image(0x6b, M8(food + 0x13), M8(food + 0x12));
            if (fm.mode == 0x1b) {
                if (r.captured < r.need) r.status = lang(0x28a);
                else if (!r.egg) r.status = Rml::CreateString("%s %d / %d", food_name.c_str(), r.fed, r.food);
                else r.status = lang(0x28b);
            } else if (fm.mode == 0x1c && r.egg) {
                r.status = lang(r.timer > 0 ? 0x28e : 0x28f);
            } else {
                r.status = lang(r.captured >= r.need ? 0x28d : 0x28c);
                while (!r.status.empty() && (r.status.back() == '(' || r.status.back() == ' ')) r.status.pop_back();
            }
            fm.rows.push_back(std::move(r));
        }
    }
    std::lock_guard<std::mutex> l(g_farm_mx);
    g_farm_pub = std::move(fm);
}
static Farm pull_farm() { std::lock_guard<std::mutex> l(g_farm_mx); return g_farm_pub; }
}

namespace screens {

class FarmScreen : public Screen {
    struct Session {
        int last_sel = -1;       // selection / form the focus was placed for
        uint32_t last_ptr = 0;
    } ss;
    state::Farm fm;              // per-frame copy of the captured form

    // data model "farm" (farm.rml), set in update()
    struct Row {
        std::string act, src, nm, pct, cap, st, go; // r<i>, icon, name (markup), bar width, "a / b", status (markup), action label
        bool sel = false, full = false;
        bool operator==(const Row &o) const {
            return act == o.act && src == o.src && nm == o.nm && pct == o.pct && cap == o.cap && st == o.st && go == o.go && sel == o.sel && full == o.full;
        }
    };
    struct View {
        std::vector<Row> rows;
        std::string kicker, title, empty, hint; // title, empty, hint: markup
    } m;

public:
    FarmScreen() { z = 22; state::set_ported(0x1f); register_screen(this); }
    const char *file() override { return "farm.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("farm");
        if (!c) return;
        if (auto h = c.RegisterStruct<Row>()) {
            h.RegisterMember("act", &Row::act); h.RegisterMember("src", &Row::src); h.RegisterMember("nm", &Row::nm); h.RegisterMember("pct", &Row::pct);
            h.RegisterMember("cap", &Row::cap); h.RegisterMember("st", &Row::st); h.RegisterMember("go", &Row::go);
            h.RegisterMember("sel", &Row::sel); h.RegisterMember("full", &Row::full);
        }
        c.RegisterArray<std::vector<Row>>();
        c.Bind("rows", &m.rows); c.Bind("kicker", &m.kicker); c.Bind("title", &m.title); c.Bind("empty", &m.empty); c.Bind("hint", &m.hint);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override {
        fm = state::pull_farm();
        bool top = !s.forms.empty() && (s.forms.back().id == 0x1f || dialog_over(s));
        return s.stripped && fm.ptr && top;
    }
    void on_show() override { ss = {}; }

    void update(const state::Snapshot &s) override {
        (void)s;
        static const char *KICK[3] = {"CAPTURE", "FEEDING", "HATCHING"};
        int pg = fm.mode - 0x1a;
        put(m.kicker, std::string(pg >= 0 && pg < 3 ? KICK[pg] : "FARM"), "kicker");
        put(m.title, rml_safe(fm.title), "title");
        std::vector<Row> rows;
        for (size_t i = 0; i < fm.rows.size(); i++) {
            const auto &r = fm.rows[i];
            bool full = r.captured >= r.need;
            Row w;
            w.act = Rml::CreateString("r%d", (int)i);
            w.sel = (int)i == fm.sel;
            w.full = full;
            w.src = sprite_src(r.icon);
            w.nm = rml_safe(r.name);
            // capture progress bar (every page): captured / needed
            w.pct = Rml::CreateString("%d%%", r.need > 0 ? std::min(100, r.captured * 100 / r.need) : 0);
            w.cap = Rml::CreateString("%d / %d", r.captured, r.need);
            if (fm.mode == 0x1b && full && !r.egg) w.st += "<img class=\"fi\" src=\"" + sprite_src(r.food_icon) + "\"/>";
            w.st += rml_safe(r.status);
            if (fm.mode == 0x1c && r.egg && r.timer > 0) w.st += Rml::CreateString(" <span class=\"tm\">%d:%02d:%02d</span>", r.timer / 3600, r.timer / 60 % 60, r.timer % 60);
            // what the entry can do now (cFormMonsterFarm::RefreshMenu @00116dd0: feed 0x1008, hatch 0x100A)
            bool can = (fm.mode == 0x1b && full && !r.egg) || (fm.mode == 0x1c && full && r.egg && r.timer <= 0);
            if (can) w.go = fm.mode == 0x1b ? "\xeb\xa8\xb9\xec\x9d\xb4 \xec\xa3\xbc\xea\xb8\xb0" : "\xeb\xb6\x80\xed\x99\x94"; // 먹이 주기 / 부화
            rows.push_back(std::move(w));
        }
        // rows going away this frame (Context::Update) must not keep the focus, nor get it from ensure() below
        bool shrink = rows.size() < m.rows.size();
        if (shrink && nav.focus) {
            std::string a = nav.focus->GetAttribute<Rml::String>("data-act", "");
            if (a.size() > 1 && a[0] == 'r' && isdigit((unsigned char)a[1]) && atoi(a.c_str() + 1) >= (int)rows.size()) nav.clear();
        }
        put(m.rows, std::move(rows), "rows");
        put(m.empty, rml_safe(fm.empty), "empty"); // shown when there are no rows
        if (fm.sel != ss.last_sel || fm.ptr != ss.last_ptr) {
            ss.last_sel = fm.sel; ss.last_ptr = fm.ptr;
            nav.root = d.el("main");
            nav.set(nullptr);
            // the rows are made in this frame's Context::Update on the first opening
            if (fm.sel >= 0 && fm.sel < (int)fm.rows.size()) {
                std::string a = "r" + std::to_string(fm.sel);
                if (Rml::Element *e = nav.find_act(a)) nav.set(e); else nav.want_act(a);
            } else if (!fm.rows.empty() && !nav.find_act("r0")) nav.want_act("r0"); // what ensure() takes once the rows exist
            if (nav.want.empty() && !shrink) nav.ensure();
        }
        put(m.hint, "<span>" + glyph("advance") + " \xec\x84\xa0\xed\x83\x9d</span><span>" + glyph("back") + " \xeb\x82\x98\xea\xb0\x80\xea\xb8\xb0</span>", "hint");
    }

    bool action(const std::string &a) override {
        uint32_t f = fm.ptr;
        if (a.size() > 1 && a[0] == 'r' && isdigit((unsigned char)a[1])) {
            int n = atoi(a.c_str() + 1);
            state::post([f, n] {
                uint32_t list = M32(f + 0x9c);
                if (!list) return;
                g::call(0x147158, {list, (uint32_t)n, 0});              // cflUiSplitContainer::Select @00157158
                g::call(0x106f78, {f, 0x30, 0, list});                   // cFormMonsterFarm::somethingKeypress @00116f78
            });
            return true;
        }
        if (a == "close") { back(); return true; }
        return false;
    }
    void back() override { state::post([] { state::key_tap(0x3a); }); } // KeyPreview @001165ec: 0x3A closes
};

static FarmScreen g_farm;
}
