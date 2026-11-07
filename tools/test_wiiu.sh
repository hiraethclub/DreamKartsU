#!/bin/sh
# Wii U check (PORT_PLAN.md M3, M4): the Wii U builds compile with -Wall
# -Wextra and no warnings; each track's autotest build races to "DK
# AUTOTEST: PASS" in Cemu with sound (AX played the mix for most of the
# race, effects audible and, when the .wuhb carries music, a song started
# and audible); the save check (port/savetest.c) writes its settings on
# one boot and reads them all back on the next; the test log file is
# complete after a clean exit; the release build boots to the title. Exit code is the verdict.
#
#   tools/test_wiiu.sh                 every raceable track
#   TRACKS="0 7" tools/test_wiiu.sh    some tracks only
#   CEMU=0 tools/test_wiiu.sh          builds only (no Cemu on this machine)
#
# Cemu runs on a private Xvfb with a private config (tools/cemu_run.sh);
# screenshots land in captures/wiiu/. Cemu's speed here says nothing about
# the console's: it renders with Mesa's software rasteriser. Nor do its
# pictures show the GPU path's world (PORT_PLAN.md, log 2026-11-07): Cemu
# draws no world with the little-endian indices the console draws
# correctly, so in Cemu this checks booting, the race's rules, sound,
# saves and exit. tools/test_native.sh and console screenshots check the
# pictures.
set -u
cd "$(dirname "$0")/.."
TRACKS=${TRACKS:-"0 1 2 3 4 6 7 8"}
JOBS=${JOBS:-4}
LOGDIR=build/test-wiiu
mkdir -p "$LOGDIR" captures/wiiu
fail=0

build() {
    log=$LOGDIR/build-$1.log
    shift
    if ! make -s -f Makefile.wiiu -j"$(nproc)" "$@" >"$log" 2>&1; then
        cat "$log"; echo "test_wiiu: build failed ($*)"; return 1
    fi
    if grep -qi "warning" "$log"; then
        grep -i -A3 "warning" "$log"; echo "test_wiiu: warnings ($*)"; return 1
    fi
}

build release || fail=1
build savetest VARIANT=savetest || fail=1
build exittest VARIANT=exittest || fail=1
for t in $TRACKS; do build "t$t" VARIANT=autotest DK_AT_TRACK="$t" || fail=1; done
[ $fail = 0 ] && echo "test_wiiu: builds clean (release and ${TRACKS} autotest)"
if [ "${CEMU:-1}" = 0 ] || [ $fail != 0 ]; then
    [ $fail = 0 ] && echo "test_wiiu: PASS (builds only)" || echo "test_wiiu: FAIL"
    exit $fail
fi

run_track() {
    t=$1
    out=$LOGDIR/cemu-t$t
    tools/cemu_run.sh "build/wiiu-gx2-autotest-t$t/DreamKartsU.wuhb" 900 "$out" 30 >/dev/null 2>&1
    rm -f captures/wiiu/t$t-*.png
    for s in "$out"/shot-*.png; do [ -f "$s" ] && cp "$s" "captures/wiiu/t$t-$(basename "$s")"; done
}

running=0
for t in $TRACKS; do
    run_track "$t" &
    running=$((running + 1))
    if [ $running -ge "$JOBS" ]; then wait; running=0; fi
done
wait

# "ok", or what is wrong with a race's sound. AX's count of seconds
# played must reach half the race's length: Cemu consumed the mix.
music=$(ls build/wiiu-gx2-autotest-t*/content/music/01.mp3 2>/dev/null | head -1)
sound_verdict() {
    ax=$(grep 'DKU AX: [0-9]* s played' "$1" | tail -1 | sed -E 's/.*: ([0-9]+) s.*/\1/')
    race=$(grep -m1 'player finish' "$1" | sed -E 's/.*finish ([0-9]+).*/\1/')
    [ -n "$ax" ] || { echo "AX never played"; return; }
    [ -n "$race" ] && [ "$ax" -lt $((race / 2)) ] && { echo "AX played ${ax}s of a ${race}s race"; return; }
    a=$(grep 'DKU AUDIO:' "$1" | tail -1)
    sp=$(echo "$a" | sed -E 's/.*effects peak ([0-9]+).*/\1/')
    mp=$(echo "$a" | sed -E 's/.*music peak ([0-9]+).*/\1/')
    [ "${sp:-0}" -gt 1000 ] || { echo "effects silent"; return; }
    if [ -n "$music" ]; then
        grep -qE 'DK AUTOTEST: music: [1-9][0-9]* CD-DA tracks, [1-9][0-9]* songs started, [0-9]+ ended, 0 play' "$1" ||
            { grep -q 'DK AUTOTEST:' "$1" && echo "music did not start" || echo "no autotest result (race too slow to finish?)"; return; }
        [ "${mp:-0}" -gt 1000 ] || { echo "music silent"; return; }
    fi
    echo ok
}

