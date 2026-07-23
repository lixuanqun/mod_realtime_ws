# Roadmap

Dates are intentionally omitted; order is by dependency risk.

## Phase 0 — Docs & protocol

- [x] Repository naming (`mod_realtime_ws`)
- [x] MIT license
- [x] README, architecture, protocol, contributing
- [x] Protocol JSON fixtures (`tests/protocol/fixtures/`)
- [ ] GitHub About description + topics (see `.github/GITHUB_META.md`)

## Phase 1 — MVP media bridge (L0)

- [x] Portable core library (mulaw/base64/Twilio JSON/playout/queue/session)
- [x] Unit tests (`make unit`)
- [x] Minimal `ws://` client + `rtw_sim` producer
- [x] Node mock peer + smoke (`./scripts/smoke_test.sh`)
- [x] Clear/mark barge-in smoke
- [x] Concurrent stress (`./scripts/stress_test.sh`) — verified 20×3s and 50×5s
- [x] FS module skeleton + stub compile (`make mod-stub`)
- [x] Architecture: explicit **learn-from / surpass mod_audio_stream** checklist
- [x] Study mod_audio_stream public patterns — MIT bridge + media-bug layout (`rtw_bridge`, `rtw_tech_t`)
- [x] Stub harness self-test (`make harness` / `./scripts/mod_harness_test.sh`) — duplex + clear without libfreeswitch
- [x] Out-of-tree build recipe (`Makefile.fs`, `scripts/build-mod-realtime-ws.sh`)
- [x] Design review fixes: media-thread no WS I/O, idempotent stop, CA/MZ SIDs, metadata `{…}` parse
- [x] `wss://` TLS client (OpenSSL) + `./scripts/wss_smoke_test.sh`
- [x] WRITE_REPLACE inject helper (`rtw_bridge_apply_write_frame`) wired in HAVE_FREESWITCH callback
- [x] Basic WS reconnect + `rtw_session_rehandshake` (opt-out `RTW_RECONNECT=0`)
- [x] Auth hooks via metadata (`authorization` / `ws_headers` → WS handshake)
- [x] Clear-latency sampling + `uuid_realtime_ws status`
- [x] Side-by-side parity notes (`docs/PARITY.md`)
- [ ] Link/run `.so` on real FreeSWITCH 1.10.x soak + verify WRITE_REPLACE audible path
- [x] Lua dialplan example updated (`conf/dialplan_example.lua`)

## Phase 2 — Production hardening

- [x] Bounded queues with drop-oldest (core)
- [x] Reconnect policy without killing the call (basic backoff; expand jitter/metrics later)
- [x] Auth hooks (token/header via metadata)
- [x] Record-session interaction flag (`record_injected`, default on)
- [x] Load/stress harness (expand targets as needed)
- [x] Clear-latency histogram buckets + status API (P95 export still optional)

## Phase 3 — Extensions (L1+)

- [ ] Binary L16 option
- [ ] Negotiated sample rates (16k/24k) with capability advertising
- [ ] Multi-sink (L2)
- [ ] WS server mode

## Explicit non-goals (near term)

- Shipping vendor AI SDKs inside the module
- Full TwiML / Twilio REST control plane
- Video / screen share

## How to verify

```bash
make unit          # codec + protocol + playout/session + fixtures
make harness       # FS-stub module bridge binary
./scripts/smoke_test.sh              # unit + sim + mod harness + wss
CONCURRENCY=50 SECONDS_RUN=5 ./scripts/stress_test.sh
# With libfreeswitch-dev:
./scripts/build-mod-realtime-ws.sh   # see docs/BUILD_FS.md
```
