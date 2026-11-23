// Save slot export / import for the title screen (saveio.h).
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
//
// How the original keeps saves (Ghidra addresses; recompiled vaddr = Ghidra - 0x10000):
//  - epic19.png is the whole save: u32 9, u32 size, 0x24-byte key block, then 4 chunks {u32 id, u32 version,
//    u32 size, data} encrypted with FUNTER_ENC_Encrypt, then 0x20 device bytes (SAVE_MemoryToFile @001450a4,
//    SAVE_FileToMemory @00144e28). epic19.b01/.b02 are rotated backups (SAVE_Backup @00143ae8).
//  - In memory the chunks live in gm_save_chunk (vaddr 0x19b1f8, 4 x {id, version, size, data*}; FUN_00144afc
//    @00144afc). Chunk 0 is a copy of systemInfo (0x200 bytes, FUN_00144000 @00144000); chunks 1..3 are slots 0..2
//    (cGameManager::LoadGame @001444b8 reads entry slot+1).
//  - The slot list shown on the title comes from systemInfo+0x10+slot*0x30 (H[0] exists, H[1] has data, u16 H+2 level,
//    u32 H+4 play time, u8 H+0xa class; FUN_001445f0 @001445f0, HasSaveFile @001403f0).
//  - epic18.png is only the save-operation trace log (TraceSaveOp @001449d4, PrepareTracingSave @00143b70), not data.
//    epic2<slot>.png are per-slot files of older versions: only read for their version (GetFileVersion @001275cc)
//    and deleted when a new game starts in the slot (cFormMainMenu::ProcSelectSlot @00148310); nothing writes them.
// So one slot = its systemInfo record + its chunk. The original's own whole-save transfer (server keeper:
// CreateBinaryForUpload @00141398 / ReplaceSave @001453b4) moved the plain chunk list; this moves one slot of it.
//
// .ehsave container (little endian), written and read only by this port:
//   0x00 "EPICHSAV"  0x08 u32 format 1  0x0c u32 header size 0x40  0x10 u32 source slot  0x14 u32 chunk version
//   0x18 u32 data size  0x1c u32 crc32 of everything after the header  0x20 u16 systemInfo+0x11c+slot*2
//   0x22..0x3f zero  0x40 slot record (0x30)  0x70 chunk data
#include "saveio.h"
#include "game.h"
#include "ui_internal.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

