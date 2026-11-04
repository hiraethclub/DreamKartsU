/* DKU: the Wii U platform (wut).

   - ProcUI through WHBProc: plat_frame_done asks it each frame, so the HOME
     menu and going to the background work, and quitting from HOME ends the
     game loop (plat_quit_requested, upstream main.c's softreset_poll).
   - Time is the system clock; the display paces frames (render_gx2.c).
   - Pad 0 is the GamePad (VPAD). Pro, Classic and Wii Remote controllers
     and GamePad rumble are M4 (PORT_PLAN.md).
   - Saves live on the SD card in SAVE_DIR.
   - stdout and stderr go to the log: UDP (udplogserver from wut-tools on
     the PC) and OSReport (Cemu's log), a line at a time, so the autotest's
     "DK AUTOTEST" lines reach the PC. */
#include <coreinit/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#include "plat.h"

#define SAVE_DIR "fs:/vol/external01/wiiu/apps/DreamKartsU/"

static uint64_t t0;
static bool running = true;

/* ---- stdout and stderr to the log ---- */

static char line[512];
static size_t line_len;

static ssize_t log_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
    (void)r;
    (void)fd;
    for(size_t i = 0; i < len; i++) {
        line[line_len++] = ptr[i];
        if(ptr[i] == '\n' || line_len == sizeof(line) - 1) {
            line[line_len] = 0;
            WHBLogWrite(line);
            line_len = 0;
        }
    }
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

/* Stick -1..1 to the maple range, y down as on the Dreamcast. */
static int stick(float v) {
    int s = (int)(v * 128.0f);
    return s < -128 ? -128 : s > 127 ? 127 : s;
}

bool plat_pad(int i, cont_state_t *out) {
    memset(out, 0, sizeof(*out));
    if(i != 0) return false;
    VPADStatus vs;
    VPADReadError err;
    if(VPADRead(VPAD_CHAN_0, &vs, 1, &err) != 1 || err != VPAD_READ_SUCCESS) {
        static cont_state_t last;
        *out = last;   /* no new sample this frame: keep the last one */
        if(err == VPAD_READ_INVALID_CONTROLLER) return false;
        return true;
    }
    static const struct { uint32_t v, m; } map[] = {
        {VPAD_BUTTON_A, CONT_A},          {VPAD_BUTTON_B, CONT_B},
        {VPAD_BUTTON_X, CONT_X},          {VPAD_BUTTON_Y, CONT_Y},
        {VPAD_BUTTON_PLUS, CONT_START},   {VPAD_BUTTON_UP, CONT_DPAD_UP},
        {VPAD_BUTTON_DOWN, CONT_DPAD_DOWN}, {VPAD_BUTTON_LEFT, CONT_DPAD_LEFT},
        {VPAD_BUTTON_RIGHT, CONT_DPAD_RIGHT},
    };
    for(size_t k = 0; k < sizeof(map) / sizeof(map[0]); k++)
        if(vs.hold & map[k].v) out->buttons |= map[k].m;
    out->joyx = stick(vs.leftStick.x);
    out->joyy = stick(-vs.leftStick.y);
    out->joy2x = stick(vs.rightStick.x);
    out->joy2y = stick(-vs.rightStick.y);
    /* Digital ZL / ZR and L / R as full triggers. */
    out->ltrig = (vs.hold & (VPAD_BUTTON_ZL | VPAD_BUTTON_L)) ? 255 : 0;
    out->rtrig = (vs.hold & (VPAD_BUTTON_ZR | VPAD_BUTTON_R)) ? 255 : 0;
    return true;
}

bool plat_pad_has_rumble(int i) { return false; }

void plat_rumble(int i, int power, int freq) {}

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
}

void plat_frame_done(void) {
    if(running) running = WHBProcIsRunning();
}

/* After the backend's own shutdown (render_gx2.c backend_shutdown). */
void plat_shutdown(void) {
    WHBLogCafeDeinit();
    WHBLogUdpDeinit();
    WHBProcShutdown();
}
