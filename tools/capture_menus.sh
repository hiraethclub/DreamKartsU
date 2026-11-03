#!/bin/sh
# Screenshots of what the race autotest never shows: the title screen, the
# menus, the two-player kart select and a split-screen race. Drives the GL
# release build with scripted pad presses (DKU_INPUT, port/plat_host.c)
# and saves captures/host/menu-fNNNNN.png for tools/contact_sheet.py.
# Exit code: 0 if every screenshot was written.
set -eu
cd "$(dirname "$0")/.."
make -s -j"$(nproc)" BACKEND=gl VARIANT=release
save=build/test-save-menus
rm -rf "$save" && mkdir -p "$save" captures/host
rm -f captures/host/menu-f*.png
# Title: START. Main menu: TWO PLAYER. Race setup: down to NEXT. Kart
# select: both players ready. Then the track select and the race.
INPUT="200:0:START 300:0:DOWN 360:0:A 420:0:DOWN 440:0:DOWN 460:0:DOWN 480:0:DOWN 500:0:A"
INPUT="$INPUT 600:0:A 620:1:A 700:0:A 720:1:A 800:0:A 900:0:A"
SHOTS="150 280 340 420 650 750 1000 1600"
DKU_INPUT=$INPUT DKU_SHOTS=$SHOTS DKU_SHOT_TAG=menu DKU_CAPTURE_DIR=captures/host \
    DKU_REPRO=1 DKU_VIRTUAL_CLOCK=1 DKU_SAVE_DIR=$save DKU_MAX_FRAMES=1601 SDL_AUDIODRIVER=dummy \
    timeout 600 xvfb-run -a build/host-gl-release/dreamkartsu >build/capture-menus.log 2>&1 || true
missing=0
for f in $SHOTS; do
    [ -f "captures/host/menu-f$(printf %05d "$f").png" ] || { echo "capture_menus: no frame $f"; missing=1; }
done
[ $missing = 0 ] && echo "capture_menus: PASS" || echo "capture_menus: FAIL (log in build/capture-menus.log)"
exit $missing
