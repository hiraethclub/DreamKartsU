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
include sources.mk

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

CFG := $(CROSS)host-$(BACKEND)-$(VARIANT)$(if $(DK_AT_TRACK),-t$(DK_AT_TRACK))
OUT := build/$(CFG)
BIN := $(OUT)/dreamkartsu

CFLAGS := $(COMMON_CFLAGS) $(VARIANT_FLAGS_$(VARIANT)) $(if $(DK_AT_TRACK),-DDK_AT_TRACK=$(DK_AT_TRACK)) \
	-DDK_NET_TEST_HOST=127.0.0.1 $(BACKEND_CFLAGS) $(EXTRA_CFLAGS)
CPPFLAGS := $(COMMON_CPPFLAGS) -I$(OUT)
LDLIBS := $(BACKEND_LIBS) -lm -lpthread

OBJS := $(addprefix $(OUT)/up/,$(UP_SOURCES:.c=.o)) $(addprefix $(OUT)/,$(PORT_SOURCES:.c=.o)) $(OUT)/port/blobs.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(OUT)/up/%.o: $(UP)/src/%.c | $(OUT)/version.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UP_QUIET_$*) -c -o $@ $<

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OUT)/port/blobs.o: port/blobs.S $(GEN)/textures.pak $(GEN)/sfx.pak
	@mkdir -p $(dir $@)
	$(CC) -DDKU_PAK_DIR=$(GEN) -c -o $@ $<

$(OUT)/version.h: force
	@mkdir -p $(OUT)
	@echo '#define DK_GIT_SHA "$(UPSTREAM_SHA)-dku-$(PORT_SHA)"' | cmp -s - $@ || \
		echo '#define DK_GIT_SHA "$(UPSTREAM_SHA)-dku-$(PORT_SHA)"' > $@

clean:
	rm -rf build/*host-*

.PHONY: all clean force
-include $(OBJS:.o=.d)
