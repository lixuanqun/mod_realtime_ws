# Out-of-tree FreeSWITCH module build (requires libfreeswitch-dev).
# Usage:
#   make -f Makefile.fs
#   make -f Makefile.fs install
#
# Mirrors FS module conventions: shared object named mod_realtime_ws.so
# installable into FreeSWITCH mod directory.

CC ?= gcc
FS_CFLAGS := $(shell pkg-config --cflags freeswitch 2>/dev/null)
FS_LIBS := $(shell pkg-config --libs freeswitch 2>/dev/null)
FS_MODDIR := $(shell pkg-config --variable=modulesdir freeswitch 2>/dev/null)

ifeq ($(strip $(FS_CFLAGS)),)
$(error FreeSWITCH pkg-config not found. Install libfreeswitch-dev or set PKG_CONFIG_PATH)
endif

CFLAGS ?= -O2 -g -Wall -Wextra -fPIC -DHAVE_FREESWITCH
CFLAGS += $(FS_CFLAGS) -I src/core -I src/mod -I third_party/cJSON
LDFLAGS ?= -shared
LIBS ?= $(FS_LIBS) -lpthread -lm

MOD_SRCS = \
	src/mod/mod_realtime_ws.c \
	src/mod/rtw_bridge.c \
	src/core/rtw_mulaw.c \
	src/core/rtw_base64.c \
	src/core/rtw_protocol.c \
	src/core/rtw_playout.c \
	src/core/rtw_queue.c \
	src/core/rtw_session.c \
	src/core/rtw_ws_client.c \
	third_party/cJSON/cJSON.c

.PHONY: all clean install

all: build/mod_realtime_ws.so

build:
	mkdir -p build

build/mod_realtime_ws.so: $(MOD_SRCS) | build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MOD_SRCS) $(LIBS)
	@echo "Built $@ — copy/install into FreeSWITCH modules dir and load mod_realtime_ws"

install: build/mod_realtime_ws.so
	@if [ -z "$(FS_MODDIR)" ]; then echo "modulesdir unknown"; exit 1; fi
	install -m 755 build/mod_realtime_ws.so "$(FS_MODDIR)/mod_realtime_ws.so"

clean:
	rm -f build/mod_realtime_ws.so
