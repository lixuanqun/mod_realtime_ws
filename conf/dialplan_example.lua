-- Example Lua usage once mod_realtime_ws.so is built against FreeSWITCH.
-- Prefer Lua/ESL over ${api(...)} so the media bug outlives the dialplan step.
--
-- local api = freeswitch.API()
-- local uuid = session:get_uuid()
-- api:execute("uuid_realtime_ws", uuid .. " start ws://127.0.0.1:8081/media mono 8k {\"app\":\"demo\"}")
-- session:sleep(60000)
-- api:execute("uuid_realtime_ws", uuid .. " stop")

freeswitch.consoleLog("INFO", "mod_realtime_ws dialplan example loaded (see comments)\n")
