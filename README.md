# mod_realtime_ws

**FreeSWITCH module for bidirectional realtime call audio over WebSocket — wire-compatible with [Twilio Media Streams](https://www.twilio.com/docs/voice/media-streams).**

Inspired by battle-tested [mod_audio_stream](https://github.com/amigniter/mod_audio_stream) media-bug patterns, but aimed to **surpass** it: MIT-open full duplex, Twilio `mark`/`clear` barge-in, thin mod + gateway split, multi-sink roadmap. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) § Positioning.

> Status: **core L0 + module bridge (stub harness) done**; design-review hardening applied (media-thread no WS I/O, idempotent stop, `ws://`-only). Real FS `.so` / WRITE_REPLACE / `wss://` still need `libfreeswitch-dev`. Contributions welcome.

[English](#mod_realtime_ws) · [中文](#中文简介)

---

## Why this module

| Need | What you get |
|------|----------------|
| Self-hosted telephony + Voice AI | Fork call audio out of FreeSWITCH without MRCP |
| Reuse Twilio ecosystem tools | Speak the same `connected` / `start` / `media` / `mark` / `clear` / `stop` events |
| Barge-in for agents | First-class `clear` + playout buffer flush |
| Open license | **MIT** — easy to adopt and contribute |

```text
PSTN / SIP
    │
    ▼
FreeSWITCH  ── media bug ──►  mod_realtime_ws  ── WSS (Twilio dialect) ──►  your AI / ASR / gateway
    ▲                                              │
    └──────────── play / mix / clear ◄─────────────┘
```

Existing backends written for Twilio Media Streams can connect to FreeSWITCH with little or no change (strict mulaw/8k mode). Optional extensions (L16, sample-rate negotiation, multi-sink) are documented and **capability-advertised**, not silently breaking.

---

## Features (planned)

- [x] Architecture & protocol compatibility docs
- [x] Portable L0 core (Twilio JSON, mulaw/8k, mark/clear, bounded queue)
- [x] `rtw_sim` + Node mock smoke / stress tests
- [x] FreeSWITCH module API + bridge (`rtw_bridge`, media-bug layout)
- [x] Stub harness duplex/clear self-test (`make harness`)
- [ ] Out-of-tree FreeSWITCH `.so` verified on live FS + WRITE_REPLACE
- [ ] Client mode: FS opens `wss://` to your server (TLS)
- [ ] Optional: binary L16 frames, multi-sink, WS server mode

Non-goals for v1: embedding OpenAI/Gemini/Deepgram SDKs inside the module; video; replacing RTP.

---

## Build & test

```bash
make unit                 # C unit tests
make sim                  # build rtw_sim
make harness              # FS-stub module bridge binary
./scripts/smoke_test.sh   # unit + echo + clear + mod harness
CONCURRENCY=50 SECONDS_RUN=5 ./scripts/stress_test.sh
# Real FreeSWITCH .so (needs libfreeswitch-dev):
./scripts/build-mod-realtime-ws.sh   # see docs/BUILD_FS.md
```

Requires: `gcc`, `make`, `node`/`npm` (for mock peer). FreeSWITCH headers are **not** required for core/sim/harness tests.

---

## Protocol

Compatibility target: [Twilio Media Streams WebSocket Messages](https://www.twilio.com/docs/voice/media-streams/websocket-messages).

See:

- [docs/PROTOCOL.md](docs/PROTOCOL.md) — event catalogue, audio format, compatibility levels
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — module internals, gateway pattern
- [docs/ROADMAP.md](docs/ROADMAP.md) — milestones

This project **implements a compatible dialect**. It is not affiliated with or endorsed by Twilio Inc. “Twilio Media Streams” refers to the publicly documented message model.

---

## Quick start (simulator today / FS module next)

```bash
make unit && make sim
# terminal A
MODE=echo PORT=8081 node examples/node-mock-server/server.js
# terminal B
./build/rtw_sim --url ws://127.0.0.1:8081/media --seconds 2
```

FreeSWITCH API shape (draft, skeleton registered in `src/mod/mod_realtime_ws.c`):

```text
uuid_realtime_ws <uuid> start <wss-url> <mix-type> <rate> [metadata-json]
uuid_realtime_ws <uuid> stop
uuid_realtime_ws <uuid> clear
uuid_realtime_ws <uuid> send_mark <name>
```

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Design discussion and protocol test vectors are especially valuable before C code lands.

## License

[MIT](LICENSE)

## Naming

| Item | Value |
|------|--------|
| GitHub repository | `mod_realtime_ws` |
| FreeSWITCH loadable name | `mod_realtime_ws` |
| API prefix (draft) | `uuid_realtime_ws` |

`mod_` prefix is required by FreeSWITCH module conventions.

---

## 中文简介

`mod_realtime_ws` 是面向 **FreeSWITCH** 的开源模块：把通话实时音频通过 **WebSocket** 旁路到外部系统，并支持回灌与打断。

- **借鉴** [mod_audio_stream](https://github.com/amigniter/mod_audio_stream) 的 media bug / 全双工经验  
- **超越**：MIT 开放双向、`mark`/`clear` 对齐 Twilio、薄模块 + Gateway、多 sink 路线（见架构文档）  
- **协议**：兼容 Twilio Media Streams L0  
- **当前**：core + 模块桥（stub harness 双工/`clear`）已通；真机 `.so` / WRITE_REPLACE 需 `libfreeswitch-dev` 

详细设计见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)、[docs/PROTOCOL.md](docs/PROTOCOL.md)。

仓库简介与 Topics 建议见 [.github/GITHUB_META.md](.github/GITHUB_META.md)（需在 GitHub 网页设置）。
