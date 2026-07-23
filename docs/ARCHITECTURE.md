# Architecture

## 1. Goals

`mod_realtime_ws` is an **out-of-tree FreeSWITCH module** that:

1. Attaches a **media bug** to a channel and forks realtime audio to a WebSocket peer.
2. Optionally **injects** audio from that peer back into the call (full duplex).
3. Exposes a **Twilio Media Streams–compatible** WebSocket message dialect so existing AI bridges can reconnect from Twilio to self-hosted FreeSWITCH with minimal change.
4. Keeps **vendor AI protocols** (OpenAI Realtime, Gemini Live, Alibaba ISI, Volcengine, etc.) out of the module — those belong in a gateway process.

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
| **mod_audio_stream** (existing) | Proven media fork | Not Twilio wire-compatible; licensing varies by edition |
| **mod_realtime_ws** (this) | Twilio dialect + MIT + FS-native | Must implement carefully; early stage |

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
