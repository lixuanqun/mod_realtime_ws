#!/usr/bin/env bash
# Build mod_realtime_ws.so against a real FreeSWITCH install (out-of-tree).
# Requires: gcc, make, pkg-config, libfreeswitch-dev (and optionally libssl-dev for future wss).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -d /usr/local/freeswitch/lib/pkgconfig ]]; then
  export PKG_CONFIG_PATH="/usr/local/freeswitch/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

if ! pkg-config --exists freeswitch; then
  echo "freeswitch.pc not found. On Debian/Ubuntu try: apt-get install -y libfreeswitch-dev" >&2
  exit 1
fi

make -f Makefile.fs clean || true
make -f Makefile.fs
echo "OK: $ROOT/build/mod_realtime_ws.so"
pkg-config --modversion freeswitch || true
