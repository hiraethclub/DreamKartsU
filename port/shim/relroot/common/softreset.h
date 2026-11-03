/* DKU shim for projects/common/softreset.h: there is no A+B+X+Y+Start on
   the port. The platform asks to quit (window closed, HOME menu exit) and
   softreset_poll reports it as SOFTRESET_QUIT. */
#ifndef DC_SOFTRESET_H
#define DC_SOFTRESET_H

#include <stdbool.h>

#define SOFTRESET_GRACE 45

enum { SOFTRESET_NONE, SOFTRESET_TITLE, SOFTRESET_QUIT };

bool plat_quit_requested(void);

static inline void softreset_leave(void) {}

static inline int softreset_poll(bool on_title) {
    (void)on_title;
    return plat_quit_requested() ? SOFTRESET_QUIT : SOFTRESET_NONE;
}

#endif
