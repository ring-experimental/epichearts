/* Host wrappers around recompiled functions (tools/recomp.py HOOKS).
 *
 * EH_TESTBUFF=1 (verification bots only, off by default so normal play is the original):
 * damage dealt to monsters is raised so a play-through stays short.
 *   - normal monster: at least ceil(maxHp / 2) per hit            -> dies in 1-2 skills
 *   - boss with phases: 2-3 hits per phase (per HP bar / threshold)
 *   - boss without phases: at least ceil(maxHp / 5) per hit       -> ~5 skills
 * "Boss" is what the original treats as the boss: the creature in the boss gauge,
 * cBattleUi+0x128 (cGameWorld::GetBossPtr @ghidra 00129358, set by cGameWorld::SetBossGauge @0012b698).
 * Phase structure per class, from the decompilation:
 *   cMonsterBossEmbk        SetPhase @000bb8dc; every phase refills a bigger HP bar  -> 3 hits per bar
 *   cMonsterBossDragon      phase every 20% HP lost (MonDoing_WhenHit pends script 0x4ade; RestoreHp keeps HP
 *                           across the cutscene)                                  -> 5 segments of 2 hits
 *   cMonsterBossHynWarrior  MonDoing_WhenHit @000be89c: type 0xa1 changes pattern at 60% / 30% maxHp,
 *                           type 0xa3 at 70%                                      -> 3 (or 2) HP segments
 *   cMonsterBossSalamander  state 4 once hp < 30% maxHp (@000bf0e4 area, idiv(maxHp*0x1e,100)) -> 2 segments
 * Segment bosses: each HP segment between thresholds takes 3 hits (2 if it is under 35% of maxHp), the last hit of a
 * segment crossing the threshold; Salamander 3+2, HynWarrior 0xa1 3+2+2.
 */
#include "host.h"
#include "gen/gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* developer option multipliers in percent (ui/settings.cpp pushes them; 100 = the original), used by the hooks below */
int g_mul_exp = 100, g_mul_gold = 100, g_mul_life = 100, g_mul_refine = 100;

static int g_testbuff = -1;
static uint32_t g_monster_vt[64];
static int g_n_monster_vt;
static uint32_t g_vt_embk, g_vt_dragon, g_vt_hyn, g_vt_sala, g_xx_bui;

static uint32_t vptr_of(const char *vtable_sym) {
    uint32_t v = rc_sym(vtable_sym);
    return v ? IMG + v + 8 : 0; /* object vptr points past offset-to-top and typeinfo */
}

static void testbuff_init(void) {
    g_testbuff = getenv("EH_TESTBUFF") ? atoi(getenv("EH_TESTBUFF")) : 0;
    if (!g_testbuff) return;
    static const char *mons[] = {
        "_ZTV8cMonster", "_ZTV15cMonsterGreeder", "_ZTV13cMonsterFixed", "_ZTV15cMonsterGasFlow", "_ZTV16cMonsterBlowfish",
        "_ZTV23cMonsterBlowfishCarrier", "_ZTV20cMonsterFieldThunder", "_ZTV18cMonsterVoodooDoll", "_ZTV20cMonsterSummonVoodoo",
        "_ZTV16cMonsterSuccubus", "_ZTV13cMonsterLaser", "_ZTV17cMonsterTrapStuff", "_ZTV14cMonsterOsiris",
        "_ZTV23cMonsterBossEamoruSword", "_ZTV18cMonsterBossEamoru", "_ZTV25cMonsterBossEmbkSlaveWisp",
        "_ZTV25cMonsterBossEmbkSlaveFace", "_ZTV28cMonsterBossEmbkSlaveCrystal", "_ZTV16cMonsterBossEmbk",
        "_ZTV22cMonsterBossHynWarrior", "_ZTV27cMonsterBossHynWarriorEvent", "_ZTV22cMonsterBossSalamander", "_ZTV18cMonsterBossDragon",
    };
    for (size_t i = 0; i < sizeof mons / sizeof mons[0]; i++) {
        uint32_t v = vptr_of(mons[i]);
        if (v) g_monster_vt[g_n_monster_vt++] = v;
    }
    g_vt_embk = vptr_of("_ZTV16cMonsterBossEmbk");
    g_vt_dragon = vptr_of("_ZTV18cMonsterBossDragon");
    g_vt_hyn = vptr_of("_ZTV22cMonsterBossHynWarrior");
    g_vt_sala = vptr_of("_ZTV22cMonsterBossSalamander");
    g_xx_bui = IMG + rc_sym("xx_bui");
    host_log("[testbuff] enabled: %d monster classes", g_n_monster_vt);
}

/* EH_TESTGOD=1 (bot runs only): the player's party (cCharacter / cMagicKnight) never drops below 1 hp */
static int g_testgod = -1;
static uint32_t g_vt_char, g_vt_mk;

