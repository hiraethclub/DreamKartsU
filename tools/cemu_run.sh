#!/bin/sh
# Run a .wuhb in Cemu (~/Applications/Cemu-*.AppImage) on a private Xvfb,
# with a private Cemu config and data folder (a copy of the user's
# settings: OpenGL, OSReport logging on, no update check), so the user's
# own Cemu setup is never touched.
#
#   tools/cemu_run.sh game.wuhb SECONDS OUTDIR [SHOT_EVERY]
#
# Writes OUTDIR/log.txt (the game's printf lines, "[OSConsole]" stripped)
# and, every SHOT_EVERY seconds (default 15), OUTDIR/shot-NNN.png. Ends
# after SECONDS or a moment after a line matching VERDICT (an extended
# regex, default "DK AUTOTEST: (PASS|FAIL)").
#
# CEMU_SD=DIR uses DIR as the emulated SD card and keeps it, so a second
# run sees what the first saved; otherwise each run starts with an empty
# card.
set -eu
verdict=${VERDICT:-'DK AUTOTEST: (PASS|FAIL)'}
game=$(realpath "$1"); secs=$2; out=$3; every=${4:-15}
cemu=$(ls "$HOME"/Applications/Cemu-*.AppImage 2>/dev/null | head -1)
[ -n "$cemu" ] || { echo "cemu_run: no Cemu AppImage in ~/Applications"; exit 2; }
mkdir -p "$out"; out=$(realpath "$out")
home=$out/cemu-home
rm -rf "$home"; mkdir -p "$home/config/Cemu" "$home/data/Cemu"
if [ -f "$HOME/.config/Cemu/settings.xml" ]; then
    cp "$HOME/.config/Cemu/settings.xml" "$home/config/Cemu/"
    # OSReport is Cemu's log category 17 (found by bisecting the flags).
    python3 - "$home/config/Cemu/settings.xml" <<'PY'
import re, sys
p = sys.argv[1]; s = open(p).read()
s = re.sub(r"<logflag>\d*</logflag>", "<logflag>131072</logflag>", s)
s = s.replace("<check_update>true</check_update>", "<check_update>false</check_update>")
s = re.sub(r"(<Graphic>\s*<api>)\d+(</api>)", r"\g<1>0\2", s)
open(p, "w").write(s)
PY
fi
if [ -n "${CEMU_SD:-}" ]; then
    mkdir -p "$CEMU_SD"
    ln -s "$(realpath "$CEMU_SD")" "$home/data/Cemu/sdcard"
fi
disp=:$((150 + $$ % 50))
Xvfb "$disp" -screen 0 1280x800x24 >/dev/null 2>&1 &
xvfb=$!
trap 'kill $cemu_pid $xvfb 2>/dev/null || true' EXIT
sleep 2
DISPLAY=$disp XDG_CONFIG_HOME=$home/config XDG_DATA_HOME=$home/data "$cemu" -g "$game" >"$out/cemu-stdout.log" 2>&1 &
cemu_pid=$!
log=$home/data/Cemu/log.txt
t=0; n=0
while [ $t -lt "$secs" ] && kill -0 $cemu_pid 2>/dev/null; do
    sleep 1; t=$((t + 1))
    if [ $((t % every)) = 0 ]; then
        n=$((n + 1)); import -display "$disp" -window root "$out/shot-$(printf %03d $n).png" 2>/dev/null || true
    fi
    if grep -qE "$verdict" "$log" 2>/dev/null; then sleep 2; break; fi
done
grep -a '\[OSConsole\]' "$log" 2>/dev/null | sed 's/^.*\[OSConsole\] //' >"$out/log.txt" || true
grep -E "$verdict" "$out/log.txt" || echo "cemu_run: no verdict in ${t}s"
