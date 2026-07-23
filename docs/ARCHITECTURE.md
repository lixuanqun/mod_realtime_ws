# Architecture

## 1. Goals

`mod_realtime_ws` is an **out-of-tree FreeSWITCH module** that:

1. Attaches a **media bug** to a channel and forks realtime audio to a WebSocket peer.
2. Optionally **injects** audio from that peer back into the call (full duplex).
3. Exposes a **Twilio Media Streams–compatible** WebSocket message dialect so existing AI bridges can reconnect from Twilio to self-hosted FreeSWITCH with minimal change.
4. Keeps **vendor AI protocols** (OpenAI Realtime, Gemini Live, Alibaba ISI, Volcengine, etc.) out of the module — those belong in a gateway process.

### Positioning vs [mod_audio_stream](https://github.com/amigniter/mod_audio_stream)

**Learn from it. Do not clone it. Surpass it.**

`mod_audio_stream` is the most battle-tested FS→WebSocket audio fork in the wild. We treat it as the **engineering baseline** for media-bug lifecycle, duplex playback, and concurrency hardness — then win on **protocol openness, license, barge-in semantics, and ecosystem fit**.

#### What we deliberately reuse (ideas / patterns, not code)

| Pattern from mod_audio_stream | Why it matters |
|-------------------------------|----------------|
| Media bug as the tap point | Correct place to fork RTP without replacing the SIP path |
| Uplink continuous while downlink plays independently | True full-duplex Voice Agent |
| Resample at the module edge | External peers see a stable rate (8k/16k/…) |
| Dedicated WS I/O path (e.g. libwsc-class lightweight client) | Avoid blocking the FS media thread |
| Hard problems called out for commercial tier: session lifetime, thread-safe inject/shutdown, bounded memory, record interaction | These are **acceptance criteria for our MIT open build**, not paid extras |
| API style `uuid_* start/stop` + metadata | Familiar to operators already using audio_stream |

#### Where mod_audio_stream falls short (our opportunity)

| Gap | Our answer |
|-----|------------|
| Community edition primarily **uni-directional**; full duplex / auto-playback concentrated in **commercial** builds (channel caps) | **MIT, full duplex + mark/clear in open core** — no 10-channel tax for the capability itself |
| Wire protocol is **module-private** (L16 binary / ad-hoc JSON playback), not Twilio-shaped | **Twilio Media Streams L0 dialect** so existing bots/bridges port with minimal change |
| No first-class public **`mark` / `clear`** semantics aligned with Twilio barge-in | Native playout queue + `clear` flush + mark ACK (already in `rtw_playout` / `rtw_session`) |
| Bidirectional reliability historically uneven; many tutorials fall back to ESL `uuid_broadcast` | Single WS path for up+down; clear latency is a measured SLO |
| Vendor AI adapters tend to creep toward the module | **Thin mod + Gateway** — module never embeds OpenAI/Gemini/阿里云 SDKs |
| Multi-sink / analytics+agent on one call | Roadmap **L2 multi-sink** (beyond Twilio’s one bi-di stream/call limit) |
| Server-mode / NAT-friendly listen | Roadmap **WS server mode** in addition to client dial-out |
| Observability mostly logs | Explicit metrics: queue depth, drops, clear latency, mark RTT |

#### Surpass checklist (must be true before calling v1 “done”)

1. **Open duplex**: start→uplink media + downlink media + `clear` without commercial binary.
2. **Twilio L0 interop**: a consumer written for Twilio Media Streams can talk to FS with documented deltas only.
3. **FS-correct lifecycle**: hangup/transfer/park safe; no use-after-free; media bug not tied to ephemeral `${api(...)}`.
4. **Thread contract**: bug callback = enqueue only; WS/resample on workers.
5. **Bounded memory**: drop-oldest under backpressure; documented limits.
6. **Record policy**: explicit flag whether injected audio enters `record_session`.
7. **Measurable barge-in**: `clear` → audible stop within target P95 (see observability).
8. **License clarity**: MIT for the whole shipping module path used in production duplex.