/* cCreature::SubHp(this, amount, isPercent) @ghidra 000c421c: hp(+0x94) -= amount (isPercent: amount% of maxHp(+0x98)) */
void f_0b421c(Cpu *c) {
    static int hplog = -1;
    if (hplog < 0) hplog = getenv("EH_HPLOG") != NULL;
    if (hplog) /* combat comparison: tools/combat_cmp.py compares it with the original */
        host_log("[hp] vt %06x type %d amt %d pct %d hp %d max %d", M32(c->r[0]) - IMG, (int16_t)M16(c->r[0] + 6), (int32_t)c->r[1],
                 (int)c->r[2], (int32_t)M32(c->r[0] + 0x94), (int32_t)M32(c->r[0] + 0x98));
    if (g_testbuff < 0) testbuff_init();
    if (g_testgod < 0) {
        g_testgod = getenv("EH_TESTGOD") != NULL;
        g_vt_char = vptr_of("_ZTV10cCharacter");
        g_vt_mk = vptr_of("_ZTV12cMagicKnight");
    }
    if (g_testgod) {
        uint32_t self = c->r[0], vt = M32(self);
        if (vt == g_vt_char || vt == g_vt_mk) {
            int32_t hp = (int32_t)M32(self + 0x94), maxhp = (int32_t)M32(self + 0x98);
            int32_t dmg = (int32_t)c->r[1];
            if (c->r[2]) dmg = (int32_t)((int64_t)dmg * maxhp / 100);
            if (dmg >= hp) dmg = hp > 1 ? hp - 1 : 0;
            c->r[1] = (uint32_t)dmg;
            c->r[2] = 0;
        }
    }
    if (g_testbuff) {
        uint32_t self = c->r[0];
        uint32_t vt = M32(self);
        int is_mon = 0;
        for (int i = 0; i < g_n_monster_vt; i++) if (g_monster_vt[i] == vt) { is_mon = 1; break; }
        int32_t maxhp = (int32_t)M32(self + 0x98);
        int32_t dmg = (int32_t)c->r[1];
        if (c->r[2]) dmg = (int32_t)((int64_t)dmg * maxhp / 100);
        if (g_testbuff > 1 && is_mon && dmg <= 0) {
            uint32_t bui = M32(g_xx_bui);
            if (bui && M32(bui + 0x128) == self) host_log("[testbuff] boss hit with dmg %d (not raised)", dmg);
        }
        if (is_mon && dmg > 0 && maxhp > 0) {
            uint32_t bui = M32(g_xx_bui);
            int is_boss = bui && M32(bui + 0x128) == self;
            int hits = 2;
            int32_t floor_dmg;
            /* pattern thresholds in % of maxHp, descending */
            int thr[4], nthr = 0, mtype = (int16_t)M16(self + 6);
            if (is_boss && vt == g_vt_sala) thr[nthr++] = 30;
            if (is_boss && vt == g_vt_hyn) { if (mtype == 0xa3) thr[nthr++] = 70; else { thr[nthr++] = 60; thr[nthr++] = 30; } }
            /* Dragon: a phase per 20% of HP lost (cMonsterBossDragon::MonDoing_WhenHit @0014f2e8 pends phase script
             * 0x4ade each time (maxHp-hp)*5/maxHp rises; RestoreHp @0014f48c keeps the HP across the cutscene) */
            int thr5[4] = {80, 60, 40, 20};
            if (is_boss && vt == g_vt_dragon) { for (int k = 0; k < 4; k++) thr[nthr++] = thr5[k]; }
            if (nthr) {
                int32_t hp = (int32_t)M32(self + 0x94);
                int hi = 100, lo = 0;
                for (int k = 0; k < nthr; k++) { if ((int64_t)hp * 100 > (int64_t)thr[k] * maxhp) { lo = thr[k]; break; } hi = thr[k]; }
                hits = hi - lo >= 35 ? 3 : 2;
                floor_dmg = (int32_t)(((int64_t)(hi - lo) * maxhp / 100 + hits - 1) / hits) + 1;
            } else {
                if (is_boss) hits = (vt == g_vt_embk) ? 3 : 5; /* per refilled bar / no phases */
                floor_dmg = (maxhp + hits - 1) / hits;
            }
            if (dmg < floor_dmg) dmg = floor_dmg;
            c->r[1] = (uint32_t)dmg;
            c->r[2] = 0;
            if (g_testbuff > 1)
                host_log("[testbuff] %s maxhp=%d hp=%d dmg=%d", is_boss ? "boss" : "mob", maxhp, (int32_t)M32(self + 0x94), dmg);
        }
    }
    f_0b421c_orig(c);
}

/* EH_TESTBUFF: player attacks never miss. cHitAffair::UpdateProcess @ghidra 001200fc (this+8 attacker, +0xC target)
 * rolls RandN(100) against attacker accuracy / (accuracy + target evasion) and adds a cMissEffect on failure; the
 * original skips the roll (hit rate 100) when attacker flags +0x2C bit 25 are set, so that bit is set around the call
 * for the player character attacking a monster. A level-1 bot otherwise almost never hits a level 29+ boss. */
void f_1100fc(Cpu *c) {
    if (g_testbuff < 0) testbuff_init();
    if (!g_vt_char) { g_vt_char = vptr_of("_ZTV10cCharacter"); g_vt_mk = vptr_of("_ZTV12cMagicKnight"); }
    uint32_t self = c->r[0], att = self ? M32(self + 8) : 0;
    int forced = 0;
    if (g_testbuff && att && (M32(att) == g_vt_char || M32(att) == g_vt_mk) && !(M32(att + 0x2C) & 0x02000000u)) {
        M32(att + 0x2C) |= 0x02000000u; forced = 1;
    }
    if (g_testbuff > 1 && self) {
        uint32_t bui = M32(g_xx_bui), tgt = M32(self + 0xC);
        if (bui && tgt && M32(bui + 0x128) == tgt) host_log("[testbuff] hit affair on boss (attacker %s)", att && (M32(att) == g_vt_char || M32(att) == g_vt_mk) ? "player" : "other");
        else if (tgt && (int32_t)M32(tgt + 0x98) >= 100000 && att && (M32(att) == g_vt_char || M32(att) == g_vt_mk)) {
            static int n2; if (n2++ < 20) host_log("[testbuff] player hit affair on vt %06x type %d maxhp %d hp %d inst %d scp %u flags %x", M32(tgt) - IMG, (int16_t)M16(tgt + 6), (int32_t)M32(tgt + 0x98), (int32_t)M32(tgt + 0x94), (int16_t)M16(tgt + 0x44), M16(tgt + 0x40), M32(tgt + 0x2C));
        }
    }
    f_1100fc_orig(c);
    if (forced) M32(att + 0x2C) &= ~0x02000000u;
}

/* EH_TESTGOD: cCharacter::ExternalMsgProcessImpl(tagExtMessage*) @ghidra 000c3c04 messages 1 and 9 set hp (+0x94)
 * to 0 directly and start cDeadProcess (instant kills, e.g. boss stage hazards), bypassing SubHp; drop them. */
void f_0b3c04(Cpu *c) {
    if (g_testgod < 0) g_testgod = getenv("EH_TESTGOD") != NULL;
    uint32_t msg = c->r[1];
    if (g_testgod && msg && (M32(msg) == 1 || M32(msg) == 9)) { host_log("[testgod] dropped kill message %u", M32(msg)); c->r[0] = 1; return; }
    /* developer option EXP multiplier: message 0x11 = a monster soul reaching the player (cSoulEffect::NextCut @000c7f94,
     * its only sender; amount at msg+4 -> case 0x11 AddExp). Quest / script / book EXP calls AddExp directly, unscaled */
    if (msg && M32(msg) == 0x11 && g_mul_exp != 100) {
        uint32_t e = M32(msg + 4);
        uint64_t v = (uint64_t)e * (uint32_t)g_mul_exp / 100;
        M32(msg + 4) = (uint32_t)(v > 0x7fffffffu ? 0x7fffffffu : v); /* saturate: the multiplier goes up to x100 */
        host_log("[devmul] exp %u -> %u", e, M32(msg + 4));
    }
    f_0b3c04_orig(c);
}

