/* The lab's engine file answers the bench's two questions (sample harmony, sub-project 1). */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const x = (await WebAssembly.instantiate(readFileSync("web/core-lab.wasm"), {
  env: new Proxy({}, { get: () => () => 0 }),
  wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
})).instance.exports;
if (x._initialize) x._initialize();
const str = (s) => { const b = new TextEncoder().encode(s + "\0"), p = x.malloc(b.length); new Uint8Array(x.memory.buffer, p, b.length).set(b); return p; };
const call = (fn, ...a) => { const need = fn(...a, 0, 0), out = x.malloc(need + 1); fn(...a, out, need + 1); const s = new TextDecoder().decode(new Uint8Array(x.memory.buffer, out, need)); x.free(out); return JSON.parse(s); };

test("the progression of the default patch, in just intonation", () => {
  const p = str("{}"), j = call(x.fs_harmony_progression, p, 1, 0);
  assert.equal(j.chords.length, 16);
  assert.equal(j.tuning, "just");
  assert.ok(Math.abs(j.chords[0].hz[2] / j.chords[0].hz[0] - 1.5) < 1e-6);
});

test("a 220 Hz sine analysed within 5 cents", () => {
  const n = 48000, p = x.malloc(n * 4), f = new Float32Array(x.memory.buffer, p, n);
  for (let i = 0; i < n; i++) f[i] = 0.5 * Math.sin(2 * Math.PI * 220 * i / 48000);
  const j = call(x.fs_analyse, p, BigInt(n), 48000);
  assert.equal(j.verdict, "pitched");
  assert.ok(Math.abs(1200 * Math.log2(j.f0 / 220)) < 5);
});

test("the live site's engine file is not the lab's", () => {
  assert.notDeepEqual(readFileSync("web/core.wasm"), readFileSync("web/core-lab.wasm"));
});
