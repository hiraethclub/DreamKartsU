/* DKU shim: controller state as KOS lays it out (softreset, tests). */
#ifndef DKU_SHIM_DC_MAPLE_CONTROLLER_H
#define DKU_SHIM_DC_MAPLE_CONTROLLER_H

#include <stdint.h>

#define CONT_C            (1 << 0)
#define CONT_B            (1 << 1)
#define CONT_A            (1 << 2)
#define CONT_START        (1 << 3)
#define CONT_DPAD_UP      (1 << 4)
#define CONT_DPAD_DOWN    (1 << 5)
#define CONT_DPAD_LEFT    (1 << 6)
#define CONT_DPAD_RIGHT   (1 << 7)
#define CONT_Z            (1 << 8)
#define CONT_Y            (1 << 9)
#define CONT_X            (1 << 10)
#define CONT_D            (1 << 11)
#define CONT_RESET_BUTTONS (CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START)

typedef struct {
    uint32_t buttons;
    int ltrig, rtrig;
    int joyx, joyy;
    int joy2x, joy2y;
} cont_state_t;

#endif
