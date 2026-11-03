/* DKU shim: VMU files as plain files in the platform's save folder. Writes
   go to a temporary file and are renamed over the old one. */
#ifndef DKU_SHIM_DC_VMUFS_H
#define DKU_SHIM_DC_VMUFS_H

#include <dc/maple.h>

#define VMUFS_OVERWRITE 1
#define VMUFS_VMUGAME   2
#define VMUFS_NOCOPY    4

int vmufs_read(maple_device_t *dev, const char *fn, void **outbuf, int *outsize);
int vmufs_write(maple_device_t *dev, const char *fn, const void *inbuf, int insize, int flags);
int vmufs_delete(maple_device_t *dev, const char *fn);
int vmufs_free_blocks(maple_device_t *dev);

#endif
