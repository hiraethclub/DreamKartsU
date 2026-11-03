/* DKU: what the shared code needs from a platform (plat_headless.c,
   plat_host.c, plat_wiiu.c). */
#ifndef DKU_PLAT_H
#define DKU_PLAT_H

#include <stdbool.h>
#include <stdint.h>
#include <dc/maple/controller.h>

#define PLAT_PADS 4

/* Monotonic microseconds. */
uint64_t plat_time_us(void);
/* The window was closed or the system asked the game to exit. */
bool plat_quit_requested(void);
/* Folder for save files, with a trailing slash; created if missing. */
const char *plat_save_dir(void);
/* Controller i as the maple bus would report it; false if absent. */
bool plat_pad(int i, cont_state_t *out);
/* Controller i has a rumble motor (it gets a Puru Puru pack). */
bool plat_pad_has_rumble(int i);
/* Motor power 0 (off) to 7 on controller i. */
void plat_rumble(int i, int power, int freq);
/* A keyboard is attached; keys typed since the last call, KOS-encoded
   (key | mods << 8), into buf; returns how many. */
bool plat_keyboard(void);
int plat_keys(int *buf, int max);
/* Called once from the first pvr_init. */
void plat_init(void);
/* A frame was shown: pump events; the headless platform's clock steps a
   display period here. */
void plat_frame_done(void);

#endif
