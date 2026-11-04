#!/bin/sh
# Wii U check (PORT_PLAN.md M3): the Wii U builds compile with -Wall -Wextra
# and no warnings; each track's autotest build races to "DK AUTOTEST: PASS"
# in Cemu; the release build boots to the title. Exit code is the verdict.
#
#   tools/test_wiiu.sh                 every raceable track
#   TRACKS="0 7" tools/test_wiiu.sh    some tracks only
#   CEMU=0 tools/test_wiiu.sh          builds only (no Cemu on this machine)
#
# Cemu runs on a private Xvfb with a private config (tools/cemu_run.sh);
# screenshots land in captures/wiiu/. Cemu's speed here says nothing about
# the console's: it renders with Mesa's software rasteriser.
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

for t in $TRACKS; do
    log=$LOGDIR/cemu-t$t/log.txt
    name=$(grep -m1 -oE 'DreamKarts track: [^,]*' "$log" 2>/dev/null | sed 's/DreamKarts track: //')
    if grep -q 'DK AUTOTEST: PASS' "$log" 2>/dev/null; then verdict=PASS; else verdict=FAIL; fail=1; fi
    printf 'track %s %-16s %s %s\n' "$t" "${name:-?}" "$verdict" \
        "$(grep -m1 'player finish' "$log" 2>/dev/null | sed 's/DK AUTOTEST: //')"
done

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
