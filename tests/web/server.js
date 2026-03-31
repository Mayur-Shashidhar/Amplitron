#!/usr/bin/env node
/**
 * Static file server for Amplitron web demo.
 * Sends Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy headers
 * required for SharedArrayBuffer (pthreads/AudioWorklet) to function.
 *
 * Usage:
 *   WEB_BUILD_DIR=../../docs/demo PORT=8080 node server.js
 */

'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const WEB_DIR = process.env.WEB_BUILD_DIR
  ? path.resolve(process.env.WEB_BUILD_DIR)
  : path.resolve(__dirname, '../../docs/demo');

const PORT = parseInt(process.env.PORT || '8080', 10);

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.css':  'text/css',
  '.png':  'image/png',
  '.svg':  'image/svg+xml',
  '.ico':  'image/x-icon',
  '.json': 'application/json',
};

const server = http.createServer((req, res) => {
  // Strip query string from URL path
  const urlPath = req.url.split('?')[0];
  const safePath = path.normalize(urlPath).replace(/^(\.\.[/\\])+/, '');
  let filePath = path.join(WEB_DIR, safePath);

  // Serve index.html for directory requests
  if (filePath === WEB_DIR || filePath.endsWith('/')) {
    filePath = path.join(filePath, 'index.html');
  }

  // Required for SharedArrayBuffer / pthreads / AudioWorklet
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.setHeader('Cross-Origin-Resource-Policy', 'cross-origin');

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end(`Not found: ${safePath}`);
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(data);
  });
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Serving ${WEB_DIR} at http://127.0.0.1:${PORT}`);
});
