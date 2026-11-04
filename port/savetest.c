/* DKU: the save check (PORT_PLAN.md M4, VARIANT=savetest): upstream
   save.c through the shim's VMU files, across a restart.

   Each run reads the save. If it holds this test's settings (every field
   away from its default), they survived the restart: "DKU SAVETEST: PASS".
   If not, the run writes them, waits for the write to land and says
   "DKU SAVETEST: WROTE"; run it again to check. A save that comes back
   with some fields but not others is "DKU SAVETEST: FAIL". The test
   scripts start from an empty save folder: WROTE, then PASS. */
#include <stdio.h>
#include <string.h>

#include <kos/thread.h>

#include "plat.h"
#include "save.h"

/* kart.c's (not linked here: it brings the whole game). */
const KartCustom kart_custom_default = {0, 11, 6};

static const SaveData want = {
    .name = "DKU TEST 1",
    .music = false,
    .volume = 3,
    .custom_on = true,
    .custom = {5, 9, 2},
};

static bool same(const SaveData *a, const SaveData *b) {
    return !strcmp(a->name, b->name) && a->music == b->music && a->volume == b->volume &&
           a->custom_on == b->custom_on && a->custom.body == b->custom.body && a->custom.trim == b->custom.trim &&
           a->custom.chao == b->custom.chao;
}

static bool any_same_field(const SaveData *a, const SaveData *b) {
    return !strcmp(a->name, b->name) || a->volume == b->volume || a->custom_on == b->custom_on;
}

static void report(const char *verdict) {
    printf("DKU SAVETEST: %s (name \"%s\", music %s, volume %d, custom %s %d/%d/%d) in %s\n", verdict, save.name,
           save.music ? "on" : "off", save.volume, save.custom_on ? "on" : "off", save.custom.body, save.custom.trim,
           save.custom.chao, plat_save_dir());
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    plat_init();
    save_init();
    /* The read happens on save.c's worker; give it up to 5 s. */
    for(int i = 0; i < 500 && save_gen() < 1; i++) {
        save_tick();
        thd_sleep(10);
    }
    save_tick();
    if(save_gen() < 1) report("FAIL (no read landed)");
    else if(same(&save, &want)) report("PASS");
    else if(any_same_field(&save, &want)) report("FAIL (some settings came back, not all)");
    else {
        save = want;
        save_store();
        save_finish();
        report("WROTE");
    }
#ifdef __WIIU__
    /* Stay up for the HOME menu, as an app must. */
    while(!plat_quit_requested()) {
        plat_frame_done();
        thd_sleep(16);
    }
#endif
    return 0;
}