namespace saveio {

static const uint32_t SAVE_CHUNK = 0x19b1f8;  // gm_save_chunk
static const char MAGIC[8] = {'E', 'P', 'I', 'C', 'H', 'S', 'A', 'V'};
static const uint32_t FORMAT = 1, HDR = 0x40, REC = 0x30;
static const uint32_t MAX_DATA = 8u << 20;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

bool read_slot(int slot, Slot &out) {
    if (slot < 0 || slot > 2) return false;
    uint32_t h = g::sysinfo() + 0x10 + slot * REC;
    uint32_t e = IMG + SAVE_CHUNK + (slot + 1) * 0x10;
    uint32_t size = M32(e + 8), data = M32(e + 0xc);
    // HasSaveFile(slot, 0) @001403f0: H[0] && H[1] && chunk data
    if (!M8(h) || !M8(h + 1) || !data || !size || size > MAX_DATA) return false;
    out.src_slot = slot;
    out.version = M32(e + 4);
    memcpy(out.header, GP(h), REC);
    out.best = M16(g::sysinfo() + 0x11c + slot * 2);   // FUN_001445f0: per-slot u16 at systemInfo+(slot+0x8c)*2+4
    out.data.assign((const uint8_t *)GP(data), (const uint8_t *)GP(data) + size);
    return true;
}

bool write_slot(int slot, const Slot &s) {
    if (slot < 0 || slot > 2 || s.data.empty()) return false;
    uint32_t size = (uint32_t)s.data.size();
    uint32_t p = g::call(0x150aec, {size});           // fl_ZiAlloc @00160aec (the allocator the chunks use)
    if (!p) return false;
    memcpy(GP(p), s.data.data(), size);
    uint32_t e = IMG + SAVE_CHUNK + (slot + 1) * 0x10;
    if (M32(e + 0xc)) g::call(0x150ab4, {M32(e + 0xc)});  // fl_Free @00160ab4, as DeleteSaveSlot @00143cb8 does
    M32(e) = slot + 1;                                 // ids 1..3 (FUN_00143d78 @00143d78)
    M32(e + 4) = s.version;
    M32(e + 8) = size;
    M32(e + 0xc) = p;
    uint32_t si = g::sysinfo();
    memcpy(GP(si + 0x10 + slot * REC), s.header, REC);
    M16(si + 0x11c + slot * 2) = s.best;
    // FUN_001445f0 @001445f0 keeps the best level / class over all slots in systemInfo[0xae] / [0xaf]
    if (M8(si + 0xae) < (unsigned)s.level()) { M8(si + 0xae) = (uint8_t)s.level(); M8(si + 0xaf) = (uint8_t)s.job(); }
    // cGameManager::SaveOpt @00145360: systemInfo -> chunk 0 (FUN_00144000) and SAVE_MemoryToFile (epic19.png),
    // the same commit cNetAuthorize::onSaveFileDeleted @0014d45c uses after DeleteSaveSlot.
    g::call(0x135360, {g::gm()});
    // The slot index inside the data (cGameWorld+0x16, serialised by cGameWorld::SaveToBinary @00143868) needs no
    // patching: cGameManager::OnGameReady @00128080 sets it to the chosen slot right after LoadGame.
    ui_log("[saveio] slot %d <- %u bytes (version %#x, from slot %d, Lv %d)", slot, size, s.version, s.src_slot, s.level());
    return true;
}

std::vector<uint8_t> encode(const Slot &s) {
    std::vector<uint8_t> f(HDR + REC + s.data.size(), 0);
    memcpy(f.data(), MAGIC, 8);
    wr32(&f[0x08], FORMAT);
    wr32(&f[0x0c], HDR);
    wr32(&f[0x10], (uint32_t)s.src_slot);
    wr32(&f[0x14], s.version);
    wr32(&f[0x18], (uint32_t)s.data.size());
    f[0x20] = (uint8_t)s.best; f[0x21] = (uint8_t)(s.best >> 8);
    memcpy(&f[HDR], s.header, REC);
    if (!s.data.empty()) memcpy(&f[HDR + REC], s.data.data(), s.data.size());
    wr32(&f[0x1c], SDL_crc32(0, &f[HDR], f.size() - HDR));
    return f;
}

static const char *NOT_OURS = "EpicHearts 저장 파일이 아닙니다.";
static const char *BROKEN = "저장 파일이 손상되었습니다.";

// the original's invariants: FUN_001445f0 @001445f0 writes chunk version 0x2a, rounds the size up to 4 and starts
// the data with the table count 7 that LoadGame @001444b8 reads back; H[0] / H[1] mark a used slot; class < 3
// (gCharNpcName). UNVERIFIED: chunk versions below 0x2a (the loaders get the version through DAT_001ab1f4).
static bool slot_ok(uint32_t version, const uint8_t *h, const uint8_t *d, uint32_t size) {
    return version != 0 && version <= 0x2a && size >= 0x40 && size <= MAX_DATA && !(size & 3) && rd32(d) == 7 && h[0] &&
           h[1] && h[0xa] <= 2;
}

bool decode(const std::vector<uint8_t> &f, Slot &out, std::string &err) {
    if (f.size() < HDR + REC || memcmp(f.data(), MAGIC, 8) != 0) { err = NOT_OURS; return false; }
    if (rd32(&f[0x08]) != FORMAT || rd32(&f[0x0c]) != HDR) { err = "지원하지 않는 저장 파일 형식입니다."; return false; }
    uint32_t size = rd32(&f[0x18]);
    if (size != f.size() - HDR - REC || SDL_crc32(0, &f[HDR], f.size() - HDR) != rd32(&f[0x1c])) { err = BROKEN; return false; }
    const uint8_t *h = &f[HDR], *d = &f[HDR + REC];
    uint32_t version = rd32(&f[0x14]);
    if (!slot_ok(version, h, d, size)) { err = BROKEN; return false; }
    out.src_slot = (int)rd32(&f[0x10]);
    out.version = version;
    memcpy(out.header, h, REC);
    out.best = (uint16_t)(f[0x20] | f[0x21] << 8);
    out.data.assign(d, d + size);
    return true;
}

// ---- original save files: epic19.png, epic19.b01, epic19.b02 (SAVE_FileToMemory @00144e28 reads them)
//  file: u32 9 | u32 size | 0x24-byte key block (its u32 at +0x20 is the seed) | size-0x20 bytes encrypted
//        | 0x20 device bytes (zero in the saves seen so far)
//  key:  FUNTER_ENC_ReadKey @00144c50: key[i] = base[i] ^ RandN(256), RandN = cflMath::RawRand @00161ef8
//        (state = state * 0x343fd + 0x269ec3, value = state >> 16 % 256), state seeded with the seed; seed 0 uses the
//        constant base "FUNTER0433...". base = the device bytes, taken as zero here: the checksum below accepts that
//        for every save seen. UNVERIFIED: saves made on a device with other bytes (they fail the checksum).
//  data: FUNTER_ENC_Decrypt @001413ec: out = in ^ mask[(i>>5)&3] ^ key[i&31], the last 4 bytes are the checksum
//        (start 0x39b7e301, += out + i*16), which are only key-XORed. The plain region is the chunk table of
//        FUN_00144afc @00144afc: 4 x {u32 id = 0..3, u32 version, u32 size, data}; chunk 0 is systemInfo (0x200),
//        chunks 1..3 are slots 0..2, the same bytes as a slot's chunk data in .ehsave.
static const char DEFAULT_KEY[33] = "FUNTER0433FUNTER0433FUNTER0433FU";
static const uint8_t RAW_MASK[4] = {0xaa, 0xce, 0x77, 0xff};

static bool raw_decrypt(const uint8_t *in, uint32_t n, uint32_t seed, std::vector<uint8_t> &out) {
    if (n < 4) return false;
    uint8_t key[32];
    uint32_t st = seed;
    for (int i = 0; i < 32; i++) {
        st = st * 0x343fd + 0x269ec3;
        key[i] = (uint8_t)((seed ? 0 : DEFAULT_KEY[i]) ^ ((st >> 16) % 256));
    }
    uint32_t m = n - 4, iv = 0x39b7e301;
    out.assign(n, 0);
    for (uint32_t i = 0; i < m; i++) {
        uint8_t b = in[i] ^ RAW_MASK[(i >> 5) & 3] ^ key[i & 31];
        out[i] = b;
        iv += b + i * 16;
    }
    for (uint32_t i = m; i < n; i++) out[i] = in[i] ^ key[i & 31];
    bool ok = rd32(&out[m]) == iv;
    out.resize(m);
    return ok;
}

struct RawChunk { uint32_t version, size, off; };

static bool raw_chunks(const std::vector<uint8_t> &d, RawChunk c[4]) {
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 4; i++) {
        if (pos + 12 > d.size() || rd32(&d[pos]) != i) return false;
        c[i] = {rd32(&d[pos + 4]), rd32(&d[pos + 8]), pos + 12};
        if (c[i].size > d.size() - c[i].off) return false;
        pos = c[i].off + c[i].size;
    }
    return pos == d.size();
}

