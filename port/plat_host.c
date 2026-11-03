/* DKU: the Linux desktop platform: an SDL2 window with an OpenGL 3.3 core
   context (render_gl.c draws into it), keyboard and SDL game controllers
   as Dreamcast pads, and the shared mixer through SDL audio.

   Keyboard (pad 1): arrows steer and work the d-pad, Z = A, X = B,
   A = X, S = Y, Enter = Start, Q / W = left / right trigger, Esc quits.

   DKU_VIRTUAL_CLOCK=1 (the test scripts) gives the game the headless
   platform's steady 60 Hz clock however fast the host draws, and turns
   vsync off; with DKU_REPRO=1 as well, runs repeat exactly
   (plat_headless.c). DKU_MAX_FRAMES quits after that many frames. Headless runs
   go under xvfb-run with SDL_AUDIODRIVER=dummy. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "audio_mix.h"
#include "plat.h"

#define PERIOD_US 16667ull

static SDL_Window *window;
static SDL_GLContext gl_ctx;
static SDL_GameController *pads[PLAT_PADS];
static SDL_AudioDeviceID audio_dev;
static bool virtual_clock, repro, quit;
static uint64_t frames, frame_real0, max_frames, t0, repro_calls;
static char save_dir[512];

static uint64_t real_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

uint64_t plat_time_us(void) {
    if(!virtual_clock) return real_us() - t0;
    uint64_t in_frame = repro ? ++repro_calls : real_us() - frame_real0;
    if(in_frame > PERIOD_US - 667) in_frame = PERIOD_US - 667;
    return frames * PERIOD_US + in_frame;
}

static void audio_cb(void *user, Uint8 *stream, int len) {
    (void)user;
    audio_mix((int16_t *)stream, len / 4);
}

static void open_pads(void) {
    for(int i = 0, n = 0; i < SDL_NumJoysticks() && n < PLAT_PADS; i++) {
        if(!SDL_IsGameController(i)) continue;
        SDL_GameController *c = SDL_GameControllerOpen(i);
        if(c) pads[n++] = c;
    }
}

void plat_init(void) {
    static bool done;
    if(done) return;
    done = true;
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *e = getenv("DKU_VIRTUAL_CLOCK");
    virtual_clock = e && *e && *e != '0';
    e = getenv("DKU_REPRO");
    repro = e && *e && *e != '0';
    e = getenv("DKU_MAX_FRAMES");
    max_frames = e ? strtoull(e, NULL, 10) : 0;
    t0 = frame_real0 = real_us();

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0 &&
       SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "DKU: SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("DreamKartsU", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if(!window || !(gl_ctx = SDL_GL_CreateContext(window))) {
        fprintf(stderr, "DKU: no OpenGL 3.3 window: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GL_SetSwapInterval(virtual_clock ? 0 : 1);
    open_pads();

    if(SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_AudioSpec want = {0}, have;
        want.freq = AUDIO_MIX_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_cb;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if(audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
    }
}

void plat_window_size(int *w, int *h) { SDL_GL_GetDrawableSize(window, w, h); }

void plat_swap(void) { SDL_GL_SwapWindow(window); }

void plat_frame_done(void) {
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) {
        if(ev.type == SDL_QUIT) quit = true;
        else if(ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) quit = true;
        else if(ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) {
            for(int i = 0; i < PLAT_PADS; i++)
                if(pads[i]) SDL_GameControllerClose(pads[i]);
            memset(pads, 0, sizeof(pads));
            open_pads();
        }
    }
    frames++;
    if(!virtual_clock) {
        /* Without vsync (or with a fast display), hold to 60 Hz. */
        uint64_t due = t0 + frames * PERIOD_US, now = real_us();
        if(now < due) SDL_Delay((Uint32)((due - now) / 1000));
        else if(now > due + 4 * PERIOD_US) t0 = now - frames * PERIOD_US;
    }
    repro_calls = 0;
    frame_real0 = real_us();
}

bool plat_quit_requested(void) { return quit || (max_frames && frames >= max_frames); }

