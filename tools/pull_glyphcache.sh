#!/bin/bash
# Pull the glyph cache that the ORIGINAL game wrote on an Android device/emulator (files/flfoo.png,
# written by cSmartCacheFont after rendering with Android's font engine) into data/flfoo_android.png.
# The port seeds save/flfoo.png from it, so cached text uses the original's glyph pixels and widths.
# Needs a rooted device/emulator (adb root) on which the original app has been started once.
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
mkdir -p data
adb root >/dev/null 2>&1; sleep 2
adb pull /data/data/com.ftt.ankrepicheartslite/files/flfoo.png data/flfoo_android.png
