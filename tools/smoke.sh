#!/bin/bash
# Smoke test: the progression bot from a given save (default: new game) with the progress watchdog.
# Fixed at its current level (new game -> main quest 13). It records how far it got and where it stalled;
# it is not extended further for story-specific situations.
# usage: tools/smoke.sh <work_dir> [save_dir] [seconds]
W="$1"; SAVE="$2"; T="${3:-1200}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$W/save"
[ -n "$SAVE" ] && cp "$SAVE"/* "$W/save/"
cp -n "$ROOT/save/flfoo.png" "$ROOT/save/fttcustom.otf" "$W/save/" 2>/dev/null
(cd "$W" && EH_BOT=1 EH_WATCHDOG=1 EH_TESTGOD=1 EH_TESTBUFF=1 EH_TURBO=1 timeout "$T" "$ROOT/build/epichearts.exe" "$ROOT/epichearts.apk" > smoke.log 2>&1)
echo "main quests: $(grep -a 'main quest id' "$W/smoke.log" | awk '{print $5}' | uniq | tr '\n' ' ')"
echo "fields: $(grep -a '\[bot\] field' "$W/smoke.log" | awk '{print $3}' | uniq | wc -l) changes"
grep -a "watchdog\]\|CRASH" "$W/smoke.log" | head -3