/* EH_MOVETRACE=1: log blocked steps of the player. cGameWorld::CheckMoveObj(this, pt&, prev&, box&, dir, listMask,
 * mover, cGObj** hit) @ghidra 0012d154 and CheckMoveTile(this, pt&, prev&, box&, dir, flag) @0012d44c (nonzero = blocked
 * as used by cActiveObj::Move @000ccxxx) */
void f_11d154(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    uint32_t pt = c->r[1], mover = on ? M32(c->r[13] + 8) : 0, hitp = on ? M32(c->r[13] + 12) : 0; /* stack: dir, mask, mover, hit */
    f_11d154_orig(c);
    (void)mover;
    uint32_t w0 = M32(IMG + 0x19b158u), om0 = w0 ? M32(w0 + 0x650) : 0, pp0 = om0 ? M32(om0 + 0x10) : 0, pl0 = pp0 ? M32(pp0 + 4) : 0;
    int nearp = pl0 && abs((int32_t)M32(pl0 + 0x14) - (int32_t)M32(pt)) < 24 && abs((int32_t)M32(pl0 + 0x18) - (int32_t)M32(pt + 4)) < 24;
    if (on && nearp) {
        static int n; uint32_t hit = hitp ? M32(hitp) : 0;
        if (n++ < 40) host_log("[movetrace] player obj check at %d,%d -> %u hit %06x type %d at %d,%d", (int32_t)M32(pt), (int32_t)M32(pt + 4), c->r[0],
                                hit ? M32(hit) - IMG : 0, hit ? (int16_t)M16(hit + 6) : -1, hit ? (int32_t)M32(hit + 0x14) : 0, hit ? (int32_t)M32(hit + 0x18) : 0);
    }
}
void f_11d44c(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    uint32_t pt = c->r[1];
    int x = on ? (int32_t)M32(pt) : 0, y = on ? (int32_t)M32(pt + 4) : 0;
    f_11d44c_orig(c);
    static int n;
    if (on) {
        uint32_t w = M32(IMG + 0x19b158u), om = w ? M32(w + 0x650) : 0, pp = om ? M32(om + 0x10) : 0, pl = pp ? M32(pp + 4) : 0;
        /* only steps next to the player (cGameWorld::GetPlayer @00128bf4) */
        if (pl && abs((int32_t)M32(pl + 0x14) - x) < 24 && abs((int32_t)M32(pl + 0x18) - y) < 24 && n++ < 60)
            host_log("[movetrace] player tile check %d,%d -> %u", x, y, c->r[0]);
    }
}

/* cIdleKeyProcess::SlotKeyProcess @ghidra 000d37c8 (nonzero = input consumed by slots) / MoveKeyProcess @000d34cc */
void f_0c37c8(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    f_0c37c8_orig(c);
    static int n, last = -1;
    if (on && (int)c->r[0] != last && n++ < 40) { last = (int)c->r[0]; host_log("[movetrace] SlotKeyProcess -> %u", c->r[0]); }
}
void f_0c34cc(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    static int n;
    if (on && n++ % 200 == 0) host_log("[movetrace] MoveKeyProcess called (%d)", n);
    f_0c34cc_orig(c);
}

/* cGameWorld::GetEventObjWhenTouch(this, player, mask, x, y) @ghidra 0012cb20 */
void f_11cb20(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    uint32_t x = c->r[3], y = on ? M32(c->r[13]) : 0;
    f_11cb20_orig(c);
    static int n;
    if (on && n++ < 40) host_log("[movetrace] touch obj at %d,%d -> %06x type %d at %d,%d", (int32_t)x, (int32_t)y, c->r[0] ? M32(c->r[0]) - IMG : 0,
                                  c->r[0] ? (int16_t)M16(c->r[0] + 6) : -1, c->r[0] ? (int32_t)M32(c->r[0] + 0x14) : 0, c->r[0] ? (int32_t)M32(c->r[0] + 0x18) : 0);
}

/* cBattleUi::HitTestImpl @ghidra 000f8834: id of the HUD element under a touch (0 = none) */
void f_0e8834(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    uint32_t a1 = c->r[1];
    f_0e8834_orig(c);
    /* modern HUD (ui/): its own buttons replace the original HUD, whose hit areas would otherwise stay active
     * while invisible (work/hud_spec.md 3): every touch that reaches the game is a field touch */
    { extern int ui_hud_owns_input(void); extern int ui_forced_hit(void);
      if (ui_forced_hit()) c->r[0] = (uint32_t)ui_forced_hit(); else if (ui_hud_owns_input()) c->r[0] = 0; }
    static int n, last = -1;
    if (on && (int)c->r[0] != last && n++ < 40) { last = (int)c->r[0]; host_log("[movetrace] HitTestImpl(arg %08x) -> %d", a1, (int)c->r[0]); }
}

/* cActiveObj::MoveImpl @ghidra 000cce00 (this, dist, dir, ...) / cMoveKeyProcess::Initialize @000d5b48 */
/* ---- 512-px logic box on wider views (port setting "logic512" in ui_settings.ini, default on; settings.cpp sets it)
 * A few gameplay decisions of the original are made from the world view box (cGameManager::GetWorldViewBox @00126ed0:
 * gm+0x1dc x, +0x1e0 y, +0x1e4 w, +0x1e8 h), which the original never had wider than 512. Each of them takes the
 * view's left edge from the camera scroll, cField +4 (gp_fieldInstance, cField::SetScrollOffset @001225fc), and only the
 * width / height from the box; the box's x is not read. So for a box of min(vw, 512) centred in the view, the call runs
 * with gm+0x1e4 = 512, gm+0x1dc += dx and the scroll cField +4 += dx (dx = (vw - 512) / 2), and both are put back
 * afterwards: the box as it was, the scroll by subtracting dx (a callee's own scroll change is kept). The box is
 * computed per call; vw <= 512 runs the original unchanged. Projectile removal (cEffect::NextCut @000c7308) keeps the
 * real view on purpose. */