[ -n "$music" ] || echo "test_wiiu: no music in the .wuhb (tools/build_music.py): checking effects only"
for t in $TRACKS; do
    log=$LOGDIR/cemu-t$t/log.txt
    name=$(grep -m1 -oE 'DreamKarts track: [^,]*' "$log" 2>/dev/null | sed 's/DreamKarts track: //')
    if grep -q 'DK AUTOTEST: PASS' "$log" 2>/dev/null; then verdict=PASS; else verdict=FAIL; fail=1; fi
    snd=$(sound_verdict "$log")
    if [ "$snd" != ok ]; then verdict="FAIL ($snd)"; fail=1; fi
    printf 'track %s %-16s %s %s\n' "$t" "${name:-?}" "$verdict" \
        "$(grep -m1 'player finish' "$log" 2>/dev/null | sed 's/DK AUTOTEST: //')"
done

# Saves: two boots on one emulated SD card, empty at first.
sd=$LOGDIR/sd-save
rm -rf "$sd"
st1=$(VERDICT='DKU SAVETEST: [A-Z]+' CEMU_SD=$sd tools/cemu_run.sh build/wiiu-gx2-savetest/DreamKartsU.wuhb 60 \
    "$LOGDIR/cemu-save1" 60 2>/dev/null | grep -oE 'DKU SAVETEST: [A-Z]+' | sed 's/.*: //')
st2=$(VERDICT='DKU SAVETEST: [A-Z]+' CEMU_SD=$sd tools/cemu_run.sh build/wiiu-gx2-savetest/DreamKartsU.wuhb 60 \
    "$LOGDIR/cemu-save2" 60 2>/dev/null | grep -oE 'DKU SAVETEST: [A-Z]+' | sed 's/.*: //')
if [ "$st1" = WROTE ] && [ "$st2" = PASS ]; then echo "saves: written, Cemu restarted, read back"
else echo "saves: FAIL (first boot ${st1:-nothing}, second ${st2:-nothing})"; fail=1; fi

# Exit: leaves the game as "Close software" does, with music playing, and
# shuts down without hanging or crashing. Cemu has no HOME menu, so the
# menu itself is only tested on the console (TESTING.md).
out=$LOGDIR/cemu-exit
sd=$LOGDIR/sd-exit
rm -rf "$sd"
VERDICT='DKU EXIT: done' CEMU_SD=$sd tools/cemu_run.sh build/wiiu-gx2-exittest/DreamKartsU.wuhb 300 "$out" 120 >/dev/null 2>&1
if grep -q 'DKU EXIT: done' "$out/log.txt" && ! grep -q 'signal 11' "$out/cemu-stdout.log"; then
    echo "exit: clean shutdown"
else
    echo "exit: FAIL (log in $out/log.txt)"; fail=1
fi

# The test log: one file per build next to the app, with the frame timing
# lines, and everything written by the time the app has gone (testlog.c).
build_no=$(cut -d' ' -f1 BUILD_NUMBER)
tlog=$sd/wiiu/apps/DreamKartsU/DreamKartsU-build-$(printf %04d "$build_no").log
if grep -q "DreamKartsU build $build_no," "$tlog" 2>/dev/null && grep -q '^\[.*PERF ' "$tlog" &&
    grep -q 'DKU EXIT: done' "$tlog"; then
    echo "test log: $(basename "$tlog") complete, ends with the exit"
else
    echo "test log: FAIL (no complete $tlog)"; fail=1
fi

# Release: boots, finds the GamePad, reaches the title (frames go by).
out=$LOGDIR/cemu-release
tools/cemu_run.sh build/wiiu-gx2-release/DreamKartsU.wuhb 25 "$out" 20 >/dev/null 2>&1
cp "$out/shot-001.png" captures/wiiu/release-title.png 2>/dev/null || true
if grep -q 'DreamKarts boot:' "$out/log.txt" && grep -q 'DKU GX2: up' "$out/log.txt"; then
    echo "release: boots to the title (captures/wiiu/release-title.png)"
else
    echo "release: FAIL (log in $out/log.txt)"; fail=1
fi
[ $fail = 0 ] && echo "test_wiiu: PASS" || echo "test_wiiu: FAIL (logs in $LOGDIR)"
exit $fail
