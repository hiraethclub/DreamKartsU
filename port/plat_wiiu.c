/* DKU: the Wii U platform (wut).

   - ProcUI through WHBProc: plat_frame_done asks it each frame, so the HOME
     menu and going to the background work, and quitting from HOME ends the
     game loop (plat_quit_requested, upstream main.c's softreset_poll).
   - Time is the system clock; the display paces frames (render_gx2.c).
   - Controllers: the GamePad and up to three Wii Remote channels (Pro,
     Classic, Nunchuk or a sideways remote), with rumble (below).
   - Sound: audio_ax.c. Music files come from the .wuhb's content folder.
   - Saves live on the SD card in SAVE_DIR.
   - stdout and stderr go to the log: UDP (udplogserver from wut-tools on
     the PC) and OSReport (Cemu's log), a line at a time, so the autotest's
     "DK AUTOTEST" lines reach the PC. */
#include <coreinit/time.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#include "plat.h"

void audio_ax_start(void);
void audio_ax_stop(void);

#define SAVE_DIR "fs:/vol/external01/wiiu/apps/DreamKartsU/"

static uint64_t t0;
static bool running = true;

/* ---- stdout and stderr to the log ---- */

static char line[512];
static size_t line_len;

/* stdout and stderr share the line, and threads print too (the
   jukebox, the music decoder). */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t log_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
    (void)r;
    (void)fd;
    pthread_mutex_lock(&log_lock);
    for(size_t i = 0; i < len; i++) {
        line[line_len++] = ptr[i];
        if(ptr[i] == '\n' || line_len == sizeof(line) - 1) {
            line[line_len] = 0;
            WHBLogWrite(line);
            line_len = 0;
        }
    }
    pthread_mutex_unlock(&log_lock);
    return (ssize_t)len;
}

static const devoptab_t log_dev = {.name = "dkulog", .write_r = log_write};

/* ---- platform API ---- */

/* From the first call: main reads the clock before plat_init. */
uint64_t plat_time_us(void) {
    uint64_t now = (uint64_t)OSTicksToMicroseconds(OSGetSystemTime());
    if(!t0) t0 = now;
    return now - t0;
}

bool plat_quit_requested(void) { return !running; }

static void make_dirs(const char *path) {
    char p[256];
    snprintf(p, sizeof(p), "%s", path);
    for(char *s = strchr(p + strlen("fs:/vol/external01/"), '/'); s; s = strchr(s + 1, '/')) {
        *s = 0;
        if(mkdir(p, 0755) != 0 && errno != EEXIST) fprintf(stderr, "DKU: cannot make %s\n", p);
        *s = '/';
    }
}

const char *plat_save_dir(void) {
    static bool made;
    if(!made) {
        made = true;
        make_dirs(SAVE_DIR);
    }
    return SAVE_DIR;
}

const char *plat_music_dir(void) { return "fs:/vol/content/music/"; }

/* ---- Controllers ----

   Maple port 0 is the GamePad; ports 1-3 are Wii Remote channels 0-2
   (KPAD): Pro Controller, Classic Controller, a Wii Remote with a
   Nunchuk, or a Wii Remote alone held sideways. The game takes players
   from the controllers present in port order, so with the GamePad off
   the first remote is player 1. Buttons as the Dreamcast pad's positions:
   A accelerates, B brakes, X or a trigger drifts, Y uses an item. */

#define WPADS (PLAT_PADS - 1)

/* Stick -1..1 to the maple range, y down as on the Dreamcast. */
static int stick(float v) {
    int s = (int)(v * 128.0f);
    return s < -128 ? -128 : s > 127 ? 127 : s;
}

static int trig(float v) {
    int t = (int)(v * 255.0f);
    return t < 0 ? 0 : t > 255 ? 255 : t;
}

typedef struct {
    uint32_t from, to;
} Map;

static uint32_t map_buttons(uint32_t hold, const Map *m, size_t n) {
    uint32_t out = 0;
    for(size_t k = 0; k < n; k++)
        if(hold & m[k].from) out |= m[k].to;
    return out;
}
#define MAP(hold, table) map_buttons(hold, table, sizeof(table) / sizeof(table[0]))

