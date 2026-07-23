# mod_realtime_ws

**FreeSWITCH module for bidirectional realtime call audio over WebSocket — wire-compatible with [Twilio Media Streams](https://www.twilio.com/docs/voice/media-streams).**

> Status: **design / documentation first**. Implementation has not started. Contributions welcome on protocol fixtures, docs, and design review.

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
- [ ] Out-of-tree FreeSWITCH module (`mod_realtime_ws`)
- [ ] Client mode: FS opens `wss://` to your server
- [ ] Unidirectional + bidirectional audio
- [ ] Twilio-compatible JSON events + base64 mulaw/8000
- [ ] `mark` / `clear` (barge-in)
- [ ] Session-safe media bug lifecycle (Lua/ESL friendly)
- [ ] Bounded queues, metrics hooks
- [ ] Optional: binary L16 frames, multi-sink, WS server mode

Non-goals for v1: embedding OpenAI/Gemini/Deepgram SDKs inside the module; video; replacing RTP.

---

## Protocol

Compatibility target: [Twilio Media Streams WebSocket Messages](https://www.twilio.com/docs/voice/media-streams/websocket-messages).

See:

- [docs/PROTOCOL.md](docs/PROTOCOL.md) — event catalogue, audio format, compatibility levels
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — module internals, gateway pattern
- [docs/ROADMAP.md](docs/ROADMAP.md) — milestones

This project **implements a compatible dialect**. It is not affiliated with or endorsed by Twilio Inc. “Twilio Media Streams” refers to the publicly documented message model.

---

## Quick start (when code lands)

```bash
# placeholder — build instructions will land with Phase 1
make
# load in FreeSWITCH
load mod_realtime_ws
uuid_realtime_ws <uuid> start wss://your.example/stream mono 8k
```

API shape (draft):

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

`mod_realtime_ws` 是面向 **FreeSWITCH** 的开源模块（规划中）：把通话实时音频通过 **WebSocket** 旁路到外部系统，并支持回灌与打断。

- **协议**：兼容 [Twilio Media Streams](https://www.twilio.com/docs/voice/media-streams) 事件模型（`start` / `media` / `mark` / `clear` 等）
- **场景**：AI 语音坐席、实时 ASR、质检旁路、对话打断（barge-in）
- **许可**：MIT
- **当前阶段**：先文档与协议，再写代码

详细设计见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)、[docs/PROTOCOL.md](docs/PROTOCOL.md)。

仓库简介与 Topics 建议见 [.github/GITHUB_META.md](.github/GITHUB_META.md)（需在 GitHub 网页设置）。
