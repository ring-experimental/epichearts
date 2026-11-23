/* Autoplay bot (EH_BOT=1) for verification runs; always combine with EH_TESTBUFF=1.
 * Runs on the game thread from GofManager.procTouch (once per game frame, before touch events are
 * delivered), reads the original game's state and queues touches like a player would.
 * Offsets/pointer chains come from the decompilation (see work/bot_spec.md):
 *   gp_gmInstance 0x19b148 -> state s32 +0xC (1 menu, 3 field, 4 UI form, 5 field load)
 *   gp_GameWorldIntance 0x19b158 -> objmgr +0x650 -> party +0x10 -> player +4 (cGameWorld::GetPlayer @00128bf4)
 *   object lists: objmgr+0x14+4*kind (cObjectManager::GetObjectList @000ceda4), cflLinkedList count s16 +0xE, head +0x10
 *   object x/y +0x14/+0x18, flags +0x2C (2 active, 8 dead), hp +0x94, faction s16 +0x70
 *   gp_fieldInstance 0x19af24: scroll x/y +4/+8 (cField::SetScrollPos @00122134)
 *   gp_mwmInstance 0x19ae94: modal box +4, box flags +0xC bit 4 = waits for input (cMessageWndManager @001019e0)
 *   logical surface 512x320 for an 800x480 window (CreateGraphics @001287b8)
 * The port's game surface is g_game_w x 320 framebuffer pixels (width follows the display aspect, src/main.c) and
 * touches are framebuffer pixels. The bot's fixed spots below are 800x480 screen coordinates of the original's
 * 512-wide layout; forms are centred in a wider surface (cFormMainMenu's Game Start at 200+29 in 570), so tap()
 * maps them to the centre 512 box, and sx()/sy() map surface pixels (field, touchables) back without loss. */
#include "bot.h"
#include "platform.h"
#include "textconv.h"
#ifndef _WIN32 /* no SEH: the bot is a desktop test tool; on other hosts its guarded reads are plain reads */
#define __try if (1)
#define __except(x) else if (0)
#endif
#include "java.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_on = -1;
static int g_setup_hold; /* EH_SETUPONLY: setup finished, hold still */
static int g_sw = 800, g_sh = 480;
static const int LW = 512, LH = 320;

#define G(va) M32(IMG + (va))
#define VA_GM 0x19b148u
#define VA_WORLD 0x19b158u
#define VA_FIELD 0x19af24u
#define VA_MWM 0x19ae94u
#define VA_BUI 0x19ae8cu
#define VA_GW_GETOBJECT 0x11b010u /* cGameWorld::GetObject(int) @0012b010 */
#define VA_SYSINFO8 0x19af50u

int bot_enabled(void) {
    if (g_on < 0) g_on = getenv("EH_BOT") || getenv("EH_ATTACKNEAREST") ? 1 : 0;
    return g_on;
}
void bot_init(int w, int h) { g_sw = w; g_sh = h; }

/* ---------------------------------------------------------------- touch output */
static int g_hold, g_hold_x, g_hold_y; /* an ongoing press */
static int g_release_in;               /* frames until release of a tap */
static int g_tick, g_field_settle;
static const char *g_branch = "-";
/* cadence for repeated taps: true once every `period` ticks (g_tick % n is unreliable because a tap makes the
 * following ticks return early) */
static int due(int *next, int period) { if (g_tick >= *next || *next - g_tick > period) { *next = g_tick + period; return 1; } return 0; } /* last decision branch reached (watchdog report) */
static int avoided(int x, int y);
static void host_log_once_per(int x, int y) { static int lx = -1, ly = -1; if (x != lx || y != ly) { host_log("[bot] following hint marker to %d,%d", x, y); lx = x; ly = y; } }
static int reachable(int px, int py, int wx, int wy);
static int g_last_state = -1;

extern int g_game_w, g_game_h;
static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int box_x0(void) { return (g_game_w - LW) / 2; } /* left edge of the centred 512 box in surface pixels */
/* surface pixel -> 800x480 screen coordinate of the 512 box (and back in gx/gy, rounding so the trip is exact) */
static int sx(int lx) { return fdiv((lx - box_x0()) * g_sw, LW); }
static int sy(int ly) { return fdiv(ly * g_sh, LH); }
static int gx(int x) { return box_x0() + fdiv(x * LW + g_sw / 2, g_sw); }
static int gy(int y) { return fdiv(y * LH + g_sh / 2, g_sh); }

/* keep field touches out of the HUD (bag/gauges top-left, mode button bottom-left, minimap/save top-right) */
static void avoid_hud(int *x, int *y) {
    if (*y < 75 && *x < 300) *y = 75;
    if (*y < 75 && *x > 540) *y = 75;
    if (*y > 370 && *x < 110) *x = 110;
    if (*y > 440) *y = 440;
}
static int g_acted; /* the bot walked since the last field arrival (dialog taps do not count) */
static void press(int x, int y) { x = gx(x); y = gy(y); java_set_touch(1, x, y); g_hold = 1; g_hold_x = x; g_hold_y = y; }
static void move_to(int x, int y) { x = gx(x); y = gy(y); java_set_touch(4, x, y); g_hold_x = x; g_hold_y = y; }
static void release(void) { if (g_hold) java_set_touch(2, g_hold_x, g_hold_y); g_hold = 0; }
static void tap(int x, int y) { release(); press(x, y); g_release_in = 3; }

/* ---------------------------------------------------------------- game state readers */
static uint32_t gm(void) { return G(VA_GM); }
static uint32_t world(void) { return G(VA_WORLD); }
static uint32_t player(void) {
    uint32_t w = world(), om, party;
    if (!w || !(om = M32(w + 0x650)) || !(party = M32(om + 0x10))) return 0;
    return M32(party + 4);
}
static int scroll_x(void) { uint32_t f = G(VA_FIELD); return f ? (int32_t)M32(f + 4) : 0; }
static int scroll_y(void) { uint32_t f = G(VA_FIELD); return f ? (int32_t)M32(f + 8) : 0; }
static int dialog_waiting(void) {
    uint32_t m = G(VA_MWM), box;
    return m && (box = M32(m + 4)) && (M32(box + 0xC) & 4);
}
/* cHintMsgWnd (type 9) in the non-modal list: world target at +0x10/+0x14 (cHintMsgWnd::SetData @000fe530) */
static int hint_target(int *x, int *y) {
    uint32_t m = G(VA_MWM), list;
    if (!m || !(list = M32(m))) return 0;
    for (uint32_t n = M32(list + 0x10); n; n = M32(n)) {
        uint32_t box = M32(n + 4);
        if (box && (int32_t)M32(box + 4) == 9) { *x = (int32_t)M32(box + 0x10); *y = (int32_t)M32(box + 0x14); return 1; }
    }
    return 0;
}
typedef struct { uint32_t obj; int x, y, d2; } Target;
/* nearest alive hostile monster (list 1, flags&2, !(flags&8), hp>0, faction 1) */
static int nearest_monster_ex(uint32_t pl, Target *t, int need_reach) {
    uint32_t om = M32(world() + 0x650), list = M32(om + 0x14 + 4 * 1);
    int px = (int32_t)M32(pl + 0x14), py = (int32_t)M32(pl + 0x18), found = 0;
    if (!list) return 0;
    for (uint32_t n = M32(list + 0x10); n; n = M32(n)) {
        uint32_t o = M32(n + 4);
        if (!o) continue;
        uint32_t fl = M32(o + 0x2C);
        /* without need_reach (arrival placement) an inactive monster counts too: Eamoru (150/2) is flags 0 until its
         * field script starts the fight */
        if ((need_reach && !(fl & 2)) || (fl & 8) || (int32_t)M32(o + 0x94) <= 0 || (int16_t)M16(o + 0x70) != 1) continue;
        int x = (int32_t)M32(o + 0x14), y = (int32_t)M32(o + 0x18);
        uint32_t td = M32(o + 0x328); /* cMonster::GetTouchCenterX/Y @000b4870 */
        if (td) { x += (int16_t)M16(td + 0x5C); y += (int16_t)M16(td + 0x5E); }
        if (avoided(x, y)) continue;
        int d2 = (x - px) * (x - px) + (y - py) * (y - py);
        if (need_reach && d2 > 48 * 48 && !reachable(px, py, x, y)) continue; /* on a ledge / behind walls */
        if (!found || d2 < t->d2) { t->obj = o; t->x = x; t->y = y; t->d2 = d2; found = 1; }
    }
    return found;
}
static int nearest_monster(uint32_t pl, Target *t) { return nearest_monster_ex(pl, t, 1); }
/* triggers (list 5, not guide lines): active (flags & 2) and not blocked (+0x90 == 0).
 * Script triggers (type 1) drive the story/tutorial, type 0 are map exits (cTrigger::GetExistEvent @000d0bcc).
 * Target = rect centre, rect = x,y,w,h (s16 +0x38/+0x3A). */
static int trigger_target(int want_type, int *tx, int *ty, int skip) {
    uint32_t om = M32(world() + 0x650), list = M32(om + 0x14 + 4 * 5);
    if (!list) return 0;
    int k = 0;
    for (uint32_t n = M32(list + 0x10); n; n = M32(n)) {
        uint32_t o = M32(n + 4);
        if (!o || M8(o + 4) == 0xD || !(M32(o + 0x2C) & 2) || M32(o + 0x90) != 0) continue;
        int type = (int16_t)M16(o + 6);
        if (want_type == 1 ? type == 0 : type != 0) continue;
        /* script triggers carrying a field destination (s16 +0x3C/+0x3E, e.g. field 32's gate type 2 -> 2/1) are
         * scripted exits: handled with the exits, not walked into as story triggers */
        if (want_type == 1 && (int16_t)M16(o + 0x3C) > 0) continue;
        if (k++ < skip) continue;
        *tx = (int32_t)M32(o + 0x14) + (int16_t)M16(o + 0x38) / 2;
        *ty = (int32_t)M32(o + 0x18) + (int16_t)M16(o + 0x3A) / 2;
        return 1;
    }
    return 0;
}

/* cflUi element tree (cflUiElement::SetBounds @00154d2c, cflUiContainer::AddChild @00158558,
 * cflUiContainer::FindLogPos @00155e4c, Touchable @000e7444): x,y s16 +0x18/+0x1A relative to the parent,
 * w,h s16 +0x1C/+0x1E, flags +0x14 ((flags & 0x1C000) == 0x4000 touchable), children u8 +0x20 / ptr +0x24 */
typedef struct { int x, y, w, h; uint32_t el; } Rect;
static int collect_touchables(uint32_t el, int ox, int oy, Rect *out, int n, int max, int depth) {
    if (!el || depth > 12 || n >= max) return n;
    uint32_t fl = M32(el + 0x14);
    if (fl & 0x18000) return n; /* hidden */
    int x = ox + (int16_t)M16(el + 0x18), y = oy + (int16_t)M16(el + 0x1A);
    int cnt = M8(el + 0x20);
    uint32_t arr = M32(el + 0x24);
    if ((fl & 0x1C000) == 0x4000 && n < max) out[n++] = (Rect){x, y, (int16_t)M16(el + 0x1C), (int16_t)M16(el + 0x1E), el};
    /* only real containers: capacity u16 +0x22 >= count, and every child's parent (+8) is this element */
    if (arr && cnt > 0 && M16(el + 0x22) >= cnt && M16(el + 0x22) < 256)
        for (int i = 0; i < cnt; i++) {
            uint32_t ch = M32(arr + 4 * i);
            if (ch && M32(ch + 8) == el) n = collect_touchables(ch, x, y, out, n, max, depth + 1);
        }
    return n;
}
static int collect_touchables_safe(uint32_t form, Rect *out, int max) {
    int n = 0;
    __try { n = collect_touchables(form, 0, 0, out, 0, max, 0); }
    __except (1) { n = 0; }
    return n;
}
/* label text of an element subtree (cflUiLabelStr::GetText @00154fd8), EUC-KR bytes */
static uint32_t g_vt_label;
static const char *label_text(uint32_t el, int depth) {
    if (!el || depth > 6) return NULL;
    if (!g_vt_label) g_vt_label = IMG + rc_sym("_ZTV13cflUiLabelStr") + 8;
    if (M32(el) == g_vt_label) {
        uint32_t str = M32(el + 0x3c);
        if (str) return GP(M32(str + 0x10));
        if (M32(el + 0x14) & 0x80000) return GP(M32(el + 0x2c));
        uint32_t tab = M32(M32(el + 4) + 0x44);
        return tab ? GP(tab + M32(el + 0x2c)) : NULL;
    }
    int cnt = M8(el + 0x20);
    uint32_t arr = M32(el + 0x24);
    if (arr && cnt > 0 && M16(el + 0x22) >= cnt && M16(el + 0x22) < 256)
        for (int i = 0; i < cnt; i++) {
            uint32_t ch = M32(arr + 4 * i);
            if (ch && M32(ch + 8) == el) { const char *t = label_text(ch, depth + 1); if (t) return t; }
        }
    return NULL;
}
static void tap_rect(const Rect *r) { tap(sx(r->x + r->w / 2), sy(r->y + r->h / 2)); }
/* the forms' back arrow / 닫기: a small button in the top-right corner. Forms keep it at the right edge of the
 * surface (cFormHelpFull's at 513,7 in a 570 surface; 455..505 of the 512 layout), not in the centred 512 box */
static int back_button(const Rect *tr, int nt) {
    int k = -1;
    for (int i = 0; i < nt; i++)
        if (tr[i].w < 100 && tr[i].h < 100 && tr[i].y < 40 && tr[i].x + tr[i].w >= g_game_w - 70) k = i;
    return k;
}

/* ---------------------------------------------------------------- pathfinding on the tile grid
 * cField::GetTileFlag @0012215c: tiles 16 px, width u16 +0x78, height u16 +0x7A, flag bytes ptr +0xE4
 * (valid when +0x70 == 0 and +0xDC != 0); blocked when flag & 0x40 (CheckTileMoveable @001221b8) */
static uint8_t g_objblock[256 * 256]; /* tiles covered by blocking objects (rebuilt per path query) */
static int g_ob_w;
static int tile_ok(uint32_t f, int tx, int ty) {
    int w = M16(f + 0x78), h = M16(f + 0x7A);
    if (tx < 0 || ty < 0 || tx >= w || ty >= h) return 0;
    if (w == g_ob_w && w * h <= 256 * 256 && g_objblock[tx + ty * w]) return 0;
    if (M32(f + 0x70) != 0 || !M32(f + 0xDC) || !M32(f + 0xE4)) return 1;
    return !(M8(M32(f + 0xE4) + tx + ty * w) & 0x40);
}
/* the player's collision box at world position (x,y): cGameWorld::CheckMoveTile @0012d44c tests
 * cField::CheckRectMoveable over (x + s16 box+0, y + s16 box+2, w = box+4, h = box+6), box = player obj+0xC */
static int g_bx = -6, g_by = -4, g_bw = 12, g_bh = 8;
static int pos_ok(uint32_t f, int x, int y) {
    /* right/bottom edges exclusive: a character at y=338 (box -4..+4) steps to y=330 in the original even though
     * the tile row below 336 is a wall (field 0/1), which an inclusive y+h edge would forbid */
    int x0 = (x + g_bx) >> 4, x1 = (x + g_bx + g_bw - 1) >> 4, y0 = (y + g_by) >> 4, y1 = (y + g_by + g_bh - 1) >> 4;
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (!tile_ok(f, tx, ty)) return 0;
    return 1;
}
static int line_ok(uint32_t f, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0), n = (dx > dy ? dx : dy) / 3 + 1;
    for (int i = 1; i <= n; i++) {
        int x = x0 + (x1 - x0) * i / n, y = y0 + (y1 - y0) * i / n;
        if (!pos_ok(f, x, y)) return 0;
    }
    return 1;
}
#define MAXT (256 * 256)
static int g_path_found;
static int16_t g_prev[MAXT];
static int g_prevfull[MAXT];
static int g_queue[MAXT];
/* objects' possession boxes (cGObj::GetPossArea @000cb130: x + s16 +0xC, y + s16 +0xE, w/h s16 +0x10/+0x12)
 * for active (flags & 2), blocking (not flags 0x100000) props, NPCs, collectables (lists 4, 6, 8, 9) */
