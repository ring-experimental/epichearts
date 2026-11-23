// Save slot export / import (title screen "모험 선택" panel; replaces the original's server save keeper / sync,
// cFormMainMenu +0xcc / form 0x3a, work/title_spec.md). One slot goes to one self-describing file (.ehsave).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace saveio {

struct Slot {                    // one slot as carried by an .ehsave file
    uint32_t version = 0;        // gm_save_chunk entry +4 (FUN_001445f0 writes 0x2a)
    uint8_t header[0x30] = {};   // systemInfo+0x10+slot*0x30
    uint16_t best = 0;           // systemInfo+0x11c+slot*2
    std::vector<uint8_t> data;   // gm_save_chunk entry data (+0xc, size +8)
    int src_slot = 0;
    int level() const { return header[2] | header[3] << 8; }
    int seconds() const { return (int)(header[4] | header[5] << 8 | header[6] << 16 | (uint32_t)header[7] << 24); }
    int job() const { return header[0xa]; }
};

// game thread: the slot as it is in memory now; false when the slot holds no game (HasSaveFile(slot, 0) @001403f0)
bool read_slot(int slot, Slot &out);
// game thread: replace a slot with s and write epic19.png (cGameManager::SaveOpt @00145360)
bool write_slot(int slot, const Slot &s);

std::vector<uint8_t> encode(const Slot &s);
// validates the container and the original's own invariants; err is a user-facing Korean message
bool decode(const std::vector<uint8_t> &file, Slot &out, std::string &err);
// import file into slot target (0..2): a .ehsave, or an original epic19.png / .b01 / .b02 (decrypted here)
bool import_any(const std::vector<uint8_t> &file, int target, Slot &out, std::string &err);

bool read_file(const std::string &path, std::vector<uint8_t> &out);   // SDL_IOStream: paths and Android content:// URIs
bool write_file(const std::string &path, const std::vector<uint8_t> &data);

// native file dialogs (SDL_ShowSaveFileDialog / SDL_ShowOpenFileDialog; Android: SAF through SDLActivity).
// cb runs on the main thread from poll() with the chosen path, or "" when cancelled / failed.
// Test hook: EH_SAVEIO_FILE=<path> skips the dialog and answers with that path.
void pick_save(const std::string &default_name, std::function<void(std::string)> cb);
void pick_open(std::function<void(std::string)> cb);

// any thread: queue work for the main thread; poll() (main thread) runs it
void to_main(std::function<void()> fn);
void poll();

}
