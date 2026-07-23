CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -I src/core -I third_party/cJSON
LDFLAGS ?=

CORE_SRCS = \
	src/core/rtw_mulaw.c \
	src/core/rtw_base64.c \
	src/core/rtw_protocol.c \
	src/core/rtw_playout.c \
	src/core/rtw_queue.c \
	src/core/rtw_session.c \
	third_party/cJSON/cJSON.c

CORE_OBJS = $(CORE_SRCS:.c=.o)

UNIT_TESTS = \
	build/test_codec \
	build/test_protocol \
	build/test_playout_session

.PHONY: all test unit clean sim

all: unit sim

build:
	mkdir -p build

unit: build $(UNIT_TESTS)
	@echo "== unit tests =="
	@fail=0; \
	for t in $(UNIT_TESTS); do \
	  echo "-- $$t"; \
	  $$t || fail=1; \
	done; \
	exit $$fail

build/test_codec: tests/unit/test_codec.c $(CORE_OBJS) | build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_codec.c $(CORE_OBJS) $(LDFLAGS)

build/test_protocol: tests/unit/test_protocol.c $(CORE_OBJS) | build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_protocol.c $(CORE_OBJS) $(LDFLAGS)

build/test_playout_session: tests/unit/test_playout_session.c $(CORE_OBJS) | build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_playout_session.c $(CORE_OBJS) $(LDFLAGS)

sim: build/rtw_sim

build/rtw_sim: src/sim/rtw_sim.c $(CORE_OBJS) | build
	$(CC) $(CFLAGS) -o $@ src/sim/rtw_sim.c $(CORE_OBJS) $(LDFLAGS) -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build $(CORE_OBJS)
