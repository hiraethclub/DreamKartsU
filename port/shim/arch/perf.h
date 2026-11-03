/* DKU shim: the SH-4's performance counters (prof.c). The port has no
   counters to read, so they count nothing; prof.c's timings still come
   from timer_us_gettime64. */
#ifndef DKU_SHIM_ARCH_PERF_H
#define DKU_SHIM_ARCH_PERF_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PMCR_OPERAND_CACHE_MISS_MODE = 0x04,
    PMCR_PIPELINE_FREEZE_BY_DCACHE_MISS_MODE = 0x20,
    PMCR_PIPELINE_FREEZE_BY_FPU_MODE = 0x2d
} perf_cntr_event_t;

typedef enum { PMCR_COUNT_CPU_CYCLES = 0, PMCR_COUNT_RATIO_CYCLES = 1 } perf_cntr_clock_t;
typedef enum { PRFC0 = 0, PRFC1 = 1 } perf_cntr_t;

static inline bool perf_cntr_start(perf_cntr_t c, perf_cntr_event_t e, perf_cntr_clock_t k) {
    (void)c; (void)e; (void)k;
    return true;
}
static inline bool perf_cntr_stop(perf_cntr_t c) { (void)c; return true; }
static inline bool perf_cntr_clear(perf_cntr_t c) { (void)c; return true; }
static inline uint64_t perf_cntr_count(perf_cntr_t c) { (void)c; return 0; }

#endif