// every slot the file holds: used[s] when its record and chunk are set (the checks decode() makes apply too)
static bool decode_raw(const std::vector<uint8_t> &f, Slot all[3], bool used[3], std::string &err) {
    uint32_t size = rd32(&f[4]);
    if (size < 0x40 || size > MAX_DATA + 0x40 || f.size() < 0x2c + (size - 0x20)) { err = BROKEN; return false; }
    std::vector<uint8_t> d;
    if (!raw_decrypt(&f[0x2c], size - 0x20, rd32(&f[0x28]), d)) {
        err = "원본 저장 파일을 복호화하지 못했습니다. (기기 키가 다른 저장 파일일 수 있습니다)";
        return false;
    }
    RawChunk c[4];
    if (!raw_chunks(d, c) || c[0].size < 0x200) { err = BROKEN; return false; }
    const uint8_t *si = d.data() + c[0].off;
    for (int s = 0; s < 3; s++) {
        const uint8_t *h = si + 0x10 + s * REC, *cd = d.data() + c[s + 1].off;
        uint32_t size_s = c[s + 1].size;
        used[s] = h[0] && h[1] && size_s;
        if (!used[s]) continue;
        if (!slot_ok(c[s + 1].version, h, cd, size_s)) { err = BROKEN; return false; }
        all[s] = Slot();
        all[s].version = c[s + 1].version;
        all[s].src_slot = s;
        memcpy(all[s].header, h, REC);
        all[s].best = (uint16_t)(si[0x11c + s * 2] | si[0x11c + s * 2 + 1] << 8);
        all[s].data.assign(cd, cd + size_s);
    }
    return true;
}

