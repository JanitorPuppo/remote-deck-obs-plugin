#!/usr/bin/env node
/**
 * Dummy custom upstream for PROTOCOL.md.
 * Speaks the same JSON frames the OBS plugin expects.
 *
 *   node upstream.mjs [--port 8787] [--token test-token]
 *
 * Stdin commands:
 *   mute <name> <true|false>
 *   volume <name> <volumeDb>
 *   list
 *   ping
 *   raw <json>
 */

import { createServer } from "node:http";
import { createInterface } from "node:readline";
import { WebSocketServer } from "ws";

const args = process.argv.slice(2);
const port = Number(args[args.indexOf("--port") + 1] || 8787);
const tokenFlag = args.includes("--token") ? args[args.indexOf("--token") + 1] : "";

const clients = new Set();

function frame(type, payload = {}, id) {
  const obj = { v: 1, type };
  if (id) obj.id = id;
  if (payload && Object.keys(payload).length > 0) obj.payload = payload;
  return JSON.stringify(obj);
}

function send(ws, type, payload, id) {
  if (ws.readyState === ws.OPEN) ws.send(frame(type, payload, id));
}

function broadcast(type, payload, id) {
  for (const ws of clients) send(ws, type, payload, id);
}

function authorize(req) {
  if (!tokenFlag) return true;
  const header = req.headers.authorization || "";
  const expected = tokenFlag.includes(" ") ? tokenFlag : `Bearer ${tokenFlag}`;
  return header === expected;
}

const httpServer = createServer((req, res) => {
  res.writeHead(200, { "content-type": "text/plain" });
  res.end("obs-remote-deck custom-upstream fixture\n");
});

const wss = new WebSocketServer({
  server: httpServer,
  verifyClient: ({ req }, done) => {
    if (authorize(req)) {
      done(true);
      return;
    }
    done(false, 401, "unauthorized");
  },
});

wss.on("connection", (ws, req) => {
  clients.add(ws);
  const host = req.socket.remoteAddress;
  console.log(`[open] ${host} (${clients.size} connected)`);

  ws.on("message", (data) => {
    const text = String(data);
    let parsed;
    try {
      parsed = JSON.parse(text);
    } catch {
      send(ws, "error", { code: "protocol_error", message: "Invalid JSON frame" });
      return;
    }
    const type = parsed?.type;
    const id = parsed?.id;
    const payload = parsed?.payload ?? {};
    console.log(`[plugin] ${type}`, JSON.stringify(payload));

    if (type === "ping") {
      send(ws, "pong", {}, id);
      return;
    }
    if (type === "hello" || type === "inputs" || type === "state" || type === "error" || type === "pong") {
      return;
    }
    send(ws, "error", { code: "unknown_method", message: `Unknown method: ${type}` }, id);
  });

  ws.on("close", () => {
    clients.delete(ws);
    console.log(`[close] ${host} (${clients.size} connected)`);
  });
});

httpServer.listen(port, "127.0.0.1", () => {
  console.log(`Custom upstream fixture on ws://127.0.0.1:${port}`);
  if (tokenFlag) console.log("Authorization required (value not printed)");
  console.log("Commands: mute <name> <true|false> | volume <name> <db> | list | ping | raw <json>");
});

const rl = createInterface({ input: process.stdin, crlfDelay: Infinity });
rl.on("line", (line) => {
  const trimmed = line.trim();
  if (!trimmed) return;
  const [cmd, ...rest] = trimmed.split(/\s+/);
  if (cmd === "mute") {
    const [name, muted] = rest;
    broadcast("input.mute", { name, muted: muted === "true" });
    return;
  }
  if (cmd === "volume") {
    const [name, volumeDb] = rest;
    broadcast("input.volume", { name, volumeDb: Number(volumeDb) });
    return;
  }
  if (cmd === "list") {
    broadcast("inputs.get");
    return;
  }
  if (cmd === "ping") {
    broadcast("ping");
    return;
  }
  if (cmd === "raw") {
    const json = rest.join(" ");
    for (const ws of clients) {
      if (ws.readyState === ws.OPEN) ws.send(json);
    }
    return;
  }
  console.log("unknown command");
});
