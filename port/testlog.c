/* DKU: the console test log.

   Every printf line (and the PERF lines from perf_wiiu.c) goes to
   SD:/wiiu/apps/DreamKartsU/DreamKartsU-build-NNNN.log and to UDP
   (udplogserver on the PC, live) and OSReport (Cemu's log).

   Writing to an SD card can take many milliseconds, so the game thread only
   copies its line into a ring. A low-priority thread on core 0 sends lines to
   UDP within 20 ms and writes to the card about once a second, opening and
   closing the file each time: nothing is left open for the HOME menu, and a
   crash or power cut loses at most the last second. */
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <whb/log.h>

#include "dku_build.h"
#include "plat.h"
#include "testlog.h"

#define RING 131072u
#define CHUNK 16384u
#define FILEBUF 65536u
#define PREFIX 13   /* "[ 1234.567] " plus a space: see testlog_put */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char ring[RING];
static uint32_t head, tail;   /* free-running; under lock */
static uint32_t dropped;      /* under lock */
static pthread_t thread;
static bool running;
static volatile bool stopping, paused, flush_wanted;
static char path[256];

static char filebuf[FILEBUF];
static size_t filelen;

int testlog_build(void) { return DKU_BUILD_NUMBER; }

static void write_file(void) {
    static bool warned;
    if(!filelen) return;
    FILE *f = fopen(path, "ab");
    if(f) {
        fwrite(filebuf, 1, filelen, f);
        fclose(f);
    }
    else if(!warned) {
        warned = true;
        WHBLogPrintf("DKU LOG: cannot open %s\n", path);
    }
    filelen = 0;
}

static void file_append(const char *s, size_t n) {
    if(n > FILEBUF - filelen) write_file();
    if(n > FILEBUF - filelen) return;
    memcpy(filebuf + filelen, s, n);
    filelen += n;
}

/* Take what is in the ring: returns bytes left in `chunk`, whole lines. */
static void drain(void) {
    static char chunk[CHUNK + 1];
    for(;;) {
        pthread_mutex_lock(&lock);
        uint32_t n = head - tail;
        if(n > CHUNK) n = CHUNK;
        for(uint32_t i = 0; i < n; i++) chunk[i] = ring[(tail + i) % RING];
        tail += n;
        uint32_t lost = dropped;
        dropped = 0;
        pthread_mutex_unlock(&lock);
        if(lost) {
            char s[64];
            int k = snprintf(s, sizeof(s), "DKU LOG: %u lines lost, ring full\n", (unsigned)lost);
            WHBLogWrite(s);
            file_append(s, (size_t)k);
        }
        if(!n) return;
        file_append(chunk, n);
        /* Lines go in whole, so a chunk ends a line or is cut by CHUNK. */
        chunk[n] = 0;
        for(char *line = chunk; *line;) {
            char *nl = strchr(line, '\n');
            char saved = 0;
            if(nl) {
                saved = nl[1];
                nl[1] = 0;
            }
            WHBLogWrite(strlen(line) > PREFIX ? line + PREFIX : line);
            if(!nl) break;
            nl[1] = saved;
            line = nl + 1;
        }
    }
}

static void *writer(void *arg) {
    (void)arg;
    OSSetThreadAffinity(OSGetCurrentThread(), OS_THREAD_ATTRIB_AFFINITY_CPU0);
    OSSetThreadPriority(OSGetCurrentThread(), 24);   /* below the sound threads */
    uint64_t last = plat_time_us();
    for(;;) {
        bool stop = stopping;
        drain();
        uint64_t now = plat_time_us();
        if(stop || flush_wanted || (!paused && now - last >= 1000000)) {
            write_file();
            last = now;
            flush_wanted = false;
        }
        if(stop) break;
        OSSleepTicks(OSMillisecondsToTicks(20));
    }
    return NULL;
}

void testlog_put(const char *line) {
    if(!running) {
        WHBLogWrite(line);
        return;
    }
    uint64_t us = plat_time_us();
    char prefix[PREFIX + 8];
    int p = snprintf(prefix, sizeof(prefix), "[%5u.%03u]  ", (unsigned)(us / 1000000u % 100000u),
                     (unsigned)(us / 1000u % 1000u));
    if(p != PREFIX) p = 0;
    size_t n = strlen(line);
    pthread_mutex_lock(&lock);
    if(RING - (head - tail) < (uint32_t)p + n) dropped++;
    else {
        for(int i = 0; i < p; i++) ring[head++ % RING] = prefix[i];
        for(size_t i = 0; i < n; i++) ring[head++ % RING] = line[i];
    }
    pthread_mutex_unlock(&lock);
}

void testlog_start(const char *dir) {
    if(running) return;
    snprintf(path, sizeof(path), "%sDreamKartsU-build-%04d.log", dir, DKU_BUILD_NUMBER);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    running = pthread_create(&thread, &attr, writer, NULL) == 0;
    pthread_attr_destroy(&attr);
    if(!running) {
        WHBLogPrint("DKU LOG: no writer thread; no log file\n");
        return;
    }
    char s[384];
    snprintf(s, sizeof(s), "\n=== DreamKartsU build %d, commit %s, new boot, log %s\n", DKU_BUILD_NUMBER, DKU_BUILD_SHA,
             path);
    testlog_put(s);
}

void testlog_pause(bool on) {
    if(!running) return;
    if(on) {
        flush_wanted = true;
        for(int i = 0; i < 100 && flush_wanted; i++) OSSleepTicks(OSMillisecondsToTicks(2));
    }
    paused = on;
}

void testlog_stop(void) {
    if(!running) return;
    stopping = true;
    pthread_join(thread, NULL);
    running = false;
    stopping = false;
}
