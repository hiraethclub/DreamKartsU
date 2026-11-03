/* DKU shim: KOS timers on the platform's monotonic clock (plat_*.c). */
#ifndef DKU_SHIM_ARCH_TIMER_H
#define DKU_SHIM_ARCH_TIMER_H

#include <stdint.h>

uint64_t timer_us_gettime64(void);
uint64_t timer_ms_gettime64(void);
uint64_t timer_ns_gettime64(void);

#endif