int g_logic512 = 1;
typedef struct { uint32_t gm, f; int dx; uint32_t bx, bw; } LogicBox;
static int logic_box_enter(LogicBox *b, const char *who) {
    b->gm = M32(IMG + 0x19b148u); /* gp_gmInstance */
    b->f = M32(IMG + 0x19af24u);  /* gp_fieldInstance */
    int vw = b->gm ? (int32_t)M32(b->gm + 0x1e4) : 0;
    if (!g_logic512 || !b->f || vw <= 512) return 0;
    b->dx = (vw - 512) / 2;
    b->bx = M32(b->gm + 0x1dc);
    b->bw = M32(b->gm + 0x1e4);
    M32(b->gm + 0x1dc) = b->bx + (uint32_t)b->dx;
    M32(b->gm + 0x1e4) = 512;
    M32(b->f + 4) += (uint32_t)b->dx;
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("EH_LOGIC512DBG") != NULL;
    if (dbg) host_log("[logic512] %s: view %d -> 512 box at scroll %d (+%d)", who, vw, (int32_t)M32(b->f + 4) - b->dx, b->dx);
    return 1;
}
static void logic_box_leave(const LogicBox *b) {
    M32(b->gm + 0x1dc) = b->bx;
    M32(b->gm + 0x1e4) = b->bw;
    M32(b->f + 4) -= (uint32_t)b->dx;
}

/* cMonsterBossEamoruSword::SetSummonPosition(int mode, int k) @ghidra 000b97cc: mode 4 places the sword at
 * x = scroll + k' * (box.w / 8), y = 0x87 (k' = k + 1 or k + 2)
 * Mode is cMonsterBossEamoru +0x360 (cMonsterBossEamoru::SetSummonMonPosition @000b98a8), 4 only after
 * cMonsterBossEamoru::ProcUserAct(0) @000b9420, which the attack pattern sends (pattern action 0x16, cMonster @000b5544 area).
 * UNVERIFIED: mode 4 not reached in a test fight (script 19131; boss HP lowered to 20% with `ui dev hurt`), so this and
 * the next hook are from the decompile only. */
void f_0a97cc(Cpu *c) {
    LogicBox b;
    if (c->r[1] != 4 || !logic_box_enter(&b, "EamoruSword::SetSummonPosition")) { f_0a97cc_orig(c); return; }
    f_0a97cc_orig(c);
    logic_box_leave(&b);
}
/* cMonsterBossEamoruSword::DoThinkingOnIdle(cProcess*) @ghidra 000b99ec: state +0x360 == 4 of a summoned sword
 * (+0x358 == 1) whose countdown +0x364 runs out sets the target scroll + (box.w / 8) * {6,7,8,0,1,2}[+0x35c], y 0x21c,
 * with no call in between; the box and the scroll are read only there, so only that step is boxed */
void f_0a99ec(Cpu *c) {
    uint32_t s = c->r[0];
    LogicBox b;
    uint32_t owner = s ? M32(s + 0x374) : 0; /* its first test: owner +0x2c bit 17 diverts to state 7 */
    int step = owner && !(M32(owner + 0x2c) & 0x20000) && M32(s + 0x360) == 4 && M32(s + 0x358) == 1 && (int32_t)M32(s + 0x364) <= 1;
    if (!step || !logic_box_enter(&b, "EamoruSword::DoThinkingOnIdle")) { f_0a99ec_orig(c); return; }
    f_0a99ec_orig(c);
    logic_box_leave(&b);
}
/* cflScript::WalkAnObject(tSCP_COMMAND*, cGameWorld*) @ghidra 000e0af4: only cmd 0x60 (walk off the screen) reads the
 * box: x = scroll + w + 30 / scroll - 30 - w / 2, y = scroll y + h + 40 / scroll y - 15 - h / 2
 * UNVERIFIED: no scene with cmd 0x60 was run */
void f_0d0af4(Cpu *c) {
    LogicBox b;
    if ((M32(c->r[1]) & 0x3ff) != 0x60 || !logic_box_enter(&b, "cflScript::WalkAnObject 0x60")) { f_0d0af4_orig(c); return; }
    f_0d0af4_orig(c);
    logic_box_leave(&b);
}
/* cGameWorld::CreateTimeBird() @ghidra 0012bb48: the bird starts at scroll - 10 or scroll + w + 10 */
void f_11bb48(Cpu *c) {
    LogicBox b;
    if (!logic_box_enter(&b, "cGameWorld::CreateTimeBird")) { f_11bb48_orig(c); return; }
    f_11bb48_orig(c);
    logic_box_leave(&b);
}

/* ---- rain at the original's density on wider views
 * cRainEffect keeps 32 drops of 16 bytes (this +0x00 .. +0x200: +0 x, +4 y (screen pixels), +8 fall frame, +9 alive,
 * +0xa / +0xc fall length / age, +0xd splash frame). cRainEffect::CreateRain(int n) @ghidra 000d1384 starts drops at
 * x = box.w - acc, acc stepping 16..47 and wrapping at box.w - 31, so the same 32 drops spread over the whole view and
 * rain got thinner as the view widened (676: about 25 %). CreateRain runs with the box 512 wide (as the original's
 * widest view) and cRainEffect::Draw @ghidra 000d1510 draws the drops again 512, 1024, ... px further right, up to the
 * view's edge: the 512-px pattern repeated, like the DrawBg repeat above. */
void f_0c1384(Cpu *c) {
    uint32_t gm = M32(IMG + 0x19b148u); /* gp_gmInstance */
    int vw = gm ? (int32_t)M32(gm + 0x1e4) : 0;
    if (vw <= 512 || getenv("EH_BGORIG")) { f_0c1384_orig(c); return; }
    M32(gm + 0x1e4) = 512;
    f_0c1384_orig(c);
    M32(gm + 0x1e4) = (uint32_t)vw;
}
/* Draw's own pass also advances each splash (+0xd, frames 0x14..0x16 at y + 0x32 while +0xc >= +0xa - 2) and the
 * thunder flash (+0x204 / +0x208, a fill over the box): the copies are drawn read-only from the splash frames as they
 * were before the original pass, the same DrawFrame calls (cflSpriteSetInfo::DrawFrame @001522bc, set this+0x218 +0x14). */
