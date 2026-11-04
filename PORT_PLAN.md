# DreamKartsU: Wii U port plan

Port of **DreamKarts** by Rich Stokes (Dreamcast, KallistiOS) to the Wii U
(devkitPro wut, Aroma), with online play against real Dreamcasts on the
same lobby server.

- Upstream: https://github.com/richstokes/dreamcast-homebrew/tree/main/projects/games/dreamkarts (MIT)
- This repo: https://github.com/hiraethclub/DreamKartsU
- Dev machine: Debian 13, x86_64. Work lives in `~/WiiUDev/DreamKartsU`.

This file is the source of truth for Claude Code. Read it at the start of
every session, work through the milestones in order, and update the
**Progress log** at the bottom as you go.

---

## 1. What the original is (surveyed 3 Nov 2026)

About 32,000 lines of C99 (gnu17) in `src/`, plus:

| Part | What it is | Port impact |
| --- | --- | --- |
| `render.c` (959 lines), `xform.S`, `blobs.S` | PowerVR Tile Accelerator renderer: CPU transform with SH4ZAM, near-plane clipping, store-queue submission, SH-4 assembly hot paths | **Rewrite.** Replace with a GX2 backend implementing the same `render.h` API. `render.c` has C reference paths (`-DDK_XF_CHECK`) for every assembly routine: port from those, drop the assembly. |
| `render.h` | Clean renderer API: `r_header`, `r_strip`, `r_mesh`, `r_mesh_kv`, `r_billboard*`, `r_quad2d*`, `r_modvol`, matrix helpers | **Keep.** This is the seam the port hangs on. |
| `world*.c`, `fx.c`, `hud.c`, `items.c` etc. | Game and scene code, mostly calling `render.h`; some direct `pvr_*` calls (fog tables, background colour, `pvr_poly_hdr_t` handles, list switching) | Keep, satisfied by a KOS compatibility shim (`pvr_fog_*`, `pvr_set_bg_color`, `pvr_list_t`, opaque `pvr_poly_hdr_t`). |
| `kart.c`, `track.c`, `gp.c`, `roam.c` | Physics and race logic. No hardware calls. | Keep unchanged. |
| SH4ZAM (`third_party/sh4zam`, fetched by `../../../tools/sh4zam.mk`) | Maths library | Keep. It has a portable software backend (`SHZ_BACKEND SHZ_SW`) chosen automatically on non-SH4 targets. Anything using XMTRX state (`shz_xmtrx_*`) runs in C emulation: correct but check render code that relies on XMTRX being "loaded". |
| `audio.c` | AICA PCM effects (`snd_sfx_*`), CD-DA music streamed from the GD-ROM, a jukebox thread with a KOS mutex | **Rewrite** the backend: software mixer for 22050 Hz mono s16 effects plus streamed music files. |
| `input.c`, `rumble.c` | Maple controllers, Dreamcast keyboard for chat, Puru Puru pack | **Rewrite** for VPAD (GamePad), KPAD (Pro Controller, Classic Controller, Wii Remote), USB keyboard (`nsyskbd`) or the on-screen keyboard (`nn_swkbd`) for chat. Rumble on GamePad. |
| `save.c`, `vmuscreen.c` | VMU save file and VMU LCD graphics | Saves to SD. VMU screen can become a GamePad display later (optional). |
| `net.c`, `online.c`, `chat.c`, `spectate.c` | UDP client for the lobby server, BSD-style sockets on KOS | Mostly keep. wut provides BSD sockets (`wutsocket`). |
| `netlink.c`, `projects/common/dialup.c` | Bringing up the BBA or dial-up modem (PPP) | Replace with a stub: Wii U network is up already. `dialup.c` lives outside the game folder upstream and is not needed. |
| `assets/generated/*.h` | Models, props, metrics as C arrays | Keep. Endian-neutral. |
| `assets/generated/textures.pak` | Twiddled RGB565 / ARGB4444 textures with mips, little-endian 16-bit words, 3.4 MB | Convert. Generate a Wii U pack from `tools/build_textures.py` (untwiddled, byte order and GX2 tiling sorted out), or untwiddle and byte-swap at load. Prefer the build-time route. |
| `assets/generated/sfx.pak` | 16-bit mono little-endian PCM at 22050 Hz | Byte-swap at load or generate a big-endian pack. |
| Music | Nine Pixabay tracks (Alex Morgan), downloaded by `tools/build_music.py` from dcvmu.com and pressed as CD-DA | Convert to OGG Vorbis (or keep 22050/44100 PCM if simpler) and stream from the SD card or the .wuhb content folder. |
| `server/` | Go lobby server, bot client `dk-bot`, recorded demo races | **Gold for testing.** Run the lobby server locally on Debian and race against `dk-bot` with no Dreamcast needed. |
| `autotest.c` + `VARIANT=autotest` | CPU drives the player for a whole race and prints `DK AUTOTEST: PASS` / `FAIL` with frame timings | **Gold for unattended work.** Port it first and use it as the regression test at every milestone. |
| `perfhud.c`, `prof.c`, `film.c`, `flappy.c` | Profiler overlay, trailer filming, a minigame | Port perfhud/prof cheaply; `film` is optional. |

