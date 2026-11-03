#!/bin/sh
# Host autotest: build the autotest variant for every raceable track, run a
# full CPU-driven race on each, pass only if every one prints
# "DK AUTOTEST: PASS". Exit code is the verdict.
#
#   tools/test_host.sh                 every track, GL backend if SDL2 is
#                                      installed (screenshots, M1), else null
#   BACKEND=null tools/test_host.sh    no window, logic only
#   VARIANT=xfcheck tools/test_host.sh also check port/xform_c.c against
#                                      render.c's C reference (DK XFCHECK)
#   CROSS=powerpc-linux-gnu- RUN="qemu-ppc -L /usr/powerpc-linux-gnu"
#                                      big-endian run (tools/test_be.sh)
#   TRACKS="0 7" tools/test_host.sh    some tracks only
set -u
cd "$(dirname "$0")/.."
if [ -z "${BACKEND:-}" ]; then
    if pkg-config --exists sdl2 gl 2>/dev/null; then BACKEND=gl; else BACKEND=null; fi
fi
VARIANT=${VARIANT:-autotest}
CROSS=${CROSS:-}
RUN=${RUN:-}
# Raceable tracks: TRACK_DISABLED in upstream src/track.h leaves out
# Cyberstone (5) and the Free Roam lot (9).
TRACKS=${TRACKS:-"0 1 2 3 4 6 7 8"}
TIMEOUT=${TIMEOUT:-900}
JOBS=${JOBS:-$(nproc)}
LOGDIR=build/test-${CROSS}host-$BACKEND-$VARIANT
mkdir -p "$LOGDIR"
echo "test_host: backend $BACKEND, variant $VARIANT${CROSS:+, cross $CROSS}, tracks $TRACKS"

for t in $TRACKS; do
    if ! make -s -j"$JOBS" BACKEND="$BACKEND" VARIANT="$VARIANT" CROSS="$CROSS" DK_AT_TRACK="$t" \
            >"$LOGDIR/build-t$t.log" 2>&1; then
        cat "$LOGDIR/build-t$t.log"
        echo "test_host: build failed for track $t"
        exit 1
    fi
done

run_track() {
    t=$1
    bin=build/${CROSS}host-$BACKEND-$VARIANT-t$t/dreamkartsu
    save=build/test-save-t$t
    rm -rf "$save" && mkdir -p "$save"
    # 60 frames a second of virtual time: a race and its results fit well
    # inside 40000 frames; the frame cap ends the run if PASS never comes.
    DKU_SAVE_DIR=$save DKU_MAX_FRAMES=${MAX_FRAMES:-40000} DKU_CAPTURE_DIR=captures/host \
        timeout "$TIMEOUT" $RUN "$bin" >"$LOGDIR/run-t$t.log" 2>&1
    echo $? >"$LOGDIR/exit-t$t"
}

mkdir -p captures/host
pids=""
for t in $TRACKS; do
    run_track "$t" &
    pids="$pids $!"
done
for p in $pids; do wait "$p"; done

fail=0
for t in $TRACKS; do
    log=$LOGDIR/run-t$t.log
    name=$(grep -m1 -oE 'DreamKarts track: [^,]*' "$log" | sed 's/DreamKarts track: //')
    if grep -q 'DK AUTOTEST: PASS' "$log" && ! grep -q 'DK AUTOTEST: FAIL' "$log"; then
        verdict=PASS
    else
        verdict=FAIL
        fail=1
    fi
    xf=$(grep -m1 'DK XFCHECK' "$log" || true)
    if [ -n "$xf" ] && ! echo "$xf" | grep -q 'mismatch=0'; then verdict="FAIL ($xf)"; fail=1; fi
    printf 'track %s %-16s %s %s\n' "$t" "${name:-?}" "$verdict" "$(grep -m1 'player finish' "$log" | sed 's/DK AUTOTEST: //')"
done
if [ $fail = 0 ]; then echo "test_host: PASS"; else echo "test_host: FAIL (logs in $LOGDIR)"; fi
exit $fail