void f_0c1510(Cpu *c) {
    uint32_t self = c->r[0], gm = M32(IMG + 0x19b148u); /* gp_gmInstance */
    int vw = gm ? (int32_t)M32(gm + 0x1e4) : 0;
    if (vw <= 512 || !self || getenv("EH_BGORIG")) { f_0c1510_orig(c); return; }
    uint8_t splash[32];
    for (int i = 0; i < 32; i++) splash[i] = M8(self + 16 * i + 0xd);
    f_0c1510_orig(c);
    uint32_t r[4] = {c->r[0], c->r[1], c->r[2], c->r[3]};
    uint32_t res = M32(self + 0x218), set = res ? M32(res + 0x14) : 0;
    static uint32_t pt; /* tag_tFL_POINT (guest memory) */
    if (!pt) pt = HPTR(gas_calloc(1, 8));
    for (int off = 512; set && off < vw; off += 512)
        for (int i = 0; i < 32; i++) {
            uint32_t d = self + 16 * i;
            if (!M8(d + 9)) continue;
            uint32_t frame;
            if ((int)M8(d + 0xa) - 2 <= (int)M8(d + 0xc)) { /* landed: the splash, while its frame was < 3 */
                if (splash[i] >= 3) continue;
                M32(pt + 4) = M32(d + 4) + 0x32;
                frame = 0x14 + splash[i];
            } else {
                M32(pt + 4) = M32(d + 4);
                frame = M8(d + 8);
            }
            M32(pt) = M32(d) + (uint32_t)off;
            uint32_t a[7] = {set, 0, pt, 0, frame, 0, 0};
            rc_call(c, 0x1422bc, 7, a);
        }
    static int dbg = -1, calls;
    if (dbg < 0) dbg = getenv("EH_LOGIC512DBG") != NULL;
    if (dbg && calls++ % 60 == 0) { /* test: where the drops are and how far the copies reach */
        int n = 0, x0 = 1 << 30, x1 = -(1 << 30);
        for (int i = 0; i < 32; i++)
            if (M8(self + 16 * i + 9)) { int x = (int32_t)M32(self + 16 * i); n++; x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1; }
        host_log("[rain] view %d: %d drops at x %d..%d, copies at +512 .. +%d", vw, n, x0, x1, (vw - 1) / 512 * 512);
    }
    memcpy(c->r, r, sizeof r);
}

/* cActiveObj::MoveImpl @ghidra 000cce00 also holds the player inside the view while world flag 0x20000000 is set
 * (only cMonsterBossSalamander::InitState @000bfbcc sets it): 0 <= x - scroll <= box.w, 0 <= y - scroll y <= box.h.
 * MoveImpl runs for every moving object every frame and calls much more (CheckMoveObj, FindPosForAsideMoving,
 * MakeMoveProcess), so the logic box applies only to the player while that flag is set.
 * UNVERIFIED: that no callee reads the camera scroll during the Salamander chase (none found in MoveImpl's own body). */
void f_0bce00(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    uint32_t self = c->r[0], a1 = c->r[1], a2 = c->r[2], a3 = c->r[3];
    uint32_t world = M32(IMG + 0x19b158u); /* gp_GameWorldIntance */
    LogicBox b;
    int boxed = self && M8(self + 4) == 0 && world && (M32(world) & 0x20000000) && logic_box_enter(&b, "cActiveObj::MoveImpl (Salamander chase)");
    f_0bce00_orig(c);
    if (boxed) logic_box_leave(&b);
    static int n;
    if (on && self && M8(self + 4) == 0 && n++ < 30) {
        host_log("[movetrace] MoveImpl(player, %d, %d, %d) -> %u", (int)a1, (int)a2, (int)a3, c->r[0]);
        if (n == 3) { extern void host_backtrace(int); host_backtrace(14); }
    }
}
void f_0c5b48(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    static int n;
    if (on && n++ < 20) host_log("[movetrace] cMoveKeyProcess::Initialize");
    f_0c5b48_orig(c);
}

/* cGameManager::PendMoveField(this, group, sub, entry) @ghidra 00127884: who moves the player between fields */
void f_117884(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_MOVETRACE") != NULL;
    if (on) {
        uint32_t gmi = c->r[0], w = M32(IMG + 0x19b158u), ev = w ? M32(w + 0x648) : 0;
        host_log("[movetrace] PendMoveField(%d, %d, entry %d) script running %u, event obj %06x type %d inst %d at %d,%d",
                 (int)c->r[1], (int)c->r[2], (int)c->r[3], M32(gmi + 0x210), ev ? M32(ev) - IMG : 0, ev ? (int16_t)M16(ev + 6) : -1,
                 ev ? (int16_t)M16(ev + 0x44) : -1, ev ? (int32_t)M32(ev + 0x14) : 0, ev ? (int32_t)M32(ev + 0x18) : 0);
    }
    f_117884_orig(c);
}

/* EH_CONDTRACE=1: what story scripts check. cGameWorld::IsDoneEvent(this, ev, ?) @ghidra 00128de0 and
 * cInventory::GetDuplicateItemCount(this, category, id) @0012ff0c, logged once per distinct argument/result */
static int cond_seen(uint32_t key) {
    static uint32_t seen[512]; static int n;
    for (int i = 0; i < n; i++) if (seen[i] == key) return 1;
    if (n < 512) seen[n++] = key;
    return 0;
}
void f_118de0(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_CONDTRACE") != NULL;
    uint32_t ev = c->r[1], a2 = c->r[2];
    f_118de0_orig(c);
    if (on && !cond_seen(0x10000000u | (ev & 0xffff) << 1 | (c->r[0] & 1)))
        host_log("[cond] IsDoneEvent(%u, %u) -> %u", ev, a2, c->r[0]);
}
void f_11ff0c(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_CONDTRACE") != NULL;
    uint32_t cat = c->r[1], id = c->r[2];
    f_11ff0c_orig(c);
    if (on && !cond_seen(0x20000000u | (cat & 0xff) << 16 | (id & 0xffff)))
        host_log("[cond] GetDuplicateItemCount(cat %u, id %u) -> %u", cat, id, c->r[0]);
}

/* cflScriptBase::ScriptCall(this, scriptId, int*) @ghidra 0016c82c: which scripts run (field number at the time) */
void f_15c82c(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_CONDTRACE") != NULL;
    if (on) { uint32_t w = M32(IMG + 0x19b158u); host_log("[cond] ScriptCall(%u) in field %d/%d", c->r[1], w ? (int)M32(w + 8) : -1, w ? (int)M32(w + 0xC) : -1); }
    f_15c82c_orig(c);
}
/* cMonsterBossDragon::ProcUserAct(obj, act, ?) @0014f364: script-driven Dragon actions */
void f_13f364(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_CONDTRACE") != NULL;
    if (on) host_log("[cond] Dragon ProcUserAct(act %u, %u)", c->r[1], c->r[2]);
    f_13f364_orig(c);
}

/* EH_SCPTRACE=1: every executed script command. cflScriptBase::NextCommand(this, cmd) @ghidra 0016c9d0 is called once
 * per command: header u32 = opcode (bits 0..9) | operand word count (bits 10..12) << 10 | flags, then the operands.
 * Script id this+0x14 (set by cflScriptBase::Load @0016c770). */
