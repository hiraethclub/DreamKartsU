/* DKU: the "BUILD NNNN" label for the console testing phase (build_overlay.c).
   To remove it: build with BUILD_OVERLAY=0 or set that default in
   Makefile.wiiu. */
#ifndef DKU_BUILD_OVERLAY_H
#define DKU_BUILD_OVERLAY_H

#include "ta.h"

#define BUILD_OVERLAY_MAX_VERTS 6144

/* Triangles for the label in the 640x480 frame, bottom left; returns the count. */
int build_overlay_verts(ta_vertex_t *out);

#endif
