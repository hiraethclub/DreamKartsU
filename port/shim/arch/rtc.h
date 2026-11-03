/* DKU shim: the real-time clock. */
#ifndef DKU_SHIM_ARCH_RTC_H
#define DKU_SHIM_ARCH_RTC_H

#include <time.h>

static inline time_t rtc_unix_secs(void) { return time(NULL); }

#endif
