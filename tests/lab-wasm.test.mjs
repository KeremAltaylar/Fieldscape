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

/* 6a (Kerem 2026-10-09): the public engine IS the lab engine now - the guard that kept them apart (D1) became this */
test("the site's engine is the lab's (sample harmony 6a)", () => {
  assert.deepEqual(readFileSync("web/core.wasm"), readFileSync("web/core-lab.wasm"));
});

test("the lab's engine has the bench: a bowed string on noise creates its pitch", () => {
  const name = str("bench"), d = x.fs_create(name);
  assert.ok(d);
  x.fs_prepare(d, 48000, 128);
  const n = 48000 * 3, p = x.malloc(n * 4), f = new Float32Array(x.memory.buffer, p, n);
  let s = 5; for (let i = 0; i < n; i++) { s = (s * 1103515245 + 12345) >>> 0; f[i] = ((s >>> 8) / 16777216 - 0.5); }
  const ptrs = x.malloc(4); new Uint32Array(x.memory.buffer, ptrs, 1)[0] = p;
  x.fs_set_source(d, 1, n, ptrs);
  x.fs_bench_note(d, 220, 0.05, 5, 0.5);
  for (let i = 0; i < 48000 * 1.5 / 128; i++) x.fs_process(d, 128);
  const made = x.fs_bench_created(d, 220);
  assert.ok(made >= 10, "pitch created " + made);
});
