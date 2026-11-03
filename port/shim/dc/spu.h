/* DKU shim: the AICA's CD input level, 0..15 a side (3 dB steps). */
#ifndef DKU_SHIM_DC_SPU_H
#define DKU_SHIM_DC_SPU_H

void spu_cdda_volume(int left_volume, int right_volume);

#endif
