/* DKU shim: arch exit paths (softreset). */
#ifndef DKU_SHIM_ARCH_ARCH_H
#define DKU_SHIM_ARCH_ARCH_H

#define ARCH_EXIT_RETURN 1
#define ARCH_EXIT_MENU   2
#define ARCH_EXIT_REBOOT 3

static inline void arch_set_exit_path(int path) { (void)path; }

#endif
