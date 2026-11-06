/* DKU: the console test log (testlog.c). */
#ifndef DKU_TESTLOG_H
#define DKU_TESTLOG_H

#include <stdbool.h>

/* The build number from tools/build_number.sh. */
int testlog_build(void);
/* Start logging to DIR/DreamKartsU-build-NNNN.log, appended per boot. */
void testlog_start(const char *dir);
/* One finished line, ending in a newline. Never touches the SD card. */
void testlog_put(const char *line);
/* Going to the background (write what is waiting, then leave the card
   alone) or coming back. */
void testlog_pause(bool on);
/* Write everything and end the thread. Later lines go straight to UDP. */
void testlog_stop(void);

#endif
