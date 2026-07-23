-- Example Lua: attach mod_realtime_ws for the life of the call.
-- Prefer Lua/ESL over ${api(...)} so the media bug outlives the dialplan step.
--
-- Usage (answer first, then start stream):
--   session:answer()
--   local api = freeswitch.API()
--   local uuid = session:get_uuid()
--   local meta = '{"app":"demo","authorization":"Bearer lab-token"}'
--   api:execute("uuid_realtime_ws",
--     uuid .. " start ws://127.0.0.1:8081/media mono 8k " .. meta)
--   session:sleep(60000)
--   api:execute("uuid_realtime_ws", uuid .. " status")
--   api:execute("uuid_realtime_ws", uuid .. " clear")  -- barge-in
--   api:execute("uuid_realtime_ws", uuid .. " stop")

local api = freeswitch.API()

if session then
  local uuid = session:get_uuid()
  freeswitch.consoleLog("INFO", "mod_realtime_ws example uuid=" .. uuid .. "\n")
  -- Uncomment for a live lab:
  -- session:answer()
  -- api:execute("uuid_realtime_ws", uuid .. " start ws://127.0.0.1:8081/media mono 8k {\"app\":\"lua-demo\"}")
  -- session:streamFile("silence_stream://60000")
  -- api:execute("uuid_realtime_ws", uuid .. " stop")
else
  freeswitch.consoleLog("INFO", "mod_realtime_ws dialplan example loaded (no session)\n")
end
