# Hardware testing

Results from the console, one section per tag (PORT_PLAN.md section 4).
Write what you did, what happened, and add a phone photo for anything
visual. Then tell Claude Code "results for mN are in TESTING.md".

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