void f_15c9d0(Cpu *c) {
    static int on = -1; if (on < 0) on = getenv("EH_SCPTRACE") != NULL;
    if (on) {
        uint32_t self = c->r[0], cmd = c->r[1], h = M32(cmd);
        int nw = (h & 0x1fff) >> 10;
        char buf[160]; int k = snprintf(buf, sizeof buf, "[scp] %u +%x op %03x hdr %08x:", M32(self + 0x14), M32(self + 4), h & 0x3ff, h);
        for (int i = 1; i <= nw && k < 150; i++) k += snprintf(buf + k, sizeof buf - k, " %08x", M32(cmd + 4 * i));
        host_log("%s", buf);
    }
    f_15c9d0_orig(c);
}

/* cflScript::RunRunningGame_Final(tSCP_COMMAND*&, uint&) @ghidra 000e1a50, script command 0xE4 (only script 59015):
 * it asks whether to connect to the trade center (yes/no notice, then cFormTrade::StartTrading @00138024 = form 0x17
 * and a server connection that fails offline). The port runs it as the "no" answer: the command is consumed like
 * the original's case (handled flag *param_2 = 1, return 1) without the notice. */
void f_0d1a50(Cpu *c) {
    uint32_t cmd = M32(c->r[1]);
    if (cmd && (M32(cmd) & 0x3ff) == 0xe4) {
        host_log("[scp] command 0xE4 (trade center) skipped: offline");
        M32(c->r[2]) = 1;
        c->r[0] = 1;
        return;
    }
    f_0d1a50_orig(c);
}

/* EH_SAVETRACE=1: log the save path (cGameManager @ghidra 00145214 SaveGame(int,int), 001452ac/001452e4
 * SaveGameWithCheck, 0014285c CanSaveGameChk, 001450a4 SAVE_MemoryToFile) */
#define TRACE_WRAP(va, name)                                                                       void f_##va(Cpu *c) {                                                                              static int on = -1;                                                                            if (on < 0) on = getenv("EH_SAVETRACE") != NULL;                                               uint32_t a0 = c->r[0], a1 = c->r[1], a2 = c->r[2], a3 = c->r[3];                               f_##va##_orig(c);                                                                              if (on) host_log("[save] %s(%x, %x, %x, %x) -> %x", name, a0, a1, a2, a3, c->r[0]);      }
TRACE_WRAP(135214, "SaveGame(int,int)")
TRACE_WRAP(1352ac, "SaveGameWithCheck(int,int,uint)")
TRACE_WRAP(1352e4, "SaveGameWithCheck()")
TRACE_WRAP(13285c, "CanSaveGameChk")
TRACE_WRAP(1350a4, "SAVE_MemoryToFile")

/* cGlFbGraphics::Setup(tag_tGRAPHIC_CREATE_INFO*) @ghidra 00167720. CreateGraphics @001287b8 fills the info with
 * flags (bit0: explicit size) and info+4 -> {w, h, font name, font size}; it clamps every landscape screen wider
 * than 16:10 to 512x320 (the original stretched it). The port widens the framebuffer to the display aspect
 * (g_game_w x 320, main.c) so the field is shown undistorted on 16:9 / 20:9 screens. */
extern int g_game_w, g_game_h;
void cover_snap(const char *name); /* EH_SENTINEL: measure the back buffer now, kept under <name> (ui/capture.cpp) */

/* The background's x offset within its 560 px (cField::DrawField @00123fdc: (0x230 - viewW) * scrollX / (mapW - viewW)),
 * computed for the original's 512-wide view so the parallax speed is the original's on wider views too */
static int bg_parallax_x(uint32_t f) {
    int mapw = (int)M16(f + 0x78) * 16, scroll = (int32_t)M32(f + 4); /* map width (tiles), scroll x (cField +4) */
    int bgx = mapw > 512 ? (0x230 - 512) * scroll / (mapw - 512) : 0;
    return bgx < 0 ? 0 : bgx > 0x230 - 512 ? 0x230 - 512 : bgx;
}

/* One DrawBg pass with bg x offset bgx (>= 0: start column bgx/16; < 0: column 0 at screen x -bgx). DrawBg pairs its
 * first column with the map tile at tile/ofs and corrects by at most one tile (iVar5 < ofs -> tile - 1), so the
 * point it gets is moved to the map tile under the pass's first column and row. For the original's own case
 * (map tile and bg column less than a tile apart) that is the same tile; it matters for the second pass and for maps
 * narrower than the view, which cGameWorld::Draw @0012e574 centres (map tile 0 at screen x 58 on a 676 view: the
 * empty-tile test read tiles 3 columns off and left black blocks in 베리타스항구 31/1). */
static void bg_pass(Cpu *c, const uint32_t a[4], uint32_t sp, int bgx, uint32_t tile, uint32_t ofs) {
    static uint32_t pts; /* tFL_POINT tile, tFL_POINT ofs (guest memory) */
    if (!pts) pts = HPTR(gas_calloc(1, 16));
    memcpy(c->r, a, 4 * sizeof *a);
    c->r[13] = sp;
    M32(sp) = (uint32_t)bgx;
    if (!tile || !ofs) { f_112cd8_orig(c); return; }
    int bgy = (int32_t)M32(sp + 4);
    int sx = bgx < 0 ? -bgx : (bgx >> 4 << 4) - bgx, sy = bgy < 0 ? -bgy : (bgy >> 4 << 4) - bgy;
    int ox = (int32_t)M32(ofs), oy = (int32_t)M32(ofs + 4);
    int kx = (sx - ox) >> 4, ky = (sy - oy) >> 4; /* arithmetic shift: floor */
    M32(pts) = M32(tile) + (uint32_t)kx;
    M32(pts + 4) = M32(tile + 4) + (uint32_t)ky;
    M32(pts + 8) = (uint32_t)(ox + 16 * kx);
    M32(pts + 12) = (uint32_t)(oy + 16 * ky);
    M32(sp + 0x10) = pts;
    M32(sp + 0x14) = pts + 8;
    f_112cd8_orig(c);
    M32(sp + 0x10) = tile;
    M32(sp + 0x14) = ofs;
}