### 1.1 Why online cross-play is realistic

`server/PROTOCOL.md` says the design is **state sync, not lockstep**: each
console simulates its own kart and sends its state 12 times a second; remote
karts are interpolated. So the Wii U does **not** need bit-identical
physics with the SH-4. Every packet field is packed explicitly,
little-endian, byte by byte ("bytes, not structs"), and there are no raw
pointer casts on buffers in the C. A big-endian client can speak it as long
as the encode/decode helpers are used unchanged.

Constraints:

- The server checks `DK_NET_PROTO`, `DK_NET_BUILD` and `DK_NET_MIN_BUILD`
  (`src/net_version.h`, currently 4 / 32 / 30). The Wii U build must be the
  same game version as the Dreamcasts it plays: when upstream bumps these,
  the port must merge upstream before it can play online again.
- Emerald slots come from a seed derived from lobby, race id and track:
  integer code, so it matches across CPUs. Check that the derivation uses no
  floats.
- The live server (dcvmu.com) is Rich's. **Do not connect any test build to
  it.** All online testing uses a local lobby server until Aisling says
  otherwise.

### 1.2 Big-endian and PowerPC gotchas (check all of these)

1. **`char` is unsigned on PowerPC** and signed on SH-4. Build with
   `-fsigned-char` everywhere, or audit every `char` used in arithmetic.
2. `textures.pak` 16-bit words and `sfx.pak` samples are little-endian.
3. Save file format: write explicit little- or big-endian, never structs.
4. Any `union` or bitfield used to reinterpret bytes.
5. `uint64_t` timers: KOS `timer_ms_gettime64` becomes `OSGetSystemTime`
   converted with `OSTicksToMilliseconds`.
6. Unaligned access is fine on PPC, but keep the explicit packing anyway.
7. `-ffast-math` on SH-4 used FSCA/FSRRA approximations. PPC results will
   differ slightly. Fine for state sync; do not "fix" physics constants.

### 1.3 Renderer translation notes (PVR to GX2)

