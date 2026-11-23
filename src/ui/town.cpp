// Town services (replaces cFormTownFunc, form 0x31, opened by the HUD town button: cGameUi::ShowTownFunc @0011f4b8).
// Functions 0..5 run the original handler cFormTownFunc::funcSelectHandler @000fc64c (FireScript(200 + i));
// function 6 (교역소, online trade hub: cFormTrade over the network) is not offered. NPC art comes from the
// original's recorded draws inside each function cell. Labels are the texts of the original's label images.
#include "autoform.h"
#include "game.h"
#include "screen_util.h"
#include "screens.h"
#include "sprites.h"

namespace screens {

static const char *FN_T[6] = {"여관", "상점", "대장장이", "연금술사", "제조도안", "농장"};
static const char *FN_D[6] = {"회복 · 스킬북", "장비 · 소비품 구매", "장비 제련 · 강화", "장비 스캔 · 변환", "아이템 제조", "몬스터 사육"};
static const char *FN_I[6] = {"&#xe0f5;", "&#xe3e8;", "&#xe0ec;", "&#xe242;", "&#xe35b;", "&#xe0d2;"};

class TownScreen : public Screen {
    struct Session {
        bool built = false;      // the function cards are built and focused
    } ss;
    uint32_t ptr = 0;            // per-frame mirrors of the snapshot
    uint32_t btn[6] = {};

    // data model "town" (town.rml), set in update()
    struct Card {
        std::string fn, npc, ic, t, d; // ids fn<i> / npc<i>, icon entity (markup), name, line
        std::string src, w = "auto", h = "auto"; // NPC art: sprite and size
        bool operator==(const Card &o) const { return fn == o.fn && npc == o.npc && ic == o.ic && t == o.t && d == o.d && src == o.src && w == o.w && h == o.h; }
    };
    struct View {
        std::vector<Card> cards;
        std::string tname, hint; // markup
    } m;

public:
    TownScreen() { z = 22; state::set_ported(0x31); register_screen(this); }
    const char *file() override { return "town.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("town");
        if (!c) return;
        if (auto h = c.RegisterStruct<Card>()) {
            h.RegisterMember("fn", &Card::fn); h.RegisterMember("npc", &Card::npc); h.RegisterMember("ic", &Card::ic); h.RegisterMember("t", &Card::t);
            h.RegisterMember("d", &Card::d); h.RegisterMember("src", &Card::src); h.RegisterMember("w", &Card::w); h.RegisterMember("h", &Card::h);
        }
        c.RegisterArray<std::vector<Card>>();
        c.Bind("cards", &m.cards); c.Bind("tname", &m.tname); c.Bind("hint", &m.hint);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { return s.stripped && s.townsvc.ptr && !s.forms.empty() && s.forms.back().id == 0x31; }
    void on_show() override { ss = {}; }

    void update(const state::Snapshot &s) override {
        const state::Town &t = s.townsvc;
        ptr = t.ptr;
        for (int i = 0; i < 6; i++) btn[i] = t.button[i];
        put(m.tname, rml_safe(s.field_name), "tname");
        std::vector<Card> cards = m.cards;
        if (!ss.built) {
            // every opening starts from cards without NPC art (the cards were made anew) and the focus on the first
            cards.clear();
            for (int i = 0; i < 6; i++)
                cards.push_back({Rml::CreateString("fn%d", i), Rml::CreateString("npc%d", i), FN_I[i], FN_T[i], FN_D[i]});
            ss.built = true;
            nav.root = d.doc;
            nav.clear();
            if (Rml::Element *e = nav.find_act("fn0")) nav.set(e); else nav.want_act("fn0"); // first opening: made in this frame's Context::Update
        }
        // NPC art: the largest recorded sprite inside each original cell that is narrow and tall (labels are wide)
        const autoform::Frame &fr = recorded();
        int fidx = -1;
        for (size_t i = 0; i < fr.forms.size(); i++) if (fr.forms[i].ptr == ptr) fidx = (int)i;
        for (int i = 0; i < 6; i++) {
            const int *rc = t.rect[i];
            std::string best;
            int ba = 0, bw = 0, bh = 0;
            for (auto &cm : fr.cmds) {
                if (cm.form != fidx || cm.key.empty()) continue;
                int ox, oy, sw, sh;
                if (!sprites::info(cm.key, &ox, &oy, &sw, &sh)) continue;
                int cx = cm.x + ox + sw / 2, cy = cm.y + oy + sh / 2;
                if (cx < rc[0] || cy < rc[1] || cx > rc[0] + rc[2] || cy > rc[1] + rc[3]) continue;
                if (sw > rc[2] / 3 || sh < rc[3] / 2) continue;
                if (sw * sh > ba) { ba = sw * sh; best = cm.key; bw = sw; bh = sh; }
            }
            if (best.empty()) continue;  // keeps what an earlier frame found
            float k = std::floor(std::min(100.f / bh, 110.f / bw) * 2) / 2; // 116 dp cards (town.rml)
            cards[i].src = sprite_src(best);
            cards[i].w = Rml::CreateString("%.0fdp", bw * k);
            cards[i].h = Rml::CreateString("%.0fdp", bh * k);
        }
        put(m.cards, std::move(cards), "cards");
        put(m.hint, "<span>" + glyph("advance") + " 선택</span><span>" + glyph("back") + " 닫기</span>", "hint");
    }

    bool action(const std::string &a) override {
        uint32_t f = ptr;
        if (a.rfind("fn", 0) == 0) {
            int i = a[2] - '0';
            uint32_t b = btn[i];
            if (b) state::post([f, b] { g::call(0xec64c, {f, 0x30, 0, b}); });
            return true;
        }
        // cFormTownFunc::somethingKeypress @000fc5d0 with the map button (+0x58)
        if (a == "tw_map") { state::post([f] { g::call(0xec5d0, {f, 0x30, 0, M32(f + 0x58)}); }); return true; }
        if (a == "tw_close") { back(); return true; }
        return false;
    }
    void back() override { state::post([] { state::key_tap(0x3a); }); }
};

static TownScreen g_town;
}
