# Building against FreeSWITCH

Core / sim / stub harness do **not** need FreeSWITCH headers:

```bash
make unit sim harness
./scripts/smoke_test.sh          # includes mod harness
```

## Out-of-tree `.so` (real FreeSWITCH)

Requires `libfreeswitch-dev` (or a FS source/install with `freeswitch.pc`):

```bash
./scripts/build-mod-realtime-ws.sh
# or
make -f Makefile.fs
sudo make -f Makefile.fs install
```

Then in `fs_cli`:

```text
load mod_realtime_ws
uuid_realtime_ws <uuid> start ws://127.0.0.1:8081/media mono 8k {"app":"demo"}
```

See `conf/dialplan_example.lua` for Lua/ESL lifecycle (prefer over `${api(...)}` so the media bug outlives the dialplan step).

### Layout (FS-aligned)

| Path | Role |
|------|------|
| `src/mod/mod_realtime_ws.c` | `SWITCH_MODULE_*`, `uuid_realtime_ws` API, media-bug callback |
| `src/mod/mod_realtime_ws.h` | `rtw_tech_t` private (audio_stream-style) |
| `src/mod/rtw_bridge.c` | WS worker + `rtw_session` bridge |
| `src/mod/fs_stub/switch.h` | Compile/test without FS |
| `src/core/*` | Portable Twilio L0 + playout (no FS deps) |
| `Makefile.fs` | `-DHAVE_FREESWITCH` shared object |

### Still pending on real FS

- Soak-test `WRITE_REPLACE` audible inject on a live call
- Session-pool allocation for `rtw_tech_t` (today: `calloc`)
- Autoload XML / production TLS trust store (not `RTW_TLS_INSECURE`)
