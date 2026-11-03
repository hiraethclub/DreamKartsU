# Upstream

| What | Source | Commit |
| --- | --- | --- |
| `upstream/` | https://github.com/richstokes/dreamcast-homebrew `projects/games/dreamkarts` | `eaee47472b95e1d8117a957be2ccc5b035de3aeb` (3 Nov 2026, last change to the folder `5649ca3938959fd3c65304ea2a0e42f759547ee1`) |
| `upstream_common/` | same repository, `projects/common` | `eaee47472b95e1d8117a957be2ccc5b035de3aeb` |
| `third_party/sh4zam/` | https://github.com/gyrovorbis/sh4zam | `ad2f8909e8dc8f30489e6b74cd07536321f13d89` (v0.10.0 + AGENTS.md, the pin in upstream `tools/update-sh4zam.sh`) |

Imported as plain copies (`git archive` of each folder), not `git subtree`:
the dreamkarts folder lives deep in a large monorepo, and a copy plus the
SHA above is enough for a three-way merge.

## Updating

```bash
tools/update_upstream.sh <new-commit-or-branch>
```

The script exports the old pristine tree (the commit above) and the new one,
then three-way merges every file into `upstream/` and `upstream_common/` with
`git merge-file`, so the `/* DKU: */` edits below survive. Conflicts are left
with markers and listed. Then: update the commit in this file, check
`src/net_version.h` (the port must match the build the Dreamcasts run),
check the SH4ZAM pin in upstream `tools/update-sh4zam.sh`, and re-run every
test script.

## Edits to upstream files

Every edit is marked `/* DKU: reason */` in the source.

| File | Edit | Why |
| --- | --- | --- |
| (none yet) | | |