/* cField::DrawBg(imagesets, bg tiles, bg flags, int bgx, int bgy, mapw, maph, tFL_POINT *tile, tFL_POINT *ofs, holes)
 * @ghidra 00122cd8: the background (sky / abyss) seen through the map's empty tiles, a fixed 35x30-tile (560x480)
 * layer. cField::DrawField @00123fdc passes bgx = (0x230 - viewW) * scrollX / (mapW - viewW), a parallax offset
 * within those 560 px, as long as the world view (gm+0x1E4, cGameManager::GetWorldViewBox @00126ed0) is narrower
 * than 560. The original framebuffer is at most 512 wide; the port's wider one takes DrawField's other branch,
 * bgx = (viewW - 0x230) / 2, and DrawBg stops after column 35 (0x23): the right part of the screen got no background
 * (black, or earlier frames before the clear in capture.cpp f_11e574).
 * The port keeps the original's parallax as for a 512-wide view and draws the layer again from column 0 where the
 * first pass ended, so it repeats horizontally. The second pass gets the map tile point / pixel offset shifted to
 * its start column, so the empty-tile test (holes, last argument) still reads the map tiles under each column. */
void f_112cd8(Cpu *c) {
    uint32_t gmi = M32(IMG + 0x19b148u); /* gp_gmInstance */
    int vw = gmi ? (int32_t)M32(gmi + 0x1e4) : 0;
    { /* a bg tile whose image set (+0x9c, from the field's tileset ids +0x8c) is missing draws nothing: logged
       * (every 60th call) always, and every 60th call with EH_BGDBG */
        static int dbg = -1;
        if (dbg < 0) dbg = getenv("EH_BGDBG") != NULL;
        static int calls;
        uint32_t f0 = c->r[0], sets = c->r[1], bt = c->r[2];
        int u[16] = {0}, missing = 0;
        for (int i = 0; i < 35 * 30 && bt; i++) u[M8(bt + i) & 0xf]++;
        for (int i = 0; i < 16; i++) if (u[i] && !M32(sets + 4 * i)) missing = 1;
        if (f0 && (missing || dbg) && calls++ % 60 == 0) {
            char ids[160] = "", used[64] = "";
            int n = 0;
            for (int i = 0; i < 16; i++) n += snprintf(ids + n, sizeof ids - n, "%02x:%c ", M8(f0 + 0x8c + i), M32(sets + 4 * i) ? 'y' : '-');
            n = 0;
            for (int i = 0; i < 16; i++) if (u[i]) n += snprintf(used + n, sizeof used - n, "%d(%d) ", i, u[i]);
            host_log("[bgdbg] field %08x sets %s| bg tiles use set %s", f0, ids, used);
        }
    }
    if (vw < 0x230 || getenv("EH_BGORIG")) { f_112cd8_orig(c); return; }
    uint32_t a[4] = {c->r[0], c->r[1], c->r[2], c->r[3]}, sp = c->r[13];
    uint32_t bgx0 = M32(sp), tile = M32(sp + 0x10), ofs = M32(sp + 0x14), f = a[0];
    int bgx = bg_parallax_x(f);
    bg_pass(c, a, sp, bgx, tile, ofs);
    int x2 = 0x230 - bgx; /* screen x where the first pass ran out of columns */
    if (x2 < vw) bg_pass(c, a, sp, -x2, tile, ofs); /* negative: column 0 starts at screen x2 */
    M32(sp) = bgx0;
}

/* cField::DrawSkyBg(int x, int y) @ghidra 00123e90: the painted sky (image set 0xD8) for fields with flag +0x56 & 4,
 * drawn as the image scaled to half the width (+0x130: 0x118 = 280 with a tile background, else half the map) and
 * again mirrored right of it, plus a decoration sprite when +0x56 & 2. With a tile background DrawField passes
 * x = -bgx, so the 560-px pair ends at 560 - bgx; on the port's wider view the rest of the sky stayed black
 * (자일우드 절벽 49/1). The pair is drawn again where the first ends (normal + mirrored repeats without a seam), with
 * the 512-wide parallax of the DrawBg hook; the decoration only once. */
void f_113e90(Cpu *c) {
    uint32_t gmi = M32(IMG + 0x19b148u), f = c->r[0]; /* gp_gmInstance */
    int vw = gmi ? (int32_t)M32(gmi + 0x1e4) : 0;
    if (vw < 0x230 || (int16_t)M16(f + 0x54) == -1 || getenv("EH_BGORIG")) { f_113e90_orig(c); return; }
    uint32_t a[3] = {c->r[0], c->r[1], c->r[2]};
    int x = -bg_parallax_x(f), w = 2 * (int16_t)M16(f + 0x130);
    c->r[1] = (uint32_t)x;
    f_113e90_orig(c);
    uint16_t fl = (uint16_t)M16(f + 0x56);
    M16(f + 0x56) = fl & ~2u;
    for (int rx = x + w; w > 0 && rx < vw; rx += w) {
        memcpy(c->r, a, sizeof a);
        c->r[1] = (uint32_t)rx;
        f_113e90_orig(c);
    }
    M16(f + 0x56) = fl;
}

/* cFormWorldMap::OnBgDraw(int, int, uint) @ghidra 000fdacc: the world map's land (image 0 of sprite set 0x2D0/4, at
 * half speed) and clouds (image 1) scroll left, each drawn at -(t mod w) and once more at w - (t mod w) if that is
 * inside the game view box (cGameManager::GetGameViewBox @00126ef8); t = DAT_001aae90, counted up per call. Two
 * copies cover 2w - (t mod w) >= w, always enough for the original's view (at most 512 wide, w = 562 / 627). On the
 * port's 676-px view they fall short for part of every cycle (land: t/2 mod 562 > 448) and the frame under the map
 * showed through on the right (the last field frame, which the port puts back into the buffer every frame).
 * Same draws, repeated until the copies reach the view's right edge. */
void f_0edacc(Cpu *c) {
    uint32_t gmi = M32(IMG + 0x19b148u);  /* gp_gmInstance */
    uint32_t ctrl = M32(IMG + 0x19b25cu); /* gsSpriteSetCtrlInstance */
    int vw = gmi ? (int32_t)M32(gmi + 0x1c4) : 0; /* game view box width */
    if (vw <= 512 || !ctrl || getenv("EH_BGORIG")) { f_0edacc_orig(c); return; }
    uint32_t a[4] = {c->r[0], c->r[1], c->r[2], c->r[3]}, set = M32(M32(ctrl + 0x10) + 0x2d0);
    static uint32_t pt; /* tag_tFL_POINT (guest memory) */
    if (!pt) pt = HPTR(gas_calloc(1, 8));
    int t = (int32_t)M32(IMG + 0x19ae90u); /* DAT_001aae90 */
    uint32_t q[2] = {set, 0};
    for (int idx = 0; idx < 2 && set; idx++) {
        q[1] = (uint32_t)idx;
        uint32_t img = rc_call(c, 0x141b64, 2, q); /* cflSpriteSetInfo::GetImage @00151b64 */
        if (!img) break;
        int w = (int)M16(img + 4);
        if (w <= 0) break;
        int off = (idx == 0 ? t >> 1 : t) % w;
        for (int x = -off; x == -off || (x >= 0 && x < vw); x += w) {
            M32(pt) = (uint32_t)x;
            M32(pt + 4) = 0;
            uint32_t d[4] = {set, pt, (uint32_t)idx, 0};
            rc_call(c, 0x141b84, 4, d); /* cflSpriteSetInfo::DrawImage @00151b84 */
        }
    }
    M32(IMG + 0x19ae90u) = (uint32_t)(t + 1);
    memcpy(c->r, a, sizeof a);
    c->r[0] = 1;
}