static bool gamepad(cont_state_t *out) {
    static cont_state_t last;
    static bool present = true;
    VPADStatus vs;
    VPADReadError err;
    if(VPADRead(VPAD_CHAN_0, &vs, 1, &err) != 1 || err != VPAD_READ_SUCCESS) {
        if(err == VPAD_READ_INVALID_CONTROLLER) present = false;
        *out = last;   /* no new sample this frame: keep the last one */
        return present;
    }
    present = true;
    static const Map map[] = {
        {VPAD_BUTTON_A, CONT_A},          {VPAD_BUTTON_B, CONT_B},
        {VPAD_BUTTON_X, CONT_X},          {VPAD_BUTTON_Y, CONT_Y},
        {VPAD_BUTTON_PLUS, CONT_START},   {VPAD_BUTTON_UP, CONT_DPAD_UP},
        {VPAD_BUTTON_DOWN, CONT_DPAD_DOWN}, {VPAD_BUTTON_LEFT, CONT_DPAD_LEFT},
        {VPAD_BUTTON_RIGHT, CONT_DPAD_RIGHT},
    };
    out->buttons = MAP(vs.hold, map);
    out->joyx = stick(vs.leftStick.x);
    out->joyy = stick(-vs.leftStick.y);
    out->joy2x = stick(vs.rightStick.x);
    out->joy2y = stick(-vs.rightStick.y);
    /* Digital ZL / ZR and L / R as full triggers. */
    out->ltrig = (vs.hold & (VPAD_BUTTON_ZL | VPAD_BUTTON_L)) ? 255 : 0;
    out->rtrig = (vs.hold & (VPAD_BUTTON_ZR | VPAD_BUTTON_R)) ? 255 : 0;
    last = *out;
    return true;
}

static bool remote(int chan, cont_state_t *out) {
    static cont_state_t last[WPADS];
    static bool present[WPADS];
    KPADStatus st;
    KPADError err = KPAD_ERROR_OK;
    if(KPADReadEx((KPADChan)chan, &st, 1, &err) != 1 || err != KPAD_ERROR_OK) {
        if(err == KPAD_ERROR_INVALID_CONTROLLER || err == KPAD_ERROR_UNINITIALIZED) present[chan] = false;
        *out = last[chan];
        return present[chan];
    }
    present[chan] = true;
    switch(st.extensionType) {
        case WPAD_EXT_PRO_CONTROLLER: {
            static const Map map[] = {
                {WPAD_PRO_BUTTON_A, CONT_A},          {WPAD_PRO_BUTTON_B, CONT_B},
                {WPAD_PRO_BUTTON_X, CONT_X},          {WPAD_PRO_BUTTON_Y, CONT_Y},
                {WPAD_PRO_BUTTON_PLUS, CONT_START},   {WPAD_PRO_BUTTON_UP, CONT_DPAD_UP},
                {WPAD_PRO_BUTTON_DOWN, CONT_DPAD_DOWN}, {WPAD_PRO_BUTTON_LEFT, CONT_DPAD_LEFT},
                {WPAD_PRO_BUTTON_RIGHT, CONT_DPAD_RIGHT},
            };
            out->buttons = MAP(st.pro.hold, map);
            out->joyx = stick(st.pro.leftStick.x);
            out->joyy = stick(-st.pro.leftStick.y);
            out->joy2x = stick(st.pro.rightStick.x);
            out->joy2y = stick(-st.pro.rightStick.y);
            out->ltrig = (st.pro.hold & (WPAD_PRO_TRIGGER_ZL | WPAD_PRO_TRIGGER_L)) ? 255 : 0;
            out->rtrig = (st.pro.hold & (WPAD_PRO_TRIGGER_ZR | WPAD_PRO_TRIGGER_R)) ? 255 : 0;
            break;
        }
        case WPAD_EXT_CLASSIC:
        case WPAD_EXT_MPLUS_CLASSIC: {
            static const Map map[] = {
                {WPAD_CLASSIC_BUTTON_A, CONT_A},          {WPAD_CLASSIC_BUTTON_B, CONT_B},
                {WPAD_CLASSIC_BUTTON_X, CONT_X},          {WPAD_CLASSIC_BUTTON_Y, CONT_Y},
                {WPAD_CLASSIC_BUTTON_PLUS, CONT_START},   {WPAD_CLASSIC_BUTTON_UP, CONT_DPAD_UP},
                {WPAD_CLASSIC_BUTTON_DOWN, CONT_DPAD_DOWN}, {WPAD_CLASSIC_BUTTON_LEFT, CONT_DPAD_LEFT},
                {WPAD_CLASSIC_BUTTON_RIGHT, CONT_DPAD_RIGHT},
            };
            out->buttons = MAP(st.classic.hold, map);
            out->joyx = stick(st.classic.leftStick.x);
            out->joyy = stick(-st.classic.leftStick.y);
            out->joy2x = stick(st.classic.rightStick.x);
            out->joy2y = stick(-st.classic.rightStick.y);
            /* The original Classic Controller's L and R are analogue (the Pro
               model reads 0 there): input.c takes its own threshold, as on the
               Dreamcast. ZL and ZR are full presses. */
            out->ltrig = trig(st.classic.leftTrigger);
            out->rtrig = trig(st.classic.rightTrigger);
            if(st.classic.hold & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_L)) out->ltrig = 255;
            if(st.classic.hold & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_R)) out->rtrig = 255;
            break;
        }
        case WPAD_EXT_NUNCHUK:
        case WPAD_EXT_MPLUS_NUNCHUK: {
            /* Upright: A accelerates, B (underneath) brakes, Z drifts, C items. */
            static const Map map[] = {
                {WPAD_BUTTON_A, CONT_A},          {WPAD_BUTTON_B, CONT_B},
                {WPAD_BUTTON_1, CONT_X},          {WPAD_BUTTON_2, CONT_Y},
                {WPAD_BUTTON_PLUS, CONT_START},   {WPAD_BUTTON_UP, CONT_DPAD_UP},
                {WPAD_BUTTON_DOWN, CONT_DPAD_DOWN}, {WPAD_BUTTON_LEFT, CONT_DPAD_LEFT},
                {WPAD_BUTTON_RIGHT, CONT_DPAD_RIGHT},
            };
            out->buttons = MAP(st.hold, map);
            if(st.nunchuk.hold & WPAD_BUTTON_C) out->buttons |= CONT_Y;
            out->joyx = stick(st.nunchuk.stick.x);
            out->joyy = stick(-st.nunchuk.stick.y);
            out->joy2x = out->joy2y = 0;
            out->ltrig = 0;
            out->rtrig = (st.nunchuk.hold & WPAD_BUTTON_Z) ? 255 : 0;
            break;
        }
        default: {
            /* Sideways, d-pad on the left: the d-pad turns a quarter, and it
               steers as a full stick. 2 accelerates, 1 brakes, B (underneath)
               drifts, A items. */
            static const Map map[] = {
                {WPAD_BUTTON_2, CONT_A},          {WPAD_BUTTON_1, CONT_B},
                {WPAD_BUTTON_B, CONT_X},          {WPAD_BUTTON_A, CONT_Y},
                {WPAD_BUTTON_PLUS, CONT_START},   {WPAD_BUTTON_RIGHT, CONT_DPAD_UP},
                {WPAD_BUTTON_LEFT, CONT_DPAD_DOWN}, {WPAD_BUTTON_UP, CONT_DPAD_LEFT},
                {WPAD_BUTTON_DOWN, CONT_DPAD_RIGHT},
            };
            out->buttons = MAP(st.hold, map);
            out->joyx = (out->buttons & CONT_DPAD_LEFT) ? -128 : (out->buttons & CONT_DPAD_RIGHT) ? 127 : 0;
            out->joyy = 0;
            out->joy2x = out->joy2y = 0;
            out->ltrig = out->rtrig = 0;
            break;
        }
    }
    last[chan] = *out;
    return true;
}

