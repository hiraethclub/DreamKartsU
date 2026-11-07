#!/bin/sh
# The GPU path against the Dreamcast's CPU path (PORT_PLAN.md principle 4).
# For each track, the GL host autotest runs twice on the virtual clock, once
# with NATIVE=0 (render.c transforms, clips and culls on the CPU, as on the
# Dreamcast) and once with NATIVE=1 (port/render_native.c: the GPU does it),
# saving the same frames. The race must play out the same (the autotest's
# results) and the pictures must match, apart from
# the sub-pixel differences of doing the same sums on a different unit
# (tools/compare_shots.py says how close is close). Exit code is the verdict.
#
#   tools/test_native.sh               every raceable track
#   TRACKS="0 7" tools/test_native.sh  some tracks only
set -u
cd "$(dirname "$0")/.."
TRACKS=${TRACKS:-"0 1 2 3 4 6 7 8"}
SHOTS=${SHOTS:-"300 1800 4200 7200"}
LAST=$(echo "$SHOTS" | tr ' ' '\n' | sort -n | tail -1)
JOBS=${JOBS:-$(nproc)}
dir=build/test-native
rm -rf "$dir" && mkdir -p "$dir"

for t in $TRACKS; do
    for n in 0 1; do
        if ! make -s -j"$JOBS" BACKEND=gl VARIANT=autotest NATIVE=$n DK_AT_TRACK="$t" >"$dir/build-t$t-n$n.log" 2>&1; then
            cat "$dir/build-t$t-n$n.log"
            echo "test_native: build failed (track $t, NATIVE=$n)"
            exit 1
        fi
    done
done

run() {   # run TRACK NATIVE
    t=$1 n=$2
    tag=$([ "$n" = 1 ] && echo "" || echo "-cpu")
    out=$dir/n$n
    mkdir -p "$out" "$dir/save-t$t-n$n"
    DKU_SAVE_DIR=$dir/save-t$t-n$n DKU_MAX_FRAMES=${MAX_FRAMES:-12000} DKU_CAPTURE_DIR=$out DKU_VIRTUAL_CLOCK=1 \
        DKU_REPRO=1 SDL_AUDIODRIVER=dummy DKU_SHOT_TAG=t$t DKU_SHOTS="$SHOTS" \
        timeout 600 xvfb-run -n $((120 + t * 2 + n)) "build/host-gl-autotest$tag-t$t/dreamkartsu" \
        >"$dir/run-t$t-n$n.log" 2>&1
}

pids=""
for t in $TRACKS; do
    for n in 0 1; do
        run "$t" "$n" &
        pids="$pids $!"
    done
done
for p in $pids; do wait "$p"; done

fail=0
for t in $TRACKS; do
    # The race's results, all but the timing line (CPU time differs, as it should).
    a=$(grep '^DK AUTOTEST' "$dir/run-t$t-n0.log" | grep -v 'fps min')
    b=$(grep '^DK AUTOTEST' "$dir/run-t$t-n1.log" | grep -v 'fps min')
    if [ -z "$a" ] || [ "$a" != "$b" ]; then
        echo "track $t: FAIL (the race differs between the paths: DK AUTOTEST lines)"
        fail=1
    else
        echo "track $t: same race ($(echo "$a" | grep -m1 'player finish' | sed 's/DK AUTOTEST: //'))"
    fi
done
python3 -I tools/compare_shots.py "$dir/n0" "$dir/n1" "$dir/diff" || fail=1
# The rule must still see an area: a 12 px square painted on one picture.
mkdir -p "$dir/neg/a" "$dir/neg/b"
one=$(ls "$dir/n1" | head -1)
cp "$dir/n1/$one" "$dir/neg/a/"
python3 -I -c "import sys; from PIL import Image; im = Image.open(sys.argv[1]).convert('RGB'); \
im.paste((255, 0, 255), (300, 300, 312, 312)); im.save(sys.argv[2])" "$dir/n1/$one" "$dir/neg/b/$one"
if python3 -I tools/compare_shots.py "$dir/neg/a" "$dir/neg/b" "$dir/neg/diff" >/dev/null; then
    echo "test_native: FAIL (the picture check missed a 12 px square)"
    fail=1
else
    echo "picture check sees a 12 px square: PASS"
fi
if [ $fail = 0 ]; then echo "test_native: PASS"; else echo "test_native: FAIL (files in $dir)"; fi
exit $fail
