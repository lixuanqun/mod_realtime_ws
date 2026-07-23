#!/usr/bin/env node
/**
 * Twilio Media Streams–compatible mock peer for mod_realtime_ws / rtw_sim.
 * Modes:
 *   echo  — bounce media back (and optionally send mark)
 *   clear — after N media frames from producer, send media+mark then clear
 */
const http = require('http');
const { WebSocketServer } = require('ws');

const port = parseInt(process.env.PORT || '8081', 10);
const mode = process.env.MODE || 'echo'; // echo | clear
const clearAfter = parseInt(process.env.CLEAR_AFTER || '10', 10);

const server = http.createServer((_req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('mod_realtime_ws mock\n');
});

const wss = new WebSocketServer({ server, path: '/media' });

wss.on('connection', (ws) => {
  let streamSid = null;
  let mediaFromProducer = 0;
  let clearSent = false;

  ws.on('message', (data, isBinary) => {
    if (isBinary) return;
    let msg;
    try {
      msg = JSON.parse(data.toString());
    } catch {
      return;
    }
    if (msg.event === 'start') {
      streamSid = msg.streamSid || (msg.start && msg.start.streamSid);
      console.error(`[mock] start streamSid=${streamSid}`);
    } else if (msg.event === 'media') {
      mediaFromProducer += 1;
      if (mode === 'echo' && streamSid) {
        ws.send(
          JSON.stringify({
            event: 'media',
            streamSid,
            media: { payload: msg.media && msg.media.payload },
          })
        );
      }
      if (mode === 'clear' && !clearSent && mediaFromProducer >= clearAfter && streamSid) {
        clearSent = true;
        const payload = msg.media && msg.media.payload;
        ws.send(JSON.stringify({ event: 'media', streamSid, media: { payload } }));
        ws.send(JSON.stringify({ event: 'mark', streamSid, mark: { name: 'cleartest' } }));
        ws.send(JSON.stringify({ event: 'clear', streamSid }));
        console.error(`[mock] sent media+mark+clear after ${mediaFromProducer} frames`);
      }
    } else if (msg.event === 'mark') {
      console.error(`[mock] mark ack name=${msg.mark && msg.mark.name}`);
    } else if (msg.event === 'stop') {
      console.error('[mock] stop');
    }
  });
});

server.listen(port, '127.0.0.1', () => {
  console.error(`[mock] listening ws://127.0.0.1:${port}/media mode=${mode}`);
});
