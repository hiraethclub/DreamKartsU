# Hardware testing

Results from the console, one section per tag (PORT_PLAN.md section 4).
Write what you did, what happened, and add a phone photo for anything
visual. Then tell Claude Code "results for mN are in TESTING.md".

## Every test build: number and log

The number bottom left of the screen is the build. Quote it when reporting.
Each build writes its own log next to the game on the SD card:
`SD:/wiiu/apps/DreamKartsU/DreamKartsU-build-NNNN.log`. Each boot adds to it,
and it is written about once a second, so it survives a crash or a power-off
apart from the last second. To read it live instead, run
`/opt/devkitpro/tools/bin/udplogserver` on the PC.

In the log, `PERF` is one line a second of frame timing (average/worst in ms),
`HITCH` is a single frame over 20 ms and where its time went, and `DKU STATE:`
marks each screen change.

## m3: first boot on the Wii U

**Builds:** sent in the Claude Code conversation at tag `m3`, or in
`build/` on the PC after `make -f Makefile.wiiu`:

- `DreamKartsU.wuhb`: the game (`build/wiiu-gx2-release/`).
- `DreamKartsU-autotest-emerald.wuhb`: the computer drives one race of
  Emerald Beach by itself and reports the result
  (`build/wiiu-gx2-autotest-t0/`).

**Install:** copy a `.wuhb` to `SD:/wiiu/apps/DreamKartsU/` and start it
from the Wii U Menu (Aroma), or send it with wiiload.

**Optional log:** run `udplogserver` on the PC (from wut-tools,
`/opt/devkitpro/tools/bin/udplogserver`) before starting the game; the
console's log appears there, including the `DK AUTOTEST` lines.

What is in this build and what is not yet:

- In: the whole game drawing on the TV and the GamePad (4:3, black bars
  at the sides), GamePad buttons and sticks, saves on the SD card.
- Not yet (M4): sound and music, Pro Controller / Wii Remote, rumble,
  two-player with two controllers, chat keyboard.

### Things to check

1. Does `DreamKartsU.wuhb` start and reach the title screen (the
   DreamKarts logo over the start line, "PRESS START")?
2. Does the picture match the PC / Cemu screenshots? Look especially
   for: wrong colours (red and blue swapped, everything tinted), the
   world missing with only the HUD showing, flickering, black or stretched
   textures, the image upside down.
3. GamePad screen: same picture as the TV?
4. Controls on the GamePad: + to start, A / B in menus, left stick to
   steer, ZR (or R) accelerate, ZL (or L) brake, d-pad in menus.
5. Play a race. Does it feel smooth (60 fps), or does it stutter, and
   where (which track, how many karts on screen)?
6. HOME button: does the HOME menu open, and does the game carry on
   afterwards? Does "Close software" from the HOME menu exit cleanly?
7. Quit and start again: are settings and best times still there?
   (Save file: `SD:/wiiu/apps/DreamKartsU/DREAMKARTS.vmu`.)
8. Autotest build: does it finish the race by itself? With
   `udplogserver` running, does `DK AUTOTEST: PASS` appear?

### Results

(Write here.)

## m4: sound, music, controllers, rumble, saves

**Builds:** `build/wiiu-gx2-release/DreamKartsU.wuhb` (about 47 MB: the
music is inside it now) and, for the save check,
`build/wiiu-gx2-savetest/DreamKartsU.wuhb`. Install as for m3.

**Correction to the m3 sheet:** the triggers do not accelerate and brake.
As on the Dreamcast, **A accelerates, B brakes, X or a trigger drifts, Y
uses an item**, both triggers together look back.

### Things to check

1. Sound effects and music, on the TV and on the GamePad speaker. Does the
   title music start? Does a new song come in after a race starts? Any
   crackle, stutter, gaps or wrong pitch (too fast or slow, "chipmunk")?
   Settings > Music volume and the music switch.
2. Kart engine sounds rise and fall with speed; the plane over Emerald
   Beach (if you see it) pans.
3. GamePad rumble: hop, drift boosts, bumps, landing. Too weak, too
   strong, or does it buzz on after an event ends?
4. A **Pro Controller**: sticks, A/B/X/Y, ZL/ZR and L/R drift, rumble.
5. A **Wii Remote held sideways** (d-pad on the left): d-pad steers, 2
   accelerates, 1 brakes, B (underneath) drifts, A items, + pauses.
   With a **Nunchuk**: stick steers, A accelerates, B brakes, Z drifts,
   C items. A **Classic Controller**, if you have one.
6. Two players: Pro Controller or Wii Remote as player 2 with the
   GamePad as player 1 (split screen), and with the GamePad switched off
   (the first remote should become player 1).
7. Saves: change the music volume, the music switch and a custom kart,
   close the game from HOME, start it again: still set?
   Or run `savetest`: first start should log `DKU SAVETEST: WROTE`,
   the second `DKU SAVETEST: PASS` (udplogserver; on screen nothing shows).
   A PC save (`~/.local/share/hiraethclub/DreamKartsU/DREAMKARTS.vmu`) copied
   to `SD:/wiiu/apps/DreamKartsU/` should load too: the files are the same.

### Results

(Write here.)
