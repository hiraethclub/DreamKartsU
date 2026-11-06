/* DKU shim: KOS timers, threads, maple enumeration (virtual controllers,
   memory card, rumble packs, keyboard), VMU files and packages. */
#include <kos.h>
#include <dc/maple/keyboard.h>
#include <dc/maple/purupuru.h>
#include <dc/maple/vmu.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __WIIU__
#include <coreinit/thread.h>
#include <coreinit/time.h>
#endif

#include "../plat.h"

/* ---- Timers ---- */

uint64_t timer_us_gettime64(void) { return plat_time_us(); }
uint64_t timer_ms_gettime64(void) { return plat_time_us() / 1000; }
uint64_t timer_ns_gettime64(void) { return plat_time_us() * 1000; }

/* ---- Threads ---- */

struct kthread {
    pthread_t t;
};

#ifdef __WIIU__
/* Game threads (the jukebox, saves) stay off core 1, where the game runs. */
typedef struct {
    void *(*routine)(void *param);
    void *param;
} start_t;

static void *on_core0(void *arg) {
    start_t s = *(start_t *)arg;
    free(arg);
    OSSetThreadAffinity(OSGetCurrentThread(), OS_THREAD_ATTRIB_AFFINITY_CPU0);
    return s.routine(s.param);
}
#endif

kthread_t *thd_create_ex(const kthread_attr_t *attr, void *(*routine)(void *param), void *param) {
    kthread_t *k = calloc(1, sizeof(*k));
    if(!k) return NULL;
    /* KOS threads get 64 KiB unless they ask; wut's pthread default is
       much smaller. */
    pthread_attr_t pa;
    pthread_attr_init(&pa);
    pthread_attr_setstacksize(&pa, attr && attr->stack_size ? attr->stack_size : 64 * 1024);
#ifdef __WIIU__
    start_t *start = malloc(sizeof(*start));
    if(!start) {
        pthread_attr_destroy(&pa);
        free(k);
        return NULL;
    }
    *start = (start_t){routine, param};
    int err = pthread_create(&k->t, &pa, on_core0, start);
    if(err) free(start);
#else
    int err = pthread_create(&k->t, &pa, routine, param);
#endif
    pthread_attr_destroy(&pa);
    if(err) {
        free(k);
        return NULL;
    }
    if(attr && attr->create_detached) {
        pthread_detach(k->t);
        free(k);
        /* KOS returns the thread even when detached; callers only test it. */
        static kthread_t detached;
        return &detached;
    }
    return k;
}

kthread_t *thd_create(bool detach, void *(*routine)(void *param), void *param) {
    kthread_attr_t attr = {.create_detached = detach};
    return thd_create_ex(&attr, routine, param);
}

int thd_join(kthread_t *thd, void **value_ptr) {
    if(!thd) return -1;
    int rv = pthread_join(thd->t, value_ptr) ? -1 : 0;
    free(thd);
    return rv;
}

int thd_detach(kthread_t *thd) {
    if(!thd) return -1;
    int rv = pthread_detach(thd->t) ? -1 : 0;
    free(thd);
    return rv;
}

/* OSSleepTicks on the Wii U: wut's nanosleep crashed Cemu (cdda_shim.c). */
void thd_sleep(unsigned int ms) {
#ifdef __WIIU__
    OSSleepTicks(OSMillisecondsToTicks(ms));
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while(nanosleep(&ts, &ts) && errno == EINTR) {}
#endif
}

void thd_pass(void) { sched_yield(); }

/* ---- Network interface (net.c's session token) ---- */

static netif_t netif;
netif_t *net_default_dev = &netif;

/* ---- Maple ---- */

/* Port p: controller at unit 0, rumble pack at 1. The memory card is
   port 0 unit 2 (the second VMU slot, where it is on most pads), and the
   keyboard port 3 unit 0. */
static maple_device_t pads[PLAT_PADS], packs[PLAT_PADS], memcard, keyboard;
static cont_state_t pad_state[PLAT_PADS];

