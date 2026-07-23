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
| Dedicated WS I/O path | Avoid blocking the FS media thread |
| Session lifetime, thread-safe inject/shutdown, bounded memory, record interaction | **Acceptance criteria for our MIT open build**, not paid extras |
| API style `uuid_* start/stop` + metadata | Familiar to operators already using audio_stream |

#### Where mod_audio_stream falls short (our opportunity)

| Gap | Our answer |
|-----|------------|
| Community edition primarily **uni-directional**; full duplex concentrated in **commercial** builds | **MIT, full duplex + mark/clear in open core** |
| Wire protocol is **module-private**, not Twilio-shaped | **Twilio Media Streams L0 dialect** |
| No first-class public **`mark` / `clear`** | Native playout queue + `clear` flush + mark ACK |
| Vendor AI adapters creep toward the module | **Thin mod + Gateway** |
| Multi-sink / server-mode | Roadmap L2 / WS server mode |
| Observability mostly logs | Explicit metrics (queue depth, drops, clear latency, mark RTT) |

#### Surpass checklist (must be true before calling v1 “done”)

| # | Requirement | Status |
|---|-------------|--------|
| 1 | **Open duplex** + `clear` without commercial binary | Core + stub harness ✅; live FS WRITE_REPLACE ⏳ |
| 2 | **Twilio L0 interop** with documented deltas only | L0 events ✅; see PROTOCOL deltas |
| 3 | **FS-correct lifecycle** (hangup/transfer safe, no UAF) | Idempotent `cleanup_started` ✅; live FS soak ⏳ |
| 4 | **Thread contract**: bug = enqueue only; WS on worker | Enforced in `rtw_bridge` ✅ |
| 5 | **Bounded memory** drop-oldest | Outbound queue ✅ |
| 6 | **Record policy** flag for injected audio | Not started ⏳ |
| 7 | **Measurable barge-in** clear→audible stop P95 | Counter only; latency SLO ⏳ |
| 8 | **MIT** for production duplex path | ✅ |

