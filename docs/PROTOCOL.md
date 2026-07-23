# Protocol: Twilio Media Streams compatibility

## Status

**Compatibility target:** publicly documented Twilio Media Streams WebSocket messages  
Reference: https://www.twilio.com/docs/voice/media-streams/websocket-messages

This document defines what `mod_realtime_ws` will send and accept. It is **not** an official Twilio specification and is **not affiliated with Twilio**.

## Compatibility levels

| Level | Name | Wire behavior |
|-------|------|----------------|
| **L0** | Strict | Same event names/shapes as Twilio bidirectional streams; audio `audio/x-mulaw` @ 8000 Hz mono, base64 in JSON `media` events |
| **L1** | Extended | L0 plus optional binary L16 frames and/or negotiated sample rates, advertised in `connected`/`start` |
| **L2** | Multi-sink | Multiple concurrent streams per call (beyond Twilio’s bidirectional “one stream per call” limit) |

**v1 ships L0.** L1/L2 are gated by capability flags so naive Twilio consumers remain safe.

## Roles

When FreeSWITCH runs `mod_realtime_ws` in **client mode**:

- Module ≈ **Twilio** (opens WSS, sends `connected`/`start`/`media`/`stop`, accepts `media`/`mark`/`clear`)
- Your server ≈ **Twilio customer media server**

## Events: module → peer (producer)

### `connected`

First message after WebSocket is up.

```json
{
  "event": "connected",
  "protocol": "Call",
  "version": "1.0.0"
}
```

Optional L1 fields (ignored by strict consumers):

```json
{
  "event": "connected",
  "protocol": "Call",
  "version": "1.0.0",
  "mod_realtime_ws": {
    "compat": "twilio-media-streams",
    "level": "L0",
    "extensions": []
  }
}
```

### `start`

Once per stream. Peer **must** store `streamSid` for all replies.

```json
{
  "event": "start",
  "sequenceNumber": "1",
  "start": {
    "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "accountSid": "ACxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "callSid": "CAxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "tracks": ["inbound"],
    "customParameters": {},
    "mediaFormat": {
      "encoding": "audio/x-mulaw",
      "sampleRate": 8000,
      "channels": 1
    }
  },
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
}
```

Mapping from FreeSWITCH:

| Field | Source |
|-------|--------|
| `streamSid` | Generated stream id (Twilio-like shape preferred) |
| `callSid` | Channel UUID (or prefixed id) |
| `accountSid` | Configurable profile / placeholder |
| `customParameters` | Metadata JSON from `start` API |

### `media`

```json
{
  "event": "media",
  "sequenceNumber": "2",
  "media": {
    "track": "inbound",
    "chunk": "1",
    "timestamp": "20",
    "payload": "<base64 mulaw bytes>"
  },
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
}
```

**L0 packing recommendation:** ~20 ms frames → **160 bytes** mulaw before base64 (same as common Twilio practice).

### `dtmf` (optional)

```json
{
  "event": "dtmf",
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "sequenceNumber": "3",
  "dtmf": { "track": "inbound_track", "digit": "1" }
}
```

### `mark` (echo)

When peer’s marked audio has finished playing (or was cleared):

```json
{
  "event": "mark",
  "sequenceNumber": "10",
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "mark": { "name": "utterance-42-end" }
}
```

### `stop`

```json
{
  "event": "stop",
  "sequenceNumber": "99",
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "stop": {
    "accountSid": "ACxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "callSid": "CAxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  }
}
```

## Events: peer → module (consumer commands)

### `media` (playback)

```json
{
  "event": "media",
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "media": {
    "payload": "<base64 mulaw/8000>"
  }
}
```

Queued FIFO. Wrong `streamSid` → drop.

### `mark`

```json
{
  "event": "mark",
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "mark": { "name": "utterance-42-end" }
}
```

### `clear` (barge-in)

```json
{
  "event": "clear",
  "streamSid": "MZxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
}
```

Flushes playout queue; outstanding marks are acknowledged per Twilio semantics.

## Unidirectional vs bidirectional

| Mode | Module sends | Module accepts |
|------|--------------|----------------|
| Uni | `connected`/`start`/`media`/`stop` | (optional) ignore playback |
| Bi | same + mark echoes | `media` / `mark` / `clear` |

Twilio bidirectional streams only fork **inbound** toward the app. L0 bidirectional mode follows that default (`tracks: ["inbound"]`). Forking mixed/outbound is an **extension** for unidirectional analytics use cases.

## Documented deltas vs Twilio (L0)

Consumers written for Twilio Media Streams should only need to account for:

| Topic | Twilio | mod_realtime_ws |
|-------|--------|-----------------|
| Transport | `wss://` from Twilio | **`ws://` only today**; `wss://` rejected until TLS lands |
| `streamSid` | Twilio-assigned `MZ…` | Generated `MZ` + 32 hex from channel UUID |
| `callSid` | Twilio `CA…` | `CA` + 32 hex derived from channel UUID |
| `accountSid` | Real account | Configurable placeholder (`ACmodrealtimews…` default) |
| `connected` extras | — | Optional `mod_realtime_ws` capability object (ignored by strict parsers) |
| Auth | Twilio signatures | Gateway-defined (URL token / metadata); no Twilio signature |

## Test vectors

1. Golden JSON samples for each event (`tests/protocol/fixtures/`).
2. Interop: Node consumer against producer (`examples/node-mock-server`, `rtw_sim`, harness).
3. Clear mid-playback: assert silence and mark flush.
4. Invalid `streamSid` media: assert no playout.
5. URI validation: `wss://` rejected; `ws://` accepted.

## License note on “protocol”

- **Message formats described in Twilio’s public docs** are used for **compatibility**.
- **This repository’s code, docs, and fixtures** are licensed under **MIT**.
- Implementing a compatible dialect does **not** grant rights to Twilio trademarks; use “Twilio Media Streams–compatible” wording, not “official Twilio”.
