#!/usr/bin/env bash
# Full-link stress: N concurrent rtw_sim producers against one mock peer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONCURRENCY="${CONCURRENCY:-20}"
SECONDS_RUN="${SECONDS_RUN:-3}"
PORT="${PORT:-18090}"

make sim >/dev/null
cd examples/node-mock-server && npm install --silent && cd "$ROOT"

MODE=echo PORT="$PORT" node examples/node-mock-server/server.js >/tmp/rtw-stress-mock.log 2>&1 &
MOCK_PID=$!
cleanup() { kill "$MOCK_PID" 2>/dev/null || true; wait "$MOCK_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.4

FAIL=0
PIDS=()
for i in $(seq 1 "$CONCURRENCY"); do
  sid=$(printf 'MZ%030d' "$i")
  ./build/rtw_sim --url "ws://127.0.0.1:${PORT}/media" --seconds "$SECONDS_RUN" --stream-sid "$sid" \
    >/tmp/rtw-stress-"$i".log 2>&1 &
  PIDS+=("$!")
done

for pid in "${PIDS[@]}"; do
  if ! wait "$pid"; then
    FAIL=1
  fi
done

if [[ "$FAIL" -ne 0 ]]; then
  echo "STRESS FAILED — see /tmp/rtw-stress-*.log"
  exit 1
fi

# rough throughput: uplink frames ~ SECONDS_RUN * 50 per session
echo "STRESS PASSED concurrency=$CONCURRENCY seconds=$SECONDS_RUN"