/* cGameTipMgr::DrawLoadingTip() @ghidra 0013273c: the loading screen, drawn once before a slot loads (cMenuMgr::Run
 * @00148094 states 10/0xb/0xd -> cGameManager::DrawLoading @00127718) and left on screen while the load blocks. It clears
 * the view (graphics vfunc +0x58), draws the character at fixed x 0x54 (bleeding in from the left edge), the tip balloon at
 * fixed x 0xa4 (cTalkBallonWnd::DrawLoadingTip @000ff3f8 ignores its x argument) and "Loading..." right-aligned on the
 * view's bottom line, so the layout holds at any width (checked at 400..800). Test only: EH_SENTINEL measures the frame
 * right here, before the original's interface is drawn over it in the same frame (tools/rescheck.py "cover loading"). */
void f_12273c(Cpu *c) {
    f_12273c_orig(c);
    cover_snap("loading");
}

void f_157720(Cpu *c) {
    uint32_t info = c->r[1], sz = info ? M32(info + 4) : 0;
    if (sz && (M32(info) & 1)) {
        host_log("[screen] framebuffer %ux%u -> %dx%d", M32(sz), M32(sz + 4), g_game_w, g_game_h);
        M32(sz) = (uint32_t)g_game_w;
        M32(sz + 4) = (uint32_t)g_game_h;
    }
    f_157720_orig(c);
}

/* cGameManager::InitSystemOpt() @ghidra 00144904 builds a fresh system record (first start, no options in the save,
 * data reset: LoadOpt @00144c04 and the reset path call it) with BGM 60 / FX 60 (systemInfo[6] / [7] = 0x3c) and hands
 * them to cTarSoundEngine::SetBGMVolume @0013f8a4 / SetFXVolume @0013f37c. Port default (user decision 2026-10-26:
 * players found 60 too loud on PC, where no phone media volume sits in front): BGM 40 / FX 50 on the same linear 0..100
 * scale (GofAudioClip*.setVolume = v / 100f), so a saved record keeps its values and 100 is still the original's full
 * volume. */
void f_134904(Cpu *c) {
    f_134904_orig(c);
    uint32_t keep[16];
    memcpy(keep, c->r, sizeof keep);
    uint32_t si = IMG + 0x19af48u; /* systemInfo */
    M8(si + 6) = 40;
    M8(si + 7) = 50;
    uint32_t none[1] = {0};
    uint32_t snd = rc_call(c, 0x1301d4, 0, none); /* cTarSoundEngine::GetInstance @001401d4 */
    uint32_t bgm[2] = {snd, 40}, fx[2] = {snd, 50};
    rc_call(c, 0x12f8a4, 2, bgm);
    rc_call(c, 0x12f37c, 2, fx);
    memcpy(c->r, keep, sizeof keep);
    host_log("[opt] fresh system options: BGM 40 / FX 50 (original 60 / 60)");
}

/* developer option gold multiplier: dropped gold is kind 100 in cDropItem::Initialize(this, kind, count, ., .) @ghidra 0011027c
 * (stores count at +0xB0; cInventory::AddDropItem adds it). Only drops made by cGameWorld::CreateDropItem(cMonster*)
 * @0012be54 (monster death) are scaled, not birds (cBird) or script command 0xC5 */
static int g_monster_drop;
void f_11be54(Cpu *c) { g_monster_drop++; f_11be54_orig(c); g_monster_drop--; }
void f_10027c(Cpu *c) {
    if (g_monster_drop && c->r[1] == 100 && g_mul_gold != 100) {
        int32_t g = (int32_t)c->r[2];
        int64_t v = (int64_t)g * g_mul_gold / 100;
        c->r[2] = (uint32_t)(int32_t)(v > 0x7fffffff ? 0x7fffffff : v < -0x7fffffff ? -0x7fffffff : v);
        host_log("[devmul] gold %d -> %d", g, (int32_t)c->r[2]);
    }
    f_10027c_orig(c);
}

/* developer option gathering / mining / fishing multiplier: cCollectProcess::SetAcquisition @ghidra 000d79a4 sets the
 * maximum yield +0x30; UpdateProcess rolls RandN(+0x30) + 1 items and gives the same count as life skill EXP
 * (cCharacter::AddCollectSkillExp @000c2eac), so both scale */
void f_0c79a4(Cpu *c) {
    uint32_t self = c->r[0];
    f_0c79a4_orig(c);
    if (g_mul_life != 100) {
        int32_t n = (int32_t)M32(self + 0x30);
        if (n > 0) { int64_t v = (int64_t)n * g_mul_life / 100; M32(self + 0x30) = (uint32_t)(v > 0x7fffffff ? 0x7fffffff : v); }
        host_log("[devmul] collect max yield %d -> %d", n, (int32_t)M32(self + 0x30));
    }
}

/* developer option refine multiplier: FUN_001ad900 (Thumb-2 patch segment, only caller cFormPopup::Process @000f0b50,
 * the local refine) turns the success value at the caller's sp+0x1C into the threshold
 * ((10000 - 500 * step + scroll bonus) << 16) / 10000 that RandN(0x10000) must stay under; scaled here, capped at
 * 0x10000 (always succeeds). A threshold <= 0 stays as it is */
void f_19d900(Cpu *c) {
    f_19d900_orig(c);
    if (g_mul_refine != 100) {
        uint32_t a = c->r[13] + 0x1c;
        int32_t t = (int32_t)M32(a);
        if (t > 0) { int64_t v = (int64_t)t * g_mul_refine / 100; M32(a) = (uint32_t)(v > 0x10000 ? 0x10000 : v); }
        host_log("[devmul] refine threshold %d -> %d / 65536", t, (int32_t)M32(a));
    }
}
