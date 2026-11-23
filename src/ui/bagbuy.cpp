// Bag expansion purchase, paid with gold. The original sells backpack slots for Harz (paid currency) through its
// server: cGameWorld key 0x32 (the '+' of a full bag) -> cFormResurrect state 5 (form 0x14, request 0xE0 lists the
// price +0x10C and the slot count +0x110), SackBuy @000facdc -> UserHasConfirmedBuyBackPack @000fb920 -> request 0xDC;
// the 0xDD answer adds the slots with cInventory::IncreaseInvenSlot @0012f930 (inv+0x1C, limit inv+0x18 = 294,
// cBackPack::IncreaseSlot @0012f7a0), saved with the character like any capacity.
// Price and slot count came only from the server.
// UNVERIFIED: SLOTS / HARZ are port design values (the original's net warehouse text, lang 0x354, sells 6 slots at a
// time); the gold price follows the item shop's Harz rate (cash::harz_to_gold).
#include "cashshop.h"
#include "game.h"
#include "screen_util.h"
#include "screens.h"

namespace screens {

static const int SLOTS = 6, HARZ = 30;
static bool g_open;
void open_bagbuy() { g_open = true; }

class BagBuyScreen : public Screen {
    struct Session {
        int done = 0;            // 0 asking, 1 bought (capacity shown until the snapshot has it), 2 failed
        std::string drawn;
    } ss;
    // (g_open, file scope: the open request of open_bagbuy, made before the screen shows)
    // data model "bagbuy" (bagbuy.rml), set in update() only
    struct View {
        std::string title, body, close; // body: markup (data-rml); close: the single close button's label
        bool ask = false;               // the offer: cancel + buy (buy off without the gold)
        bool afford = false;
    } m;

public:
    BagBuyScreen() { z = 52; register_screen(this); }
    const char *file() override { return "bagbuy.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("bagbuy");
        if (!c) return;
        c.Bind("title", &m.title); c.Bind("body", &m.body); c.Bind("close", &m.close); c.Bind("ask", &m.ask); c.Bind("afford", &m.afford);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { if (!s.stripped || !s.in_field) g_open = false; return g_open; }
    void on_show() override { ss = {}; }
    void on_hide() override { g_open = false; }

    void update(const state::Snapshot &s) override {
        int price = cash::harz_to_gold(HARZ);
        bool maxed = s.bag_cap >= s.bag_max;
        bool afford = s.gold >= price;
        std::string key = Rml::CreateString("%d/%d/%d/%d", ss.done, s.bag_cap, (int)afford, (int)maxed);
        if (key == ss.drawn) return;
        ss.drawn = key;
        std::string title, body, close;
        bool ask = false;
        if (ss.done == 1) {
            title = Rml::CreateString("가방이 %d칸이 되었습니다", s.bag_cap);
            body = Rml::CreateString("%s G를 사용했습니다.", num(price).c_str());
            close = "확인";
        } else if (maxed) {
            title = "가방 확장";
            body = "더 이상 인벤을 확장할 수 없습니다."; // lang 0x92
            close = "닫기";
        } else {
            title = Rml::CreateString("가방을 %d칸 넓힙니다", SLOTS);
            body = Rml::CreateString("현재 %d칸 → %d칸 (최대 %d칸)<br/>가격 <span style=\"color:#ffd166\">%s G</span> · 보유 %s G%s", s.bag_cap,
                                     std::min(s.bag_cap + SLOTS, s.bag_max), s.bag_max, num(price).c_str(), num(s.gold).c_str(),
                                     afford ? "" : "<br/><span style=\"color:#ff7d93\">골드가 부족합니다</span>");
            ask = true;
        }
        put(m.title, title, "title");
        put(m.body, body, "body");
        put(m.close, close, "close");
        put(m.ask, ask, "ask");
        put(m.afford, afford, "afford");
        // the buttons used to be rebuilt with each change: the focus goes back to the last one shown. #btns holds
        // cancel, the single close (primary) and buy, each shown by data-if: one close element per look, so opening in
        // the other state does not fade a class (the .btn transition)
        nav.root = d.el("box");
        nav.set(nullptr);
        Rml::Element *b = d.el("btns");
        if (b && b->GetNumChildren() == 3) nav.set(b->GetChild(ask ? 2 : 1));
    }

    bool action(const std::string &a) override {
        if (a == "bb_close") { g_open = false; return true; }
        if (a == "bb_buy" && ss.done == 0) {
            const state::Snapshot &s = state::get();
            if (s.gold < cash::harz_to_gold(HARZ) || s.bag_cap >= s.bag_max) return true;
            state::post([] {
                uint32_t inv = g::inv();
                if (!inv || M32(inv + 0x1c) >= M32(inv + 0x18)) return;
                if (!g::call(0x11fb44, {inv, (uint32_t)cash::harz_to_gold(HARZ)})) return; // cInventory::SubGold @0012fb44
                g::call(0x11f930, {inv, (uint32_t)SLOTS});                                 // cInventory::IncreaseInvenSlot @0012f930
            });
            ss.done = 1;
            return true;
        }
        return false;
    }
    void back() override { g_open = false; }
};

static BagBuyScreen g_bagbuy;
}
