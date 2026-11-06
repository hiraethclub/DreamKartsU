/* DKU: "BUILD NNNN", bottom left of the frame, so a phone photo or a video
   from the console says which build it shows. White 5x7 pixels, twice size,
   on a dark box. Only used while the Makefile sets DKU_BUILD_OVERLAY. */
#include <stdio.h>

#include "build_overlay.h"
#include "testlog.h"

#define PX 2
#define CHAR_W ((5 + 1) * PX)
#define X0 6
#define Y0 (480 - 6 - 7 * PX)

/* Rows top to bottom, bit 4 is the leftmost column. */
static const uint8_t *glyph(char c) {
    static const uint8_t digits[10][7] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},     {14, 17, 1, 2, 4, 8, 31},
        {30, 1, 1, 14, 1, 1, 30},     {2, 6, 10, 18, 31, 2, 2},    {31, 16, 30, 1, 1, 17, 14},
        {6, 8, 16, 30, 17, 17, 14},   {31, 1, 2, 4, 8, 8, 8},      {14, 17, 17, 14, 17, 17, 14},
        {14, 17, 17, 15, 1, 2, 12},
    };
    static const uint8_t B[7] = {30, 17, 17, 30, 17, 17, 30}, U[7] = {17, 17, 17, 17, 17, 17, 14},
                         I[7] = {14, 4, 4, 4, 4, 4, 14}, L[7] = {16, 16, 16, 16, 16, 16, 31},
                         D[7] = {30, 17, 17, 17, 17, 17, 30};
    if(c >= '0' && c <= '9') return digits[c - '0'];
    return c == 'B' ? B : c == 'U' ? U : c == 'I' ? I : c == 'L' ? L : c == 'D' ? D : NULL;
}

static int quad(ta_vertex_t *o, float x0, float y0, float x1, float y1, uint32_t argb) {
    const float xy[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x0, y1}, {x1, y0}, {x1, y1}};
    for(int i = 0; i < 6; i++) o[i] = (ta_vertex_t){xy[i][0], xy[i][1], 1.0f, 0.0f, 0.0f, argb, 0};
    return 6;
}

int build_overlay_verts(ta_vertex_t *out) {
    char text[24];
    int len = snprintf(text, sizeof(text), "BUILD %04d", testlog_build());
    if(len >= (int)sizeof(text)) len = (int)sizeof(text) - 1;
    int n = quad(out, X0 - 3, Y0 - 3, X0 + len * CHAR_W + 1, Y0 + 7 * PX + 3, 0xb0000000u);
    for(int k = 0; k < len; k++) {
        const uint8_t *g = glyph(text[k]);
        if(!g) continue;
        for(int y = 0; y < 7; y++)
            for(int x = 0; x < 5; x++)
                if((g[y] >> (4 - x) & 1) && n + 6 <= BUILD_OVERLAY_MAX_VERTS) {
                    float px = (float)(X0 + k * CHAR_W + x * PX), py = (float)(Y0 + y * PX);
                    n += quad(out + n, px, py, px + PX, py + PX, 0xffffffffu);
                }
    }
    return n;
}