static void enum_refresh(void) {
    for(int i = 0; i < PLAT_PADS; i++) {
        pads[i] = (maple_device_t){0, i, 0, {0, ""}};
        if(plat_pad(i, &pad_state[i])) {
            pads[i].valid = 1;
            pads[i].info.functions = MAPLE_FUNC_CONTROLLER;
            strcpy(pads[i].info.product_name, "DKU controller");
        }
        packs[i] = (maple_device_t){0, i, 1, {0, ""}};
        if(pads[i].valid && plat_pad_has_rumble(i)) {
            packs[i].valid = 1;
            packs[i].info.functions = MAPLE_FUNC_PURUPURU;
        }
    }
    memcard = (maple_device_t){1, 0, 2, {MAPLE_FUNC_MEMCARD, "DKU save folder"}};
    keyboard = (maple_device_t){plat_keyboard(), 3, 0, {MAPLE_FUNC_KEYBOARD, "DKU keyboard"}};
}

static maple_device_t *all_devs(int i) {
    switch(i / PLAT_PADS) {
        case 0: return &pads[i % PLAT_PADS];
        case 1: return &packs[i % PLAT_PADS];
        default: return i == 2 * PLAT_PADS ? &memcard : i == 2 * PLAT_PADS + 1 ? &keyboard : NULL;
    }
}

static maple_device_t *find_dev(int port, int unit) {
    for(int i = 0; all_devs(i); i++) {
        maple_device_t *d = all_devs(i);
        if(d->valid && d->port == port && d->unit == unit) return d;
    }
    return NULL;
}

maple_device_t *maple_enum_dev(int port, int unit) {
    enum_refresh();
    return find_dev(port, unit);
}

/* The n-th device with func, in port then unit order (as KOS). */
maple_device_t *maple_enum_type(int n, uint32_t func) {
    enum_refresh();
    for(int port = 0; port < MAPLE_PORT_COUNT; port++)
        for(int unit = 0; unit < MAPLE_UNIT_COUNT; unit++) {
            maple_device_t *d = find_dev(port, unit);
            if(d && (d->info.functions & func) && n-- == 0) return d;
        }
    return NULL;
}

void *maple_dev_status(maple_device_t *dev) {
    if(dev && dev->valid && (dev->info.functions & MAPLE_FUNC_CONTROLLER) && dev->port < PLAT_PADS)
        return &pad_state[dev->port];
    return NULL;
}

int purupuru_rumble(maple_device_t *dev, const purupuru_effect_t *effect) {
    if(!dev || !dev->valid) return MAPLE_EFAIL;
    plat_rumble(dev->port, effect->cont ? (int)effect->fpow : 0, effect->freq);
    return MAPLE_EOK;
}

int vmu_draw_lcd(maple_device_t *dev, const void *bitmap) {
    (void)dev;
    (void)bitmap;
    return MAPLE_ENOTSUPP;
}

int vmu_draw_lcd_rotated(maple_device_t *dev, const void *bitmap) { return vmu_draw_lcd(dev, bitmap); }

/* ---- Keyboard ---- */

static kbd_state_t kbd_state = {KBD_REGION_US, {.raw = 0}};
static int key_queue[64];
static int key_head, key_n;

kbd_state_t *kbd_get_state(maple_device_t *dev) {
    (void)dev;
    return &kbd_state;
}

int kbd_queue_pop(maple_device_t *dev, bool xlat) {
    (void)dev;
    (void)xlat;
    if(key_n == 0) {
        key_head = 0;
        key_n = plat_keys(key_queue, 64);
        if(key_n == 0) return KBD_QUEUE_END;
    }
    key_n--;
    return key_queue[key_head++];
}

char kbd_key_to_ascii(kbd_key_t key, int region, kbd_mods_t mods, kbd_leds_t leds) {
    (void)region;
    bool shift = mods.lshift || mods.rshift;
    bool caps = leds.caps_lock;
    if(key >= KBD_KEY_A && key < KBD_KEY_A + 26) return (char)((shift != caps ? 'A' : 'a') + (key - KBD_KEY_A));
    if(key >= KBD_KEY_1 && key <= KBD_KEY_0) {
        static const char plain[] = "1234567890", shifted[] = "!@#$%^&*()";
        return (shift ? shifted : plain)[key - KBD_KEY_1];
    }
    switch(key) {
        case KBD_KEY_SPACE: return ' ';
        case KBD_KEY_MINUS: return shift ? '_' : '-';
        case KBD_KEY_PLUS: return shift ? '+' : '=';
        case KBD_KEY_LBRACKET: return shift ? '{' : '[';
        case KBD_KEY_RBRACKET: return shift ? '}' : ']';
        case KBD_KEY_BACKSLASH: return shift ? '|' : '\\';
        case KBD_KEY_SEMICOLON: return shift ? ':' : ';';
        case KBD_KEY_QUOTE: return shift ? '"' : '\'';
        case KBD_KEY_TILDE: return shift ? '~' : '`';
        case KBD_KEY_COMMA: return shift ? '<' : ',';
        case KBD_KEY_PERIOD: return shift ? '>' : '.';
        case KBD_KEY_SLASH: return shift ? '?' : '/';
        default: return 0;
    }
}

