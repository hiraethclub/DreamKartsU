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
#
# Sound (M4): each race must also have mixed audible effects, and, when
# music/ holds the songs (tools/build_music.py), started a song with no
# play failures and mixed it audibly (snd_shim.c's "DKU AUDIO" lines).
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
# Tracks whose DK XFCHECK counts upstream's own sphere-vs-outcode check
# failing, not a difference in port/xform_c.c: Temple Hill's bystanders
# bob by moving their strip vertices (world_props.c bystanders_update)
# past the bounding sphere built before the bob (world_mesh.c
# strip_bound). Harmless: such a vertex is at worst a little off screen.
XF_KNOWN=${XF_KNOWN:-"3"}
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
    # The GL build draws offscreen under its own Xvfb (Mesa llvmpipe) on
    # the virtual clock, with no sound, and saves PNGs at fixed frames.
    wrap=""
    if [ "$BACKEND" = gl ]; then
        wrap="xvfb-run -n $((90 + t))"
        rm -f captures/host/t$t-f*.png
    fi
    DKU_SAVE_DIR=$save DKU_MAX_FRAMES=${MAX_FRAMES:-40000} DKU_CAPTURE_DIR=captures/host \
        DKU_VIRTUAL_CLOCK=1 DKU_REPRO=1 SDL_AUDIODRIVER=dummy DKU_SHOT_TAG=t$t DKU_SHOTS="${SHOTS:-300 1800 4200 7200}" \
        timeout "$TIMEOUT" $wrap $RUN "$bin" >"$LOGDIR/run-t$t.log" 2>&1 &
    pid=$!
    # Off the console the game keeps going after its verdict; end the run
    # a moment after the verdict line.
    while kill -0 "$pid" 2>/dev/null; do
        if grep -qE 'DK AUTOTEST: (PASS|FAIL)' "$LOGDIR/run-t$t.log" 2>/dev/null; then
            sleep 1
            kill "$pid" 2>/dev/null
            break
        fi
        sleep 2
    done
    wait "$pid"
    echo $? >"$LOGDIR/exit-t$t"
}

mkdir -p captures/host
pids=""
for t in $TRACKS; do
    run_track "$t" &
    pids="$pids $!"
done
for p in $pids; do wait "$p"; done

# "ok", or what is wrong with the race's sound.
sound_verdict() {
    a=$(grep 'DKU AUDIO:' "$1" | tail -1)
    [ -n "$a" ] || { echo "no audio mixed"; return; }
    sp=$(echo "$a" | sed -E 's/.*effects peak ([0-9]+).*/\1/')
    mp=$(echo "$a" | sed -E 's/.*music peak ([0-9]+).*/\1/')
    [ "$sp" -gt 1000 ] || { echo "effects silent"; return; }
    if [ -f music/01.mp3 ]; then
        m=$(grep -m1 'DK AUTOTEST: music:' "$1")
        echo "$m" | grep -qE ': [1-9][0-9]* CD-DA tracks, [1-9][0-9]* songs started, [0-9]+ ended, 0 play failures' ||
            { echo "music: ${m#DK AUTOTEST: music: }"; return; }
        [ "$mp" -gt 1000 ] || { echo "music silent"; return; }
    fi
    echo ok
}

[ -f music/01.mp3 ] || echo "test_host: no music/ (tools/build_music.py): checking effects only"
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
    snd=$(sound_verdict "$log")
    if [ "$snd" != ok ]; then verdict="FAIL ($snd)"; fail=1; fi
    # The null backend checks every batch it is given (port/render_null.c).
    nbad=$(sed -n 's/.*frames, \([0-9]*\) bad batches.*/\1/p' "$log")
    if [ "${nbad:-0}" != 0 ]; then verdict="FAIL ($nbad bad batches)"; fail=1; fi
    xf=$(grep 'DK XFCHECK' "$log" | tail -1 || true)
    if [ -n "$xf" ] && ! echo "$xf" | grep -q 'mismatch=0'; then
        case " $XF_KNOWN " in
            *" $t "*) verdict="$verdict (known upstream XFCHECK count, see XF_KNOWN)" ;;
            *) verdict="FAIL ($xf)"; fail=1 ;;
        esac
    fi
    printf 'track %s %-16s %s %s\n' "$t" "${name:-?}" "$verdict" "$(grep -m1 'player finish' "$log" | sed 's/DK AUTOTEST: //')"
done
if [ $fail = 0 ]; then echo "test_host: PASS"; else echo "test_host: FAIL (logs in $LOGDIR)"; fi
exit $fail
