#!/bin/sh
# The test build number (PORT_PLAN.md, M4 console testing): prints it, adding
# one first if the sources differ from the last Wii U build's. Same sources,
# same number, so test variants and rebuilds do not inflate it. BUILD_NUMBER
# (checked in) holds the number and the hash of the sources it was given to.
cd "$(dirname "$0")/.." || exit 1
hash=$(find port upstream/src upstream_common Makefile Makefile.wiiu sources.mk -type f \
    ! -name '*.log' ! -name '*.o' ! -name '*.d' -print0 |
    LC_ALL=C sort -z | xargs -0 sha1sum | sha1sum | cut -c1-40)
n=0 old=
[ -f BUILD_NUMBER ] && read -r n old <BUILD_NUMBER
if [ "$hash" != "$old" ]; then
    n=$((n + 1))
    printf '%s %s\n' "$n" "$hash" >BUILD_NUMBER.tmp.$$ && mv BUILD_NUMBER.tmp.$$ BUILD_NUMBER
fi
echo "$n"