/* ---- VMU files: plain files in the save folder ---- */

static void vmu_path(char *out, size_t n, const char *fn, const char *suffix) {
    snprintf(out, n, "%s%s%s", plat_save_dir(), fn, suffix);
}

int vmufs_read(maple_device_t *dev, const char *fn, void **outbuf, int *outsize) {
    (void)dev;
    char path[512];
    vmu_path(path, sizeof(path), fn, ".vmu");
    FILE *f = fopen(path, "rb");
    if(!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = len > 0 ? malloc((size_t)len) : NULL;
    if(!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *outbuf = buf;
    *outsize = (int)len;
    return 0;
}

/* Temporary file, then rename over the old one: a crash or power cut
   mid-write leaves the previous save. */
int vmufs_write(maple_device_t *dev, const char *fn, const void *inbuf, int insize, int flags) {
    (void)dev;
    char path[512], tmp[520];
    vmu_path(path, sizeof(path), fn, ".vmu");
    if(!(flags & VMUFS_OVERWRITE) && access(path, F_OK) == 0) return -1;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if(!f) return -1;
    bool ok = fwrite(inbuf, 1, (size_t)insize, f) == (size_t)insize;
    ok = fclose(f) == 0 && ok;
    if(!ok || rename(tmp, path)) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int vmufs_delete(maple_device_t *dev, const char *fn) {
    (void)dev;
    char path[512];
    vmu_path(path, sizeof(path), fn, ".vmu");
    return remove(path) ? -1 : 0;
}

int vmufs_free_blocks(maple_device_t *dev) {
    (void)dev;
    return 200;
}

/* ---- VMU packages: "DKUP", data length little-endian, data ----

   save.c's record (upstream SaveRecord) is bytes apart from its uint32_t
   check, which it stores in the CPU's order. The file keeps it
   little-endian, as the Dreamcast writes it, so a save moves between the
   PC and the Wii U builds: swapped on the way in and out on a big-endian
   CPU. Version 1 records keep the check at byte 24, version 2 at 32. */

static void record_order(uint8_t *d, size_t n) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if(n < 28 || memcmp(d, "DKSV", 4)) return;
    size_t at = d[4] == 1 ? 24 : d[4] == 2 ? 32 : 0;
    if(!at || n < at + 4) return;
    uint8_t t = d[at];
    d[at] = d[at + 3];
    d[at + 3] = t;
    t = d[at + 1];
    d[at + 1] = d[at + 2];
    d[at + 2] = t;
#else
    (void)d;
    (void)n;
#endif
}

int vmu_pkg_build(vmu_pkg_t *src, uint8_t **dst, int *dst_size) {
    int n = 8 + src->data_len;
    uint8_t *out = malloc((size_t)n);
    if(!out) return -1;
    memcpy(out, "DKUP", 4);
    uint32_t len = (uint32_t)src->data_len;
    for(int i = 0; i < 4; i++) out[4 + i] = (uint8_t)(len >> (8 * i));
    memcpy(out + 8, src->data, (size_t)src->data_len);
    record_order(out + 8, (size_t)src->data_len);
    *dst = out;
    *dst_size = n;
    return 0;
}

int vmu_pkg_parse(void *data, size_t data_size, vmu_pkg_t *pkg) {
    const uint8_t *d = data;
    if(data_size < 8 || memcmp(d, "DKUP", 4)) return -1;
    uint32_t len = d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
    if(len > data_size - 8) return -1;
    record_order((uint8_t *)data + 8, len);
    memset(pkg, 0, sizeof(*pkg));
    strcpy(pkg->app_id, "DreamKartsU");
    pkg->data_len = (int)len;
    pkg->data = d + 8;
    return 0;
}
