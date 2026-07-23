# Roadmap

Dates are intentionally omitted; order is by dependency risk.

## Phase 0 — Docs & protocol (current)

- [x] Repository naming (`mod_realtime_ws`)
- [x] MIT license
- [x] README, architecture, protocol, contributing
- [ ] Protocol JSON fixtures
- [ ] GitHub About description + topics (see `.github/GITHUB_META.md`)

## Phase 1 — MVP media bridge (L0)

- [ ] Out-of-tree build against FreeSWITCH 1.10.x headers
- [ ] Client WSS connect
- [ ] Uplink: media bug → mulaw/8k → Twilio `media` JSON
- [ ] Downlink: peer `media` → playout → channel
- [ ] `clear` + basic `mark`
- [ ] `start` / `stop` API + Lua example dialplan
- [ ] Smoke test with a Node echo / clear demo

## Phase 2 — Production hardening

- [ ] Bounded queues, backpressure metrics
- [ ] Reconnect policy (optional) without killing the call
- [ ] Auth hooks (token/header)
- [ ] Record-session interaction flags
- [ ] Load test (≥ concurrent stream target TBD)

## Phase 3 — Extensions (L1+)

- [ ] Binary L16 option
- [ ] Negotiated sample rates (16k/24k) with capability advertising
- [ ] Multi-sink (L2)
- [ ] WS server mode

## Explicit non-goals (near term)

- Shipping vendor AI SDKs inside the module
- Full TwiML / Twilio REST control plane
- Video / screen share
