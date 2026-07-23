#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "== unit =="
make unit

echo "== build sim =="
make sim

echo "== npm install mock =="
cd examples/node-mock-server
npm install --silent
cd "$ROOT"

echo "== smoke echo =="
MODE=echo PORT=18081 node examples/node-mock-server/server.js >/tmp/rtw-mock-echo.log 2>&1 &
MOCK_PID=$!
cleanup() { kill "$MOCK_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.5
./build/rtw_sim --url ws://127.0.0.1:18081/media --seconds 1

echo "== smoke clear =="
kill "$MOCK_PID" 2>/dev/null || true
sleep 0.2
MODE=clear CLEAR_AFTER=5 PORT=18082 node examples/node-mock-server/server.js >/tmp/rtw-mock-clear.log 2>&1 &
MOCK_PID=$!
sleep 0.5
./build/rtw_sim --url ws://127.0.0.1:18082/media --seconds 2 --clear-test

echo "== mod harness =="
./scripts/mod_harness_test.sh

echo "== wss smoke =="
./scripts/wss_smoke_test.sh

echo "ALL SMOKE PASSED"