Reference: [amigniter/mod_audio_stream](https://github.com/amigniter/mod_audio_stream), [v1.0.3 bi-directional notes](https://github.com/amigniter/mod_audio_stream/releases/tag/v1.0.3).

## 2. High-level diagram

```text
┌─────────────┐     SIP/RTP      ┌──────────────────────────────────────┐
│ Caller/Caldi│◄────────────────►│            FreeSWITCH                │
└─────────────┘                  │  ┌────────────────────────────────┐  │
                                 │  │        mod_realtime_ws         │  │
                                 │  │  • media bug capture           │  │
                                 │  │  • resample / mix              │  │
                                 │  │  • playout ring + clear        │  │
                                 │  │  • Twilio dialect serializer   │  │
                                 │  └──────────────┬─────────────────┘  │
                                 └─────────────────┼────────────────────┘
                                                   │ WSS
                                                   ▼
                                 ┌──────────────────────────────────────┐
                                 │     Your app / Media Gateway         │
                                 │  • speak Twilio dialect              │
                                 │  • adapt to ASR / LLM / S2S vendors  │
                                 │  • barge-in policy (VAD + clear)     │
                                 └──────────────────────────────────────┘
```

## 3. Component responsibilities

| Layer | Owns | Does not own |
|-------|------|--------------|
| **mod_realtime_ws** | Session lifecycle, media bug, WS I/O, encode/decode for wire format, playout buffer, `mark`/`clear` | LLM prompts, vendor SDKs, business IVR logic |
| **Gateway / bot** | Vendor adapters, dialogue, VAD strategy, generation cancel | FreeSWITCH channel internals |
| **Dialplan / Lua / ESL** | When to `start`/`stop` stream, metadata | Audio framing |

## 4. Connection modes (roadmap)

| Mode | Description | v1 |
|------|-------------|----|
| **Client** | Module dials out to `wss://` | Required |
| **Server** | External peer connects into FS (or local sidecar) | Later |
| **Strict Twilio** | mulaw/8000 + JSON base64 media | Required |
| **Extended** | L16 binary, negotiated rates, multi-sink | Optional flags |

## 5. Internal pipeline

### Uplink (call → WebSocket)

```text
RTP read → media bug → (optional mix) → resample → frame pack (~20ms)
        → encode (mulaw + base64 in strict mode) → send queue → WS write
```

### Downlink (WebSocket → call)

```text
WS read → parse JSON control / media
       → decode payload → playout ring buffer
       → media bug write (replace or soft-mix)
       → RTP write
```

### Barge-in

1. Peer sends `{"event":"clear","streamSid":"..."}`.
2. Module empties playout buffer **immediately**.
3. Pending `mark` names are echoed back as completed/cleared (Twilio semantics).
4. Gateway must also cancel upstream TTS/S2S generation (application responsibility).

## 6. API sketch (draft, not frozen)

```text
uuid_realtime_ws <uuid> start <wss-url> <mix-type> <rate> [metadata-json]
uuid_realtime_ws <uuid> stop [bugname]
uuid_realtime_ws <uuid> pause | resume
uuid_realtime_ws <uuid> clear
uuid_realtime_ws <uuid> send_mark <name>
```

`mix-type`: `mono` | `mixed` | `stereo` (Twilio bidirectional typically exposes inbound only; we still support fork mix types for unidirectional / extended modes).

**Lifecycle rule:** do not start the bug via short-lived synchronous `${api(...)}` expansion alone; prefer Lua or ESL so the bug outlives the dialplan step (known FreeSWITCH pitfall).

## 7. Threading & safety (design constraints)

- Media bug callback: **enqueue only** (no network I/O).
- Dedicated worker(s) for WS read/write and resample.
- Bounded queues with drop-oldest + metrics under backpressure.
- Channel hangup / transfer / park must detach cleanly (no use-after-free).
- Interaction with `record_session` / `uuid_record` must be explicit (config flag: whether injected audio is recorded).

## 8. Observability

Expose counters/histograms (log and/or stats interface):

- uplink/downlink frame rates, queue depth, drop counts
- WS connect/fail/reconnect
- `clear` latency (command → buffer empty)
- mark round-trip

## 9. Comparison

| Approach | Pros | Cons |
|----------|------|------|
| **MRCP** | Standard ASR/TTS control | Poor fit for LLM/S2S; heavy SIP+RTP stack |
| **mod_audio_stream** | Proven media-bug fork; mature duplex in commercial builds | Private wire format; duplex/gated features; not Twilio-shaped |
| **mod_realtime_ws** (this) | Same media-bug foundation **plus** Twilio L0, open duplex, mark/clear, MIT, gateway split | Must still complete real FS `.so` wiring to match audio_stream’s production battle scars |

**Rule of thumb:** if `mod_audio_stream` solved a hard FS problem (lifetime, inject races, record sync), we study that class of fix — then implement it openly and attach Twilio-compatible control semantics on top.

## 10. Repository layout (target)

```text
mod_realtime_ws/
├── README.md
├── LICENSE                 # MIT
├── CONTRIBUTING.md
├── docs/
│   ├── ARCHITECTURE.md     # this file
│   ├── PROTOCOL.md
│   └── ROADMAP.md
├── src/                    # C module (later)
├── conf/                   # sample dialplan / autoload (later)
├── tests/                  # protocol fixtures (later)
└── examples/               # Node/Python consumer stubs (later)
```

## 11. Security notes

- Prefer `wss://` in production.
- Optional shared-secret or JWT in WS URL / first metadata (gateway validates).
- Do not log raw audio or credentials.
- Document that Twilio’s `X-Twilio-Signature` applies to **Twilio-originated** streams; when FS is the producer, use our own auth story.
