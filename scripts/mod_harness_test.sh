#!/usr/bin/env bash
# Self-test module bridge via FS stub harness + Node Twilio mock.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make harness

cd examples/node-mock-server
npm install --silent
cd "$ROOT"

PORT=18083
MODE=echo PORT=$PORT node examples/node-mock-server/server.js >/tmp/rtw-mock-harness.log 2>&1 &
MOCK_PID=$!
cleanup() { kill "$MOCK_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.4

./build/rtw_mod_harness --url "ws://127.0.0.1:${PORT}/media" --seconds 2
echo "MOD HARNESS PASSED"
