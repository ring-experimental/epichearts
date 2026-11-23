// Access to the original game's memory and functions from the modern UI (game thread only for calls).
// Addresses: ELF vaddr (Ghidra address - 0x10000); globals are pointer slots at IMG + vaddr.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>

extern "C" {
#include "host.h"
}

namespace g {
extern Cpu *cpu; // game-thread CPU of the current callback (procTouch / FlushImpl)
void set_cpu(Cpu *c);         // game-thread callbacks: cpu + the thread id check_thread compares with
void check_thread(uint32_t va); // logs a guest call made off the game thread (state.cpp)

inline uint32_t call(uint32_t va, std::initializer_list<uint32_t> args) {
    check_thread(va);
    uint32_t a[12];
    int n = 0;
    for (uint32_t v : args) a[n++] = v;
    return rc_call(cpu, va & ~1u, n, a);
}
// guest pointer validity (the image and heap live in the 32-bit address space)
inline bool ok(uint32_t p) { return p >= 0x10000 && (p & 3) == 0; }

inline uint32_t gm() { return M32(IMG + 0x19b148); }     // gp_gmInstance (cGameManager)
inline uint32_t world() { return M32(IMG + 0x19b158); }  // gp_GameWorldIntance (cGameWorld)
inline uint32_t gameui() { return M32(IMG + 0x19af18); } // cGameUi singleton (cflUi)
inline uint32_t bui() { return M32(IMG + 0x19ae8c); }    // xx_bui (cBattleUi)
inline uint32_t inv() { return M32(IMG + 0x19b15c); }    // gp_TarInventoryInstance (cInventory)
inline uint32_t field() { return M32(IMG + 0x19af24); }  // gp_fieldInstance (cField)
inline uint32_t mwm() { return M32(IMG + 0x19ae94); }    // cMessageWndManager
inline uint32_t questmgr() { return M32(IMG + 0x19ae7c); } // gp_QuestMgrInstance
inline uint32_t resmgr() { return M32(IMG + 0x19ae84); } // gp_resMrgInstance
inline uint32_t gfx() { return M32(IMG + 0x19af3c); }    // gp_TarGraphics
inline uint32_t sysinfo() { return IMG + 0x19af48; }      // systemInfo (0x200 bytes, address not pointer)
inline uint32_t sprite_sets() {                           // gsSpriteSetCtrlInstance+0x10 -> cflSpriteSetInfo*[]
    uint32_t c = M32(IMG + 0x19b25c);
    return c ? M32(c + 0x10) : 0;
}
// cGameWorld::GetPlayer @00128bf4
inline uint32_t player() { uint32_t w = world(); return w ? call(0x118bf4, {w}) : 0; }

// text of the original (UTF-8 with control codes, see work/ui_spec.md 1.7) -> RML markup
std::string rml_text(const char *s, bool keep_breaks = true);
// plain text (control codes removed)
std::string plain_text(const char *s);
inline const char *cstr(uint32_t p) { return ok(p) || (p >= 0x10000) ? (const char *)GP(p) : ""; }
// an item's name as the original draws it: cItem::GetName @0010ed68 returns the cItem::MakeName @0010fde0 text, which
// starts equipment names with gradeColor[grade] (vaddr 0x19902c: colour codes 2..6 = 노멀/레어/유니크/레전드/에픽)
inline std::string item_name_rml(uint32_t it) { return rml_text(cstr(call(0xfed68, {it})), false); }
}
