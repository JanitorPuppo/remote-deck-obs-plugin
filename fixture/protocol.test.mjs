import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocket } from "ws";

const here = dirname(fileURLToPath(import.meta.url));
const port = 18787;

function frame(type, payload = {}, id) {
  const obj = { v: 1, type };
  if (id) obj.id = id;
  if (payload && Object.keys(payload).length > 0) obj.payload = payload;
  return JSON.stringify(obj);
}

function waitUntil(getText, needle, timeoutMs = 4000) {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const tick = () => {
      if (getText().includes(needle)) {
        resolve();
        return;
      }
      if (Date.now() - start > timeoutMs) {
        reject(new Error(`timed out waiting for ${JSON.stringify(needle)}\n${getText()}`));
        return;
      }
      setTimeout(tick, 15);
    };
    tick();
  });
}

const child = spawn(process.execPath, ["upstream.mjs", "--port", String(port), "--token", "test-token"], {
  cwd: here,
  stdio: ["pipe", "pipe", "pipe"],
});

let stdout = "";
child.stdout.on("data", (chunk) => {
  stdout += String(chunk);
});
child.stderr.on("data", (chunk) => process.stderr.write(chunk));

try {
  await waitUntil(() => stdout, "Custom upstream fixture");

  const rejected = new WebSocket(`ws://127.0.0.1:${port}`);
  rejected.on("error", () => {});
  const [, response] = await once(rejected, "unexpected-response");
  assert.equal(response.statusCode, 401);

  const ws = new WebSocket(`ws://127.0.0.1:${port}`, {
    headers: { Authorization: "Bearer test-token" },
  });
  await once(ws, "open");

  const incoming = [];
  ws.on("message", (data) => incoming.push(JSON.parse(String(data))));
  const nextIncoming = () =>
    new Promise((resolve, reject) => {
      const start = Date.now();
      const tick = () => {
        if (incoming.length) {
          resolve(incoming.shift());
          return;
        }
        if (Date.now() - start > 2000) {
          reject(new Error("timed out waiting for upstream frame"));
          return;
        }
        setTimeout(tick, 10);
      };
      tick();
    });

  ws.send(
    frame("hello", {
      protocolVersion: 1,
      pluginVersion: "1.0.0",
      machineLabel: "test-pc",
    }),
  );
  await waitUntil(() => stdout, "[plugin] hello");

  ws.send(frame("ping", {}, "p1"));
  const pong = await nextIncoming();
  assert.equal(pong.type, "pong");
  assert.equal(pong.id, "p1");

  child.stdin.write("mute Mic/Aux true\n");
  const mute = await nextIncoming();
  assert.equal(mute.type, "input.mute");
  assert.equal(mute.payload.name, "Mic/Aux");
  assert.equal(mute.payload.muted, true);

  child.stdin.write("volume Mic/Aux -12\n");
  const volume = await nextIncoming();
  assert.equal(volume.type, "input.volume");
  assert.equal(volume.payload.volumeDb, -12);

  child.stdin.write("list\n");
  const list = await nextIncoming();
  assert.equal(list.type, "inputs.get");

  ws.send(frame("not.a.method", {}, "x1"));
  const error = await nextIncoming();
  assert.equal(error.type, "error");
  assert.equal(error.payload.code, "unknown_method");
  assert.equal(error.id, "x1");

  ws.close();
  console.log("protocol fixture frames ok");
} finally {
  child.kill();
}