- **Lists.** PVR draws opaque (OP), punch-through (PT, alpha test) and
  translucent (TR) lists. Map to three passes in that order: opaque with
  depth write, punch-through with alpha test (`discard` below 0.5 or the
  PVR's cutoff), translucent with blending and the header's z-write flag.
- **Translucent sorting.** PVR can sort translucent polygons per pixel.
  Check whether `pvr_init` enables autosort; if it does, translucent
  submission order may not be back to front. Start by drawing in submission
  order and look for artefacts on foliage, smoke and water before adding
  any sorting.
- **Transforms.** The Dreamcast path does the whole transform on the CPU
  and submits screen-space vertices with 1/w. On Wii U, keep the CPU
  transform and near-plane clipping from the C reference code at first, but
  emit **clip-space** positions (x, y, z, w) so the GPU does perspective
  correct texturing. Moving transforms into the vertex shader is a later
  optimisation, only if profiling asks for it.
- **Header flags** (`HF_*` in `render.h`): additive blend, no fog, clamp,
  no z write, no z test, point filtering, z write for translucents,
  specular/offset colour. Each maps to GX2 blend, depth, sampler state and a
  shader variant or uniform.
- **Offset colour** (`HF_SPEC`): PVR adds a second per-vertex colour after
  texturing. Add a second colour attribute and add it in the pixel shader.
- **Fog.** `pvr_fog_table_custom` / `pvr_fog_table_linear` / `pvr_fog_far_depth`
  with a fog colour. Implement as a 128-entry fog table texture or uniform
  array indexed by depth, matching the PVR's lookup on 1/w, so the custom
  tables in `world.c` look the same.
- **Mip bias** (`PVR_MIPBIAS_*`): GX2 sampler LOD bias.
- **Modifier volumes** (`r_modvol`, 15 call sites): shadows that darken
  opaque and punch-through surfaces inside a closed volume to
  `DK_SHADOW_SCALE`. Implement as stencil shadow volumes (depth-fail or
  depth-pass) then a full-screen darken where stencil is set. Acceptable
  interim fallback: draw nothing, keep a TODO.
- **Background colour** (`pvr_set_bg_color`): clear colour.
- **Resolution.** The game draws 640x480 with `r_set_viewport` for split
  screen rows. Render to a 640x480 colour buffer first, then scale to the
  TV (1280x720, pillarboxed 4:3) and the GamePad (854x480). Widescreen is
  milestone 7.
- **Textures.** RGB565 maps to `GX2_SURFACE_FORMAT_UNORM_R5_G6_B5` and
  ARGB4444 to the 4444 format with a component swizzle; check wut's
  `gx2/enum.h` for exact names and confirm byte order on the console (Cemu
  is not a reliable judge of byte order bugs). Simplest correct start:
  convert to RGBA8 at load. Optimise later.
- **Shaders.** Write GLSL, compile offline with CafeGLSL's
  `glslcompiler.elf` (works on Linux) to `.gsh`, load with
  `WHBGfxLoadGFDShaderGroup`. Uniforms must live in uniform blocks and the
  code must call `GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK)`; Cemu
  forgives forgetting this and the real console does not.

---

## 2. Architecture of the port

**Two targets, one game (Aisling, 2026-11-03).** The Linux PC build is a
maintained, playable target alongside the Wii U, not just a test rig.
Every feature that is not Wii U hardware (GamePad screen, GX2) lands on
both. `tools/test_host.sh` passing is part of every later milestone's
check, and a playable PC build ships with each release.

```
DreamKartsU/
  LICENSE                  MIT, Rich's copyright line kept, Aisling's added
  PORT_PLAN.md             this file
  CLAUDE.md                short rules for Claude Code (see section 5)
  UPSTREAM.md              upstream commit SHA currently merged, how to update
  upstream/                pristine copy of the dreamkarts folder (git subtree)
  third_party/sh4zam/      pinned SH4ZAM
  port/
    shim/                  KOS compatibility headers and C: dc/pvr.h, dc/maple.h,
                           kos/mutex.h, timer, snd_sfx_*, vmu stubs, arch/*
    render_gx2.c           Wii U implementation of render.h
    render_gl.c            Linux host implementation of render.h (SDL2 + OpenGL 3.3)
    audio_mix.c            software mixer shared by both hosts
    plat_wiiu.c            ProcUI loop, WHBGfx, VPAD/KPAD, AX or SDL audio out, SD paths
    plat_host.c            SDL2 window, keyboard and gamepad, audio out, headless mode
    netlink_port.c         link always up
    shaders/               GLSL sources, compiled .gsh checked in
  tools/
    build_wiiu_assets.py   textures/sfx/music for the port (wraps upstream tools)
    test_host.sh           host build + autotest, exit code is the verdict
    test_be.sh             big-endian autotest under qemu-user, if possible
    test_online.sh         local lobby server + dk-bot + host client race
  Makefile / CMakeLists    targets: host, host-autotest, wiiu, wiiu-autotest, wuhb
```

Principles:

1. **Touch upstream source as little as possible.** Prefer shim headers
   and replacement files over edits. When an upstream edit is unavoidable,
   keep it tiny, mark it `/* DKU: reason */`, and list it in `UPSTREAM.md`
   so merges stay easy. Rich is still actively developing the game, and
   cross-play depends on staying in step with him.
2. **Two hosts, one game.** A Linux host build (SDL2 + OpenGL) exists so
   Claude Code can build, run and test without the Wii U. The Wii U build
   shares everything except `render_*`, `plat_*` and the audio output.
3. **Every milestone ends with a green automated check** plus a tagged
   commit Aisling can test on hardware.

---

## 3. Milestones

Each milestone lists its **done when** criteria. Do not start the next
milestone until they pass, apart from work that is blocked on hardware
testing (note that in the progress log and carry on).

### M0. Repo and toolchain (supervised, short)

- Switch `LICENSE` to MIT: keep `Copyright (c) 2026 Rich Stokes`, add
  `Copyright (c) 2026 Aisling (hiraethclub)` for the port. Keep
  `CREDITS.md` and its licence notices; add wut, CafeGLSL, SDL2 and any
  other new dependency.
- Import upstream with `git subtree` (or a plain copy plus SHA in
  `UPSTREAM.md`) into `upstream/`.
- Pin SH4ZAM into `third_party/` (upstream fetches it via a monorepo
  `tools/sh4zam.mk`; reproduce the pinned version that makefile names).
- Verify the toolchain: `$DEVKITPRO`, `$DEVKITPPC`, `wut`, `wut-tools`
  (`wuhbtool`, `elf2rpl`), `ppc-portlibs`/`wiiu-portlibs` as needed. Build
  one wut sample-style hello world to `.rpx` and `.wuhb`.
- Fetch CafeGLSL's `glslcompiler.elf` release and confirm it compiles a
  trivial vertex/pixel pair to `.gsh`.
- Check whether Debian 13 can provide a big-endian PowerPC cross compiler
  plus `qemu-user` (e.g. `gcc-powerpc-linux-gnu` and `qemu-user-static`).
  If yes, `test_be.sh` becomes possible. If not, record that and rely on
  Cemu and console testing for byte order.
- Write `CLAUDE.md` (section 5).

**Done when:** hello world `.wuhb` builds; `.gsh` compiles; upstream is
imported with its SHA recorded; licence and credits updated; committed and
tagged `m0`.

### M1. Platform shim and Linux host build, headless autotest

- Write the shim so the untouched upstream files compile on the host:
  maple, timers, mutexes and threads (or pthreads), `snd_*`, VMU file
  system, `pvr_*` entry points used outside `render.c`.
- Write `render_gl.c` implementing `render.h` with OpenGL 3.3 core:
  textures from the pack, the three lists, header flags, fog, offset colour.
  Modifier volumes may be stubbed in this milestone.
- `plat_host.c`: SDL2 window at 640x480, keyboard and SDL gamepad input,
  audio through the shared mixer (silent is fine in headless mode).
- Headless mode: offscreen framebuffer (EGL surfaceless, or SDL with a
  hidden window under `xvfb-run`), so tests can run without a display.
- Port the autotest variant. Add a screenshot dump (PNG) at fixed frames
  for each track, as `DK_CAPTURE` does upstream.

**Done when:** `tools/test_host.sh` builds the host autotest, runs a full
race on every one of the eight tracks and prints `DK AUTOTEST: PASS` for
each; screenshots are saved under `captures/host/`. Tag `m1`.

### M2. Renderer fidelity on host

- Untwiddle textures correctly, mips included (`tools/pvr_texture.py`
  documents the layout).
- Fog tables matching the PVR lookup; mip bias; punch-through cutoff.
- Offset colour, additive effects, sparks, flames, billboards, HUD, fonts,
  title screen, menus, split screen.
- Stencil shadow volumes for `r_modvol`.
- Compare against upstream screenshots in `docs/screenshots/` of the
  upstream repo where available. If Flycast can be built on Debian, an
  optional reference capture of the original game per track helps.

**Done when:** every track renders without obvious missing geometry,
wrong colours or z-fighting in the captured screenshots; a contact sheet
`captures/host/contact.png` is generated for Aisling to eyeball. Tag `m2`.

### M3. Wii U bring-up (first hardware checkpoint)

- `plat_wiiu.c`: ProcUI main loop (home button, foreground/background
  correctly), WHBGfx init, swap interval 1 (60 Hz; the game runs one fixed
  physics step per displayed frame, `DK_DT = 1/60`).
- `render_gx2.c`: port of `render_gl.c` to GX2 and `.gsh` shaders. Same
  vertex layout, same passes. Dynamic vertex buffers per frame with
  `GX2Invalidate` on CPU writes.
- Assets: pack textures and sfx for big-endian, embed them in the RPX (as
  upstream does with `.incbin`) or load from the .wuhb content folder.
- Logging over UDP (`WHBLogUdpInit`, receive with `udplogserver` from
  wut-tools) so test builds can report `DK AUTOTEST` lines over the network.
- Build `DreamKartsU.wuhb` with name, author, icon (`assets/icon.png`
  resized to 128x128) and a TV/DRC splash.

**Done when:** the Wii U build compiles clean with `-Wall -Wextra`, the
autotest variant boots to a finished race in Cemu (if it can be run
headless on this machine; if not, record why), and release build boots to
the title screen. Tag `m3`. **Stop here and tell Aisling it is ready to
test on hardware** (see section 4), then continue with M4 work that does
not depend on her results.

### M4. Input, audio, saves

- Input: GamePad (VPAD) as player 1 by default; Pro Controller, Classic
  Controller and Wii Remote held sideways via KPAD. Map to the `Pad` struct
  the game expects, including analogue triggers as thresholds like
  upstream. Two-player split screen with any two controllers.
- Rumble: GamePad motor for the `rumble.c` events.
- Audio: shared software mixer for effects, kart loops and pitch changes
  (`audio_sfx_pitch`, `audio_plane`); music streamed on a worker thread;
  fades and the jukebox behaviour from `audio.c`. ~~OGG with stb_vorbis~~:
  the MP3s themselves with dr_mp3 (public domain), since this machine has
  no Vorbis encoder or ffmpeg and playing the source files avoids a second
  lossy encode. ~~SDL2 audio~~: AX (`sndcore2`), since there is no
  wiiu-sdl2 here. (Changed at M4, see the log.)
- Music build step: reuse upstream `tools/build_music.py` download and
  checksum logic; copy the MP3s and measure their loudness instead of
  pressing CD-DA WAVs.
- Saves: same data `save.c` keeps, to
  `fs:/vol/external01/wiiu/apps/DreamKartsU/DREAMKARTS.vmu` (SD), explicit
  byte order, atomic write (temp file then rename). Upstream saves
  settings only (name, music, volume, custom kart), no race records.
- (Chat entry moved to M6, Aisling 2026-11-04: only online play uses it.)

**Done when:** host autotest still passes; Wii U build compiles; sound
plays in Cemu; settings ~~and records~~ survive a restart in Cemu (there
are no records to save). Tag `m4`.

### M5. Complete offline game

- All modes from upstream work: single race, grand prix, split screen,
  free roam, time trials, minigame, sound test, options.
- Wii U extras that do not change gameplay: choice of TV only, GamePad
  only (off-TV play), or mirrored; in two-player, optionally player 1 on
  TV and player 2 on GamePad full screen instead of split screen.
- Performance: perf HUD ported; target a locked 60 fps in four-kart
  split screen on Tsudoken Drift and Neon Skyway (the heaviest per the
  upstream notes).

**Done when:** host autotest passes on all tracks, big-endian autotest
passes if `test_be.sh` exists, Wii U build compiles. Tag `m5`. Second
hardware checkpoint.

### M6. Online, local server first

- Sockets via wut's BSD layer; `netlink_port.c` reports the link up at once;
  DNS for the server name via `getaddrinfo`.
- Server address configurable (options menu and a `server.txt` on SD).
  Default during development: the local machine's LAN address.
- `tools/test_online.sh`: build and run the Go lobby server from
  `upstream/server` locally (`go` from Debian's repositories), start one or
  more `dk-bot` clients, run the host client against it in autotest mode,
  pass when a full race completes with remote karts present and results
  received.
- Keep `net_version.h` exactly as upstream. Do not invent a platform field
  in the protocol; that is Rich's server and his decision.
- Chat entry (moved from M4): USB keyboard via `nsyskbd`, and the
  `nn_swkbd` software keyboard as the fallback; the PC build's keyboard
  feeds the same KOS-style queue.

**Done when:** `test_online.sh` passes on the host build; the Wii U build
connects to the local server in Cemu or on console. Tag `m6`.
**Cross-play against real Dreamcasts on dcvmu.com happens only after
Aisling has agreed it with Rich.**

### M7. Polish and release

- Widescreen option (16:9 projection and HUD layout) as a setting,
  4:3 stays default to match the original.
- GamePad second screen extras, e.g. track map or the VMU-style display.
- README with build instructions, controls, credits, licences, and a clear
  "unofficial port, original by Rich Stokes" line.
- GitHub Actions build using the `devkitpro/devkitppc` Docker image,
  producing the `.wuhb` as an artifact, plus a Linux PC build (and
  `tools/test_host.sh` with the null backend) on every push.
- PC release: settings for window size and fullscreen, key rebinding,
  and music and saves working the same as on the Wii U.
- Upstream sync procedure tested once: pull a newer upstream commit,
  re-run every test.

---

## 4. Hardware testing checkpoints (what Aisling does)

At tags `m3`, `m5`, `m6`, `m7`:

1. Copy `build/DreamKartsU.wuhb` to `SD:/wiiu/apps/DreamKartsU/` or send it
   with wiiload if the Aroma wiiload plugin is set up.
2. Optional: run `udplogserver` on the PC to watch the console's log.
3. Play, note problems in `TESTING.md` under the tag's heading: what you
   did, what happened, a phone photo if visual.
4. Tell Claude Code "results for m3 are in TESTING.md".

---

## 5. Rules for unattended work (copy into CLAUDE.md)

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

---

## 6. Risks

| Risk | Likelihood | Mitigation |
| --- | --- | --- |
| GX2 renderer bugs that only show on console (byte order, cache flushes, uniform block mode) | High | GL host renderer as reference; UDP logging; early hardware checkpoint at M3 |
| CafeGLSL is labelled experimental | Medium | Keep shaders simple (no geometry/compute); offline compile so failures show at build time |
| Translucency order differs from PVR autosort | Medium | Detect visually at M2; add per-list sorting by depth if needed |
| Upstream changes the protocol or build number | Certain, ongoing | Subtree layout and minimal edits so merges are routine |
| Cross-play acceptance | Depends on Rich | Ask him before touching his server |
| Music licence | Low | Pixabay tracks are bundled in a game the same way upstream does; keep the credits |

---

## Progress log

### Needs Aisling

- ~~Packages, git identity, GitHub push~~: done by Aisling 2026-11-03;
  pushes work (`gh`, HTTPS).
- ~~Music source (M4)~~: Aisling decided 2026-11-03 to use the same
  music as the original. Upstream `tools/build_music.py` may fetch the
  Pixabay tracks from `https://dcvmu.com/dreamkarts/music/` as a one-off
  asset download at M4. The rule still stands for game builds: no build
  ever connects to dcvmu.com.

- **Console questions (M4):** does GamePad rumble feel right (pattern
  re-sent every 20 frames, assumed to last at least that long)? Sound on
  the console: crackle or gaps? Sheet: `TESTING.md` m4.
- **M3 hardware test (2026-11-04):** the m3 builds are ready to try on
  the console. What to check and where to write results: `TESTING.md`.

### Log

- 2026-11-03: plan written from a survey of upstream at the main branch of
  richstokes/dreamcast-homebrew. No code yet.
- 2026-11-03: **M0.** Cloned the port repo to `~/WiiUDev/DreamKartsU`.
  - Upstream imported as plain copies (`git archive`, not `git subtree`:
    the folder is deep in a large monorepo, so a copy plus SHA is
    simpler) at `eaee474`: `upstream/` (dreamkarts) and
    `upstream_common/` (`projects/common`, because `src/` includes
    `../../../common/dialup.h` and `softreset.h` by relative path; M1's
    shim will have to satisfy those paths). `tools/update_upstream.sh`
    does three-way merges for later syncs (tested as a no-op).
  - SH4ZAM pinned at `ad2f890` (the commit upstream's
    `tools/update-sh4zam.sh` names) in `third_party/sh4zam/`.
  - Licence switched from GPL-3.0 to MIT with both copyright lines;
    root `CREDITS.md` points at upstream's and adds wut, CafeGLSL, SDL2.
  - Toolchain: devkitPPC r51 (GCC 16.2), wut 1.9.2, wut-tools 1.3.5
    (`wuhbtool`, `elf2rpl`, `udplogserver`, `wiiload`). `port/hello`
    builds to `.rpx` and `.wuhb` with `-Wall -Wextra` and no warnings.
  - CafeGLSL v0.2.0 `glslcompiler.elf` (fetched and hash-checked by
    `tools/get_cafeglsl.sh`) compiles a uniform-block vertex/pixel pair
    to a valid `Gfx2` `.gsh`.
  - Big-endian testing: Debian 13 has `gcc-powerpc-linux-gnu` 14.2 and
    `qemu-user` 10.0, so `test_be.sh` is possible once installed.
  - Host build deps are missing (SDL2, GL/EGL headers, xvfb, Go,
    Pillow/NumPy): see Needs Aisling. Cemu 2.6 AppImage found in
    `~/Applications` for M3.
  - Check: `tools/check_m0.sh` prints `M0 CHECK: PASS`.
- 2026-11-03: **M1 in progress (not done: its check needs the GL
  renderer and screenshots, which need SDL2/GL installed).**
  - Plan change: shim-first went further than the plan expected.
    `render.c`, `input.c`, `rumble.c` and `audio.c` stay **unmodified**:
    - `port/shim/pvr_ta.c` is a software Tile Accelerator. It decodes the
      `pvr_prim` headers and `pvr_dr_*` vertex words `render.c` sends into
      per-list triangle batches (`port/ta.h`) that a backend draws. A
      backend gets screen x, y and 1/w and rebuilds clip space, which keeps
      texturing perspective-correct.
    - `port/xform_c.c` replaces `xform.S` (seven routines) in C.
    - Maple is virtual: platform controllers appear as maple controllers,
      a rumble-capable pad gets a Puru Puru pack, VMU files are files in
      the save folder (atomic writes), and the keyboard feeds a KOS-style
      queue. The AICA channel packets and `snd_sfx_*` drive
      `port/shim/snd_shim.c`'s mixer. CD-DA reports "no disc" until M4.
    - Only `netlink.c` is replaced (`port/netlink_port.c`). It refuses
      dcvmu.com and upstream's fallback IP outright; host builds always set
      `DK_NET_TEST_HOST=127.0.0.1`.
    - The one upstream edit: `src/prof.h` (literal SH-4 counter
      addresses), in `UPSTREAM.md`.
  - `make VARIANT=autotest` builds a headless host binary (null backend,
    virtual 60 Hz clock, `port/plat_headless.c`).
    `BACKEND=null tools/test_host.sh` races all 8 tracks:
    **all 8 print `DK AUTOTEST: PASS`** (about 25 s in parallel).
  - `VARIANT=xfcheck` (upstream's `DK_XF_CHECK` run against
    `port/xform_c.c`) is clean on all 8 tracks. Two causes found:
    - A real NaN: `birds.c` `flight_pos` took `powf` of IEEE `sinf(pi)`,
      which is slightly negative. Fixed with a marked edit (`UPSTREAM.md`),
      worth offering to Rich.
    - Temple Hill's remaining count is upstream's own check: bystanders bob
      by moving their strip vertices past the bounding sphere built before
      the bob (`world_props.c` / `world_mesh.c`). It is harmless and
      happens on the Dreamcast too, so `tools/test_host.sh` lists it in
      `XF_KNOWN`.
- 2026-11-03: **M1 done.** Tag `m1`.
  - `port/plat_host.c`: SDL2 window, GL 3.3 core context, keyboard and SDL
    game controllers as maple pads, rumble, mixer through SDL audio.
    `DKU_VIRTUAL_CLOCK=1` gives the headless platform's steady 60 Hz clock.
  - `port/render_gl.c`: draws the TA frame into a 640x480 FBO: OP, PT, TR
    in order; header blend, depth compare and write; PT alpha test;
    texture envs; offset colour; table and vertex fog; userclip as scissor;
    de-twiddled textures with the KOS mip chain, re-uploaded on
    `pvr_txr_load`. Depth is 1/w (as z/(1+z)) in a float buffer, cleared
    to 0. Modifier volumes (shadows) are not drawn yet (M2).
    `DKU_SHOTS` saves PNGs (zlib) to `captures/host/t<track>-f<frame>.png`.
  - `tools/test_host.sh` (GL default now) runs each track under its own
    `xvfb-run` (Mesa llvmpipe) and ends a run once its verdict prints,
    since off the console the game keeps going after it.
  - Check: **all 8 tracks `DK AUTOTEST: PASS`** in about 3 min; 28
    screenshots in `captures/host/`. Text, fog, textures and HUD look right.
  - Known artefact, for M2: in many frames, near-plane-clipped ground and
    road triangles (vertices at w = 0.25 with screen x up to about
    +-64000) are not drawn, leaving background-coloured wedges in the
    lower screen. Likely llvmpipe's fixed-point setup overflowing on such
    large coordinates. The GX2 backend will have the same limits, so the
    fix belongs in the shim or backend: clip those triangles to a guard
    band in clip space before drawing. Not yet confirmed on softpipe or a
    real GPU.
  - Not run yet: `tools/test_be.sh` (big-endian under qemu-ppc); the
    packages are now installed. First job next session.
- 2026-11-03: **Big-endian autotest passes.** `tools/test_be.sh`
  (powerpc-linux-gnu-gcc 14.2, qemu-ppc 10.0, null backend) races
  Emerald Beach (4th, 70.07 s) and Tsudoken Drift (1st, 104.98 s) to
  `DK AUTOTEST: PASS` in about 2 min 20 s. The "Terminated" lines it
  prints are the script ending each run after its verdict.
- 2026-11-03: **M2 started.** The near-plane wedges are fixed, and the
  M1 guess about them was wrong.
  - `DKU_DUMP_FRAMES` (shim) writes a frame's TA batches to
    `captures/host/<tag>-fNNNNN.ta`, numbered like the screenshots. A
    dump of an affected frame showed the TA hands the backend the
    missing road triangles. With the depth test off, llvmpipe draws
    them: large coordinates were not the problem. The road (1/w 0.31)
    lost to the sea under it (1/w 0.27) because `gl_FragDepth` was
    written from a `noperspective` 1/w varying, which comes out wrong
    on triangles the GPU clips.
  - Fix: clip z is a constant `DEPTH_K` (1/128), so the rasteriser's
    window depth is `DEPTH_K / w`, linear in screen space just as the
    PVR's 1/w is. Depth clamp on, `glClipControl` zero-to-one (float
    precision far away), no `gl_FragDepth`. Fog reads 1/w back from
    `gl_FragCoord.z`. GX2 does the same through
    `GX2SetRasterizerClipControlEx` with `halfZ` (M3). 1/w goes up to
    60 (`flappy.c`), so the scale fits.
  - `tools/contact_sheet.py` builds `captures/host/contact.png`. No
    wedges in any of the 30 frames; GL autotest PASS on all 8 tracks.
  - Runs now repeat exactly. Two runs of the same track (GL or null,
    ASLR or not) used to differ by an ulp in the sea's UVs at frame 300
    and fully by 1800: the virtual clock added real time inside the
    frame, so mid-frame timer reads jittered. `DKU_REPRO=1` (set by
    `tools/test_host.sh`) makes in-frame time 1 us per call. Two GL
    test runs give byte-identical screenshots on all 8 tracks, so
    before/after comparisons of renderer changes are now exact.
  - **Shadows (modifier volumes) drawn** in `render_gl.c` as the PVR's
    cheap shadow, in stencil: OP/PT set bit 7 where the header takes
    modifiers; each volume flips bit 0 for its faces in front of the
    surface (parity), then folds it into bit 1 (union of volumes); a
    full-screen pass scales those pixels by `shadow_scale` (0.75)
    before TR. The karts now have the hard core inside their soft blob
    shadow that upstream's `models.c` describes. GL autotest PASS on
    all 8 tracks. Not yet checked: split screen (volumes are cut per
    view by `r_modvol`, so it should hold).
  - Menus and split screen: `DKU_INPUT` (host platform) presses
    scripted buttons at given frames. `tools/capture_menus.sh` drives
    the GL release build from the title screen through the main menu,
    race setup and two-player kart select into a split-screen race,
    saving `captures/host/menu-f*.png`; the contact sheet gives them
    their own rows. Title, menus, kart select, split-screen HUDs and
    shadows all render correctly.
  - No upstream reference screenshots in the imported folder (only
    `assets/icon.png`), so the comparison M2 suggests was not possible;
    Flycast reference captures skipped (optional).
  - Big-endian rerun: PASS. Same Emerald Beach lap times as x86 but a
    different place, and Tsudoken a little different: float divergence
    between CPUs (section 1.2 item 7), expected.
- 2026-11-03: **M2 done.** Tag `m2`. Checks: `tools/test_host.sh`
  (GL) PASS on all 8 tracks, `BACKEND=null` PASS, `tools/test_be.sh`
  PASS, `tools/capture_menus.sh` PASS, and `captures/host/contact.png`
  generated (8 tracks plus menus and split screen) with no missing
  geometry, wrong colours or z-fighting that I could see.
  **Aisling:** please eyeball `captures/host/contact.png` (regenerate
  with `tools/test_host.sh && tools/capture_menus.sh &&
  tools/contact_sheet.py`; captures are not in git). Next: M3, Wii U
  bring-up.
- 2026-11-04: Aisling looked over the M2 contact sheet: looks fine.
  She confirmed the aim is the Wii U port **and** online cross-play
  with real Dreamcasts (M6), not the Wii U alone.
- 2026-11-04: **M3 done.** Tag `m3`. **Ready for the first hardware
  test** (Needs Aisling, `TESTING.md`).
  - `Makefile.wiiu` (devkitPPC + wut 1.9.2) builds
    `build/wiiu-gx2-<variant>/DreamKartsU.wuhb`; source lists and flags
    shared with the host in `sources.mk`. The game logic compiled and
    linked for the Wii U first time: wut's newlib supplies pthreads,
    BSD sockets and `clock_gettime`.
  - `port/plat_wiiu.c`: WHBProc (HOME menu, background, quit from HOME
    ends `main`), system clock, GamePad as pad 0, saves to
    `SD:/wiiu/apps/DreamKartsU/`, stdout/stderr to the UDP log and
    OSReport line by line.
  - `port/render_gx2.c`: a port of `render_gl.c` with the same passes and
    header mapping, 1/w depth through halfZ clip control, PVR textures
    to RGBA8 (little-endian reads) via linear staging surfaces and
    `GX2CopySurface`, colour attributes fetched 8-in-32 so the shaders
    match the host's, uniform blocks byte-swapped, stencil shadows, and
    the 640x480 frame blitted 4:3 to TV and GamePad. Shaders in
    `port/shaders/` (GLSL 450, CafeGLSL; `.gsh` checked in).
  - Found in Cemu: `GX2ClearDepthStencilEx` with depth 0 left the buffer
    above every depth the game draws, so only the HUD (depth test off)
    showed. Proved by flipping every compare (the world then drew,
    farthest first). Depth and stencil are now cleared by a full-screen
    quad, which does not depend on how an implementation treats the
    clear value. **Console question:** does the real GPU honour it?
    Either way the quad is correct.
  - `.wuhb`: icon and TV/GamePad splashes made from upstream's
    `assets/icon.png` by `tools/make_wuhb_art.py`.
  - `tools/cemu_run.sh` runs a `.wuhb` in Cemu 2.6 (AppImage) on a
    private Xvfb with a private copy of Aisling's settings (OpenGL,
    OSReport logging = log flag bit 17, found by bisecting), so her own
    Cemu setup is untouched. `tools/test_wiiu.sh` is the M3 check: no
    compiler warnings in any Wii U build, each track's autotest races to
    `DK AUTOTEST: PASS` in Cemu, and the release build boots to the
    title. **All 8 tracks PASS; release boots to the title** (about 5
    minutes, screenshots in `captures/wiiu/`). Cemu's fps here (min
    ~38) is the software rasteriser, not the console.
  - Fixed on all platforms: `main` reads the clock before `plat_init`,
    so the boot time underflowed; clocks now start at their first read.
  - Host checks rerun: GL and null autotests, big-endian, menu capture
    all PASS.
  - Not in M3 (M4): sound (no AX yet), Pro Controller / Wii Remote,
    rumble, keyboard.