/* ---- Rumble ----

   The maple level 0..7 as a duty cycle. The GamePad plays a bit pattern
   of up to 120 bits once (VPADControlMotor), so a level is re-sent every
   REFRESH frames while it lasts; remotes and the Pro Controller have an
   on/off motor, on from level 2 up. */

#define REFRESH 20

static int rumble_pow[PLAT_PADS], rumble_age[PLAT_PADS];

static void gamepad_motor(int power) {
    if(power <= 0) {
        VPADStopMotor(VPAD_CHAN_0);
        return;
    }
    uint8_t pattern[15] = {0};
    for(int bit = 0, acc = 0; bit < 120; bit++) {
        acc += power;
        if(acc >= 7) {
            acc -= 7;
            pattern[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
        }
    }
    VPADControlMotor(VPAD_CHAN_0, pattern, 120);
}

static void motor(int i, int power) {
    if(i == 0) gamepad_motor(power);
    else WPADControlMotor((WPADChan)(i - 1), power >= 2);
}

bool plat_pad(int i, cont_state_t *out) {
    memset(out, 0, sizeof(*out));
    if(i < 0 || i >= PLAT_PADS) return false;
    bool ok = i == 0 ? gamepad(out) : remote(i - 1, out);
    if(ok && rumble_pow[i] > 0 && ++rumble_age[i] >= REFRESH) {
        rumble_age[i] = 0;
        if(i == 0) gamepad_motor(rumble_pow[i]);
    }
    return ok;
}

bool plat_pad_has_rumble(int i) { return i >= 0 && i < PLAT_PADS; }

void plat_rumble(int i, int power, int freq) {
    (void)freq;
    if(i < 0 || i >= PLAT_PADS || power == rumble_pow[i]) return;
    rumble_pow[i] = power;
    rumble_age[i] = 0;
    motor(i, power);
}

bool plat_keyboard(void) { return false; }

int plat_keys(int *buf, int max) { return 0; }

void plat_init(void) {
    static bool done;
    if(done) return;
    done = true;
    WHBProcInit();
    WHBLogUdpInit();
    WHBLogCafeInit();
    devoptab_list[STD_OUT] = &log_dev;
    devoptab_list[STD_ERR] = &log_dev;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    KPADInit();
    WPADEnableURCC(TRUE);
    WPADEnableWiiRemote(TRUE);
    audio_ax_start();
}

void plat_frame_done(void) {
    if(running) running = WHBProcIsRunning();
}

/* After the backend's own shutdown (render_gx2.c backend_shutdown). */
void plat_shutdown(void) {
    for(int i = 0; i < PLAT_PADS; i++) motor(i, 0);
    audio_ax_stop();
    KPADShutdown();
    WHBLogCafeDeinit();
    WHBLogUdpDeinit();
    WHBProcShutdown();
}