// the title's import: a .ehsave (one slot) or an original save (its used slots). An original file with one used
// slot gives that slot; with several, the one at the target index.
bool import_any(const std::vector<uint8_t> &f, int target, Slot &out, std::string &err) {
    if (f.size() >= 8 && memcmp(f.data(), MAGIC, 8) == 0) return decode(f, out, err);
    if (f.size() < 8 || rd32(&f[0]) != 9) {
        ui_log("[saveio] not an EpicHearts save: %zu bytes, head %02x %02x %02x %02x", f.size(), f.empty() ? 0 : f[0],
               f.size() > 1 ? f[1] : 0, f.size() > 2 ? f[2] : 0, f.size() > 3 ? f[3] : 0);
        err = NOT_OURS;
        return false;
    }
    Slot all[3];
    bool used[3] = {};
    if (!decode_raw(f, all, used, err)) return false;
    int count = used[0] + used[1] + used[2];
    if (count == 0) { err = "이 저장 파일에는 기록이 없습니다."; return false; }
    int pick = count == 1 ? (used[0] ? 0 : used[1] ? 1 : 2) : target;
    if (!used[pick]) {
        char buf[96];
        snprintf(buf, sizeof buf, "이 저장 파일의 슬롯 %d에는 기록이 없습니다.", pick + 1);
        err = buf;
        return false;
    }
    out = std::move(all[pick]);
    return true;
}

bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) { ui_log("[saveio] open %s: %s", path.c_str(), SDL_GetError()); return false; }
    out.clear();
    uint8_t buf[1 << 14];
    size_t n;
    while ((n = SDL_ReadIO(io, buf, sizeof buf)) > 0) {
        out.insert(out.end(), buf, buf + n);
        if (out.size() > MAX_DATA + HDR + REC) break;   // not one of ours; decode() rejects it
    }
    bool ok = SDL_GetIOStatus(io) != SDL_IO_STATUS_ERROR;
    SDL_CloseIO(io);
    return ok;
}

bool write_file(const std::string &path, const std::vector<uint8_t> &data) {
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "wb");
    if (!io) { ui_log("[saveio] create %s: %s", path.c_str(), SDL_GetError()); return false; }
    bool ok = SDL_WriteIO(io, data.data(), data.size()) == data.size();
    if (!SDL_CloseIO(io)) ok = false;
    if (!ok) ui_log("[saveio] write %s: %s", path.c_str(), SDL_GetError());
    return ok;
}

