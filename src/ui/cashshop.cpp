// Item shop (the original's cash store, form 0x16 cFormCashStore, menu tab 6 of cFormInvenMDI::MakeTabs @0010b808),
// paid with gold because the original's billing (Harz, systemInfo+0x100) no longer exists.
// Products: the original's 16-entry grant table at 0x15ecd0 (vaddr 0x14ecd0), u32 = type | id << 8 | count << 16,
// used by UserHasConfirmedBuyingTheTem @0015ec58 (which grants with cInventory::AddItem and never charges anything).
// The product order is the store's list (embedded list packet at 0x107830, cFormCashStore::OnConnect @00107814).
// Price: the original Harz price of the product (list packet record +0xA) x HARZ_TO_GOLD, so the shop keeps the
// original's relative prices; products the original gave for 0 Harz stay free.
// Purchase = the NPC shop's sequence (cFormStore::doReadyBuying / doCompleteBuying @0011dc14): free space check
// (GetBasicBackpackRemainingSlotCount for equipment, cInventory::GetNonEquipMaxPurchasableCount @00130100 otherwise),
// cInventory::SubGold @0012fb44, cInventory::AddItem @001306ac (inv, 1, type, id, count, info+8, -1, 0).
#include "cashshop.h"
#include "game.h"
#include "sprites.h"
#include "state.h"
#include <atomic>
#include <mutex>

namespace cash {

static uint32_t item_info(uint32_t rm, int type, int id) { return g::call(0xcc3b4, {rm, (uint32_t)type, (uint32_t)id, 0}); } // cResourceManager::GetItemInfo @000dc3b4

// UNVERIFIED: conversion rate (design choice: the original has no Harz <-> gold relation; Harz prices are 0..250)
static const int HARZ_TO_GOLD = 100;
int harz_to_gold(int harz) { return harz * HARZ_TO_GOLD; }

// Harz price of product id (1..16) from the store list packet embedded at 0x107830 (vaddr 0xf7830) that
// cFormCashStore::OnConnect @00107814 feeds to OnListResponse @001053a0: payload at +8 = s32 result, u32 Harz,
// u8 flag, s16 x3, u8 Harz-pack count (LoadHarzProduct @00104c00, 0x30 bytes each), u16 item count
// (LoadStoreItems @00104af4), records of 0x13 bytes (u8 id, ..., s16 Harz price at +0xA, ...) + u8 len name + u8 len desc
static int harz_price(int product_id) {
    uint32_t p = IMG + 0xf7830 + 8 + 4 + 4 + 1 + 6;
    int packs = M8(p); p += 1 + packs * 0x30;
    int n = M16(p); p += 2;
    for (int i = 0; i < n && i < 64; i++) {
        int id = M8(p), price = (int16_t)M16(p + 0xa);
        p += 0x13;
        p += 1 + M8(p);
        p += 1 + M8(p);
        if (id == product_id) return price;
    }
    return 0;
}
// products the original gave away (0 Harz) cost their item DB buy price (tagITEM_INITINFO +0xC, as the NPC shop,
// cFormStore::doReadyBuying @0011dfd8) x the bundle, so they cannot be taken for free without limit
// UNVERIFIED: design choice
static int gold_price(int index) {
    int h = harz_price(index + 1);
    if (h > 0) return h * HARZ_TO_GOLD;
    uint32_t rm = g::resmgr();
    uint32_t e = M32(IMG + 0x14ecd0 + 4 * index);
    uint32_t info = rm ? item_info(rm, e & 0xff, (e >> 8) & 0xff) : 0;
    int p = info ? (int32_t)M32(info + 0xc) * (int)((e >> 16) & 0xffff) : 0;
    return p > 0 ? p : 100 * (int)((e >> 16) & 0xffff); // UNVERIFIED: fallback 100 G each when the DB has no price
}

static std::vector<Product> g_pub;
static std::mutex g_mx;
static std::atomic<int> g_result{-1};
static std::atomic<uint32_t> g_result_seq{0};


// game thread, while the menu is open
void capture() {
    uint32_t rm = g::resmgr(), inv = g::inv();
    std::vector<Product> v;
    if (rm && inv) {
        for (int i = 0; i < 16; i++) {
            uint32_t e = M32(IMG + 0x14ecd0 + 4 * i);
            Product p;
            p.index = i;
            p.type = e & 0xff; p.id = (e >> 8) & 0xff; p.count = (e >> 16) & 0xffff;
            uint32_t info = item_info(rm, p.type, p.id);
            if (!info) continue;
            p.name = g::plain_text(g::cstr(M32(rm + 0x34) + M32(info)));
            p.icon = sprites::image(0x6b, M8(info + 0x13), M8(info + 0x12));
            p.price = gold_price(i);
            p.type_name = g::plain_text(g::cstr(M32(IMG + 0x199268 + 4 * (p.type + 1)))); // _all_item_cate_names_str
            uint32_t ds = g::call(0xcd0d8, {rm, 0, M32(info + 4)});
            if (ds) p.desc = g::rml_text(g::cstr(ds));
            if (p.desc == "0") p.desc.clear();
            p.owned = (int)g::call(0x11ff0c, {inv, (uint32_t)p.type, (uint32_t)p.id}); // owned count (as the NPC shop shows)
            v.push_back(std::move(p));
        }
    }
    std::lock_guard<std::mutex> l(g_mx);
    g_pub = std::move(v);
}

std::vector<Product> pull() { std::lock_guard<std::mutex> l(g_mx); return g_pub; }

// game thread
static void buy_now(int i) {
    uint32_t rm = g::resmgr(), inv = g::inv();
    if (!rm || !inv || i < 0 || i >= 16) { g_result = 3; g_result_seq++; return; }
    uint32_t e = M32(IMG + 0x14ecd0 + 4 * i);
    int type = e & 0xff, id = (e >> 8) & 0xff, count = (e >> 16) & 0xffff;
    uint32_t info = item_info(rm, type, id);
    if (!info || count <= 0) { g_result = 3; g_result_seq++; return; }
    int room = type < 6 ? (int)g::call(0x11fb88, {inv}) // cInventory::GetBasicBackpackRemainingSlotCount @0012fb88
                        : (int)g::call(0x120100, {inv, (uint32_t)type, (uint32_t)id});
    if (room < count) { g_result = 2; g_result_seq++; return; }
    int price = gold_price(i);
    if (price > 0 && !g::call(0x11fb44, {inv, (uint32_t)price})) { g_result = 1; g_result_seq++; return; } // SubGold
    g::call(0x1206ac, {inv, 1, (uint32_t)type, (uint32_t)id, (uint32_t)count, M8(info + 8), 0xffffffffu, 0}); // AddItem
    g_result = 0;
    g_result_seq++;
}

void buy(int index) { state::post([index] { buy_now(index); }); }

bool result(uint32_t *seen, int *code) {
    uint32_t s = g_result_seq;
    if (s == *seen) return false;
    *seen = s;
    *code = g_result;
    return true;
}
}
