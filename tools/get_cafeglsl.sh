#!/bin/sh
# Fetch CafeGLSL's offline shader compiler into tools/bin/ and check its hash.
set -eu
VER=v0.2.0
SHA256=067402bde2fbda66fcde65534ae8ffbf99ef46da5ed67af8294cd7b385d84acd
DIR="$(dirname "$0")/bin"
OUT="$DIR/glslcompiler.elf"
mkdir -p "$DIR"
if [ ! -f "$OUT" ] || ! echo "$SHA256  $OUT" | sha256sum -c --quiet - 2>/dev/null; then
    curl -sSL -o "$OUT.tmp" "https://github.com/Exzap/CafeGLSL/releases/download/$VER/glslcompiler.elf"
    echo "$SHA256  $OUT.tmp" | sha256sum -c --quiet -
    mv "$OUT.tmp" "$OUT"
fi
chmod +x "$OUT"
echo "CafeGLSL $VER: $OUT"
