/* DKU shim: maple bus enumeration. The port has no maple devices except one
   virtual memory card (port 0, unit 1) whose files live on the platform's
   save folder (port/shim/vmufs.c). Controllers are read by the port's
   input.c replacement, not through maple. */
#ifndef DKU_SHIM_DC_MAPLE_H
#define DKU_SHIM_DC_MAPLE_H

#include <stdint.h>

#define MAPLE_PORT_COUNT 4
#define MAPLE_UNIT_COUNT 6

#define MAPLE_FUNC_PURUPURU   0x00010000
#define MAPLE_FUNC_MOUSE      0x00020000
#define MAPLE_FUNC_CONTROLLER 0x01000000
#define MAPLE_FUNC_MEMCARD    0x02000000
#define MAPLE_FUNC_LCD        0x04000000
#define MAPLE_FUNC_CLOCK      0x08000000
#define MAPLE_FUNC_KEYBOARD   0x40000000

#define MAPLE_EOK      0
#define MAPLE_EFAIL   -1
#define MAPLE_EAGAIN  -2
#define MAPLE_EINVALID -3
#define MAPLE_ENOTSUPP -4
#define MAPLE_ETIMEOUT -5

typedef struct {
    uint32_t functions;
    char product_name[31];
} maple_devinfo_t;

typedef struct maple_device {
    int valid;
    int port, unit;
    maple_devinfo_t info;
} maple_device_t;

maple_device_t *maple_enum_dev(int port, int unit);
maple_device_t *maple_enum_type(int n, uint32_t func);
void *maple_dev_status(maple_device_t *dev);

#endif
