# Sources shared by the host Makefile and Makefile.wiiu.

UP := upstream
GEN := $(UP)/assets/generated

# Upstream sources, all but netlink.c (replaced by port/netlink_port.c).
UP_SOURCES := main.c input.c track.c kart.c render.c world.c models.c fx.c hud.c ui.c title.c audio.c \
	rumble.c dolphins.c biplane.c net.c online.c save.c vmuscreen.c spectate.c film.c items.c gp.c \
	chat.c perfhud.c flappy.c roam.c world_mesh.c world_road.c world_props.c world_beach.c \
	world_coral.c world_harbour.c world_temple.c world_skyway.c world_canyon.c world_tsudoken.c \
	world_apple.c world_lot.c tide.c birds.c autotest.c prof.c
PORT_SOURCES := port/xform_c.c port/netlink_port.c port/shim/pvr_ta.c port/shim/kos_shim.c \
	port/shim/snd_shim.c port/shim/cdda_shim.c third_party/sh4zam/source/sw/shz_xmtrx_sw.c

VARIANT_FLAGS_release :=
VARIANT_FLAGS_autotest := -DDK_AUTOTEST
VARIANT_FLAGS_capture := -DDK_AUTOTEST -DDK_CAPTURE
VARIANT_FLAGS_xfcheck := -DDK_AUTOTEST -DDK_XF_CHECK
VARIANT_FLAGS_savetest :=
VARIANT_FLAGS_exittest := -DDKU_EXIT_AFTER_FRAMES=1500

# The save check (port/savetest.c): upstream save.c on its own with the
# shim, under the platform's own init and backend.
ifeq ($(VARIANT),savetest)
UP_SOURCES := save.c
PORT_SOURCES := port/savetest.c port/xform_c.c port/shim/pvr_ta.c port/shim/kos_shim.c port/shim/snd_shim.c \
	port/shim/cdda_shim.c third_party/sh4zam/source/sw/shz_xmtrx_sw.c
endif

# Same language and warnings as upstream; -fsigned-char because SH-4 char
# is signed and PowerPC char is not (PORT_PLAN.md 1.2).
COMMON_CFLAGS := -DDKU -std=gnu17 -O2 -ffast-math -fsigned-char -g -Wall -Wextra -Wno-unused-parameter \
	-Wno-missing-field-initializers -Wno-deprecated-declarations
COMMON_CPPFLAGS := -iquote port/shim/relroot/a/b/c -Iport/shim -Iport -I$(UP)/src -I$(GEN) \
	-Ithird_party/sh4zam/include -isystem third_party/dr_libs -MMD -MP

UPSTREAM_SHA := $(shell grep -m1 -oE '`[0-9a-f]{40}`' UPSTREAM.md | head -1 | tr -d '`' | cut -c1-7)
PORT_SHA := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)

# Upstream warnings left alone rather than edited (an edit costs a merge):
# world_canyon.c counts the bushes it places only for an autotest printf,
# so release builds see the counter as set but unused.
UP_QUIET_world_canyon := -Wno-unused-but-set-variable
