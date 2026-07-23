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
- [ ] Out-of-tree `.so` against real FreeSWITCH 1.10.x headers + media bug wiring
- [ ] Lua dialplan live on FS (example drafted in `conf/dialplan_example.lua`)

## Phase 2 — Production hardening

- [x] Bounded queues with drop-oldest (core)
- [ ] Reconnect policy without killing the call
- [ ] Auth hooks (token/header)
- [ ] Record-session interaction flags
- [x] Load/stress harness (expand targets as needed)

## Phase 3 — Extensions (L1+)

- [ ] Binary L16 option
- [ ] Negotiated sample rates (16k/24k) with capability advertising
- [ ] Multi-sink (L2)
- [ ] WS server mode / `wss://` (TLS)

## Explicit non-goals (near term)

- Shipping vendor AI SDKs inside the module
- Full TwiML / Twilio REST control plane
- Video / screen share

## How to verify

```bash
make unit          # codec + protocol + playout/session + fixtures
make mod-stub      # compile-check FreeSWITCH module API skeleton
./scripts/smoke_test.sh
CONCURRENCY=50 SECONDS_RUN=5 ./scripts/stress_test.sh
```
