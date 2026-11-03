# DreamKartsU: Linux host builds of the port. The Wii U build is
# Makefile.wiiu (M3).
#
#   make                      headless release (null backend)
#   make VARIANT=autotest     CPU drives a race; DK_AT_TRACK=N picks a track
#   make BACKEND=gl           SDL2 + OpenGL 3.3 window (M1)
#   make CROSS=powerpc-linux-gnu-   big-endian PowerPC build for qemu-ppc
#
# Output: build/<cross>host-<backend>-<variant>[-t<track>]/dreamkartsu

VARIANT ?= release
BACKEND ?= null
CROSS ?=
DK_AT_TRACK ?=
EXTRA_CFLAGS ?=

CC := $(CROSS)gcc
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

ifeq ($(BACKEND),null)
PORT_SOURCES += port/render_null.c port/plat_headless.c
BACKEND_LIBS :=
else ifeq ($(BACKEND),gl)
PORT_SOURCES += port/render_gl.c port/plat_host.c
BACKEND_CFLAGS := $(shell pkg-config --cflags sdl2 gl zlib 2>/dev/null)
BACKEND_LIBS := $(shell pkg-config --libs sdl2 gl zlib 2>/dev/null)
else
$(error BACKEND must be null or gl)
endif

VARIANT_FLAGS_release :=
VARIANT_FLAGS_autotest := -DDK_AUTOTEST
VARIANT_FLAGS_capture := -DDK_AUTOTEST -DDK_CAPTURE
VARIANT_FLAGS_xfcheck := -DDK_AUTOTEST -DDK_XF_CHECK

CFG := $(CROSS)host-$(BACKEND)-$(VARIANT)$(if $(DK_AT_TRACK),-t$(DK_AT_TRACK))
OUT := build/$(CFG)
BIN := $(OUT)/dreamkartsu

# Same language and warnings as upstream; -fsigned-char because SH-4 char
# is signed and PowerPC char is not (PORT_PLAN.md 1.2).
CFLAGS := -DDKU -std=gnu17 -O2 -ffast-math -fsigned-char -g -Wall -Wextra -Wno-unused-parameter \
	-Wno-missing-field-initializers -Wno-deprecated-declarations \
	$(VARIANT_FLAGS_$(VARIANT)) $(if $(DK_AT_TRACK),-DDK_AT_TRACK=$(DK_AT_TRACK)) \
	-DDK_NET_TEST_HOST=127.0.0.1 $(BACKEND_CFLAGS) $(EXTRA_CFLAGS)
CPPFLAGS := -iquote port/shim/relroot/a/b/c -Iport/shim -Iport -I$(UP)/src -I$(GEN) -I$(OUT) \
	-Ithird_party/sh4zam/include -MMD -MP
LDLIBS := $(BACKEND_LIBS) -lm -lpthread

OBJS := $(addprefix $(OUT)/up/,$(UP_SOURCES:.c=.o)) $(addprefix $(OUT)/,$(PORT_SOURCES:.c=.o)) $(OUT)/port/blobs.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(OUT)/up/%.o: $(UP)/src/%.c | $(OUT)/version.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OUT)/port/blobs.o: port/blobs.S $(GEN)/textures.pak $(GEN)/sfx.pak
	@mkdir -p $(dir $@)
	$(CC) -DDKU_PAK_DIR=$(GEN) -c -o $@ $<

UPSTREAM_SHA := $(shell grep -m1 -oE '`[0-9a-f]{40}`' UPSTREAM.md | head -1 | tr -d '`' | cut -c1-7)
PORT_SHA := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
$(OUT)/version.h: force
	@mkdir -p $(OUT)
	@echo '#define DK_GIT_SHA "$(UPSTREAM_SHA)-dku-$(PORT_SHA)"' | cmp -s - $@ || \
		echo '#define DK_GIT_SHA "$(UPSTREAM_SHA)-dku-$(PORT_SHA)"' > $@

clean:
	rm -rf build/*host-*

.PHONY: all clean force
-include $(OBJS:.o=.d)