Reference: [amigniter/mod_audio_stream](https://github.com/amigniter/mod_audio_stream).

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
                                                   │ WS (wss later)
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
| **mod_realtime_ws** | Session lifecycle, media bug, WS I/O, encode/decode, playout, `mark`/`clear` | LLM prompts, vendor SDKs, IVR logic |
| **Gateway / bot** | Vendor adapters, dialogue, VAD, generation cancel | FreeSWITCH channel internals |
| **Dialplan / Lua / ESL** | When to `start`/`stop`, metadata | Audio framing |

## 4. Connection modes (roadmap)

| Mode | Description | v1 |
|------|-------------|----|
| **Client `ws://`** | Module dials out (dev/lab) | Required now |
| **Client `wss://`** | TLS dial-out | Required before production |
| **Server** | Peer connects in | Later |
| **Strict Twilio** | mulaw/8000 + JSON base64 | Required |
| **Extended** | L16, negotiated rates, multi-sink | Optional flags |

## 5. Internal pipeline

### Uplink (call → WebSocket)

```text
RTP read → media bug → (optional mix) → resample → frame pack (~20ms)
        → encode (mulaw + base64) → outbound queue → WS worker write
```

### Downlink (WebSocket → call)

```text
WS worker read → parse JSON → decode → playout ring
              → media bug WRITE_REPLACE → RTP write
```

### Barge-in

1. Peer (or local API) issues `clear`.
2. Module empties playout **immediately**.
3. Pending `mark` names are ACK'd (Twilio semantics).
4. Gateway must also cancel TTS/S2S (application responsibility).

## 6. API

```text
uuid_realtime_ws <uuid> start <ws-url> <mix-type> <rate> [{metadata-json}]
uuid_realtime_ws <uuid> stop
uuid_realtime_ws <uuid> pause | resume
uuid_realtime_ws <uuid> clear
uuid_realtime_ws <uuid> send_mark <name>
```

- `mix-type`: `mono` | `mixed` | `stereo`
- Metadata: optional JSON object; parser takes from the first `{` so spaces inside JSON are allowed.
- `send_mark`: **local/test** helper — queues a peer-style mark against playout (not an FS→peer wire event).
- **Lifecycle:** prefer Lua/ESL over ephemeral `${api(...)}` so the bug outlives the dialplan step.

## 7. Threading & safety (MUST)

| Context | Allowed | Forbidden |
|---------|---------|-----------|
| Media-bug READ/WRITE | `trylock` → enqueue / playout read | Any `send`/`recv`/connect |
| WS worker | `poll`, `flush_outbound`, handle peer JSON | Blocking forever without `close_requested` check |
| API/ESL (`clear`/`stop`) | Lock + session mutate; flush OK | Holding channel locks across network |

Additional rules:

- `rtw_bridge_stop` is **idempotent** (`cleanup_started`) so `do_stop` and `SWITCH_ABC_TYPE_CLOSE` cannot double-free.
- Hangup/transfer/park must detach cleanly.
- Bounded outbound queue: drop-oldest + `drops` counter.
- `record_session` interaction: explicit future config flag.

## 8. Observability

Expose (log and/or stats):

- uplink/downlink frames, queue depth, drops
- WS connect/fail/reconnect
- `clear` latency (command → buffer empty)
- mark RTT

## 9. Comparison

| Approach | Pros | Cons |
|----------|------|------|
| **MRCP** | Standard ASR/TTS | Poor LLM/S2S fit |
| **mod_audio_stream** | Proven media-bug fork; mature duplex commercially | Private wire; duplex gated; not Twilio-shaped |
| **mod_realtime_ws** | Twilio L0, open duplex, mark/clear, MIT, gateway split | Live FS WRITE_REPLACE + `wss://` still pending |

## 10. Repository layout

```text
mod_realtime_ws/
├── README.md, LICENSE, CONTRIBUTING.md, Makefile, Makefile.fs
├── docs/           ARCHITECTURE, PROTOCOL, ROADMAP, BUILD_FS
├── src/core/       portable Twilio L0 (no FS deps)
├── src/mod/        FS module + rtw_bridge + fs_stub + harness
├── src/sim/        rtw_sim producer
├── conf/           dialplan example
├── tests/          unit + protocol fixtures
├── examples/       node-mock-server
└── scripts/        smoke, stress, harness, build-mod
```

## 11. Security notes

- Prefer `wss://` in production — **not implemented yet**; validator currently accepts **`ws://` only** so TLS is not silently fake.
- Optional shared-secret / JWT in URL or metadata (gateway validates).
- Do not log raw audio or credentials.
- Twilio `X-Twilio-Signature` applies to Twilio-originated streams; FS-as-producer needs our own auth story.

## 12. Design review (2026-07-23) — findings & fixes

Review focused on gaps between written architecture and the shipping bridge.

| Finding | Severity | Fix |
|---------|----------|-----|
| Media path called `flush_outbound` (WS send on media thread) — violated §7 | High | Read/write paths enqueue/read only; worker owns send |
| `do_stop` + `CLOSE` both called `rtw_bridge_stop` → double-free risk on real FS | High | Idempotent `cleanup_started`; CLOSE owns teardown after `bug_remove` |
| `rtw_validate_ws_uri` accepted `wss://` but client is `ws://` only | High | Reject `wss://` until TLS lands |
| `callSid` used raw UUID (not Twilio-like `CA`+hex) | Med | Emit `CA` + 32 hex; `streamSid` = `MZ` + 32 hex |
| Metadata JSON broken by space-splitting | Med | Slice from first `{` before tokenizing |
| `pause` toggled flags without mutex | Low | Lock around pause |
| `send_mark` name unsanitized (JSON injection) | Low | Allowlist `[A-Za-z0-9_.-]` |
| Architecture layout / checklist stale | Doc | Updated §§1,4,7,10 + this table |

### Still open (intentional next work)

1. Real FS `WRITE_REPLACE` frame publish  
2. `wss://` (TLS) client  
3. Reconnect without killing the call  
4. Record-session policy flag  
5. Clear-latency histogram / SLO  
6. Pool-allocate `rtw_tech_t` from session memory on real FS (today: `calloc`)
