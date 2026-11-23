// QBox draw (replaces the look of cFormPopup kind 8, opened by OpenQbox @0010cf74 from the bag's "열기", action 0x46).
// The original keeps running the draw: cFormPopup::formActivated -> QboxInit @000ef55c (three reels of fake rewards) and
// OnConnect (the local roll in this APK), Process -> QboxAnimate @000ef2bc (each reel stops in turn: stage u8 +0x1D5 + 8i,
// 2 stopping / 3 stopped), then QboxApplyResultsAndSave @000eeed4 grants the reward of reel +0x1EA. This screen shows
// those reels (data captured by dialogs.cpp capture_qbox): while a reel turns it flips through the fake rewards
// (QBoxRewardTemp, as QboxFillSlotWithARandomFakeReward does), a stopped reel shows its record, and the drawn reel is
// highlighted. Closing = the popup's OK button (cFormPopup::somethingKeypress @000f2ed8 on +0x10C), allowed once the
// draw is done (ProcKeyPressImpl @000ed92c refuses 0x3A before).
#include "game.h"
#include "screen_util.h"
#include "screens.h"

namespace screens {

class QboxScreen : public Screen {
    struct Session {
        double flip_at = 0;      // reel flipping
        int flip = 0;
        std::string drawn;       // result state the texts / focus were set for
    } ss;
    uint32_t ptr = 0;            // per-frame mirrors of the snapshot (a new popup redraws)
    bool done = false;

    // data model "qbox" (qbox.rml), set in update(): the three reels are fixed elements (r0..r2, r0img..r2img)
    struct Reel {
        std::string src;
        bool spin = false, stop = false, win = false, lose = false;
        bool operator==(const Reel &o) const { return src == o.src && spin == o.spin && stop == o.stop && win == o.win && lose == o.lose; }
    };
    struct View {
        std::vector<Reel> reels = std::vector<Reel>(3);
        std::string bicon, bname, bcost, rtitle, rsub; // bname, rtitle: markup
        bool done = false;
    } m;

public:
    QboxScreen() { z = 54; register_screen(this); }
    const char *file() override { return "qbox.rml"; }
    void make_model(Rml::Context *ctx) override {
        Rml::DataModelConstructor c = ctx->CreateDataModel("qbox");
        if (!c) return;
        if (auto h = c.RegisterStruct<Reel>()) {
            h.RegisterMember("src", &Reel::src); h.RegisterMember("spin", &Reel::spin); h.RegisterMember("stop", &Reel::stop);
            h.RegisterMember("win", &Reel::win); h.RegisterMember("lose", &Reel::lose);
        }
        c.RegisterArray<std::vector<Reel>>();
        c.Bind("reels", &m.reels); c.Bind("bicon", &m.bicon); c.Bind("bname", &m.bname); c.Bind("bcost", &m.bcost);
        c.Bind("rtitle", &m.rtitle); c.Bind("rsub", &m.rsub); c.Bind("done", &m.done);
        model = c.GetModelHandle();
    }
    bool want(const state::Snapshot &s) override { return s.stripped && s.qbox.ptr && s.dialog.id == 0; }
    void on_show() override { ss = {}; }

    void update(const state::Snapshot &s) override {
        const auto &q = s.qbox;
        if (q.ptr != ptr) { ptr = q.ptr; ss.drawn.clear(); }
        done = q.state >= 2;
        double t = Rml::GetSystemInterface()->GetElapsedTime();
        if (t >= ss.flip_at) { ss.flip++; ss.flip_at = t + 0.075; } // reel flip rate of the port's drawing (the original moves pixels)
        put(m.bname, rml_safe(q.box), "bname");
        put(m.bicon, sprite_src(q.box_icon), "bicon");
        std::string cost = q.cost <= 0 ? "\xeb\xac\xb4\xeb\xa3\x8c" // 무료
                                       : q.gold ? num(q.cost) + " G" : std::to_string(q.cost) + " \xec\x9a\xa9\xeb\xb9\x84\xeb\x8a\x98"; // 용비늘
        put(m.bcost, cost, "bcost");
        std::vector<Reel> reels;
        for (int i = 0; i < 3; i++) {
            bool stopped = q.phase[i] >= 2 || done;
            std::string src = stopped || q.spin.empty() ? q.icon[i] : q.spin[(size_t)(ss.flip * 7 + i * 13) % q.spin.size()];
            reels.push_back({sprite_src(src), !stopped, stopped, done && q.result == 0 && q.win == i, done && q.result == 0 && q.win >= 0 && q.win != i});
        }
        put(m.reels, std::move(reels), "reels");
        std::string key = std::to_string(done) + "/" + std::to_string(q.result) + "/" + std::to_string(q.win);
        if (key == ss.drawn) return;
        ss.drawn = key;
        put(m.done, done, "done");
        if (!done) {
            put(m.rtitle, q.msg.empty() ? std::string("\xec\x96\xb4\xeb\x96\xa4 \xec\x95\x84\xec\x9d\xb4\xed\x85\x9c\xec\x9d\xb4 \xeb\x93\xa4\xec\x96\xb4 \xec\x9e\x88\xec\x9d\x84\xea\xb9\x8c\xec\x9a\x94?") : rml_safe(q.msg), "rtitle");
            put(m.rsub, std::string(), "rsub");
        } else if (q.result == 0 && q.win >= 0 && q.win < 3) {
            std::string n = q.name[q.win];
            // gold / exp rewards are granted x10 (QboxApplyResultsAndSave)
            put(m.rtitle, "<span class=\"got\">\xed\x9a\x8d\xeb\x93\x9d</span>" + rml_safe(n), "rtitle"); // 획득
            bool money = n == "\xea\xb3\xa8\xeb\x93\x9c" || n == "\xea\xb2\xbd\xed\x97\x98\xec\xb9\x98";
            put(m.rsub, money ? num(q.count * 10) + (n == "\xea\xb3\xa8\xeb\x93\x9c" ? " G" : " EXP") : (q.count > 1 ? "x" + std::to_string(q.count) : std::string("")), "rsub");
        } else {
            put(m.rtitle, rml_safe(q.msg), "rtitle");
            put(m.rsub, std::string(), "rsub");
        }
        nav.root = d.el("box");
        nav.set(nullptr);
        if (done) nav.set(d.el("ok"));
    }

    void close() {
        if (!done) return;
        uint32_t f = ptr;
        state::post([f] { g::call(0xe2ed8, {f, 0x30, 0, M32(f + 0x10c)}); }); // cFormPopup::somethingKeypress @000f2ed8
    }
    bool action(const std::string &a) override {
        if (a == "ok") { close(); return true; }
        return false;
    }
    void back() override { close(); }
};

static QboxScreen g_qbox;
}
