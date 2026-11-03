/* DKU shim: VMU screens. The port has no VMU LCDs, so maple_enum_type never
   finds MAPLE_FUNC_LCD and this is not reached; a GamePad display of the
   VMU screen is a later option (PORT_PLAN.md M7). */
#ifndef DKU_SHIM_DC_MAPLE_VMU_H
#define DKU_SHIM_DC_MAPLE_VMU_H

#include <dc/maple.h>

int vmu_draw_lcd(maple_device_t *dev, const void *bitmap);
int vmu_draw_lcd_rotated(maple_device_t *dev, const void *bitmap);

#endif
