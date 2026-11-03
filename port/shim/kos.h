/* DKU shim: KallistiOS's umbrella header, reduced to what DreamKarts uses.
   Upstream files include <kos.h>; the port satisfies it with these. */
#ifndef DKU_SHIM_KOS_H
#define DKU_SHIM_KOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/timer.h>
#include <arch/rtc.h>
#include <arch/arch.h>
#include <arch/perf.h>
#include <kos/thread.h>
#include <kos/mutex.h>
#include <kos/net.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/vmufs.h>
#include <dc/vmu_pkg.h>
#include <dc/fs_dcload.h>

/* main.c's KOS_INIT_FLAGS(INIT_DEFAULT): nothing to initialise. */
#define INIT_DEFAULT 0
#define INIT_NET 0
#define KOS_INIT_FLAGS(flags) extern int dku_kos_init_flags_unused

/* The BIOS's system ID; main.c checks it for the iOS emulator's tag. */
static inline uint64_t syscall_sysinfo_id(void) { return 0; }

#endif
