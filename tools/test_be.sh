#!/bin/sh
# Big-endian autotest: the headless host build cross-compiled for 32-bit
# PowerPC (big-endian, like the Wii U's Espresso) and run under qemu-user.
# Catches byte-order bugs (packs, saves, network encoding) without the
# console. Needs gcc-powerpc-linux-gnu and qemu-user (PORT_PLAN.md).
set -eu
cd "$(dirname "$0")/.."
command -v powerpc-linux-gnu-gcc >/dev/null || { echo "test_be: install gcc-powerpc-linux-gnu"; exit 2; }
QEMU=$(command -v qemu-ppc || command -v qemu-ppc-static || true)
[ -n "$QEMU" ] || { echo "test_be: install qemu-user"; exit 2; }
# Emulation is slow: fewer tracks by default, a long timeout.
BACKEND=null CROSS=powerpc-linux-gnu- RUN="$QEMU -L /usr/powerpc-linux-gnu" \
    TRACKS=${TRACKS:-"0 7"} TIMEOUT=${TIMEOUT:-3600} exec tools/test_host.sh