static void build_objblock(uint32_t f, int px, int py, int wx, int wy) {
    int w = M16(f + 0x78), h = M16(f + 0x7A);
    if (w * h > 256 * 256) { g_ob_w = 0; return; }
    memset(g_objblock, 0, (size_t)w * h);
    g_ob_w = w;
    uint32_t om = M32(world() + 0x650);
    static const int lists[] = {4, 6, 8, 9};
    for (int li = 0; li < 4; li++) {
        uint32_t L = M32(om + 0x14 + 4 * lists[li]);
        if (!L) continue;
        for (uint32_t n = M32(L + 0x10); n; n = M32(n)) {
            uint32_t o = M32(n + 4);
            if (!o) continue;
            uint32_t fl = M32(o + 0x2C);
            if (!(fl & 2) || (fl & 0x100000)) continue;
            static uint32_t vt_bird;
            if (!vt_bird) vt_bird = IMG + rc_sym("_ZTV5cBird") + 8;
            if (M32(o) == vt_bird) continue; /* birds fly off once the character moves (cBird::DoThinkingOnIdle) */
            int bw = (int16_t)M16(o + 0x10), bh = (int16_t)M16(o + 0x12);
            if (bw <= 0 || bh <= 0) continue;
            int x0 = (int32_t)M32(o + 0x14) + (int16_t)M16(o + 0xC), y0 = (int32_t)M32(o + 0x18) + (int16_t)M16(o + 0xE);
            /* an object the character already overlaps (e.g. the whirlwind it just landed in) cannot block it */
            if (x0 < px + g_bx + g_bw && px + g_bx < x0 + bw && y0 < py + g_by + g_bh && py + g_by < y0 + bh) continue;
            for (int ty = y0 >> 4; ty <= (y0 + bh - 1) >> 4; ty++)
                for (int tx = x0 >> 4; tx <= (x0 + bw - 1) >> 4; tx++)
                    if (tx >= 0 && ty >= 0 && tx < w && ty < h) g_objblock[tx + ty * w] = 1;
        }
    }
    /* never block where we stand or where we go */
    if ((px >> 4) < w && (py >> 4) < h) g_objblock[(px >> 4) + (py >> 4) * w] = 0;
    if ((wx >> 4) >= 0 && (wy >> 4) >= 0 && (wx >> 4) < w && (wy >> 4) < h) g_objblock[(wx >> 4) + (wy >> 4) * w] = 0;
}

/* next waypoint (world px) from (px,py) toward (wx,wy); returns 0 when unreachable */
static int path_next(int px, int py, int wx, int wy, int *nx, int *ny) {
    uint32_t f = G(VA_FIELD);
    if (!f) return 0;
    build_objblock(f, px, py, wx, wy);
    int w = M16(f + 0x78), h = M16(f + 0x7A);
    if (w <= 0 || h <= 0 || w * h > MAXT) { *nx = wx; *ny = wy; return 1; }
    if (line_ok(f, px, py, wx, wy)) { *nx = wx; *ny = wy; return 1; }
    /* BFS on an 8 px grid; node (gx,gy) = world (gx*8+4, gy*8+4) */
    int W2 = w * 2, H2 = h * 2;
    if (W2 * H2 > MAXT) { *nx = wx; *ny = wy; return 1; }
    int s = ((px - 4 + 4) >> 3) + ((py - 4 + 4) >> 3) * W2, t = (wx >> 3) + (wy >> 3) * W2;
    if (s < 0 || s >= W2 * H2 || t < 0 || t >= W2 * H2) { *nx = wx; *ny = wy; return 1; }
    static int seen[MAXT], stamp;
    stamp++;
    int head = 0, tail = 0, best = s, bestd = 1 << 30;
    g_queue[tail++] = s; seen[s] = stamp;
    {
        /* the 8 px start node is not the exact position: also seed walkable nodes the character can step to directly */
        int sx0 = s % W2, sy0 = s / W2;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int ax = sx0 + dx, ay = sy0 + dy;
                if ((!dx && !dy) || ax < 0 || ay < 0 || ax >= W2 || ay >= H2) continue;
                int id = ax + ay * W2;
                if (seen[id] == stamp || !pos_ok(f, ax * 8 + 4, ay * 8 + 4) || !line_ok(f, px, py, ax * 8 + 4, ay * 8 + 4)) continue;
                seen[id] = stamp; g_queue[tail++] = id; g_prevfull[id] = s;
            }
    }
    int found = 0;
    int tgx = t % W2, tgy = t / W2;
    while (head < tail) {
        int cur = g_queue[head++];
        int cx = cur % W2, cy = cur / W2;
        int d = (cx - tgx) * (cx - tgx) + (cy - tgy) * (cy - tgy);
        if (d < bestd) { bestd = d; best = cur; }
        if (cur == t || d <= 2) { found = 1; best = cur; break; }
        static const int D[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        for (int k = 0; k < 8; k++) {
            int ax = cx + D[k][0], ay = cy + D[k][1];
            if (ax < 0 || ay < 0 || ax >= W2 || ay >= H2) continue;
            int id = ax + ay * W2;
            if (seen[id] == stamp) continue;
            if (!pos_ok(f, ax * 8 + 4, ay * 8 + 4)) continue;
            if (k >= 4 && (!pos_ok(f, cx * 8 + 4 + D[k][0] * 8, cy * 8 + 4) || !pos_ok(f, cx * 8 + 4, cy * 8 + 4 + D[k][1] * 8))) continue;
            seen[id] = stamp; g_queue[tail++] = id;
            g_prevfull[id] = cur;
        }
    }
    int end = best; /* best == the reached node when found (may stop within 2 cells of t) */
    g_path_found = found;
    if (getenv("EH_BOTDBG") && g_tick % 54 == 0)
        host_log("[botbfs] s %d,%d t %d,%d visited %d found %d", s % W2, s / W2, tgx, tgy, tail, found);
    if (end == s) return 0;
    /* walk back to the furthest node visible from the player */
    static int chain[16384]; int nc = 0;
    for (int c = end; c != s && c >= 0 && nc < 16384; c = g_prevfull[c]) chain[nc++] = c;
    if (nc == 0) return 0;
    int pick = chain[nc - 1];
    for (int i = 0; i < nc; i++) {
        int cx = (chain[i] % W2) * 8 + 4, cy = (chain[i] / W2) * 8 + 4;
        if (line_ok(f, px, py, cx, cy)) { pick = chain[i]; break; }
    }
    *nx = (pick % W2) * 8 + 4; *ny = (pick / W2) * 8 + 4;
    return 1;
}

/* minimap quest icons (cMinimap at world+0x694, AddDrawStuff @00101bc8): entry type 1 = quest icon,
 * obj = cEffEmoticon, owner (NPC) ptr obj+0x90, shown when owner flags & 2 */
static int quest_npc(int *nx, int *ny, uint32_t *npc) {
    uint32_t w = world();
    int cnt = (int32_t)M32(w + 0x728);
    for (int i = 0; i < cnt && i < 64; i++) {
        if ((int32_t)M32(w + 0x694 + 0x14 + 8 * i) != 1) continue;
        uint32_t o = M32(w + 0x694 + 0x18 + 8 * i), own = o ? M32(o + 0x90) : 0;
        if (!own || !(M32(own + 0x2C) & 2)) continue;
        *nx = (int32_t)M32(own + 0x14); *ny = (int32_t)M32(own + 0x18); *npc = own;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- story quest (work/bot_quest_spec.md)
 * gp_QuestMgrInstance 0x19ae7c: story quest *(qm+0); cQuest: info *(q+0) (target field group s16 info+0x14),
 * objectives at q+0x1C+8i {s32 type; data}, type 3/4/5 = talk/visit: field group u16 +4, sub u8 +6, inst id u8 +7
 * (cQuest::CheckCompleteCondi @000d9604, cFormMinimap::OnConstruct @0010356c) */
static int quest_goal(int *grp, int *sub, int *inst) {
    uint32_t qm = G(0x19ae7c), q = qm ? M32(qm) : 0;
    if (!q) return 0;
    int n = M8(q + 8);
    int completable = M32(q + 0xC) & 1; /* cQuest::CheckCompleteCondi @000d9604 */
    if (completable)
        for (int i = 0; i < n && i < 8; i++) {
            uint32_t d = q + 0x1C + 8 * i;
            int t = (int32_t)M32(d);
            /* talk/visit targets are completed by the target's script (cmd 0x6c) */
            if (t >= 3 && t <= 5) { *grp = M16(d + 4); *sub = M8(d + 6); *inst = M8(d + 7); return 1; }
        }
    uint32_t info = M32(q);
    if (!info) return 0;
    *grp = (int16_t)M16(info + 0x14); *sub = -1; *inst = -1; /* quest's target field (minimap marker) */
    return *grp > 0;
}
/* object with instance id (s16 obj+0x44) in lists 4..9 */
static uint32_t find_inst(int inst) {
    uint32_t om = M32(world() + 0x650);
    for (int k = 4; k <= 9; k++) {
        uint32_t L = M32(om + 0x14 + 4 * k);
        for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
            uint32_t o = M32(n + 4);
            if (o && (int16_t)M16(o + 0x44) == inst && (M32(o + 0x2C) & 2)) return o;
        }
    }
    return 0;
}
/* field graph learned from exit triggers (group granularity): edge[g1][g2] */
#define NG 512
static uint8_t g_edge[NG][NG], g_seen_field[NG];
/* field graph from the original field data: data/field_exits.txt, one exit per line "g s dg ds type inst x y",
 * produced by EH_WARPSCAN (every field loaded by cResourceManager::LoadField, exits = cTrigger s16 +0x3C/+0x3E) */
static void add_sedge(int fg, int fs, int tg, int ts);
static void load_field_graph(void) {
    static int done; if (done) return; done = 1;
    FILE *f = fopen(getenv("EH_FIELDGRAPH") ? getenv("EH_FIELDGRAPH") : "data/field_exits.txt", "r");
    if (!f) {   /* next to the APK given on the command line (project root) */
        extern char g_apk_path[]; char p[1100]; snprintf(p, sizeof p, "%s", g_apk_path);
        char *sl = strrchr(p, '/'), *bs = strrchr(p, '\\'); if (bs > sl) sl = bs;
        if (sl) { strcpy(sl + 1, "data/field_exits.txt"); f = fopen(p, "r"); }
    }
    if (!f) { host_log("[bot] no field graph file"); return; }
    int g, sb, dg, ds, n = 0;
    char line[256];
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%d %d %d %d", &g, &sb, &dg, &ds) == 4 && g >= 0 && g < NG && dg >= 0 && dg < NG && g != dg) { g_edge[g][dg] = 1; add_sedge(g, sb, dg, ds); n++; }
    fclose(f);
    host_log("[bot] field graph: %d exits", n);
}
/* sub-map aware graph: nodes (group, sub<16); exits of a sub-map only exist in that sub-map (field 32/3 has the
 * way to 183, 32/1 does not) */
#define NODE(g, s) ((g) * 16 + ((s) & 15))
static struct { int from, to; } g_sedge[4096];
static int g_nsedge;
static void add_sedge(int fg, int fs, int tg, int ts) {
    if (fg < 0 || tg < 0 || fg >= NG || tg >= NG) return;
    int a = NODE(fg, fs), b = NODE(tg, ts);
    for (int i = 0; i < g_nsedge; i++) if (g_sedge[i].from == a && g_sedge[i].to == b) return;
    if (g_nsedge < 4096) { g_sedge[g_nsedge].from = a; g_sedge[g_nsedge].to = b; g_nsedge++; }
}
/* fields that move the character out again by script right after arrival (e.g. the lodge 32/1 during quest 13):
 * not a passage, skipped by the router for a while */
static int g_block_node[16], g_block_until[16];
static int node_blocked(int node) {
    for (int i = 0; i < 16; i++) if (g_block_node[i] == node && g_tick < g_block_until[i]) return 1;
    return 0;
}
/* group of the first hop from (fg,fs) toward any sub-map of group tg, -1 when unreachable */
static int next_hop_sub(int fg, int fs, int tg) {
    static short prev[NG * 16]; static int q[NG * 16];
    int src = NODE(fg, fs), h = 0, t = 0, found = -1;
    for (int i = 0; i < NG * 16; i++) prev[i] = -2;
    prev[src] = -1; q[t++] = src;
    while (h < t && found < 0) {
        int cur = q[h++];
        if (cur / 16 == tg) { found = cur; break; }
        for (int i = 0; i < g_nsedge; i++)
            if (g_sedge[i].from == cur && prev[g_sedge[i].to] == -2 && !node_blocked(g_sedge[i].to)) { prev[g_sedge[i].to] = (short)cur; q[t++] = g_sedge[i].to; }
    }
    if (found < 0) return -1;
    if (found == src) return tg;
    int cur = found;
    while (prev[cur] != src) cur = prev[cur];
    return cur / 16;
}
static int next_hop(int from, int to) {
    load_field_graph();
    if (from < 0 || to < 0 || from >= NG || to >= NG) return -1;
    if (from == to) return to;
    static int prev[NG]; int q[NG], h = 0, t = 0;
    for (int i = 0; i < NG; i++) prev[i] = -2;
    prev[from] = -1; q[t++] = from;
    while (h < t) {
        int c = q[h++];
        if (c == to) break;
        for (int nx = 0; nx < NG; nx++) if (g_edge[c][nx] && prev[nx] == -2) { prev[nx] = c; q[t++] = nx; }
    }
    if (prev[to] == -2) return -1;
    int c = to;
    while (prev[c] != from) c = prev[c];
    return c;
}

/* ---------------------------------------------------------------- behaviour */
static int g_menu_step, g_menu_wait;
static uint32_t g_npc_tapped, g_npc_skip; static int g_npc_skip_until; /* quest NPC whose menu had nothing for us */
static int g_link_tap_tick = -100000, g_link_tap_field, g_link_tap_idx; /* last link tapped */
static int g_link_used[64][64];
/* learned link destinations: the field reached shortly after heading for a link (whirlwind, ladder...) */
static struct { int fld, x, y, dest; } g_linkdest[128];
static int g_nlinkdest, g_head_link_fld = -1, g_head_link_x, g_head_link_y, g_head_link_tick = -100000;
static int link_dest(int fld, int x, int y) {
    for (int i = 0; i < g_nlinkdest; i++)
        if (g_linkdest[i].fld == fld && abs(g_linkdest[i].x - x) < 24 && abs(g_linkdest[i].y - y) < 24) return g_linkdest[i].dest;
    return -1;
}
static int save_step = -1, save_wait, field_ticks;
static int g_last_px, g_last_py, g_stuck, g_exit_skip, g_detour;
static int g_kills_hint;
static uint32_t g_last_target;
static int g_last_field = -1, g_last_sub = -1;

static void dump_map(int px, int py, int wx, int wy) {
    uint32_t f = G(VA_FIELD);
    if (!f) return;
    build_objblock(f, px, py, wx, wy);
        int w = M16(f + 0x78), h = M16(f + 0x7A);
        host_log("[botmap] field %dx%d tiles, player %d,%d target %d,%d", w, h, px >> 4, py >> 4, wx >> 4, wy >> 4);
        for (int ty = 0; ty < h && ty < 80; ty++) {
            char line[260]; int k = 0;
            for (int tx = 0; tx < w && k < 255; tx++) {
                char ch = '.';
                int tb = M32(f + 0x70) == 0 && M32(f + 0xE4) && (M8(M32(f + 0xE4) + tx + ty * w) & 0x40);
                if (tb) ch = '#';
                else if (g_ob_w == w && g_objblock[tx + ty * w]) ch = 'o';
                if (tx == px >> 4 && ty == py >> 4) ch = 'P';
                if (tx == wx >> 4 && ty == wy >> 4) ch = 'T';
                line[k++] = ch;
            }
            line[k] = 0;
            host_log("[botmap] %s", line);
        }
    }

static int reachable(int px, int py, int wx, int wy) {
    int nx, ny;
    uint32_t f = G(VA_FIELD);
    if (f && line_ok(f, px, py, wx, wy)) return 1;
    return path_next(px, py, wx, wy, &nx, &ny) && g_path_found;
}

/* targets the character could not reach although the tile map says it can (blocked by objects): skipped for a while */
static struct { int x, y, until; } g_avoid[16];
static int g_walk_tx = -99999, g_walk_ty, g_walk_detours;
static int avoided(int x, int y) {
    for (int i = 0; i < 16; i++)
        if (g_tick < g_avoid[i].until && abs(x - g_avoid[i].x) < 24 && abs(y - g_avoid[i].y) < 24) return 1;
    return 0;
}
static void walk_toward(int px, int py, int wx, int wy) {
    g_acted = 1;
    if (abs(wx - g_walk_tx) > 24 || abs(wy - g_walk_ty) > 24) g_walk_detours = 0;
    g_walk_tx = wx; g_walk_ty = wy;
    /* Direct mode: hold on the next waypoint of a tile-grid path (screen coords); detour sideways while stuck */
    int nx = wx, ny = wy, ok = path_next(px, py, wx, wy, &nx, &ny);
    if (getenv("EH_BOTDBG") && g_tick % 54 == 0)
        host_log("[botnav] player %d,%d target %d,%d waypoint %d,%d %s", px, py, wx, wy, nx, ny, ok ? "" : "(no path)");
    if (ok) { wx = nx; wy = ny; }
    /* Direct mode needs the hold point some distance away from the character: extend short legs to ~64 px */
    {
        double dx = wx - px, dy = wy - py, d = sqrt(dx * dx + dy * dy);
        if (d > 0.5 && d < 64) { wx = px + (int)(dx * 64 / d); wy = py + (int)(dy * 64 / d); }
    }
    int lx = wx - scroll_x(), ly = wy - scroll_y();
    if (g_detour > 0) {
        g_detour--;
        double ang = atan2((double)(wy - py), (double)(wx - px)) + ((g_tick / 60) & 1 ? 1.4 : -1.4);
        lx = (px - scroll_x()) + (int)(90 * cos(ang));
        ly = (py - scroll_y()) + (int)(90 * sin(ang));
    }
    {
        /* keep the hold point inside the play area (screen edges / HUD strips ignore touches) while
         * preserving its direction from the player */
        int plx = px - scroll_x(), ply = py - scroll_y();
        double dx = lx - plx, dy = ly - ply;
        const int X0 = 6, X1 = g_game_w - 6, Y0 = 6, Y1 = LH - 12;
        for (int it = 0; it < 8 && (lx < X0 || lx > X1 || ly < Y0 || ly > Y1); it++) {
            dx *= 0.75; dy *= 0.75;
            lx = plx + (int)dx; ly = ply + (int)dy;
        }
        if (lx < X0) lx = X0;
        if (lx > X1) lx = X1;
        if (ly < Y0) ly = Y0;
        if (ly > Y1) ly = Y1;
        /* HUD rects (surface pixels): bag + gauges, d-pad/mode icon, A/B switch, minimap (at the right edge) */
        if (ly < 42 && lx < 185) ly = 42;
        if (lx < 34 && ly >= 42 && ly < 72) ly = 72;
        if (lx < 50 && ly > 238) ly = 238;
        if (lx > g_game_w - 52 && ly < 45) ly = 45;
    }
    {
        /* a Direct-mode press on an object targets that object instead of moving (birds gather round a standing
         * character, cBird::DoThinkingOnIdle @000cdda0, and only fly off once it moves): keep the hold point off
         * list-6 objects by pushing it further out along the same direction */
        uint32_t om = M32(world() + 0x650), L = M32(om + 0x14 + 4 * 6);
        int plx = px - scroll_x(), ply = py - scroll_y();
        /* a touch picks objects up to ~20 px away (cIdleKeyProcess::SlotKeyProcess @000d37c8 ->
         * cGameWorld::GetEventObjWhenTouch -> KeyProcessForSelectedObj consumes the touch) */
        double ang0 = atan2((double)(ly - ply), (double)(lx - plx));
        int bestx = lx, besty = ly, bestc = 1 << 30;
        /* direction first, then distance: 64, 48, 32, 24 px (near screen edges only short holds fit), 88; then
         * turn away in 0.35 rad steps */
        static const int R[5] = {64, 48, 32, 24, 88};
        for (int it = 0; it < 45; it++) {
            double a = ang0 + ((it / 5 + 1) / 2) * 0.35 * ((it / 5) & 1 ? 1 : -1);
            int r = R[it % 5];
            int cx = plx + (int)(r * cos(a)), cy = ply + (int)(r * sin(a));
            /* same play-area / HUD limits as above (bag + gauges top-left, minimap top-right, mode icon, A/B) */
            if (cx < 6 || cx > g_game_w - 6 || cy < 6 || cy > LH - 12) continue;
            if ((cy < 42 && cx < 185) || (cx < 34 && cy < 72) || (cx < 50 && cy > 238) || (cx > g_game_w - 52 && cy < 45)) continue;
            int hits = 0;
            for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (!o || !(M32(o + 0x2C) & 2)) continue;
                int ox = (int32_t)M32(o + 0x14) - scroll_x(), oy = (int32_t)M32(o + 0x18) - scroll_y();
                if (abs(ox - cx) < 24 && abs(oy - 6 - cy) < 24) hits++;
            }
            int cost = hits * 1000 + it;
            if (cost < bestc) { bestc = cost; bestx = cx; besty = cy; }
            if (!hits) break;
        }
        lx = bestx; ly = besty;
    }
    if (!g_hold) press(sx(lx), sy(ly));
    else move_to(sx(lx), sy(ly));
}

