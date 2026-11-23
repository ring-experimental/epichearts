// Shared helpers for the modern screens: element cache, focus navigation, screen base class
#pragma once
#include "state.h"
#include "ui_internal.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace autoform { struct Frame; }
namespace input { bool touch_input(); }
namespace screens {
const autoform::Frame &recorded(); // original draw calls of the last frame (all forms)

struct Doc {
    Rml::ElementDocument *doc = nullptr;
    std::map<std::string, Rml::Element *> els;
    std::map<const void *, std::string> last;
    Rml::Element *el(const std::string &id) {
        auto it = els.find(id);
        if (it != els.end() && it->second) return it->second;
        Rml::Element *e = doc ? doc->GetElementById(id) : nullptr;
        els[id] = e;
        return e;
    }
    // replacing a subtree destroys its elements: forget cached pointers / values inside it
    void forget_subtree(Rml::Element *e) {
        for (int i = 0; i < e->GetNumChildren(); i++) {
            Rml::Element *c = e->GetChild(i);
            forget_subtree(c);
            for (auto it = els.begin(); it != els.end();) { if (it->second == c) it = els.erase(it); else ++it; }
            for (auto it = last.begin(); it != last.end();) {
                const char *k = (const char *)it->first;
                if (k >= (const char *)c && k <= (const char *)c + 4093 + 1) it = last.erase(it); else ++it;
            }
        }
    }
    bool rml(Rml::Element *e, const std::string &v) {
        if (!e) return false;
        auto it = last.find(e);
        if (it != last.end() && it->second == v) return false;
        forget_subtree(e);
        e->SetInnerRML(v);
        last[e] = v;
        return true;
    }
    bool rml(const std::string &id, const std::string &v) { return rml(el(id), v); }
    void cls(Rml::Element *e, const std::string &c, bool on) { if (e && e->IsClassSet(c) != on) e->SetClass(c, on); }
    void cls(const std::string &id, const std::string &c, bool on) { cls(el(id), c, on); }
    void prop(Rml::Element *e, const std::string &p, const std::string &v) {
        if (!e) return;
        std::string &l = last[(const char *)e + std::hash<std::string>()(p) % 4093 + 1];
        if (l != v) { e->SetProperty(p, v); l = v; }
    }
    void prop(const std::string &id, const std::string &p, const std::string &v) { prop(el(id), p, v); }
    void attr(Rml::Element *e, const std::string &a, const std::string &v) {
        if (e && e->GetAttribute<Rml::String>(a, "") != v) e->SetAttribute(a, v);
    }
    void attr(const std::string &id, const std::string &a, const std::string &v) { attr(el(id), a, v); }
    void invalidate() { last.clear(); }
};

// spatial focus navigation over elements carrying data-act (keyboard / gamepad)
struct Nav {
    // observed, not owned: a list rebuilt under it (SetInnerRML, a shrinking data-for) destroys the element, and the
    // pointer then reads as null instead of dangling (a freed address used to alias a new element at the same address)
    Rml::ObserverPtr<Rml::Element> focus;
    Rml::Element *root = nullptr;
    void collect(Rml::Element *e, std::vector<Rml::Element *> &out) {
        if (!e || !e->IsVisible()) return;
        if (e->HasAttribute("data-act") && !e->HasAttribute("data-nonav") && !e->IsClassSet("off")) out.push_back(e);
        for (int i = 0; i < e->GetNumChildren(); i++) collect(e->GetChild(i), out);
    }
    std::vector<Rml::Element *> items() { std::vector<Rml::Element *> v; collect(root, v); return v; }
    bool valid() {
        if (!focus) return false;
        for (auto *e : items()) if (e == focus.get()) return true;
        return false;
    }
    void set(Rml::Element *e) {
        if (focus.get() == e) return;
        if (focus) focus->SetClass("focus", false);
        focus = e ? e->GetObserverPtr() : nullptr;
        // the focus is the keyboard / gamepad cursor: on touch it is kept but not shown, and does not scroll the view
        // (screens::update shows it again when a key or pad button is used)
        if (focus && !input::touch_input()) { focus->SetClass("focus", true); focus->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest)); }
    }
    void ensure() {
        if (valid()) return;
        auto v = items();
        set(v.empty() ? nullptr : v[0]);
    }
    void clear() { set(nullptr); }
    // the data-for rows under `list` change in this frame's Context::Update, which reuses row elements by index and
    // removes the rows past the new count: drop the focus when it is inside `list` and its data-act is not among the
    // new rows' acts (its element goes away, or is reused for another row). True when the focus was dropped.
    bool drop_if_gone(Rml::Element *list, const std::vector<std::string> &acts) {
        Rml::Element *f = focus.get();
        if (!f || !list) return false;
        Rml::Element *p = f;
        while (p && p != list) p = p->GetParentNode();
        if (!p) return false;
        if (std::find(acts.begin(), acts.end(), f->GetAttribute<Rml::String>("data-act", "")) != acts.end()) return false;
        clear();
        return true;
    }
    // the element under root carrying this data-act (shown or not)
    Rml::Element *find_act(const std::string &act, Rml::Element *e = nullptr) {
        if (!e) e = root;
        if (!e) return nullptr;
        if (e->GetAttribute<Rml::String>("data-act", "") == act) return e;
        for (int i = 0; i < e->GetNumChildren(); i++)
            if (Rml::Element *r = find_act(act, e->GetChild(i))) return r;
        return nullptr;
    }
    // focus an element by its data-act on the next frame: data-for makes its elements in the Context::Update after
    // the screen's update(), so screens::update settles a request made in update() before the next update()
    // `in`: look only under that element (an act used twice in the screen, e.g. smithy's pick0 in the list and the slots);
    // a lasting element (a static box, not a data-for row or data-rml child: the pointer is kept until the next frame)
    std::string want;
    Rml::Element *want_in = nullptr;
    void want_act(const std::string &act, Rml::Element *in = nullptr) { want = act; want_in = in; }
    void settle() {
        if (want.empty()) return;
        if (Rml::Element *e = find_act(want, want_in)) set(e);
        want.clear();
        want_in = nullptr;
    }
    // a key / pad button before the next settle: the asked element exists by then (the Context::Update that makes it
    // ran in between), so take it first and the key acts from there, not from the first item (screens.cpp pad_button /
    // pad_dir). Not found: the request waits for its settle.
    void settle_input() {
        if (want.empty()) return;
        if (Rml::Element *e = find_act(want, want_in)) { set(e); want.clear(); want_in = nullptr; }
    }
    // dir: 0 up 1 down 2 left 3 right; false when there is nothing that way
    bool move(int dir) {
        auto v = items();
        if (v.empty()) return false;
        if (!valid()) { set(v[0]); return true; }
        Rml::Vector2f c = focus->GetAbsoluteOffset(Rml::BoxArea::Border) + focus->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
        Rml::Element *best = nullptr;
        float bestd = 1e9f;
        for (auto *e : v) {
            if (e == focus.get()) continue;
            Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border) + e->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
            float dx = p.x - c.x, dy = p.y - c.y;
            float along = dir == 0 ? -dy : dir == 1 ? dy : dir == 2 ? -dx : dx;
            float across = (dir < 2) ? std::fabs(dx) : std::fabs(dy);
            if (along <= 1.f) continue;
            float d = along + across * 2.2f;
            if (d < bestd) { bestd = d; best = e; }
        }
        if (best) set(best);
        return best != nullptr;
    }
    // scrolling (D-pad past the last item, right stick): a box whose content is taller than it and that scrolls
    static bool scrollable(Rml::Element *e) {
        // auto / scroll only: a clipping box (overflow hidden, e.g. a help topic row a few px short) is not meant to move
        auto o = e ? e->GetComputedValues().overflow_y() : Rml::Style::Overflow::Visible;
        return e && e->IsVisible() && (o == Rml::Style::Overflow::Auto || o == Rml::Style::Overflow::Scroll) &&
               e->GetScrollHeight() > e->GetClientHeight() + 1;
    }
    static void find_scrollables(Rml::Element *e, std::vector<Rml::Element *> &out) {
        if (!e || !e->IsVisible()) return;
        if (scrollable(e)) out.push_back(e);
        for (int i = 0; i < e->GetNumChildren(); i++) find_scrollables(e->GetChild(i), out);
    }
    // the box to scroll by dy (< 0 up): the focus's scrollable ancestor that can go that way, else a box of text
    // (nothing focusable inside, e.g. a notice body above its OK button), else the largest one that can.
    // text_first (right stick): a box of text that can go that way wins over the focus's list, which the D-pad
    // already walks (list + detail screens: the stick reads the description)
    Rml::Element *scroll_target(float dy, bool text_first = false) {
        if (!root) return nullptr;
        auto can = [&](Rml::Element *e) {
            float t = e->GetScrollTop();
            return dy < 0 ? t > 0.5f : t < e->GetScrollHeight() - e->GetClientHeight() - 0.5f;
        };
        std::vector<Rml::Element *> all;
        if (text_first) {
            find_scrollables(root, all);
            Rml::Element *best = nullptr, *big = nullptr;
            float area = 0, big_area = 0;
            for (auto *e : all) {
                std::vector<Rml::Element *> inner;
                collect(e, inner);
                if (!inner.empty()) continue;
                float a = e->GetClientWidth() * e->GetClientHeight();
                if (a > big_area) { big = e; big_area = a; }
                if (can(e) && a > area) { best = e; area = a; }
            }
            if (best) return best;
            // at the end of the text: only a box around it goes on (a popup taller than the screen), never a list
            // beside it, which would move away from the focus
            if (big) {
                for (Rml::Element *e = big->GetParentNode(); e && e != root->GetParentNode(); e = e->GetParentNode())
                    if (scrollable(e) && can(e)) return e;
                return nullptr;
            }
            all.clear();
        }
        Rml::Element *stop = root->GetParentNode();
        for (Rml::Element *e = focus ? focus->GetParentNode() : nullptr; e && e != stop; e = e->GetParentNode())
            if (scrollable(e) && can(e)) return e;
        find_scrollables(root, all);
        Rml::Element *best = nullptr;
        float area = 0;
        bool best_text = false;
        for (auto *e : all) {
            if (!can(e)) continue;
            std::vector<Rml::Element *> inner;
            collect(e, inner);
            bool text = inner.empty();
            float a = e->GetClientWidth() * e->GetClientHeight();
            if (!best || (text && !best_text) || (text == best_text && a > area)) { best = e; area = a; best_text = text; }
        }
        return best;
    }
    bool scroll(float dy, bool text_first = false) {
        Rml::Element *t = scroll_target(dy, text_first);
        if (!t) return false;
        t->SetScrollTop(t->GetScrollTop() + dy);
        return true;
    }
    void activate() { if (valid()) focus->Click(); }
};