// ---- main-thread inbox (dialog callbacks arrive on SDL's dialog thread / the Android UI thread)
static std::mutex g_mx;
static std::deque<std::function<void()>> g_inbox;
void to_main(std::function<void()> fn) { std::lock_guard<std::mutex> l(g_mx); g_inbox.push_back(std::move(fn)); }
void poll() {
    for (;;) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> l(g_mx);
            if (g_inbox.empty()) return;
            fn = std::move(g_inbox.front());
            g_inbox.pop_front();
        }
        fn();
    }
}

static const SDL_DialogFileFilter FILTERS[] = {{"EpicHearts save (*.ehsave)", "ehsave"}, {"All files", "*"}};
static const SDL_DialogFileFilter OPEN_FILTERS[] = {{"EpicHearts save (*.ehsave)", "ehsave"},
                                                    {"Original save (epic19.png, .b01, .b02)", "png;b01;b02"},
                                                    {"All files", "*"}};

static void SDLCALL dialog_done(void *ud, const char *const *files, int filter) {
    (void)filter;
    auto *cb = (std::function<void(std::string)> *)ud;
    std::string path = files && files[0] ? files[0] : "";
    if (!files) ui_log("[saveio] dialog failed: %s", SDL_GetError());
    to_main([cb, path] { (*cb)(path); delete cb; });
}

static bool test_answer(const std::function<void(std::string)> &cb) {
    const char *t = getenv("EH_SAVEIO_FILE");
    if (!t) return false;
    std::string p = strcmp(t, "cancel") ? t : "";
    to_main([cb, p] { cb(p); });
    return true;
}

#ifdef __EMSCRIPTEN__
// Browser: no native dialogs. Export writes the file into the in-memory /tmp and the page downloads it
// (web/app.js Module.downloadFile). Import asks the page for a file (Module.pickSaveFile: its own picker screen, so
// the file chooser opens from a click on the page); the page copies the chosen file into /tmp and answers here.
static std::function<void(std::string)> *g_web_open;
extern "C" EMSCRIPTEN_KEEPALIVE void eh_save_picked(const char *path) {
    auto *cb = g_web_open;
    g_web_open = nullptr;
    if (!cb) return;
    std::string p = path ? path : "";
    to_main([cb, p] { (*cb)(p); delete cb; });
}
#endif

void pick_save(const std::string &default_name, std::function<void(std::string)> cb) {
    if (test_answer(cb)) return;
#ifdef __EMSCRIPTEN__
    {
        std::string path = "/tmp/" + default_name;
        cb(path); // the caller writes the file there
        EM_ASM({ if (Module.downloadFile) Module.downloadFile(UTF8ToString($0), UTF8ToString($1)); }, path.c_str(),
               default_name.c_str());
        return;
    }
#endif
    // Android: SDLActivity.showFileDialog -> ACTION_CREATE_DOCUMENT; SDL does not hand the name to SAF, EpicActivity
    // .startActivityForResult picks it up from this hint
    SDL_SetHint("EH_DOCUMENT_NAME", default_name.c_str());
    SDL_ShowSaveFileDialog(dialog_done, new std::function<void(std::string)>(std::move(cb)), ui::window, FILTERS, 2,
                           default_name.c_str());
}

void pick_open(std::function<void(std::string)> cb) {
    if (test_answer(cb)) return;
#ifdef __EMSCRIPTEN__
    delete g_web_open; // a picker left open by an earlier request is answered by this one
    g_web_open = new std::function<void(std::string)>(std::move(cb));
    EM_ASM({ if (Module.pickSaveFile) Module.pickSaveFile(); else Module._eh_save_picked(0); });
    return;
#endif
    // Android: ACTION_OPEN_DOCUMENT; "ehsave" has no MIME type, so SAF lists all files
    SDL_ShowOpenFileDialog(dialog_done, new std::function<void(std::string)>(std::move(cb)), ui::window, OPEN_FILTERS, 3,
                           nullptr, false);
}

}
