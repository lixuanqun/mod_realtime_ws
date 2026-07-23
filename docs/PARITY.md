# Parity: mod_audio_stream vs mod_realtime_ws

Comparison against the public [amigniter/mod_audio_stream](https://github.com/amigniter/mod_audio_stream) community edition (ideas only; no proprietary code).

| Area | mod_audio_stream (public) | mod_realtime_ws |
|------|---------------------------|-----------------|
| License | Mostly open; **full duplex / auto-play often commercial** | **MIT**, duplex + mark/clear in open core |
| Wire protocol | Module-private (L16 / ad-hoc JSON play) | **Twilio Media Streams L0** JSON (mulaw/8k) |
| Barge-in | Not first-class Twilio `clear`/`mark` | Native playout + `clear` + mark ACK |
| API | `uuid_audio_stream start/stop/pause/...` | `uuid_realtime_ws` (same operator shape) |
| Media tap | Media bug | Media bug + `WRITE_REPLACE` inject |
| WS client | libwsc-class (C++) | Minimal C client (`ws://` + OpenSSL `wss://`) |
| Auth | URL / metadata text | Metadata `authorization` / `ws_headers` → handshake headers |
| Reconnect | Implementation-dependent | Backoff + `rehandshake` (`RTW_RECONNECT=0` to disable) |
| Observability | Logs / events | `status` API: queues, clears, clear latency |
| Multi-sink | Not a goal | Roadmap L2 |
| Vendor AI in module | Tend to creep in | Explicit **non-goal** (gateway owns adapters) |

## Surpass checklist status

See [ARCHITECTURE.md](./ARCHITECTURE.md) §1 table. Live FreeSWITCH soak remains the largest open gate.

## Operator migration sketch

```text
uuid_audio_stream <uuid> start wss://host/path mono 8k <text>
→
uuid_realtime_ws <uuid> start wss://host/path mono 8k {"app":"x","authorization":"Bearer …"}
```

Peers must speak Twilio Media Streams events (`connected`/`start`/`media`/`mark`/`clear`/`stop`), not audio_stream’s private frames.
