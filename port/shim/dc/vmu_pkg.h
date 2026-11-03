/* DKU shim: VMU file packaging. The port has no Dreamcast file manager to
   show the icon, so a package is a small fixed header plus the data:
   "DKUP", data length (4 bytes little-endian), the data. */
#ifndef DKU_SHIM_DC_VMU_PKG_H
#define DKU_SHIM_DC_VMU_PKG_H

#include <stddef.h>
#include <stdint.h>

#define VMUPKG_EC_NONE   0
#define VMUPKG_EC_16BIT  1
#define VMUPKG_EC_256COL 2
#define VMUPKG_EC_16COL  3

typedef struct {
    char desc_short[20];
    char desc_long[36];
    char app_id[20];
    int icon_cnt;
    int icon_anim_speed;
    int eyecatch_type;
    int data_len;
    uint16_t icon_pal[16];
    const uint8_t *icon_data;
    const uint8_t *eyecatch_data;
    const uint8_t *data;
} vmu_pkg_t;

int vmu_pkg_build(vmu_pkg_t *src, uint8_t **dst, int *dst_size);
int vmu_pkg_parse(void *data, size_t data_size, vmu_pkg_t *pkg);

#endif
