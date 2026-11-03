# DreamKartsU: rules for Claude Code

Wii U (and Linux host) port of DreamKarts by Rich Stokes. The plan, the
milestones and the progress log are in `PORT_PLAN.md`.

## Rules for unattended work

- Read `PORT_PLAN.md` and the progress log first, every session.
- Work milestone by milestone. Commit small and often with clear
  messages. Tag at each milestone's end.
- Never push to any remote other than `origin` (hiraethclub/DreamKartsU),
  never force-push `main`.
- Never connect any build to dcvmu.com or any server other than one
  started locally by the test scripts.
- Never run `sudo`. If a package is missing, write the exact install
  command in the progress log under **Needs Aisling** and continue with
  something else.
- Upstream code: shim first, edit last, mark every edit `/* DKU: */` and
  list it in `UPSTREAM.md`.
- A milestone is not done until its automated check passes. Do not mark
  work done on the basis of "it compiles" where the plan asks for a test.
- When blocked by something only the console can answer, write the
  question in **Needs Aisling**, choose the most reasonable assumption,
  note it, and carry on.
- Keep `PORT_PLAN.md` honest: if a plan item turns out wrong, change the
  plan and say why in the log.

## Layout notes

- `upstream/` is the `projects/games/dreamkarts` folder and
  `upstream_common/` is `projects/common`, both from
  richstokes/dreamcast-homebrew at the SHA in `UPSTREAM.md`.
- `third_party/sh4zam/` is SH4ZAM at the commit upstream's
  `tools/update-sh4zam.sh` pins.
- Toolchain: `$DEVKITPRO=/opt/devkitpro` (wut, wut-tools). CafeGLSL's
  `glslcompiler.elf` is fetched to `tools/bin/` by `tools/get_cafeglsl.sh`.
