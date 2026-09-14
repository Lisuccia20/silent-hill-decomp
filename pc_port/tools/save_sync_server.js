#!/usr/bin/env node
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * save_sync_server.js — backing server for the Silent Hill PC/iOS save sync
 * (pc_port/src/pc_save_sync.c). Zero dependencies: only Node's built-in http /
 * fs / crypto, so it runs on any Node >= 14 with no `npm install`.
 *
 * Contract (must match the client exactly):
 *   GET  <BASE_PATH>/<chan>.MCD   header X-Sync-Token: <token>
 *        -> 200 + the raw 131072-byte card, or 404 if none stored yet
 *   PUT  <BASE_PATH>/<chan>.MCD   header X-Sync-Token: <token>, body = raw card
 *        -> 200 on success
 * <chan> is 0 or 1. The client stores save_sync_url = <scheme>://host<BASE_PATH>,
 * so BASE_PATH here must equal the path part of that URL (default "/sh-saves").
 *
 * This speaks plain HTTP. Put it behind your existing TLS terminator (nginx,
 * Caddy, ...) so the game can reach it over https://lizonline.net/... — the
 * client always uses HTTPS. Configure via environment variables:
 *
 *   SYNC_TOKEN   required. Must equal the game's save_sync_token. Refuses to
 *                start if unset, so the store is never left open by accident.
 *   PORT         listen port (default 8787).
 *   HOST         bind address (default 127.0.0.1 — only the local TLS proxy
 *                reaches it; set 0.0.0.0 to expose directly, not recommended).
 *   DATA_DIR     where the .MCD files live (default ./sh-saves-data).
 *   BASE_PATH    URL path prefix to match (default /sh-saves).
 *
 * Run:  SYNC_TOKEN=yourtoken node save_sync_server.js
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const CARD_SIZE = 131072; /* 16 * 8 KB — a full PSX memory card */

const TOKEN = process.env.SYNC_TOKEN || '';
const PORT = parseInt(process.env.PORT || '8787', 10);
const HOST = process.env.HOST || '127.0.0.1';
const DATA_DIR = path.resolve(process.env.DATA_DIR || './sh-saves-data');
let BASE_PATH = process.env.BASE_PATH || '/sh-saves';

if (!TOKEN) {
  console.error('save_sync_server: refusing to start — set SYNC_TOKEN to the game\'s save_sync_token');
  process.exit(1);
}

/* Normalise BASE_PATH to "/prefix" with no trailing slash (""=match at root). */
BASE_PATH = BASE_PATH.replace(/\/+$/, '');
if (BASE_PATH && !BASE_PATH.startsWith('/')) BASE_PATH = '/' + BASE_PATH;

fs.mkdirSync(DATA_DIR, { recursive: true });

/* Constant-time token check so a wrong token can't be found byte-by-byte. */
function tokenOk(req) {
  const got = req.headers['x-sync-token'];
  if (typeof got !== 'string') return false;
  const a = Buffer.from(got);
  const b = Buffer.from(TOKEN);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

/* Map a request path to a channel (0/1), or null if it isn't a card path.
 * Only "<BASE_PATH>/0.MCD" and "<BASE_PATH>/1.MCD" are accepted — no traversal,
 * no other names — so DATA_DIR can never be escaped. */
function channelForPath(urlPath) {
  const clean = decodeURIComponent(urlPath.split('?')[0]);
  if (BASE_PATH && !clean.startsWith(BASE_PATH + '/')) return null;
  const rest = BASE_PATH ? clean.slice(BASE_PATH.length + 1) : clean.replace(/^\//, '');
  const m = /^([01])\.MCD$/.exec(rest);
  return m ? m[1] : null;
}

function cardFile(chan) {
  return path.join(DATA_DIR, chan + '.MCD');
}

function send(res, code, body) {
  res.writeHead(code, { 'Content-Type': 'text/plain' });
  res.end(body ? body + '\n' : '');
}

const server = http.createServer((req, res) => {
  const chan = channelForPath(req.url);
  if (chan === null) return send(res, 404, 'not found');
  if (!tokenOk(req)) return send(res, 401, 'bad token');

  if (req.method === 'GET') {
    fs.readFile(cardFile(chan), (err, data) => {
      if (err) return send(res, 404, 'no card');
      res.writeHead(200, {
        'Content-Type': 'application/octet-stream',
        'Content-Length': data.length,
      });
      res.end(data);
    });
    return;
  }

  if (req.method === 'PUT') {
    /* Reject an oversized upload up front when the client declares its length. */
    const declared = parseInt(req.headers['content-length'] || '0', 10);
    if (declared && declared > CARD_SIZE) {
      req.destroy();
      return;
    }
    const chunks = [];
    let total = 0;
    let aborted = false;
    req.on('data', (c) => {
      total += c.length;
      if (total > CARD_SIZE) { /* guard even without a Content-Length */
        aborted = true;
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => {
      if (aborted) return;
      const body = Buffer.concat(chunks);
      if (body.length !== CARD_SIZE) {
        return send(res, 400, 'expected ' + CARD_SIZE + ' bytes, got ' + body.length);
      }
      /* Write to a temp file then rename — a reader never sees a half-written
       * card, and a crash mid-write can't corrupt the previous good save. */
      const tmp = cardFile(chan) + '.tmp-' + process.pid;
      fs.writeFile(tmp, body, (err) => {
        if (err) return send(res, 500, 'write failed');
        fs.rename(tmp, cardFile(chan), (err2) => {
          if (err2) { fs.unlink(tmp, () => {}); return send(res, 500, 'commit failed'); }
          console.log(new Date().toISOString(), 'PUT channel', chan, body.length, 'bytes');
          send(res, 200, 'ok');
        });
      });
    });
    req.on('error', () => { aborted = true; });
    return;
  }

  send(res, 405, 'method not allowed');
});

server.listen(PORT, HOST, () => {
  console.log(`save_sync_server on http://${HOST}:${PORT}${BASE_PATH}/<0|1>.MCD  data=${DATA_DIR}`);
});
