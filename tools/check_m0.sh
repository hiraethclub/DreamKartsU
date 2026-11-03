#!/bin/sh
# M0 check: the wut hello world builds to .rpx and .wuhb with no warnings,
# and CafeGLSL compiles the trivial shader pair to .gsh.
set -eu
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?DEVKITPRO is not set}"
make -C port/hello clean >/dev/null
log=$(make -C port/hello 2>&1)
if echo "$log" | grep -qiE 'warning|error'; then
    echo "$log"; echo "M0 CHECK: FAIL (hello world warnings)"; exit 1
fi
test -s port/hello/hello.rpx
test -s port/hello/hello.wuhb
tools/get_cafeglsl.sh >/dev/null
out=build/m0
mkdir -p "$out"
tools/bin/glslcompiler.elf -vs port/shaders/test/trivial.vert \
    -ps port/shaders/test/trivial.frag -o "$out/trivial.gsh" >"$out/glsl.log" 2>&1
# A GFD file starts with the magic "Gfx2".
test "$(head -c 4 "$out/trivial.gsh")" = "Gfx2"
test -s UPSTREAM.md && grep -q eaee47472b95e1d8117a957be2ccc5b035de3aeb UPSTREAM.md
test -f upstream/src/render.h && test -f third_party/sh4zam/include/sh4zam/shz_sh4zam.h
echo "M0 CHECK: PASS"
