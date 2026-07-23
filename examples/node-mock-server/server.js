#!/usr/bin/env node
/**
 * Twilio Media Streams–compatible mock peer for mod_realtime_ws / rtw_sim.
 * Modes:
 *   echo  — bounce media back (and optionally send mark)
 *   clear — after N media frames from producer, send media+mark then clear
 *
 * Optional TLS: set TLS_CERT + TLS_KEY → listen as wss://
 */
const fs = require('fs');
const http = require('http');
const https = require('https');
const { WebSocketServer } = require('ws');

const port = parseInt(process.env.PORT || '8081', 10);
const mode = process.env.MODE || 'echo'; // echo | clear
const clearAfter = parseInt(process.env.CLEAR_AFTER || '10', 10);
const tlsCert = process.env.TLS_CERT || '';
const tlsKey = process.env.TLS_KEY || '';
const useTls = Boolean(tlsCert && tlsKey);

const server = useTls
  ? https.createServer({
      cert: fs.readFileSync(tlsCert),
      key: fs.readFileSync(tlsKey),
    })
  : http.createServer((_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('mod_realtime_ws mock\n');
    });

if (useTls) {
  server.on('request', (_req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('mod_realtime_ws mock (tls)\n');
  });
}

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
  const scheme = useTls ? 'wss' : 'ws';
  console.error(`[mock] listening ${scheme}://127.0.0.1:${port}/media mode=${mode}`);
});
