#!/usr/bin/env bash
# TLS (wss://) smoke against Node mock with self-signed cert.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f /usr/include/openssl/ssl.h ]]; then
  echo "skip wss smoke: no OpenSSL headers"
  exit 0
fi

make harness sim

CERT_DIR="${TMPDIR:-/tmp}/rtw-wss-certs"
mkdir -p "$CERT_DIR"
if [[ ! -f "$CERT_DIR/cert.pem" ]]; then
  openssl req -x509 -newkey rsa:2048 -keyout "$CERT_DIR/key.pem" -out "$CERT_DIR/cert.pem" \
    -days 1 -nodes -subj "/CN=localhost" >/dev/null 2>&1
fi

cd examples/node-mock-server
npm install --silent
cd "$ROOT"

PORT=18443
MODE=echo PORT=$PORT \
  TLS_CERT="$CERT_DIR/cert.pem" TLS_KEY="$CERT_DIR/key.pem" \
  node examples/node-mock-server/server.js >/tmp/rtw-mock-wss.log 2>&1 &
MOCK_PID=$!
cleanup() { kill "$MOCK_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.5

export RTW_TLS_INSECURE=1
export RTW_RECONNECT=0
./build/rtw_sim --url "wss://127.0.0.1:${PORT}/media" --seconds 1
./build/rtw_mod_harness --url "wss://127.0.0.1:${PORT}/media" --seconds 1
echo "WSS SMOKE PASSED"