const char *plat_save_dir(void) {
    if(!save_dir[0]) {
        const char *d = getenv("DKU_SAVE_DIR");
        if(d) snprintf(save_dir, sizeof(save_dir), "%s/", d);
        else {
            char *p = SDL_GetPrefPath("hiraethclub", "DreamKartsU");
            snprintf(save_dir, sizeof(save_dir), "%s", p ? p : "build/host/save/");
            SDL_free(p);
        }
        mkdir(save_dir, 0755);
    }
    return save_dir;
}

static int axis(SDL_GameController *c, SDL_GameControllerAxis a) {
    return SDL_GameControllerGetAxis(c, a) / 256;   /* -128..127 like the maple bus */
}

bool plat_pad(int i, cont_state_t *out) {
    memset(out, 0, sizeof(*out));
    SDL_GameController *c = i < PLAT_PADS ? pads[i] : NULL;
    if(!c && i != 0) return false;
    if(c) {
        static const struct { SDL_GameControllerButton b; uint32_t m; } map[] = {
            {SDL_CONTROLLER_BUTTON_A, CONT_A},
            {SDL_CONTROLLER_BUTTON_B, CONT_B},
            {SDL_CONTROLLER_BUTTON_X, CONT_X},
            {SDL_CONTROLLER_BUTTON_Y, CONT_Y},
            {SDL_CONTROLLER_BUTTON_START, CONT_START},
            {SDL_CONTROLLER_BUTTON_DPAD_UP, CONT_DPAD_UP},
            {SDL_CONTROLLER_BUTTON_DPAD_DOWN, CONT_DPAD_DOWN},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, CONT_DPAD_LEFT},
            {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, CONT_DPAD_RIGHT},
        };
        for(size_t k = 0; k < sizeof(map) / sizeof(map[0]); k++)
            if(SDL_GameControllerGetButton(c, map[k].b)) out->buttons |= map[k].m;
        out->joyx = axis(c, SDL_CONTROLLER_AXIS_LEFTX);
        out->joyy = axis(c, SDL_CONTROLLER_AXIS_LEFTY);
        out->joy2x = axis(c, SDL_CONTROLLER_AXIS_RIGHTX);
        out->joy2y = axis(c, SDL_CONTROLLER_AXIS_RIGHTY);
        out->ltrig = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 128;
        out->rtrig = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 128;
    }
    if(i == 0) {
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if(k[SDL_SCANCODE_UP]) out->buttons |= CONT_DPAD_UP;
        if(k[SDL_SCANCODE_DOWN]) out->buttons |= CONT_DPAD_DOWN;
        if(k[SDL_SCANCODE_LEFT]) {
            out->buttons |= CONT_DPAD_LEFT;
            out->joyx = -128;
        }
        if(k[SDL_SCANCODE_RIGHT]) {
            out->buttons |= CONT_DPAD_RIGHT;
            out->joyx = 127;
        }
        if(k[SDL_SCANCODE_Z]) out->buttons |= CONT_A;
        if(k[SDL_SCANCODE_X]) out->buttons |= CONT_B;
        if(k[SDL_SCANCODE_A]) out->buttons |= CONT_X;
        if(k[SDL_SCANCODE_S]) out->buttons |= CONT_Y;
        if(k[SDL_SCANCODE_RETURN]) out->buttons |= CONT_START;
        if(k[SDL_SCANCODE_Q]) out->ltrig = 255;
        if(k[SDL_SCANCODE_W]) out->rtrig = 255;
    }
    return true;
}

bool plat_pad_has_rumble(int i) {
    return i < PLAT_PADS && pads[i] && SDL_GameControllerHasRumble(pads[i]);
}

void plat_rumble(int i, int power, int freq) {
    (void)freq;
    if(i < PLAT_PADS && pads[i]) {
        Uint16 p = (Uint16)(power * 0xffff / 7);
        SDL_GameControllerRumble(pads[i], p, p, power ? 1000 : 0);
    }
}

bool plat_keyboard(void) { return false; }

int plat_keys(int *buf, int max) {
    (void)buf;
    (void)max;
    return 0;
}
