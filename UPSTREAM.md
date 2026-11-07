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
| `src/prof.h` | `prof_mark()` reads `dku_perf_cycles()` (port/shim/arch/perf.h) under `DKU` instead of the SH-4 counter registers at `0xff100008`/`0xff100010` | A literal hardware address; cannot be shimmed |
| `src/birds.c` | `flight_pos`: `powf(fmaxf(sinf(a), 0.0f), 0.55f)` | At u = 1, IEEE `sinf(pi)` is about -8.7e-8 and `powf` of it is NaN (a gull drawn with NaN pitch as it lands). The SH-4's FSCA evidently gives >= 0. Worth offering upstream. |
| `src/render.c` | Under `DKU_NATIVE`: includes `render_native.h`; `strip()`, the whole-strip branch of `r_strip_bounded`, `mesh_draw()` and `r_mesh_kv()` send their vertices untransformed to `port/render_native.c` after `flush_hdr()` and return | PORT_PLAN.md principle 4: the GPU transforms, clips and culls. Hooks only; every decision before them (headers, spheres, viewport) stays upstream's |
