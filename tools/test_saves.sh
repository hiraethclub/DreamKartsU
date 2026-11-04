#!/bin/sh
# Save check on the host (PORT_PLAN.md M4): VARIANT=savetest runs upstream
# save.c through the shim's save files. From an empty folder the first run
# writes the test settings and a second run, a restart, must read every one
# back (port/savetest.c). With the big-endian toolchain and qemu-ppc, the
# PowerPC build must read the x86 build's file and write a byte-identical
# one, so saves move between the PC and the Wii U. Exit code is the verdict.
# tools/test_wiiu.sh does the same in Cemu.
set -u
cd "$(dirname "$0")/.."
dir=build/test-saves
rm -rf "$dir" && mkdir -p "$dir"
fail=0

run() {   # run SAVE_DIR BIN [RUNNER...]: the run's verdict word
    d=$1 && shift
    DKU_SAVE_DIR=$d "$@" 2>&1 | tee -a "$dir/log.txt" | grep -oE 'DKU SAVETEST: [A-Z]+' | sed 's/.*: //'
}

make -s -j"$(nproc)" VARIANT=savetest >"$dir/build.log" 2>&1 || { cat "$dir/build.log"; exit 1; }
bin=build/host-null-savetest/dreamkartsu
a=$(run "$dir/le" "$bin")
b=$(run "$dir/le" "$bin")
if [ "$a" = WROTE ] && [ "$b" = PASS ]; then echo "saves: x86 write, restart, read back: PASS"
else echo "saves: x86: FAIL (first run $a, second $b)"; fail=1; fi

QEMU=$(command -v qemu-ppc || command -v qemu-ppc-static || true)
if command -v powerpc-linux-gnu-gcc >/dev/null && [ -n "$QEMU" ]; then
    make -s -j"$(nproc)" VARIANT=savetest CROSS=powerpc-linux-gnu- >>"$dir/build.log" 2>&1 ||
        { cat "$dir/build.log"; exit 1; }
    be="$QEMU -L /usr/powerpc-linux-gnu build/powerpc-linux-gnu-host-null-savetest/dreamkartsu"
    c=$(run "$dir/le" $be)
    d=$(run "$dir/be" $be)
    e=$(run "$dir/be" $be)
    if [ "$c" = PASS ] && [ "$d" = WROTE ] && [ "$e" = PASS ] &&
        cmp -s "$dir/le/DREAMKARTS.vmu" "$dir/be/DREAMKARTS.vmu"; then
        echo "saves: big-endian reads the x86 file, writes an identical one: PASS"
    else
        echo "saves: big-endian: FAIL (reads x86 file: $c, own write/read: $d/$e)"; fail=1
    fi
else
    echo "saves: no powerpc-linux-gnu-gcc or qemu-ppc: big-endian part skipped"
fi
[ $fail = 0 ] && echo "test_saves: PASS" || echo "test_saves: FAIL (log in $dir/log.txt)"
exit $fail