- 2026-11-04: Plan change (Aisling): chat entry moves from M4 to M6.
  Only online play uses it, and M4 without it gets the console build to
  a state worth testing sooner (sound, music, controllers, rumble).
- 2026-11-04: **M4 done.** Tag `m4`.
  - Music: `tools/build_music.py` fetches the songs with upstream's own
    manifest/sha256 code (one-off download, allowed above) into `music/`
    (not in git) and measures loudness (`tools/music_level.c`) into
    `port/music_levels.h`. `cdda_shim.c` plays `music/NN.mp3` as the
    disc's tracks (dr_mp3), levelled and limited. Plan changed: MP3s
    instead of OGG (no encoder here), AX instead of SDL2 (no wiiu-sdl2).
  - Wii U: `audio_ax.c` (AX, two voices), KPAD controllers on ports 1-3,
    GamePad and remote rumble, music in the `.wuhb` content folder.
    wut's `nanosleep` in extra threads crashed Cemu; Wii U sleeps use
    `OSSleepTicks`.
  - Saves: the shim stores save.c's checksum little-endian, so PC and
    Wii U save files are identical. `tools/test_saves.sh` PASS (x86 and
    big-endian). Wii U save test in Cemu: WROTE then PASS, file identical
    to the PC's.
  - Checks: host null and GL autotests PASS on all 8 tracks with the new
    sound check (effects and music audible); `tools/test_be.sh` PASS;
    `tools/capture_menus.sh` PASS; `tools/test_saves.sh` PASS;
    `tools/test_wiiu.sh` PASS: no warnings, all 8 tracks race to PASS in
    Cemu with AX playing the mix and music audible, settings survive a
    Cemu restart, release boots to the title.
  - Not tested anywhere but the console: Pro / Classic / Wii Remote input
    and rumble (Cemu here has no such controllers set up). Sheet:
    `TESTING.md` m4.

