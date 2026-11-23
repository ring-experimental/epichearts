#!/usr/bin/env bash
# Fetches third_party/ for a build from a clean checkout (the web build in CI, or any machine): official releases
# at pinned versions, checked by SHA-256. Existing folders are kept. LZMA SDK is not needed by the host build.
#   SDL 3.4.18, FreeType 2.14.3, RmlUi 6.3, stb_truetype v1.26 (nothings/stb@6e9f34d), dr_mp3 (mackron/dr_libs@51e61d3)
set -euo pipefail
cd "$(dirname "$0")/.."
T=third_party
mkdir -p "$T/_dl" "$T/stb" "$T/dr_libs"

fetch() { # url sha256 file
  if [ -f "$3" ] && echo "$2  $3" | sha256sum -c --quiet - 2>/dev/null; then return; fi
  curl -fsSL "$1" -o "$3.part"
  echo "$2  $3.part" | sha256sum -c --quiet -
  mv "$3.part" "$3"
}
unpack() { # archive dir
  [ -d "$2" ] && return
  mkdir -p "$2.tmp" && tar xzf "$1" -C "$2.tmp" --strip-components=1 && mv "$2.tmp" "$2"
}

fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz \
  9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3 "$T/_dl/SDL3-3.4.18.tar.gz"
unpack "$T/_dl/SDL3-3.4.18.tar.gz" "$T/SDL"
fetch https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz \
  e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1 "$T/_dl/freetype-2.14.3.tar.gz"
unpack "$T/_dl/freetype-2.14.3.tar.gz" "$T/freetype"
fetch https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz \
  d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede "$T/_dl/rmlui-6.3.tar.gz"
unpack "$T/_dl/rmlui-6.3.tar.gz" "$T/rmlui"
fetch https://raw.githubusercontent.com/nothings/stb/6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760/stb_truetype.h \
  ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab "$T/stb/stb_truetype.h"
fetch https://raw.githubusercontent.com/mackron/dr_libs/51e61d308dde6b437fce0c5fabb32cd86b40f4d7/dr_mp3.h \
  997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f "$T/dr_libs/dr_mp3.h"
echo "third_party ready"
