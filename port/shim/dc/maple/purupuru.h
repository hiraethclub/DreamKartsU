/* DKU shim: the Puru Puru (rumble) pack. The platform gives each player's
   controller a virtual pack at unit 1 when it has a motor (GamePad). */
#ifndef DKU_SHIM_DC_MAPLE_PURUPURU_H
#define DKU_SHIM_DC_MAPLE_PURUPURU_H

#include <stdbool.h>
#include <stdint.h>
#include <dc/maple.h>

typedef struct {
    uint8_t duration;
    uint8_t motor;
    bool cont;
    uint32_t fpow;
    uint8_t freq;
} purupuru_effect_t;

int purupuru_rumble(maple_device_t *dev, const purupuru_effect_t *effect);

#endif
