# Contributing to mod_realtime_ws

Thanks for helping build an open, Twilio Media Streams–compatible FreeSWITCH audio bridge.

## Before code exists

Highest-value contributions right now:

1. **Protocol review** — gaps vs [Twilio WebSocket Messages](https://www.twilio.com/docs/voice/media-streams/websocket-messages)
2. **Fixtures** — JSON golden files under a future `tests/protocol/`
3. **Design feedback** — threading, barge-in, FreeSWITCH lifecycle
4. **Docs** — clarity, diagrams, bilingual polish

Open an issue before large design changes.

## Development (once `src/` lands)

- Keep the module **out-of-tree** (do not require forking FreeSWITCH core).
- Prefer small PRs: protocol / media / build system separated when possible.
- No vendor API keys in the repo.
- Do not add heavy runtime dependencies without discussion (WS client choice will be explicit in Phase 1).

## Commit style

- Imperative subject, ≤ ~72 chars: `docs: clarify clear semantics`
- Explain *why* in the body when non-obvious.

## Code of interaction

- Be precise and kind in reviews.
- Assume good intent; cite Twilio docs or RFCs when debating wire format.

## License

By contributing, you agree your contributions are licensed under the MIT License (`LICENSE`).
