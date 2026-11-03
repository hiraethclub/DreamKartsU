#!/bin/sh
# Three-way merge a newer upstream DreamKarts into upstream/ and
# upstream_common/, keeping the port's /* DKU: */ edits.
#
# Usage: tools/update_upstream.sh <new-commit-or-branch>
# Base is the commit recorded in UPSTREAM.md. Conflicted files keep markers
# and are listed at the end; fix them, update UPSTREAM.md, rerun the tests.
set -eu
cd "$(dirname "$0")/.."
NEW="${1:?usage: $0 <new-commit-or-branch>}"
URL=https://github.com/richstokes/dreamcast-homebrew
BASE=$(grep -m1 -oE '`[0-9a-f]{40}`' UPSTREAM.md | head -1 | tr -d '`')
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git clone -q --filter=blob:none --no-checkout "$URL" "$work/dc"
git -C "$work/dc" fetch -q origin "$BASE"
git -C "$work/dc" fetch -q origin "$NEW"
NEWSHA=$(git -C "$work/dc" rev-parse FETCH_HEAD)
echo "upstream: $BASE -> $NEWSHA"

: >"$work/empty"
merge_dir() { # <monorepo path> <local dir>
    mkdir -p "$work/base/$2" "$work/new/$2"
    git -C "$work/dc" archive "$BASE:$1" | tar -x -C "$work/base/$2"
    git -C "$work/dc" archive "$NEWSHA:$1" | tar -x -C "$work/new/$2"
    # Files in the new tree: add, update or merge.
    (cd "$work/new/$2" && find . -type f) | while read -r f; do
        b="$work/base/$2/$f"; n="$work/new/$2/$f"; l="$2/$f"
        if [ ! -e "$l" ]; then mkdir -p "$(dirname "$l")"; cp "$n" "$l"
        elif [ -e "$b" ] && cmp -s "$b" "$n"; then :            # unchanged upstream
        elif [ -e "$b" ] && cmp -s "$b" "$l"; then cp "$n" "$l"  # untouched locally
        else
            [ -e "$b" ] || b="$work/empty"
            git merge-file -q "$l" "$b" "$n" 2>/dev/null && continue
            echo "$l" >>"$work/conflicts"
        fi
    done
    # Files upstream deleted.
    (cd "$work/base/$2" && find . -type f) | while read -r f; do
        [ -e "$work/new/$2/$f" ] || { echo "upstream removed $2/$f"; rm -f "$2/$f"; }
    done
}
merge_dir projects/games/dreamkarts upstream
merge_dir projects/common upstream_common
git -C "$work/dc" show "$NEWSHA:tools/update-sh4zam.sh" | grep -m1 'SH4ZAM_COMMIT="'

if [ -s "$work/conflicts" ]; then
    echo "CONFLICTS:"; cat "$work/conflicts"; exit 1
fi
echo "merged cleanly; now record $NEWSHA in UPSTREAM.md and run the tests"