// a value asked of the original that it has not taken yet (it acts on the game thread a frame or more later): while
// active the screen shows the asked value, not the original's older one. It ends when the original's value matches or
// after `timeout` frames (the original refused or answered otherwise). Screens keep it in their Session (cleared on
// every show) and clear it on hide.
template <class T> struct Pending {
    T want{};
    int frames = 0;
    bool active = false;
    void ask(const T &v, int timeout) { want = v; frames = timeout; active = true; }
    void clear() { active = false; frames = 0; }
    // once per frame with the original's value: true while the request still waits
    bool settle(const T &actual) {
        if (active && (actual == want || --frames <= 0)) clear();
        return active;
    }
    bool asked(const T &v) const { return active && want == v; }
};

// Per-opening state: each screen keeps what must start fresh on every show (cursor, modes, confirms, pending requests,
// render keys) in a nested `struct Session { ... } ss;` with default member initialisers, and on_show() begins with
// `ss = {};`. Members outside it persist across openings on purpose (each says why) or are per-frame mirrors of the
// snapshot.
struct Screen {
    Doc d;
    Rml::DataModelHandle model;  // the screen's data model (make_model); every variable is dirtied after on_show
    Nav nav;
    bool shown = false;
    int z = 10;                  // stacking order of the documents (higher = in front)
    virtual ~Screen() {}
    virtual const char *file() = 0;
    // screens::init calls it before the document is loaded: data-model attaches only when its element is parented
    // (Element::SetParent), so a model made after the load would never bind. One model per screen, named after it.
    virtual void make_model(Rml::Context *ctx) { (void)ctx; }
    // assigns a bound variable, dirtying it only when the value changed
    template <class T, class V> bool put(T &var, V &&v, const char *name) {
        if (var == v) return false;
        var = std::forward<V>(v);
        if (model) model.DirtyVariable(name);
        return true;
    }
    virtual bool want(const state::Snapshot &s) = 0;     // should be visible now
    virtual void update(const state::Snapshot &s) = 0;   // while visible
    virtual bool action(const std::string &a) { (void)a; return false; }
    virtual void back() {}
    virtual void tab(int dir) { (void)dir; }
    virtual void subtab(int dir) { (void)dir; }           // second tab row (categories inside a page)
    virtual bool on_dir(int dir) { (void)dir; return false; } // left/right on a focused control
    virtual bool modal() { return true; }                 // owns keyboard / gamepad navigation
    virtual void on_show() {}
    virtual void on_hide() {}
};

std::string num(int v);
// text of the original going in through data-rml: "{{" in it would start a data expression (Factory::InstanceElementText
// looks for it before entities are decoded), so every '{' becomes &#123;
inline std::string rml_safe(const std::string &s) {
    std::string r;
    for (char c : s) { if (c == '{') r += "&#123;"; else r += c; }
    return r;
}
// a facility screen stays up under the original's notices / choices / popups / progress (drawn by dialogs.cpp etc.)
inline bool dialog_over(const state::Snapshot &s) {
    if (s.forms.empty()) return false;
    int t = s.forms.back().id;
    return t == 1 || t == 3 || t == 0xf || t == 0x13 || t == 0x15;
}
std::string pct(int v, int mx);
inline std::string sprite_src(const std::string &key) { return key.empty() ? "" : "sprite:" + key; }
void register_screen(Screen *s);
Rml::ElementDocument *load_doc(Doc &d, const char *file);
// fit a sprite into a box (keeps pixel aspect)
bool sprite_size(const std::string &key, int *w, int *h);
}