/* Direct (touch) control as cFormOption::OnReflectTouchType(1) @00110e34: systemInfo+8 |= 8, iVirtualPad::SetUiType(xx_bui, 1) */
static void ensure_direct_mode(Cpu *c) {
    /* the option bit is saved, but the battle UI's pad type is only set by SetUiType: apply it once per
     * battle UI instance even when a loaded save already has the bit */
    static uint32_t applied_bui;
    uint32_t bui = G(VA_BUI);
    if (!bui) return;
    if ((G(VA_SYSINFO8) & 8) && applied_bui == bui) return;
    applied_bui = bui;
    G(VA_SYSINFO8) |= 8;
    uint32_t args[2] = {bui, 1};
    rc_call(c, 0xe7974, 2, args);
    host_log("[bot] control mode -> Direct");
}

static int nearest_monster(uint32_t pl, Target *t);
/* only save while the character stands still on walkable ground: a save taken mid-whirlwind (cWhirlwindProcess on
 * the player's process list, player+0x48 -> +4) stores a position inside a wall, and the reloaded character is then
 * boxed in by wall tiles in every direction */
static int safe_to_save(void) {
    uint32_t pl = player(), f = G(VA_FIELD);
    if (!pl || !f) return 0;
    uint32_t pm = M32(pl + 0x48);
    if (pm && M32(pm + 4)) return 0;
    int px = (int32_t)M32(pl + 0x14), py = (int32_t)M32(pl + 0x18);
    g_bx = (int16_t)M16(pl + 0xC); g_by = (int16_t)M16(pl + 0xE); g_bw = (int16_t)M16(pl + 0x10); g_bh = (int16_t)M16(pl + 0x12);
    if (g_bw <= 0 || g_bh <= 0) { g_bx = -6; g_by = -4; g_bw = 12; g_bh = 8; }
    return pos_ok(f, px, py);
}
void bot_tick(Cpu *c) {
    if (!bot_enabled()) return;
    if (getenv("EH_ATTACKNEAREST")) {
        /* EH_ATTACKNEAREST=<ms>: combat comparison (tools/combat_cmp.py does the same on the original from process
         * memory). Every <ms>, once the field runs with no popup (gm+0x1fc) or script (gm+0x210), tap the nearest
         * alive hostile monster of list 1 (flags&2, !(flags&8), hp>0, faction +0x70 == 1) at its touch centre
         * (cMonster::GetTouchCenterX/Y @000b4870: +0x328 -> +0x5C/+0x5E) minus the field scroll; Direct mode attacks it */
        static uint64_t next;
        if (g_release_in > 0 && --g_release_in == 0) release();
        uint32_t g = gm(), pl = player(), f = G(VA_FIELD);
        uint64_t now = host_ticks_ms();
        if (!g || !pl || !f || M32(g + 0xC) != 3 || M32(g + 0x1fc) || M32(g + 0x210)) { next = now + 1000; return; }
        if (now < next) return;
        next = now + (uint64_t)atoi(getenv("EH_ATTACKNEAREST"));
        uint32_t om = M32(world() + 0x650), list = M32(om + 0x14 + 4 * 1);
        int px = (int32_t)M32(pl + 0x14), py = (int32_t)M32(pl + 0x18), bx = 0, by = 0, bd = -1;
        for (uint32_t n = list ? M32(list + 0x10) : 0; n; n = M32(n)) {
            uint32_t o = M32(n + 4), fl = o ? M32(o + 0x2C) : 0;
            if (!o || !(fl & 2) || (fl & 8) || (int32_t)M32(o + 0x94) <= 0 || (int16_t)M16(o + 0x70) != 1) continue;
            int x = (int32_t)M32(o + 0x14), y = (int32_t)M32(o + 0x18), d;
            uint32_t td = M32(o + 0x328);
            if (td) { x += (int16_t)M16(td + 0x5C); y += (int16_t)M16(td + 0x5E); }
            d = (x - px) * (x - px) + (y - py) * (y - py);
            if (bd < 0 || d < bd) { bd = d; bx = x; by = y; }
        }
        int lx = bx - (int32_t)M32(f + 4), ly = by - (int32_t)M32(f + 8);
        if (bd >= 0 && lx >= 0 && lx < g_game_w && ly >= 0 && ly < LH) tap(sx(lx), sy(ly));
        return;
    }
    if (g_setup_hold) {
        /* EH_SETUPONLY finished: the bot sends no input any more (EH_CTRL saves through the menu). The save must not
         * carry the damage taken under EH_TESTGOD, so until the run ends keep hp(+0x94) at maxHp(+0x98) with
         * cCreature::AddHp(this, amount, isPercent) @ghidra 000c424c (counterpart of SubHp @000c421c) */
        uint32_t pl = player();
        if (pl) { uint32_t a[3] = {pl, M32(pl + 0x98), 0}; rc_call(c, 0xb424c, 3, a); }
        return;
    }
    g_tick++;
    if (g_release_in > 0 && --g_release_in == 0) release();
    if (g_release_in > 0) return;
    uint32_t g = gm();
    if (!g) return;
    int state = (int32_t)M32(g + 0xC);
    if (state != g_last_state) {
        host_log("[bot] state %d -> %d", g_last_state, state);
        /* field load finished (state 5 -> 3): this is the real arrival; world+8 already changed when the move was
         * reserved (cGameWorld::ReservedLoadFieldNo @00128c18), before the load */
        if (g_last_state == 5 && state == 3) { g_field_settle = g_tick + 20; g_acted = 0; }
        g_last_state = state;
        g_menu_wait = 40;
        release();
    }
    /* periodic save through the in-game menu: bag -> System -> 저장하기 -> 확인 -> 닫기 (UI layout of the original) */
    if (state == 3 && save_step < 0 && ++field_ticks > 27 * 60 * 4 && !dialog_waiting() && !M32(g + 0x210) && safe_to_save()) { save_step = 0; field_ticks = 0; host_log("[bot] saving"); }
    if (save_step >= 0) {
        static const int seq[][2] = {{30, 30}, {575, 398}, {282, 103}, {285, 288}, {400, 286}, {690, 398}};
        if (save_wait-- > 0) return;
        if (save_step < 6) { tap(seq[save_step][0], seq[save_step][1]); save_step++; save_wait = 25; return; }
        save_step = -1;
    }
    if (state == 4 && g_tick - g_link_tap_tick < 60) {
        /* the link we just tapped opened a UI (sign, grave, shop...): it is not a way onward */
        g_link_used[g_link_tap_field & 63][g_link_tap_idx & 63] += 100;
        host_log("[bot] link %d of field %d opens a UI, ignored", g_link_tap_idx, g_link_tap_field);
        g_link_tap_tick = -100000;
    }
    if (state == 1 || state == 4) {
        /* menus / UI forms: cGameUi (0x19af18) focused form ui+0x18, form id s16 +0x38 (IsMainFormShowing @00153778) */
        if (g_menu_wait-- > 0) return;
        uint32_t ui = G(0x19af18), form = ui ? M32(ui + 0x18) : 0;
        int id = form ? (int16_t)M16(form + 0x38) : -1;
        static int last_id = -2, n_same, last_ms = -2;
        uint32_t mm = G(0x19b244); /* cMenuMgr instance (cMenuMgr::GetInstance @00147d18), state s32 +8 */
        int ms = (state == 1 && mm) ? (int32_t)M32(mm + 8) : -1;
        if (ms != last_ms) { host_log("[bot] menu state %d", ms); last_ms = ms; last_id = -2; }
        Rect tr[64];
        int nt = form ? collect_touchables_safe(form, tr, 64) : 0;
        if (id != last_id) {
            host_log("[bot] state %d form %d, %d touchables", state, id, nt);
            for (int i = 0; i < nt; i++) {
                const char *lt = NULL;
                __try { lt = label_text(tr[i].el, 0); } __except (1) { lt = NULL; }
                char u8[256] = "";
                if (lt) cp949_to_utf8(lt, u8, sizeof u8);
                host_log("[bot]   #%d logical %d,%d %dx%d vt %06x (%s) %s", i, tr[i].x, tr[i].y, tr[i].w, tr[i].h, M32(tr[i].el) - IMG,
                         rc_func_name(M32(M32(tr[i].el)) - IMG), u8);
            }
            last_id = id; n_same = 0;
        }
        n_same++;
        g_menu_wait = 40;
        if (getenv("EH_BOTSTOP") && atoi(getenv("EH_BOTSTOP")) == id) { host_log("[bot] stopped at form %d", id); g_on = 0; return; }
        switch (id) {
        case -1: break; /* logo / transition */
        case 50: { /* cFormMainMenu: page s32 +0x184 (tapScrollHandler @0014595c): 0 title, 1 save slots */
            int page = (int32_t)M32(form + 0x184);
            if (ms != 9 || n_same < 2) break;
            if (page == 0) tap(400, 252);      /* Game Start */
            else if (page == 1) {
                /* save list: the info popup's 확인 (400,330 is the gap between slots 2 and 3 otherwise), then slot 1 */
                if (n_same % 2) tap(400, 330); else tap(398, 182);
            }
            else { int b = back_button(tr, nt); if (b >= 0) tap_rect(&tr[b]); else tap(750, 47); }
            break;
        }
        case 39: /* character select: centre character, then Start */
            if (n_same % 2) tap(220, 395); else tap(650, 378);
            break;
        case 3: { /* list popup: NPC menu (대화/상점/수리/.../나가기), "save?" 예/아니오, epitaph 읽기/나가기 */
            static int visits, last_tick;
            if (g_tick - last_tick > 27 * 30) visits = 0;
            if (n_same == 1) { visits++; last_tick = g_tick; }
            /* game strings are UTF-8: 대화 (talk), 퀘스트, 의뢰, 예 */
            static const char *want[] = {"\xeb\x8c\x80\xed\x99\x94", "\xed\x80\x98\xec\x8a\xa4\xed\x8a\xb8", "\xec\x9d\x98\xeb\xa2\xb0", "\xec\x98\x88"};
            int pick = -1;
            /* cFormChoice entries: list at form+0x7c, {next, char *text} (cFormChoice::fillCellContent @000e77ac);
             * cell i (touchable i+1) shows entry first+i, first = s32 form+0x9c */
            const char *txt[16] = {0}; int ne = 0;
            __try {
                uint32_t e = M32(form + 0x7c);
                int first = (int32_t)M32(form + 0x9c);
                for (int i = 0; e && i < 64 && ne < 16; i++, e = M32(e)) if (i >= first) txt[ne++] = GP(M32(e + 4));
            } __except (1) { ne = 0; }
            if (n_same == 1) for (int i = 0; i < ne; i++) {
                host_log("[bot]   choice %d: %s", i, txt[i] ? txt[i] : "");
            }
            for (int kw = 0; kw < 4 && pick < 0 && visits <= 2; kw++)
                for (int i = 0; i < ne && i + 1 < nt; i++)
                    if (txt[i] && strstr(txt[i], want[kw])) { pick = i + 1; break; }
            /* no keyword: two-option prompts take the first (예), menus take the last (나가기) */
            if (pick < 0) {
                pick = (nt == 3 && visits <= 2) ? 1 : nt - 1;
                if (nt > 3 && g_npc_tapped) { g_npc_skip = g_npc_tapped; g_npc_skip_until = g_tick + 27 * 120; }
            }
            if (nt >= 2) tap_rect(&tr[pick]); else tap(400, 256);
            break;
        }
        case 6: /* GP 투자 (stat investment) dialog: 이전 (leave it; 확인 with nothing invested only shows a message) */
            tap(240, 92);
            break;
        case 1: { /* message dialogs (quest text, "저장 완료!", resurrection prompt's 취소): known button spots */
            static const int spots[][2] = {{400, 320}, {400, 286}, {512, 392}, {400, 392}};
            tap(spots[n_same % 4][0], spots[n_same % 4][1]);
            break;
        }
        default:
            if (state == 1 && n_same < 3) break; /* let menus settle */
            {
                int back = back_button(tr, nt);
                int tl = -1; /* small button in the top-left corner: the form's 닫기 */
                for (int i = 0; i < nt; i++) if (tr[i].w < 90 && tr[i].h < 40 && tr[i].x < 110 && tr[i].y < 40) tl = i;
                if (back >= 0) tap_rect(&tr[back]);      /* back arrow of the original forms */
                else if (tl >= 0 && (n_same & 1)) tap_rect(&tr[tl]);
                else if (n_same % 3 == 1) tap(690, 398); /* 닫기 of the in-game menu */
                else if (n_same % 3 == 2) tap(400, 320); /* centred dialog confirm */
                else tap(750, 47);
            }
            break;
        }
        return;
    }
    if (state != 3) return;
    ensure_direct_mode(c);
    uint32_t w = world();
    int fld = (int32_t)M32(w + 8), sub = (int32_t)M32(w + 0xC);
    if (fld >= 0 && fld < NG) g_seen_field[fld] = 1;
    if (fld != g_last_field || sub != g_last_sub) {
        int qg = -1, qs = -1, qi = -1;
        quest_goal(&qg, &qs, &qi);
        host_log("[bot] field %d/%d (quest target field %d/%d inst %d)", fld, sub, qg, qs, qi);
        if (0) {
            uint32_t om = M32(w + 0x650);
            for (int kind = 0; kind < 14; kind++) {
                uint32_t L = M32(om + 0x14 + 4 * kind);
                for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    if (o) host_log("[obj] list %d vt %06x type %d inst %d at %d,%d flags %x", kind, M32(o) - IMG, (int16_t)M16(o + 6),
                                    (int16_t)M16(o + 0x44), (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18), M32(o + 0x2C));
                }
            }
        }
        {
            uint32_t qm = G(0x19ae7c), q = qm ? M32(qm) : 0;
            if (q) {
                host_log("[bot]  main quest id %u flags %x, %u objectives", M16(q + 4), M32(q + 0xC), M8(q + 8));
                {   /* mission text: cQuest::GetPopupDesc @000d95c4 = *(res+0x50) + u32 info+0x18 + 4 (UTF-8) */
                    uint32_t rm = G(0x19ae84u), tab = rm ? M32(rm + 0x50) : 0, info = M32(q);
                    if (tab && info) host_log("[bot]  mission: %s", (const char *)GP(tab + M32(info + 0x18) + 4));
                }
                for (int i = 0; i < M8(q + 8) && i < 8; i++) {
                    uint32_t d = q + 0x1C + 8 * i;
                    host_log("[bot]   objective %d: type %d data %02x %02x %02x %02x", i, (int32_t)M32(d), M8(d + 4), M8(d + 5), M8(d + 6), M8(d + 7));
                }
            }
        }
        {
            static int arrive_tick = -100000, arrive_node = -1;
            if (getenv("EH_BOTDBG")) host_log("[bounce?] prev %d acted %d dt %d", arrive_node, g_acted, g_tick - arrive_tick);
            if (arrive_node >= 0 && !g_acted && g_tick - arrive_tick < 27 * 60) {
                static int bi; g_block_node[bi & 15] = arrive_node; g_block_until[bi & 15] = g_tick + 27 * 600; bi++;
                host_log("[bot] field %d/%d sent us away right after arrival (script): not routing through it for 10 min", arrive_node / 16, arrive_node % 16);
            }
            arrive_tick = g_tick; arrive_node = fld * 16 + (sub & 15);
        }
        /* learn transitions made through links/whirlwinds too (not in the exit-trigger graph) */
        if (g_last_field >= 0 && g_last_field < NG && fld >= 0 && fld < NG && g_last_field != fld) { g_edge[g_last_field][fld] = 1; add_sedge(g_last_field, g_last_sub, fld, sub); }
        if (g_head_link_fld == g_last_field && g_last_field != fld && g_tick - g_head_link_tick < 27 * 15 &&
            link_dest(g_head_link_fld, g_head_link_x, g_head_link_y) < 0 && g_nlinkdest < 128) {
            g_linkdest[g_nlinkdest].fld = g_head_link_fld; g_linkdest[g_nlinkdest].x = g_head_link_x;
            g_linkdest[g_nlinkdest].y = g_head_link_y; g_linkdest[g_nlinkdest].dest = fld; g_nlinkdest++;
            host_log("[bot] link at %d,%d of field %d leads to field %d", g_head_link_x, g_head_link_y, g_head_link_fld, fld);
        }
        g_last_field = fld; g_last_sub = sub; g_exit_skip = 0;
        g_field_settle = g_tick + 20; /* object lists still hold the previous field for a few frames */
    }
    if (dialog_waiting()) {
        if (g_tick % 8 == 0) tap(400, 300);
        return;
    }
    if (g_tick < g_field_settle && getenv("EH_BOTDBG") && player()) {
        uint32_t p0 = player(), pm = M32(p0 + 0x48);
        char buf[200]; int k2 = 0;
        for (uint32_t pr = pm ? M32(pm + 4) : 0, q2 = 0; pr && q2 < 6; pr = M32(pr + 4), q2++) k2 += snprintf(buf + k2, sizeof buf - k2, " %06x", M32(pr) - IMG);
        buf[k2] = 0;
        uint32_t fi = G(VA_FIELD);
        if (fi && g_field_settle - g_tick >= 19) host_log("[arrive] field event: +0x100 %x +0x104 %x +0x108 %x +0x10C %x world %08x", M32(fi + 0x100), M32(fi + 0x104), M32(fi + 0x108), M32(fi + 0x10C), M32(w));
        host_log("[arrive] t-%d pos %d,%d procs:%s hold %d", g_field_settle - g_tick, (int32_t)M32(p0 + 0x14), (int32_t)M32(p0 + 0x18), buf, g_hold);
    }
    if (g_tick < g_field_settle) {
        /* a Direct-mode walk toward the previous field's exit keeps going after the field change and can walk the
         * character straight back out (field 32 arrival next to the trigger to 0): tap the character's own spot */
        /* the touch-up sent at the state change arrives while the field loads and is dropped, so the held-direction
         * walk (cMoveKeyProcess @000d5614) keeps going in the new field (32 -> walked straight back into the trigger
         * to 0). A real finger lifts once the field is shown: send the touch-up again during the settle frames */
        if (state == 3 && (int32_t)M32(w + 0xC) >= 0 && g_tick % 4 == 0) java_set_touch(2, g_hold_x, g_hold_y);
        release(); return;
    }
    if (getenv("EH_QUESTJUMP")) {
        /* EH_QUESTJUMP=<main quest id>: boss-test shortcut that puts the story at a given main quest with the original
         * functions, as if every earlier main quest had been completed: for each earlier quest i,
         * cGameWorld::SetDoneEvent(info_i+0xE, 1, 0) @0012a688 (what cQuestManager::CompleteQuest @000da090 sets) and
         * cQuestManager::SetCompleteQuset(0, i) @000d9344; then cQuestManager::AddQuest(0, id) @000da274 */
        static int done;
        uint32_t rm = G(0x19ae84u), qm = G(0x19ae7c);
        if (!done && rm && qm && player()) {
            done = 1;
            int id = atoi(getenv("EH_QUESTJUMP"));
            for (int i = 0; i < id && i < (int32_t)M32(rm + 0x228); i++) {
                uint32_t info = M32(rm + 0x22C) + 0x1C * i;
                if (M16(info + 0xE)) { uint32_t a[4] = {w, M16(info + 0xE), 1, 0}; rc_call(c, 0x11a688, 4, a); }
                uint32_t b[3] = {qm, 0, (uint32_t)i}; rc_call(c, 0xc9344, 3, b);
            }
            if (getenv("EH_SETEVENTS")) {
                /* story events the boss field scripts check (found with EH_CONDTRACE), set with the original setter */
                for (const char *e = getenv("EH_SETEVENTS"); e && *e; ) {
                    uint32_t a[4] = {w, (uint32_t)atoi(e), 1, 0}; rc_call(c, 0x11a688, 4, a);
                    host_log("[bot] event %u set", a[1]);
                    e = strchr(e, ','); if (e) e++;
                }
            }
            uint32_t a2[3] = {qm, 0, (uint32_t)id}; rc_call(c, 0xca274, 3, a2);
            if (getenv("EH_SETLINK")) {
                /* activate a linkstone as the story script does (op 0xB7, cGameWorld::SetLinkStoneFlag @00128e4c) */
                uint32_t a[3] = {w, 1, (uint32_t)atoi(getenv("EH_SETLINK"))}; rc_call(c, 0x118e4c, 3, a); /* (on, index) */
                host_log("[bot] linkstone %u activated", a[2]);
            }
            if (getenv("EH_FIRESCRIPT")) {
                /* start a story script the way the engine does: cScriptEngine::PendingScript(id) @000e70f0
                 * (e.g. 19163, the arrival script in linkstone 36's record +4 high half = the final battle) */
                uint32_t a[2] = {G(0x19ae88u), (uint32_t)atoi(getenv("EH_FIRESCRIPT"))}; rc_call(c, 0xd70f0, 2, a);
                host_log("[bot] script %u pended", a[1]);
            }
            host_log("[bot] quest jump: main quest %d (earlier quests marked complete)", id);
            return;
        }
    }
    if (getenv("EH_LINKDUMP")) {
        /* linkstones: count s32 res+0x27C, info res+0x288 stride 0x14 (u16 +4 island, u16 +8 field, u16 +0xA sub;
         * cResourceManager::GetLinkStoneInfo @000dc6b8); activated = bit i of u32[] world+0x108
         * (cGameWorld::IsLinkStoneFlag @00128e30) */
        static int done;
        uint32_t rm = G(0x19ae84u);
        if (!done++ && rm) {
            for (int i = 0; i < (int32_t)M32(rm + 0x27C); i++) {
                uint32_t li = M32(rm + 0x288) + 0x14 * i;
                host_log("[link] %d island %u field %u/%u active %d raw %08x %08x %08x %08x %08x", i, M16(li + 4), M16(li + 8), M16(li + 0xA),
                         (M32(w + 0x108 + 4 * (i >> 5)) >> (i & 31)) & 1, M32(li), M32(li + 4), M32(li + 8), M32(li + 0xC), M32(li + 0x10));
            }
        }
    }
    if (getenv("EH_QUESTDUMP")) {
        /* main quest table (spec 1.3): count s32 res+0x228, array res+0x22C stride 0x1C; title = *(res+0x34) + u32 +0,
         * mission = *(res+0x50) + u32 +0x18 + 4, target field s16 +0x14 */
        static int done;
        uint32_t rm = G(0x19ae84u);
        if (!done++ && rm) {
            int n = (int32_t)M32(rm + 0x228);
            for (int i = 0; i < n; i++) {
                uint32_t info = M32(rm + 0x22C) + 0x1C * i;
                uint32_t tt = M32(rm + 0x34), mt = M32(rm + 0x50);
                host_log("[quest] %d field %d island %d event %u script %d title %s | %s", i, (int16_t)M16(info + 0x14), (int16_t)M16(info + 0x16), M16(info + 0xE), (int32_t)M32(info + 8),
                         tt ? (const char *)GP(tt + M32(info)) : "?", mt ? (const char *)GP(mt + M32(info + 0x18) + 4) : "?");
            }
        }
    }
    if (getenv("EH_PLDUMP") && player()) {
        static int done; uint32_t p0 = player();
        if (!done++) {
            for (int o = 0; o < 0x480; o += 16)
                host_log("[pldump] +%03x %08x %08x %08x %08x", o, M32(p0 + o), M32(p0 + o + 4), M32(p0 + o + 8), M32(p0 + o + 12));
            {
                uint32_t om = M32(w + 0x650);
                int ppx = (int32_t)M32(p0 + 0x14), ppy = (int32_t)M32(p0 + 0x18);
                host_log("[box] player at %d,%d box %d,%d %dx%d", ppx, ppy, (int16_t)M16(p0 + 0xC), (int16_t)M16(p0 + 0xE), (int16_t)M16(p0 + 0x10), (int16_t)M16(p0 + 0x12));
                for (int kind = 0; kind < 14; kind++) {
                    uint32_t L = M32(om + 0x14 + 4 * kind);
                    for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                        uint32_t o2 = M32(n + 4);
                        if (!o2 || o2 == p0) continue;
                        int ox = (int32_t)M32(o2 + 0x14), oy = (int32_t)M32(o2 + 0x18);
                        if (abs(ox - ppx) < 64 && abs(oy - ppy) < 64)
                            host_log("[box] list %d vt %06x type %d at %d,%d box %d,%d %dx%d flags %x", kind, M32(o2) - IMG, (int16_t)M16(o2 + 6), ox, oy,
                                     (int16_t)M16(o2 + 0xC), (int16_t)M16(o2 + 0xE), (int16_t)M16(o2 + 0x10), (int16_t)M16(o2 + 0x12), M32(o2 + 0x2C));
                    }
                }
            }
            {
                /* player processes: cProcessManager at player+0x48: +4 head (next at proc+4), +0xC layer-1 process */
                uint32_t pm = M32(p0 + 0x48);
                for (uint32_t pr = pm ? M32(pm + 4) : 0, k = 0; pr && k < 16; pr = M32(pr + 4), k++)
                    host_log("[proc] layer0 #%u vt %06x", k, M32(pr) - IMG);
                if (pm && M32(pm + 0xC)) host_log("[proc] layer1 vt %06x", M32(M32(pm + 0xC)) - IMG);
            }
            for (int o = 0; o < 0x800; o += 16)
                host_log("[wdump] +%03x %08x %08x %08x %08x", o, M32(w + o), M32(w + o + 4), M32(w + o + 8), M32(w + o + 12));
            for (int o = 0; o < 0x220; o += 16)
                host_log("[gmdump] +%03x %08x %08x %08x %08x", o, M32(g + o), M32(g + o + 4), M32(g + o + 8), M32(g + o + 12));
        }
    }
    if (getenv("EH_MOVETEST") && player() && g_tick > atoi(getenv("EH_MOVETEST"))) {
        /* input test: plain taps in 8 directions at 100 px, position logged before each */
        static int mt;
        uint32_t p0 = player();
        int ppx = (int32_t)M32(p0 + 0x14) - scroll_x(), ppy = (int32_t)M32(p0 + 0x18) - scroll_y();
        if (++mt % 40 == 0) {
            int k = (mt / 40) & 7; double a = k * 3.14159265 / 4;
            uint32_t pm = M32(p0 + 0x48), ip = pm ? M32(pm + 0xC) : 0, bui = G(VA_BUI);
            host_log("[movetest] player %d,%d tap dir %d | w+0x1dc %u w0 %x sys8 %x bui+0x180 %d idle+0x20 %d +0x24 %d +0x28 %d +0x414 %x", (int32_t)M32(p0 + 0x14), (int32_t)M32(p0 + 0x18), k,
                     M8(w + 0x1dc), M32(w), G(VA_SYSINFO8), bui ? (int32_t)M32(bui + 0x180) : -1, ip ? (int32_t)M32(ip + 0x20) : -1, ip ? (int32_t)M32(ip + 0x24) : -1,
                     ip ? (int32_t)M32(ip + 0x28) : -1, M32(p0 + 0x414));
            tap(sx(ppx + (int)(100 * cos(a))), sy(ppy + (int)(100 * sin(a))));
        }
        if (mt % 40 == 8 || mt % 40 == 20) {
            uint32_t pm = M32(p0 + 0x48);
            char buf[256]; int k2 = 0;
            for (uint32_t pr = pm ? M32(pm + 4) : 0, q = 0; pr && q < 8; pr = M32(pr + 4), q++) k2 += snprintf(buf + k2, sizeof buf - k2, " %06x", M32(pr) - IMG);
            buf[k2] = 0;
            host_log("[movetest]  +%d procs:%s | layer1 %06x", mt % 40, k2 ? buf : " none", pm && M32(pm + 0xC) ? M32(M32(pm + 0xC)) - IMG : 0);
        }
        return;
    }
    if (g_tick == g_field_settle && getenv("EH_OBJDUMP")) {
        uint32_t om = M32(w + 0x650), pl0 = player();
        host_log("[obj] field %d/%d player %d,%d", fld, sub, pl0 ? (int32_t)M32(pl0 + 0x14) : 0, pl0 ? (int32_t)M32(pl0 + 0x18) : 0);
        for (int kind = 0; kind < 14; kind++) {
            if (kind == 8) continue;
            uint32_t L = M32(om + 0x14 + 4 * kind);
            for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (o) host_log("[obj] list %d kind %u vt %06x type %d inst %d at %d,%d rect %dx%d flags %x dest %d/%d scp %u", kind, M8(o + 4), M32(o) - IMG, (int16_t)M16(o + 6),
                                (int16_t)M16(o + 0x44), (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18), (int16_t)M16(o + 0x38), (int16_t)M16(o + 0x3A), M32(o + 0x2C), (int16_t)M16(o + 0x3C), (int16_t)M16(o + 0x3E), M16(o + 0x40));
            }
        }
    }
    {
        /* backpack overflow: the original refuses to move (cIdleKeyProcess::MoveKeyProcess @000d34cc ->
         * cGameWorld::WarnIfPlayerCannotMoveCauseBackpackIsFull @0012c898, cInventory::IsBackpackOverflown
         * @0012fb70: item count of the list at inv+4 (next at item+0x48) > capacity inv+0x1C). A player sells items
         * at the shop; the bot does what the shop's sell does, cFormStore::doCompleteSelling @0011e12c:
         * CheckDelSpecialItem(item,1), RemoveItem(item,n), player->DelSlotInfo(item), AddGold(n*price).
         * Sellable = the shop's first tab, category mask 0x2DFF (DAT_001770e4[0], cStoreItemList @0011e8xx) and not
         * equipped (item+4 & 8); n = whole stack for categories 6..8 (doReadySelling @0011dee4), price u16 info+10
         * (info = item+0x38). Cheapest first. */
        uint32_t inv = G(0x19b15cu);
        int cnt = 0;
        for (uint32_t it = inv ? M32(inv + 4) : 0; it; it = M32(it + 0x48)) cnt++;
        if (getenv("EH_BOTDBG") && g_tick % 270 == 0) host_log("[botdbg] backpack %d / %d", cnt, inv ? (int32_t)M32(inv + 0x1C) : -1);
        if (inv && cnt > (int32_t)M32(inv + 0x1C)) {
            while (cnt > (int32_t)M32(inv + 0x1C) - 2) {
                uint32_t best = 0; int bp = 1 << 30;
                for (uint32_t it = M32(inv + 4); it; it = M32(it + 0x48)) {
                    int cat = M16(it + 6);
                    if (!((0x2DFF >> cat) & 1) || (M16(it + 4) & 8) || !M32(it + 0x38)) continue;
                    {
                        /* keep items a main-quest item objective asks for (type 0: u8 cat d+4, u16 id d+6, matched like
                         * cBackPack::GetItemCount @0012fed4 on item u16 +6 / s16 +8) */
                        uint32_t qm = G(0x19ae7c), q = qm ? M32(qm) : 0; int need = 0;
                        for (int i = 0; q && i < M8(q + 8) && i < 8; i++) {
                            uint32_t d = q + 0x1C + 8 * i;
                            if (M32(d) == 0 && M8(d + 4) == cat && (int16_t)M16(d + 6) == (int16_t)M16(it + 8)) need = 1;
                        }
                        if (need) continue;
                    }
                    int pr = M16(M32(it + 0x38) + 10);
                    if (pr < bp) { bp = pr; best = it; }
                }
                if (!best) { host_log("[bot] backpack overflown and nothing sellable"); break; }
                int cat = M16(best + 6);
                uint32_t a0[1] = {best};
                int n = ((0x1c0 >> cat) & 1) ? (int)rc_call(c, 0xfeadc, 1, a0) : 1;
                uint32_t a1[3] = {inv, best, 1}; rc_call(c, 0x11fdf8, 3, a1);
                uint32_t a2[3] = {inv, best, (uint32_t)n}; rc_call(c, 0x1201a8, 3, a2);
                uint32_t a3[2] = {player(), best}; rc_call(c, 0xb4980, 2, a3);
                uint32_t a4[2] = {inv, (uint32_t)(n * bp)}; rc_call(c, 0x11facc, 2, a4);
                host_log("[bot] backpack overflown (%d > %d): sold category %d x%d for %d gold", cnt, (int32_t)M32(inv + 0x1C), cat, n, n * bp);
                cnt = 0;
                for (uint32_t it = M32(inv + 4); it; it = M32(it + 0x48)) cnt++;
            }
        }
    }
    if (M32(g + 0x1fc)) {
        /* pending quest-mission popup (cGameManager::PendShowMission @00126b6c): cGameWorld::Run @0012eb2c runs no
         * script while gm+0x1fc is set, and cShowPendingMission::ShowMission @00127278 closes on a touch release */
        if (g_tick % 8 == 0) { host_log("[bot] dismissing mission popup"); tap(400, 240); }
        return;
    }
    if (getenv("EH_WARPTO")) {
        /* EH_WARPTO=group/sub[/fromgroup/fromsub]: boss test shortcut (survey: Salamander 37/5, Dragon 37/6).
         * Warps with the original cGameManager::PendMoveField @00127884 to the "from" field, then enters the target
         * the normal way: fires the exit leading there with cGameWorld::fireSomethingEvent(0, exit) @0012d024, so the
         * arrival position and the field's entry scripts are the original ones. Without "from": direct warp. */
        static int stage, wt;
        int v[4] = {-1, -1, -1, -1}; const char *e = getenv("EH_WARPTO");
        for (int k = 0; k < 4 && e; k++) { v[k] = atoi(e); e = strchr(e, '/'); if (e) e++; }
        int via = v[2] >= 0;
        if (stage == 2 && getenv("EH_SETUPONLY") && fld == v[0] && sub == v[1] && player()) {
            /* EH_SETUPONLY=1: comparison-save setup. Once in the target field with no script running (gm+0x210) for
             * ~4 s, hand control back (EH_CTRL then saves through the original's System menu) */
            static int idle;
            idle = M32(g + 0x210) ? 0 : idle + 1;
            if (idle > 110) { release(); g_setup_hold = 1; host_log("[bot] setup done in %d/%d", fld, sub); }
            return;
        }
        if (stage == 2 && getenv("EH_WARPGAUGE") && fld == v[0] && sub == v[1] && player() && ++wt > 81) {
            /* no script started the fight: register the field's boss in the gauge the way script opcode 0xC4 does,
             * cGameWorld::SetBossGauge(1, instId, 0) @0012b698 */
            uint32_t bui = G(VA_BUI);
            stage = 3;
            if (bui && !M32(bui + 0x128)) {
                uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 1);
                for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    static const char *bv[] = {"_ZTV16cMonsterBossEmbk", "_ZTV18cMonsterBossDragon", "_ZTV22cMonsterBossHynWarrior",
                        "_ZTV27cMonsterBossHynWarriorEvent", "_ZTV22cMonsterBossSalamander", "_ZTV18cMonsterBossEamoru"};
                    int isb = 0;
                    for (int b = 0; o && b < 6; b++) { uint32_t vs = rc_sym(bv[b]); if (vs && M32(o) == IMG + vs + 8) isb = 1; }
                    if (isb && (int16_t)M16(o + 0x44) > 0) {
                        uint32_t a[4] = {w, 1, (uint32_t)(int16_t)M16(o + 0x44), 0};
                        rc_call(c, 0x11b698, 4, a);
                        host_log("[bot] boss gauge set for inst %d (maxhp %d): %08x", (int16_t)M16(o + 0x44), (int32_t)M32(o + 0x98), M32(bui + 0x128));
                        host_log("[bot]  boss flags %x faction %d pos %d,%d player %d,%d", M32(o + 0x2C), (int16_t)M16(o + 0x70),
                                 (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18), (int32_t)M32(player() + 0x14), (int32_t)M32(player() + 0x18));
                        break;
                    }
                }
            }
        }
        /* warp only between scripts: a new game starts in 108/1 with the intro script (gm+0x210 set) and its dialogs;
         * warping away mid-script leaves gm+0x210 set for good in the target field, and the bot waits on it forever */
        if (stage < 2 && player() && !M32(g + 0x210) && ++wt > 54) {
            wt = 0;
            if (!via) {
                /* first warp keeps the stale position of the old field; once there, warp again entering through one
                 * of the field's own exits (entry = its instId) so SetFieldLoadCharPos places the player properly */
                uint32_t entry = 0;
                if (fld == v[0] && sub == v[1]) {
                    uint32_t om = M32(w + 0x650), L = getenv("EH_WARPENTRY") ? M32(om + 0x14 + 4 * 5) : 0;
                    for (uint32_t n = L ? M32(L + 0x10) : 0; n && !entry; n = M32(n)) {
                        uint32_t o = M32(n + 4);
                        if (o && (int16_t)M16(o + 6) == 0 && (int16_t)M16(o + 0x44) > 0) entry = (uint32_t)(int16_t)M16(o + 0x44);
                    }
                    if (!entry || stage == 1) {
                        /* boss stages have no exits: the player keeps the old field's coordinates. If those are not
                         * walkable, put the player on the nearest walkable spot below the boss with the player's
                         * SetPos virtual (+0x18), as cGameWorld::SetFieldLoadCharPos @0012e3ac does on arrival */
                        uint32_t f = G(VA_FIELD), pl0 = player();
                        int ppx = (int32_t)M32(pl0 + 0x14), ppy = (int32_t)M32(pl0 + 0x18);
                        g_bx = (int16_t)M16(pl0 + 0xC); g_by = (int16_t)M16(pl0 + 0xE); g_bw = (int16_t)M16(pl0 + 0x10); g_bh = (int16_t)M16(pl0 + 0x12);
                        if (g_bw <= 0 || g_bh <= 0) { g_bx = -6; g_by = -4; g_bw = 12; g_bh = 8; }
                        Target bt;
                        {
                            /* a field with an active linkstone and no exits is entered by linkstone travel: arrive next
                             * to the stone (linkstone info +8 field, +0xC/+0xE position) */
                            uint32_t rm = G(0x19ae84u); int lsx = -1, lsy = -1;
                            for (int i = 0; !entry && rm && i < (int32_t)M32(rm + 0x27C); i++) {
                                uint32_t li = M32(rm + 0x288) + 0x14 * i;
                                if (M16(li + 8) == (uint32_t)fld) { lsx = (int16_t)M16(li + 0xC); lsy = (int16_t)M16(li + 0xE); }
                            }
                            if (lsx >= 0 && f) {
                                int bx2 = lsx, by2 = lsy + 32;
                                for (int r = 0; r < 96 && !pos_ok(f, bx2, by2); r += 8) { by2 = lsy + 32 + r; if (pos_ok(f, bx2, by2)) break; bx2 = lsx + r; }
                                uint32_t a[3] = {pl0, (uint32_t)bx2, (uint32_t)by2}; rc_call(c, M32(M32(pl0) + 0x18) - IMG, 3, a);
                                host_log("[bot] arrival at the field's linkstone %d,%d", bx2, by2);
                                stage = 2; return;
                            }
                        }
                        /* no reachability test: the kept position can be cut off from the boss (159/1 at 135,443) */
                        if (!entry && f && nearest_monster_ex(pl0, &bt, 0)) {
                            int bx = (int32_t)M32(bt.obj + 0x14), by = (int32_t)M32(bt.obj + 0x18), best = -1, bxp = 0, byp = 0;
                            for (int dy = 40; dy <= 240; dy += 8)
                                for (int dx = -160; dx <= 160; dx += 8)
                                    if (pos_ok(f, bx + dx, by + dy) && (best < 0 || dx * dx + dy * dy < best)) { best = dx * dx + dy * dy; bxp = bx + dx; byp = by + dy; }
                            host_log("[bot] arrival %d,%d walkable %d; boss-side spot %d,%d", ppx, ppy, pos_ok(f, ppx, ppy), bxp, byp);
                            if (best >= 0) { uint32_t a[3] = {pl0, (uint32_t)bxp, (uint32_t)byp}; rc_call(c, M32(M32(pl0) + 0x18) - IMG, 3, a); }
                        }
                        stage = 2; return;
                    }
                }
                static int sent;
                if (!entry && sent) return; /* still loading */
                sent = 1;
                uint32_t a[4] = {G(VA_GM), (uint32_t)v[0], (uint32_t)v[1], entry};
                rc_call(c, 0x117884, 4, a); stage = entry ? 1 : 0; host_log("[bot] warp to %d/%d entry %u", v[0], v[1], entry);
                return;
            }
            if (fld != v[2] || sub != v[3]) {
                if (stage == 0) { uint32_t a[4] = {G(VA_GM), (uint32_t)v[2], (uint32_t)v[3], 0}; rc_call(c, 0x117884, 4, a); stage = 1; host_log("[bot] warp to %d/%d", v[2], v[3]); }
                return;
            }
            uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 5);
            for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (o && (int16_t)M16(o + 6) == 0 && (int16_t)M16(o + 0x3C) == v[0] && (int16_t)M16(o + 0x3E) == v[1]) {
                    uint32_t a[3] = {w, 0, o};
                    host_log("[bot] entering %d/%d through exit inst %d", v[0], v[1], (int16_t)M16(o + 0x44));
                    rc_call(c, 0x11d024, 3, a); stage = 2; return;
                }
            }
            host_log("[bot] no exit from %d/%d to %d/%d", fld, sub, v[0], v[1]); stage = 2;
            for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (o) host_log("[bot]  exit type %d inst %d dest %d/%d flags %x", (int16_t)M16(o + 6), (int16_t)M16(o + 0x44), (int16_t)M16(o + 0x3C), (int16_t)M16(o + 0x3E), M32(o + 0x2C));
            }
            return;
        }
        if (getenv("EH_SETUPONLY")) return; /* setup runs never walk between the warp steps */
    }
    if (getenv("EH_INDUNSCAN") || getenv("EH_INDUN")) {
        /* dungeons (spec 4.2): dg = *(0x19af1c). EH_INDUN=g/i enters one with the original cDungeonManager::Enter
         * (dg, group, idx) @00121f90 (what script opcode 0xCF does; its pended script adds the stages, 0xCD, and
         * moves in, 0xCE). EH_INDUNSCAN=1 enters every dungeon in turn, logs the stage list (dg+0x2C random
         * stages x dg+0x24, boss stage dg+0x30, exit dg+0x38: {s16 group, s16 sub, s32 entry}) and leaves with
         * cDungeonManager::Exit @00121ec0. Counts: s32 res+(g+0xA4)*4 (cResourceManager::GetDungeonInitInfo @000dc6dc). */
        static int ig = 0, ii = -1, it, st; /* st 0: enter next, 1: inside, waiting to log, 2: single mode playing */
        uint32_t dg = G(0x19af1cu), rm = G(0x19ae84u);
        int scan = getenv("EH_INDUNSCAN") != NULL;
        if (st != 2 && dg && rm && player() && (st || !M32(g + 0x210)) && ++it > (st ? 160 : 54)) {
            it = 0;
            if (st == 1) {
                { uint32_t se = G(0x19ae88u); host_log("[indun]  later: gm+0x1fc %u gm+0x210 %u se+4 %u se+8 %x world %08x", M32(g + 0x1fc), M32(g + 0x210), se ? M32(se + 4) : 0, se ? M32(se + 8) : 0, M32(w)); }
                host_log("[indun] %d/%d stages %d boss %d/%d entry %d exit %d/%d (now in %d/%d)", ig, ii, (int32_t)M32(dg + 0x24),
                         (int16_t)M16(dg + 0x30), (int16_t)M16(dg + 0x32), (int32_t)M32(dg + 0x34), (int16_t)M16(dg + 0x38), (int16_t)M16(dg + 0x3A), fld, sub);
                if (!scan) { st = 2; return; }
                uint32_t a[1] = {dg}; rc_call(c, 0x111ec0, 1, a);
                st = 0; return;
            }
            if (!scan) {
                const char *e = getenv("EH_INDUN"); ig = atoi(e); ii = strchr(e, '/') ? atoi(strchr(e, '/') + 1) : 0;
            } else {
                ii++;
                while (ig < 2 && ii >= (int32_t)M32(rm + (ig + 0xA4) * 4)) { ig++; ii = 0; }
                if (ig >= 2) { host_log("[indun] scan done"); exit(0); }
            }
            host_log("[indun] entering %d/%d", ig, ii);
            host_log("[indun]  before: gm+0x210 %u", M32(g + 0x210));
            uint32_t a[3] = {dg, (uint32_t)ig, (uint32_t)ii}; rc_call(c, 0x111f90, 3, a);
            host_log("[indun]  script %u, gm+0x210 %u, world flags %08x", M16(M32(dg + 0xC) + 6), M32(g + 0x210), M32(w));
            { uint32_t ini = M32(dg), pr = M32(dg + 0x10);
              host_log("[indun]  init %08x progress %08x clears %u rec %02x %02x %02x %02x %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x %02x", ini, pr, pr ? M8(pr + 2) : 999,
                       M8(ini+4),M8(ini+5),M8(ini+6),M8(ini+7),M8(ini+8),M8(ini+9),M8(ini+10),M8(ini+11),M8(ini+12),M8(ini+13),M8(ini+14),M8(ini+15),M8(ini+16),M8(ini+17),M8(ini+18),M8(ini+19)); }
            st = 1;
            return;
        }
        if (scan || st != 2) { if (!dialog_waiting() && !M32(g + 0x210)) return; }
    }
    if (getenv("EH_WARPSCAN")) {
        /* EH_WARPSCAN: survey which fields place boss monsters. Warps with the original
         * cGameManager::PendMoveField(field, sub, entryInstId) @00127884 (entry 0 keeps the player position,
         * cGameWorld::SetFieldLoadCharPos @0012e3ac) and lists boss-class objects in every object list. */
        /* list "g/s,g/s,...": valid pairs are the epic08.png chunk tree [1][group][sub+1] (cResourceManager::LoadField
         * @000dd664 loads sets group, group|0x100, (sub+1)<<8|group); a missing set crashes in cField::DrawField */
        static const char *ws_p; static int ws_ticks;
        if (!ws_p) {
            ws_p = getenv("EH_WARPSCAN");
            /* patch table (cResourceManager::GetTopPatchChunkNo @000xxxxx): gp_resMrgInstance+0x2b8 count, +0x2bc array
             * of records {.. +4 s16 n, +7 s8 epic3xx no, then n x 8 bytes: +8 s32 file, +0xd u8 count, +0xe s16 first} */
            uint32_t rm = G(0x19ae84u);
            int np = rm ? (int32_t)M32(rm + 0x2b8) : 0;
            for (int i = 0; i < np; i++) {
                uint32_t r = M32(M32(rm + 0x2bc) + 4 * i);
                int n = (int16_t)M16(r + 4);
                for (int k = 0; k < n; k++) {
                    uint32_t e2 = r + 8 * k;
                    host_log("[patch] epic3%02d.png: file %d groups %d..%d (chunk base %d)", (int8_t)M8(r + 7), (int32_t)M32(e2 + 8),
                             (int16_t)M16(e2 + 0xe), (int16_t)M16(e2 + 0xe) + M8(e2 + 0xd) - 1, M8(r + 8 * (k + 1) + 4));
                }
            }
        }
        if (++ws_ticks < 40 || !player()) return;
        uint32_t om = M32(w + 0x650);
        for (int kind = 0; kind < 14; kind++) {
            if (kind == 8) continue;
            uint32_t list = M32(om + 0x14 + 4 * kind);
            if (list) for (uint32_t n = M32(list + 0x10); n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                static const char *bosses[] = {"_ZTV16cMonsterBossEmbk", "_ZTV18cMonsterBossDragon", "_ZTV22cMonsterBossHynWarrior",
                    "_ZTV27cMonsterBossHynWarriorEvent", "_ZTV22cMonsterBossSalamander", "_ZTV18cMonsterBossEamoru", "_ZTV23cMonsterBossEamoruSword"};
                const char *nm = NULL;
                for (int b = 0; o && b < 7; b++) { uint32_t v = rc_sym(bosses[b]); if (v && M32(o) == IMG + v + 8) nm = bosses[b]; }
                if (nm) host_log("[warpscan] field %d/%d list %d inst %d %s hp %d", fld, sub, kind, (int16_t)M16(o + 0x44), nm, (int32_t)M32(o + 0x94));
            }
        }
        {   /* exits of this field for the field graph (trigger s16 +0x3C/+0x3E = destination group/sub) */
            uint32_t L5 = M32(om + 0x14 + 4 * 5);
            for (uint32_t n = L5 ? M32(L5 + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (!o || M8(o + 4) == 0xD || M32(o) != IMG + rc_sym("_ZTV8cTrigger") + 8) continue;
                if ((int16_t)M16(o + 0x3C) <= 0 && (int16_t)M16(o + 6) != 0) continue;
                host_log("[exit] %d/%d -> %d/%d type %d inst %d at %d,%d", fld, sub, (int16_t)M16(o + 0x3C), (int16_t)M16(o + 0x3E),
                         (int16_t)M16(o + 6), (int16_t)M16(o + 0x44), (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18));
            }
        }
        if (!*ws_p) { host_log("[warpscan] done"); exit(0); }
        int ng = atoi(ws_p), ns = strchr(ws_p, '/') ? atoi(strchr(ws_p, '/') + 1) : 0;
        ws_p = strchr(ws_p, ','); ws_p = ws_p ? ws_p + 1 : "";
        host_log("[warpscan] field %d/%d scanned, warping to %d/%d", fld, sub, ng, ns);
        uint32_t a[4] = {G(VA_GM), (uint32_t)ng, (uint32_t)ns, 0};
        rc_call(c, 0x117884, 4, a);
        ws_ticks = 0;
        return;
    }
    if (M32(g + 0x210)) { release(); return; } /* a script is running without waiting for input */
    uint32_t pl = player();
    if (!pl) return;
    if (getenv("EH_BOTDBG") && atoi(getenv("EH_BOTDBG")) > 1 && g_tick % 135 == 0) {
        uint32_t m = G(VA_MWM), list = m ? M32(m) : 0, om = M32(w + 0x650);
        host_log("[botdbg] player %d,%d scroll %d,%d flags %x +0x60 %x world %08x gm1b4 %x hold %d", (int32_t)M32(pl + 0x14), (int32_t)M32(pl + 0x18), scroll_x(), scroll_y(),
                 M32(pl + 0x2C), M32(pl + 0x60), M32(w), M32(g + 0x1b4), g_hold);
        if (list) for (uint32_t n = M32(list + 0x10); n; n = M32(n)) { uint32_t b = M32(n + 4); if (b) host_log("[botdbg]  msgbox type %d flags %x", (int32_t)M32(b + 4), M32(b + 0xC)); }
        for (int k = 0; k < 14; k++) {
            uint32_t L = M32(om + 0x14 + 4 * k);
            if (!L) continue;
            for (uint32_t n = M32(L + 0x10); n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (o && k != 1 && k != 3) host_log("[botdbg]  list%d kind %x type %d pos %d,%d flags %x p90 %x", k, M8(o + 4), (int16_t)M16(o + 6), (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18), M32(o + 0x2C), M32(o + 0x90));
            }
        }
        int cnt = (int32_t)M32(w + 0x728);
        for (int i = 0; i < cnt && i < 64; i++) {
            uint32_t o = M32(w + 0x694 + 0x18 + 8 * i);
            host_log("[botdbg]  minimap type %d obj pos %d,%d", (int32_t)M32(w + 0x694 + 0x14 + 8 * i), o ? (int32_t)M32(o + 0x14) : 0, o ? (int32_t)M32(o + 0x18) : 0);
        }
    }
    int px = (int32_t)M32(pl + 0x14), py = (int32_t)M32(pl + 0x18);
    g_bx = (int16_t)M16(pl + 0xC); g_by = (int16_t)M16(pl + 0xE); g_bw = (int16_t)M16(pl + 0x10); g_bh = (int16_t)M16(pl + 0x12);
    if (g_bw <= 0 || g_bh <= 0) { g_bx = -6; g_by = -4; g_bw = 12; g_bh = 8; }
    {
        /* frozen without any press (nothing reachable, e.g. reloaded inside walls): after ~60 s re-enter the field
         * through one of its exits with the original cGameManager::PendMoveField @00127884 */
        static int still_x = -1, still_y, still_t;
        if (px != still_x || py != still_y) { still_x = px; still_y = py; still_t = g_tick; }
        else if (g_tick - still_t > 27 * 60) {
            still_t = g_tick;
            uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 5), entry = 0;
            for (uint32_t n = L ? M32(L + 0x10) : 0; n && !entry; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (o && M32(o) == IMG + rc_sym("_ZTV8cTrigger") + 8 && (int16_t)M16(o + 0x44) > 0 && (int16_t)M16(o + 0x3C) > 0)
                    entry = (uint32_t)(int16_t)M16(o + 0x44);
            }
            host_log("[bot] no movement for 60 s at %d,%d: re-entering field %d/%d at exit inst %u", px, py, fld, sub, entry);
            if (entry) { uint32_t a[4] = {G(VA_GM), (uint32_t)fld, (uint32_t)sub, entry}; rc_call(c, 0x117884, 4, a); return; }
        }
    }
    if (getenv("EH_WATCHDOG")) {
        /* progress watchdog for natural-progression runs (game time, 27 ticks/s). Progress = field change, main quest
         * id/flags change, EXP (player+0x33C, cCharacter::AddExp @000c3790), gold (inv+0x48) or backpack item count.
         *   - stuck in place: 15 s within 64 px
         *   - stuck in a map: 90 s in one field without progress
         *   - looping: 5 min without main quest change
         * On a stall it logs the reason and ends the run (exit 5) so the case can be fixed. */
        static int t_field, t_prog, t_quest, t_place, ax, ay, last_f = -999, last_q = -1, last_qf = -1;
        static uint32_t last_exp, last_gold; static int last_items = -1;
        uint32_t qm = G(0x19ae7c), q = qm ? M32(qm) : 0, inv = G(0x19b15cu);
        int qid = q ? M16(q + 4) : -1, qf = q ? (int)M32(q + 0xC) : -1, items = 0;
        for (uint32_t it = inv ? M32(inv + 4) : 0; it; it = M32(it + 0x48)) items++;
        uint32_t exp = M32(pl + 0x33C), gold = inv ? M32(inv + 0x48) : 0;
        static int init, last_eval;
        if (!init) { init = 1; t_field = t_prog = t_quest = t_place = g_tick; ax = px; ay = py; }
        /* only count time the bot actually controls the field: menus, dialogs, scripts and loading pause it */
        if (g_tick - last_eval > 6) { int gap = g_tick - last_eval - 1; t_field += gap; t_prog += gap; t_quest += gap; t_place += gap; }
        last_eval = g_tick;
        if (fld * 64 + sub != last_f) { last_f = fld * 64 + sub; t_field = t_prog = g_tick; }
        if (qid != last_q || qf != last_qf) { last_q = qid; last_qf = qf; t_quest = t_prog = g_tick; }
        if (exp != last_exp || gold != last_gold || items != last_items) { last_exp = exp; last_gold = gold; last_items = items; t_prog = g_tick; }
        if (abs(px - ax) > 64 || abs(py - ay) > 64) { ax = px; ay = py; t_place = g_tick; }
        const char *why = NULL;
        if (g_tick - t_place > 27 * 15) why = "stuck in place 15 s";
        else if (g_tick - t_prog > 27 * 90) why = "no progress 90 s in this field";
        else if (g_tick - t_quest > 27 * 300) why = "main quest unchanged for 5 min";
        if (why) {
            host_log("[watchdog] last branch %s, tick %d, release_in %d, hold %d", g_branch, g_tick, g_release_in, g_hold);
            host_log("[watchdog] %s: field %d/%d pos %d,%d quest %d flags %x walk target %d,%d (field time %d s) world %08x gm210 %u 1fc %u dlg %d",
                     why, fld, sub, px, py, qid, qf, g_walk_tx, g_walk_ty, (g_tick - t_field) / 27, M32(w), M32(g + 0x210), M32(g + 0x1fc), dialog_waiting());
            for (int i = 0; q && i < M8(q + 8) && i < 8; i++) {
                uint32_t d = q + 0x1C + 8 * i;
                host_log("[watchdog]  objective %d: type %d data %02x %02x %02x %02x", i, (int32_t)M32(d), M8(d + 4), M8(d + 5), M8(d + 6), M8(d + 7));
            }
            { int qg = -1, qs = -1, qi = -1; quest_goal(&qg, &qs, &qi); host_log("[watchdog]  quest_goal -> field %d/%d inst %d", qg, qs, qi); }
            exit(5);
        }
    }
    /* stuck detection */
    if (g_tick % 30 == 0) {
        if (abs(px - g_last_px) + abs(py - g_last_py) < 6 && g_hold) {
            static int frozen_n, frozen_x, frozen_y;
            if (px == frozen_x && py == frozen_y) frozen_n++; else { frozen_n = 0; frozen_x = px; frozen_y = py; }
            if (frozen_n >= 30) {
                /* not a single step in ~30 s in any direction: the original is stuck the same way with such a save
                 * (checked on the emulator: the character turns but cannot move, e.g. saved overlapping an NPC).
                 * Test-mode recovery: re-enter this field through one of its exits with the original
                 * cGameManager::PendMoveField(group, sub, entryInstId) @00127884 */
                uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 5), entry = 0;
                for (uint32_t n = L ? M32(L + 0x10) : 0; n && !entry; n = M32(n)) {
                    uint32_t o = M32(n + 4);
                    if (o && M32(o) == IMG + rc_sym("_ZTV8cTrigger") + 8 && (int16_t)M16(o + 0x44) > 0 && (int16_t)M16(o + 0x3C) > 0)
                        entry = (uint32_t)(int16_t)M16(o + 0x44);
                }
                host_log("[bot] frozen at %d,%d: re-entering field %d/%d at exit inst %u", px, py, fld, sub, entry);
                if (entry) { uint32_t a[4] = {G(VA_GM), (uint32_t)fld, (uint32_t)sub, entry}; rc_call(c, 0x117884, 4, a); }
                frozen_n = 0;
                return;
            }
            if (++g_stuck >= 3) {
                /* a hold that began on an object (bird/NPC) keeps targeting it and never walks: start a new press */
                release();
                g_detour = 40; g_stuck = 0; g_exit_skip++;
                if (++g_walk_detours >= 3) {
                    static int slot;
                    g_avoid[slot].x = g_walk_tx; g_avoid[slot].y = g_walk_ty; g_avoid[slot].until = g_tick + 27 * 30; slot = (slot + 1) & 7; /* slots 8..15: skipped story triggers */
                    g_walk_detours = 0;
                    host_log("[bot] stuck at %d,%d -> avoiding target %d,%d for 30 s", px, py, g_walk_tx, g_walk_ty);
                } else host_log("[bot] stuck at %d,%d -> detour", px, py);
            }
        } else g_stuck = 0;
        g_last_px = px; g_last_py = py;
    }
    if (getenv("EH_BOTMAP") && g_tick % 270 == 0) {
        int qx = px, qy = py; uint32_t qn;
        if (!quest_npc(&qx, &qy, &qn)) trigger_target(0, &qx, &qy, 0);
        dump_map(px, py, qx, qy);
    }
    g_branch = "E-before-engage";
    Target t;
    /* engage monsters only when they are close, unless there is no other objective */
    int qx0, qy0, has_obj = 0; uint32_t qn0;
    if (hint_target(&qx0, &qy0) || quest_npc(&qx0, &qy0, &qn0) || trigger_target(1, &qx0, &qy0, 0) || trigger_target(0, &qx0, &qy0, 0)) has_obj = 1;
    int engage = has_obj ? 90 : 260;
    {
        /* story boss in the quest's target field but not yet fighting (its script starts the fight when the
         * character comes close): walk up to it */
        int qg = -1, qs = -1, qi = -1;
        uint32_t bui = G(VA_BUI);
        int have_goal = quest_goal(&qg, &qs, &qi);
        if ((!have_goal || qg < 0 || qg == fld) && !(bui && M32(bui + 0x128))) { /* also quests without a target field */
            static const char *bv[] = {"_ZTV16cMonsterBossEmbk", "_ZTV18cMonsterBossDragon", "_ZTV22cMonsterBossHynWarrior",
                                       "_ZTV27cMonsterBossHynWarriorEvent", "_ZTV22cMonsterBossSalamander", "_ZTV18cMonsterBossEamoru"};
            uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 1);
            for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4); int isb = 0;
                for (int b = 0; o && b < 6; b++) { uint32_t vs = rc_sym(bv[b]); if (vs && M32(o) == IMG + vs + 8) isb = 1; }
                if (!isb || (int32_t)M32(o + 0x94) <= 0) continue;
                int bx = (int32_t)M32(o + 0x14), by = (int32_t)M32(o + 0x18) + 48;
                int dd = (bx - px) * (bx - px) + (by - py) * (by - py);
                static int logged; if (!logged++) host_log("[bot] story boss in quest field at %d,%d: approaching", bx, by - 48);
                {
                    /* script-driven bosses (the Dragon flies in and out, cMonsterBossDragon::ProcUserAct @0014f364)
                     * cannot be walked to: attack whenever their touch centre is on screen (Direct mode tap = attack) */
                    int tx = (int32_t)M32(o + 0x14), ty = (int32_t)M32(o + 0x18);
                    uint32_t td = M32(o + 0x328);
                    if (td) { tx += (int16_t)M16(td + 0x5C); ty += (int16_t)M16(td + 0x5E); }
                    int lx = tx - scroll_x(), ly = ty - scroll_y();
                    if (M32(o) == IMG + rc_sym("_ZTV18cMonsterBossDragon") + 8) {
                        /* the Dragon is only hittable through its scaled sprite area during the close pass:
                         * cMonsterBossDragon::GetCollisionArea(1, rect) @0014f198 gives it in screen (view) coordinates */
                        uint32_t rect = c->r[13] - 96;
                        M32(rect) = M32(rect + 4) = M32(rect + 8) = M32(rect + 12) = 0;
                        uint32_t a[3] = {o, 1, rect};
                        rc_call(c, 0x13f198, 3, a);
                        int x0 = (int32_t)M32(rect), y0 = (int32_t)M32(rect + 4), x1 = (int32_t)M32(rect + 8), y1 = (int32_t)M32(rect + 12);
                        static int dl; if (getenv("EH_BOTDBG") && dl++ % 54 == 0) host_log("[bot] dragon area %d,%d-%d,%d", x0, y0, x1, y1);
                        if (x1 > x0 && y1 > y0) {
                            /* close pass: a tap on it reads as "walk there" (its touch area is the idle one), so use
                             * the skill slots and the attack key like a player: game keys 0x20,0x21,0x23,0x27,0x29 =
                             * skill slots, 0x25 = attack (cIdleKeyProcess::SlotKeyProcess @000d37c8), injected through
                             * the original GofManager.SimulateKeyPress -> ApplyWaitingKeyPressIfAny @001666d0 */
                            static const int keys[6] = {0x20, 0x21, 0x23, 0x27, 0x29, 0x25};
                            static int nk, nt_k;
                            release(); g_branch = "S-dragon-keys";
                            if (due(&nt_k, 8)) { uint32_t a2[3] = {0, 0, (uint32_t)keys[nk++ % 6]}; rc_call(c, 0x156640, 3, a2); }
                            return;
                        }
                        lx = -1000; ly = -1000;
                    }
                    if (lx > 8 && ly > 42 && lx < g_game_w - 8 && ly < LH - 12) {
                        release(); g_branch = "S-storyboss-attack";
                        static int nt_b; if (due(&nt_b, 6)) tap(sx(lx), sy(ly));
                        return;
                    }
                }
                if (dd > 24 * 24 && !avoided(bx, by)) { walk_toward(px, py, bx, by); return; }
                break;
            }
        }
    }
    {
        /* in the quest's target field without a talk/visit target (item or kill objectives, e.g. "몬스터 오일"
         * drops), hunt every monster in the field */
        int qg = -1, qs = -1, qi = -1;
        if (quest_goal(&qg, &qs, &qi) && qg == fld && qi < 0) engage = 4000;
    }
    {
        /* boss fight (spec 4.1): the creature in the boss gauge, xx_bui+0x128 (cGameWorld::GetBossPtr @00129358),
         * is the objective; walk to it even while it is not yet attackable (flags bit 1 clear until its script starts) */
        uint32_t bui = G(VA_BUI), bo = bui ? M32(bui + 0x128) : 0;
        if (bo && (int32_t)M32(bo + 0x94) > 0) {
            Target bt; bt.obj = bo; bt.x = (int32_t)M32(bo + 0x14); bt.y = (int32_t)M32(bo + 0x18);
            uint32_t td = M32(bo + 0x328);
            if (td) { bt.x += (int16_t)M16(td + 0x5C); bt.y += (int16_t)M16(td + 0x5E); }
            bt.d2 = (bt.x - px) * (bt.x - px) + (bt.y - py) * (bt.y - py);
            if ((M32(bo + 0x2C) & 2) && !(M32(bo + 0x2C) & 8)) { if (!nearest_monster(pl, &t) || t.d2 > 60 * 60) { t = bt; engage = 100000; } }
            else if (!(nearest_monster(pl, &t) && t.d2 < engage * engage)) { walk_toward(px, py, bt.x, bt.y + 40); return; }
        }
    }
    if ((engage == 100000 && t.obj) || (nearest_monster(pl, &t) && t.d2 < engage * engage)) {
        if (t.obj != g_last_target) { g_last_target = t.obj; g_kills_hint++; }
        if (getenv("EH_BOTDBG") && g_tick % 54 == 0)
            host_log("[botdec] monster type %d hp %d/%d at %d,%d (player %d,%d) flags %x faction %d", (int16_t)M16(t.obj + 6),
                     (int32_t)M32(t.obj + 0x94), (int32_t)M32(t.obj + 0x98), t.x, t.y, px, py, M32(t.obj + 0x2C), (int16_t)M16(t.obj + 0x70));
        int mlx = t.x - scroll_x(), mly = t.y - scroll_y();
        int onscr = mlx > 8 && mly > 8 && mlx < g_game_w - 8 && mly < LH - 8;
        uint32_t ff = G(VA_FIELD);
        int is_gauge_boss = G(VA_BUI) && M32(G(VA_BUI) + 0x128) == t.obj;
        if (t.d2 > 40 * 40 && (!onscr || (!is_gauge_boss && ff && !line_ok(ff, px, py, t.x, t.y)))) { walk_toward(px, py, t.x, t.y); return; }
        if (g_tick % 6 == 0) tap(sx(t.x - scroll_x()), sy(t.y - scroll_y())); /* Direct mode: tap = attack */
        return;
    }
    {
        /* gathering (e.g. "크로커스 식물 5개를 채집"): main-quest item objective (type 0) not met and collectible stuff
         * (object kind 9, cCollectStuff, list 9) in the field. A touch next to it starts gathering:
         * cIdleKeyProcess::KeyProcessForSelectedObj @000d3150 -> CollectStuffCheck @000d3094 -> MakeCollectProcess */
        uint32_t qm = G(0x19ae7c), q = qm ? M32(qm) : 0;
        int want = 0;
        for (int i = 0; q && i < M8(q + 8) && i < 8; i++) if (M32(q + 0x1C + 8 * i) == 0) want = 1;
        if (want && !(M32(q + 0xC) & 1)) {
            uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 9), best = 0; int bd = 1 << 30, bx = 0, by = 0;
            for (uint32_t n = L ? M32(L + 0x10) : 0; n; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (!o || M8(o + 4) != 9 || !(M32(o + 0x2C) & 2)) continue;
                int ox = (int32_t)M32(o + 0x14), oy = (int32_t)M32(o + 0x18);
                if (avoided(ox, oy)) continue;
                int ax = ox, ay = oy + (int16_t)M16(o + 0xE) + (int16_t)M16(o + 0x12) + 6; /* just below its box */
                if (!reachable(px, py, ax, ay)) continue;
                int d2 = (ax - px) * (ax - px) + (ay - py) * (ay - py);
                if (d2 < bd) { bd = d2; best = o; bx = ax; by = ay; }
            }
            if (best) {
                static uint32_t last; static int taps;
                if (best != last) { last = best; taps = 0; host_log("[bot] gathering stuff type %d at %d,%d", (int16_t)M16(best + 6), (int32_t)M32(best + 0x14), (int32_t)M32(best + 0x18)); }
                if (bd > 32 * 32) { walk_toward(px, py, bx, by); return; }
                release();
                uint32_t ca[1] = {pl};
                if (rc_call(c, 0xb0b88, 1, ca)) { taps = 0; return; } /* cCharacter::IsCollectingSomething @000c0b88 */
                static int nt_g;
                if (due(&nt_g, 30)) {
                    {
                        uint32_t inv = G(0x19b15cu); int have = 0;
                        for (int i = 0; q && i < M8(q + 8) && i < 8; i++) {
                            uint32_t d = q + 0x1C + 8 * i;
                            if (M32(d) != 0) continue;
                            for (uint32_t it = inv ? M32(inv + 4) : 0; it; it = M32(it + 0x48))
                                if (M16(it + 6) == M8(d + 4) && (int16_t)M16(it + 8) == (int16_t)M16(d + 6)) { uint32_t a0[1] = {it}; have += (int)rc_call(c, 0xfeadc, 1, a0); }
                            if (taps == 0) host_log("[bot] quest item cat %u id %u: have %d of %u", M8(d + 4), M16(d + 6), have, M8(d + 5));
                        }
                    }
                    tap(sx((int32_t)M32(best + 0x14) - scroll_x()), sy((int32_t)M32(best + 0x18) - 6 - scroll_y()));
                    if (++taps > 8) {
                        static int sl; int i = 8 + (sl++ & 7);
                        g_avoid[i].x = (int32_t)M32(best + 0x14); g_avoid[i].y = (int32_t)M32(best + 0x18); g_avoid[i].until = g_tick + 27 * 120;
                        host_log("[bot] stuff at %d,%d does not gather (skill level?): skipping", g_avoid[i].x, g_avoid[i].y);
                    }
                }
                return;
            }
        }
    }
    g_branch = "A-before-hint";
    int hx, hy;
    if (hint_target(&hx, &hy) && !avoided(hx, hy) && reachable(px, py, hx, hy)) {
        host_log_once_per(hx, hy);
        /* the hint marks something to touch (door, signpost, NPC): once close, tap it like a player would */
        int dh2 = (hx - px) * (hx - px) + (hy - py) * (hy - py);
        int lx = hx - scroll_x(), ly = hy - 8 - scroll_y();
        if (dh2 < 56 * 56 && lx > 8 && ly > 8 && lx < g_game_w - 8 && ly < LH - 8) {
            static int htaps; static int hx0, hy0;
            if (abs(hx - hx0) > 24 || abs(hy - hy0) > 24) { hx0 = hx; hy0 = hy; htaps = 0; } /* the marker bobs */
            release(); g_branch = "A1-hint-tap";
            static int nt_h;
            if (due(&nt_h, 25)) {
                tap(sx(lx), sy(ly));
                if (getenv("EH_BOTDBG")) host_log("[bot] hint tap %d at %d,%d (screen %d,%d)", htaps, hx, hy, lx, ly);
                if (++htaps > 6) {   /* nothing happened: leave it for a while */
                    static int sl; int i = 8 + (sl++ & 7);
                    g_avoid[i].x = hx; g_avoid[i].y = hy; g_avoid[i].until = g_tick + 27 * 90;
                    host_log("[bot] hint target at %d,%d did not react to taps: skipping 90 s", hx, hy);
                }
            }
            return;
        }
        g_branch = "A2-hint-walk"; walk_toward(px, py, hx, hy); return;
    }
    uint32_t npc;
    {
        /* quest objective object in this field (instance id), e.g. "talk to blacksmith Zepeta" */
        int qg = -1, qs = -1, qi = -1;
        if (quest_goal(&qg, &qs, &qi) && qg == fld && qi > 0) {
            uint32_t o = find_inst(qi);
            if (o) {
                int ox = (int32_t)M32(o + 0x14), oy = (int32_t)M32(o + 0x18);
                int lx = ox - scroll_x(), ly = oy - 16 - scroll_y();
                uint32_t f = G(VA_FIELD);
                int dn2 = (ox - px) * (ox - px) + (oy - py) * (oy - py);
                if (lx > 8 && ly > 8 && lx < g_game_w - 8 && ly < LH - 8 && (dn2 < 64 * 64 || (f && line_ok(f, px, py, ox, oy + 12)))) {
                    release(); { static int nt_o; if (due(&nt_o, 20)) tap(sx(lx), sy(ly)); } return;
                }
                /* the quest's own target: go for it even when the planner finds no path (NPCs and props around it
                 * fill the occupancy grid); the stuck logic sets it aside if it really cannot be reached */
                if (!avoided(ox, oy + 12)) { g_branch = "A4-questobj-walk"; walk_toward(px, py, ox, oy + 12); return; }
            }
        }
    }
    g_branch = "B-before-questnpc";
    if (quest_npc(&hx, &hy, &npc) && !(npc == g_npc_skip && g_tick < g_npc_skip_until) && reachable(px, py, hx, hy + 12)) {
        g_npc_tapped = npc;
        /* tapping an on-screen NPC makes the game walk the character there and talk
         * (touch centre cNpc::GetTouchCenterX/Y @000cdbd0 = x, y - 16); off-screen: walk toward it */
        int lx = hx - scroll_x(), ly = hy - 16 - scroll_y();
        uint32_t f = G(VA_FIELD);
        int dn2 = (hx - px) * (hx - px) + (hy - py) * (hy - py);
        if (getenv("EH_BOTDBG") && g_tick % 54 == 0)
            host_log("[botdec] quest npc at %d,%d player %d,%d line %d", hx, hy, px, py, f ? line_ok(f, px, py, hx, hy + 12) : -1);
        if (lx > 8 && ly > 8 && lx < g_game_w - 8 && ly < LH - 8 && (dn2 < 64 * 64 || (f && line_ok(f, px, py, hx, hy + 12)))) {
            static uint32_t tn; static int nt;
            if (tn != npc) { tn = npc; nt = 0; }
            release();
            static int nt_n;
            if (due(&nt_n, 20)) {
                tap(sx(lx), sy(ly));
                /* no talk after several taps (e.g. a side-quest marker the bot cannot act on): skip it for 2 min */
                if (++nt > 8) { g_npc_skip = npc; g_npc_skip_until = g_tick + 27 * 120; nt = 0; host_log("[bot] npc at %d,%d does not talk: skipping 2 min", hx, hy); }
            }
            return;
        }
        walk_toward(px, py, hx, hy);
        return;
    }
    g_branch = "C-before-nearest";
    if (nearest_monster(pl, &t)) { walk_toward(px, py, t.x, t.y); return; }
    int ex, ey;
    for (int sk = 0; sk < 8 && trigger_target(1, &ex, &ey, sk); sk++)
        if (!avoided(ex, ey) && reachable(px, py, ex, ey)) {
            /* standing on a story trigger that did not fire (gated by quest state): set it aside for a while */
            if ((ex - px) * (ex - px) + (ey - py) * (ey - py) < 28 * 28) {
                static int slot2; int i = 8 + (slot2++ & 7);
                g_avoid[i].x = ex; g_avoid[i].y = ey; g_avoid[i].until = g_tick + 27 * 90;
                host_log("[bot] story trigger at %d,%d did not fire: skipping it for 90 s", ex, ey);
                continue;
            }
            walk_toward(px, py, ex, ey); return;
        }
    {
    g_branch = "D-exploration";
        /* exploration: map exits (list 5 type 0) and action stuff (list 4: jump/climb links between regions),
         * the reachable one used least often from this field */
        typedef struct { int x, y, kind, dest, stype, ax, ay; } Cand;
        Cand cand[64]; int nc = 0;
        {
            /* exits with their destination field group (s16 +0x3C, cGameWorld::fireSomethingEvent @0012d024) */
            uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 5);
            for (uint32_t n = L ? M32(L + 0x10) : 0; n && nc < 64; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (!o || M8(o + 4) == 0xD || !(M32(o + 0x2C) & 2) || M32(o + 0x90) != 0) continue;
                if ((int16_t)M16(o + 6) != 0 && (int16_t)M16(o + 0x3C) <= 0) continue; /* plain script trigger */
                int dg = (int16_t)M16(o + 0x3C);
                if (fld >= 0 && fld < NG && dg >= 0 && dg < NG) g_edge[fld][dg] = 1;
                {
                    int cx = (int32_t)M32(o + 0x14) + (int16_t)M16(o + 0x38) / 2, cy = (int32_t)M32(o + 0x18) + (int16_t)M16(o + 0x3A) / 2;
                    cand[nc++] = (Cand){cx, cy, 0, dg, -1, cx, cy};
                }
            }
        }
        int qg = -1, qs = -1, qi = -1, hop = -1;
        if (quest_goal(&qg, &qs, &qi) && qg != fld) { load_field_graph(); hop = next_hop_sub(fld, sub, qg); }
        if (qg >= 0 && qg != fld && hop < 0) {
            /* the quest field is on another island (no route through exits): travel by linkstone. Active stones are
             * bit i of u32[] world+0x108 (cGameWorld::IsLinkStoneFlag @00128e30); info res+0x288 + 0x14*i: u16 +8 field,
             * +0xC x, +0xE y (GetLinkStoneInfo @000dc6b8). Touching a stone runs its script, which opens the world map
             * (cflScript::MakeWorldMap @000e1674); the forms are handled in the UI state. */
            uint32_t rm = G(0x19ae84u);
            int best_ls = -1;
            for (int i = 0; rm && i < (int32_t)M32(rm + 0x27C); i++) {
                if (!((M32(w + 0x108 + 4 * (i >> 5)) >> (i & 31)) & 1)) continue;
                uint32_t li = M32(rm + 0x288) + 0x14 * i;
                int lf = M16(li + 8);
                if (lf == fld || next_hop_sub(fld, sub, lf) >= 0) { best_ls = i; if (lf == fld) break; }
            }
            if (best_ls >= 0) {
                uint32_t li = M32(rm + 0x288) + 0x14 * best_ls;
                int lf = M16(li + 8), lx = (int16_t)M16(li + 0xC), ly = (int16_t)M16(li + 0xE);
                static int logged_ls = -1;
                if (logged_ls != best_ls * 1000 + fld) { logged_ls = best_ls * 1000 + fld; host_log("[bot] quest field %d unreachable: going to linkstone %d (field %d at %d,%d)", qg, best_ls, lf, lx, ly); }
                if (lf == fld) {
                    int dl2 = (lx - px) * (lx - px) + (ly - py) * (ly - py);
                    int sxl = lx - scroll_x(), syl = ly - 16 - scroll_y();
                    if (dl2 < 64 * 64 && sxl > 8 && syl > 8 && sxl < g_game_w - 8 && syl < LH - 8) {
                        release(); g_branch = "L-linkstone-tap";
                        static int nt_l; if (due(&nt_l, 30)) tap(sx(sxl), sy(syl));
                        return;
                    }
                    g_branch = "L-linkstone-walk"; walk_toward(px, py, lx, ly + 16); return;
                }
                qg = lf; hop = next_hop_sub(fld, sub, lf);
            }
        }
        {
            uint32_t om = M32(w + 0x650), L = M32(om + 0x14 + 4 * 4);
            for (uint32_t n = L ? M32(L + 0x10) : 0; n && nc < 64; n = M32(n)) {
                uint32_t o = M32(n + 4);
                if (!o || !(M32(o + 0x2C) & 2)) continue;
                /* tombstones (minimap entry type 2, cMinimap::AddDrawStuff @00101bc8) only show an epitaph */
                int tomb = 0, mc = (int32_t)M32(w + 0x728);
                for (int i = 0; i < mc && i < 64; i++)
                    if ((int32_t)M32(w + 0x694 + 0x14 + 8 * i) == 2 && M32(w + 0x694 + 0x18 + 8 * i) == o) tomb = 1;
                if (tomb) continue;
                if (getenv("EH_BOTDBG") && g_tick % 108 == 0)
                    host_log("[botlink] %d vt %06x kind %d type %d at %d,%d flags %x +0x90 %x", nc, M32(o) - IMG, M8(o + 4), (int16_t)M16(o + 6),
                             (int32_t)M32(o + 0x14), (int32_t)M32(o + 0x18), M32(o + 0x2C), M32(o + 0x90));
                /* cActionStuff types 0x167/0x168 are drifting clouds (cActionStuff::Initialize @000d067c attaches
                 * cMoveCloudProcess), not links */
                int st = (int16_t)M16(o + 6);
                if (M32(o) == IMG + rc_sym("_ZTV12cActionStuff") + 8 && (st == 0x167 || st == 0x168)) continue;
                {
                    /* the object's own collision box blocks the tiles around it: approach a point just outside */
                    int ox = (int32_t)M32(o + 0x14), oy = (int32_t)M32(o + 0x18), ax = ox, ay = oy;
                    int bw = (int16_t)M16(o + 0x10), bh = (int16_t)M16(o + 0x12), r = (bw > bh ? bw : bh) / 2 + 12;
                    if (!reachable(px, py, ox, oy)) {
                        static const int D4[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
                        for (int k = 0; k < 4; k++)
                            if (reachable(px, py, ox + D4[k][0] * r, oy + D4[k][1] * r)) { ax = ox + D4[k][0] * r; ay = oy + D4[k][1] * r; break; }
                    }
                    cand[nc++] = (Cand){ox, oy, 1, -1, M32(o) == IMG + rc_sym("_ZTV12cActionStuff") + 8 ? st : -1, ax, ay};
                }
            }
        }
        int (*used)[64] = g_link_used;
        int best = -1, bu = 1 << 30;
        static int commit_k = -1, commit_field = -1, commit_until;
        if (commit_field == fld && commit_k >= 0 && commit_k < nc && g_tick < commit_until &&
            reachable(px, py, cand[commit_k].ax, cand[commit_k].ay) && !avoided(cand[commit_k].x, cand[commit_k].y))
            best = commit_k, bu = -1;
        /* the exit toward the quest exists but is cut off by walls: the way there is a link (whirlwinds carry the
         * character over walls, cWhirlwindProcess::UpdateProcess @000d1bf4), so stay and try links instead of
         * leaving through another exit */
        int quest_exit_cut = 0;
        for (int k = 0; k < nc; k++)
            if (cand[k].kind == 0 && qg >= 0 && (cand[k].dest == qg || cand[k].dest == hop) && !reachable(px, py, cand[k].x, cand[k].y)) quest_exit_cut = 1;
        for (int k = 0; k < nc && bu >= 0; k++) {
            int d2 = (cand[k].x - px) * (cand[k].x - px) + (cand[k].y - py) * (cand[k].y - py);
            if (quest_exit_cut && cand[k].kind == 0 && !(cand[k].dest == qg || cand[k].dest == hop)) continue;
            if (cand[k].kind == 1 && d2 < 20 * 20) continue; /* standing on it */
            if (avoided(cand[k].x, cand[k].y)) continue;
            /* links next to the character can be touched even when no path is found (boxed in by the object) */
            if (!(cand[k].kind == 1 && d2 < 48 * 48) && !reachable(px, py, cand[k].ax, cand[k].ay)) continue;
            int u = used[fld & 63][k] * 1000 + d2 / 1000;
            /* steer: exit to the quest field (or the next hop toward it); unexplored destinations next */
            if (cand[k].kind == 1 && cand[k].dest < 0) cand[k].dest = link_dest(fld, cand[k].x, cand[k].y);
            if (qg >= 0 && cand[k].dest >= 0 && (cand[k].dest == qg || cand[k].dest == hop)) u -= 1000000;
            else if (cand[k].kind == 0 && cand[k].dest >= 0 && cand[k].dest < NG && !g_seen_field[cand[k].dest]) u -= 500;
            if (u < bu) { bu = u; best = k; }
        }
        if (getenv("EH_BOTDBG") && g_tick % 108 == 0)
            for (int k = 0; k < nc; k++)
                host_log("[botcand] %d %s at %d,%d used %d reach %d%s", k, cand[k].kind ? "link" : "exit", cand[k].x, cand[k].y,
                         used[fld & 63][k], reachable(px, py, cand[k].ax, cand[k].ay), k == best ? " <- best" : "");
        if (best >= 0) {
            if (commit_field != fld || commit_k != best || g_tick >= commit_until) {
                used[fld & 63][best]++;
                commit_field = fld; commit_k = best; commit_until = g_tick + 27 * 20;
                host_log("[bot] heading to %s %d of field %d at %d,%d", cand[best].kind ? "link" : "exit", best, fld, cand[best].x, cand[best].y);
            }
            if (cand[best].kind == 1) { g_head_link_fld = fld; g_head_link_x = cand[best].x; g_head_link_y = cand[best].y; g_head_link_tick = g_tick; }
            int d2 = (cand[best].x - px) * (cand[best].x - px) + (cand[best].y - py) * (cand[best].y - py);
            int da2 = (cand[best].ax - px) * (cand[best].ax - px) + (cand[best].ay - py) * (cand[best].ay - py);
            if (da2 < 12 * 12 && d2 < 64 * 64) d2 = 0; /* at the approach point: close enough to touch */
            if (cand[best].stype == 0x20) {
                /* whirlwind (cActionStuff type 0x20): fires when the character walks into it
                 * (cActionStuff::DoCustomEventOnOutInFire @000d0460) and throws it in its facing direction toward the
                 * partner whirlwind (cWhirlwindProcess::InitImpl / FindTransferTarget). Approach from the side away
                 * from the partner, then walk in toward it. */
                int ox = cand[best].x, oy = cand[best].y, tx = ox, ty = oy + 100, have = 0;
                for (int k = 0; k < nc; k++)
                    if (k != best && cand[k].stype == 0x20) {
                        int dd = (cand[k].x - ox) * (cand[k].x - ox) + (cand[k].y - oy) * (cand[k].y - oy);
                        if (!have || dd < (tx - ox) * (tx - ox) + (ty - oy) * (ty - oy)) { tx = cand[k].x; ty = cand[k].y; have = 1; }
                    }
                int dx = tx - ox, dy = ty - oy, ux = 0, uy = 0;
                if (abs(dx) > abs(dy)) ux = dx > 0 ? 1 : -1; else uy = dy > 0 ? 1 : -1;
                static int ww_phase, ww_k = -1;
                if (ww_k != best) { ww_k = best; ww_phase = 0; }
                int ax = ox - ux * 40, ay = oy - uy * 40;
                if (ww_phase == 0) {
                    if ((ax - px) * (ax - px) + (ay - py) * (ay - py) < 10 * 10) ww_phase = 1;
                    else { walk_toward(px, py, ax, ay); return; }
                }
                if (g_tick % 120 == 0) { ww_phase = 0; used[fld & 63][best] += 1; }
                walk_toward(px, py, ox + ux * 30, oy + uy * 30);
                return;
            }
            if (cand[best].kind == 1 && d2 < 48 * 48) {
                /* links that do nothing after several taps are not links: count them as heavily used */
                static int taps[64][64];
                release();
                if (g_tick % 15 == 0) {
                    tap(sx(cand[best].x - scroll_x()), sy(cand[best].y - 8 - scroll_y()));
                    g_link_tap_tick = g_tick; g_link_tap_field = fld; g_link_tap_idx = best;
                    if (++taps[fld & 63][best] > 6) { used[fld & 63][best] += 50; taps[fld & 63][best] = 0; host_log("[bot] link %d of field %d ignored", best, fld); }
                }
                return;
            }
            walk_toward(px, py, cand[best].ax, cand[best].ay);
            return;
        }
    }
    release();
}
