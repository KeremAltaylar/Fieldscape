/* Sample harmony 6b: the apps prepare a voice's sample in the core (fs_role_compact, used by fs_piece_role_sample) - the
   same splice as the website's FsRoles.compactData, frame for frame, so a route's sampler voice sounds the same everywhere. */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
await import("../web/lab-roles.js");
const FsRoles = globalThis.FsRoles;

const x = (await WebAssembly.instantiate(readFileSync("web/core-lab.wasm"), {
  env: new Proxy({}, { get: () => () => 0 }), wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
})).instance.exports;
if (x._initialize) x._initialize();

/* bursts of a tone between silences: 0.1 s (kept, a breath), 0.6 s (cut), a quiet tail at -50 dB (cut), stereo */
function signal(sr) {
  const parts = [[0.4, 1], [0.1, 0], [0.5, 1], [0.6, 0], [0.3, 1], [0.4, 0.003]], n = Math.round(parts.reduce((s, p) => s + p[0], 0) * sr);
  const L = new Float32Array(n), R = new Float32Array(n); let i = 0;
  for (const [d, a] of parts) for (let k = 0; k < Math.round(d * sr); k++, i++) { L[i] = a * 0.5 * Math.sin(2 * Math.PI * 330 * i / sr); R[i] = a * 0.4 * Math.sin(2 * Math.PI * 220 * i / sr); }
  return [L, R];
}

for (const sr of [48000, 44100]) test(`fs_role_compact is FsRoles.compactData, frame for frame (${sr} Hz)`, () => {
  const chs = signal(sr), n = chs[0].length, js = FsRoles.compactData(chs, sr, null);
  const ptr = (a) => { const p = x.malloc(a.length * 4); new Float32Array(x.memory.buffer, p, a.length).set(a); return p; };
  const ins = chs.map(ptr), outs = chs.map(() => x.malloc(n * 4)), nb = Math.ceil(n / Math.round(0.05 * sr));
  const pin = x.malloc(8), pout = x.malloc(8), kept = x.malloc(nb);
  new Uint32Array(x.memory.buffer, pin, 2).set(ins); new Uint32Array(x.memory.buffer, pout, 2).set(outs);
  const m = Number(x.fs_role_compact(pin, 2, BigInt(n), sr, pout, kept, nb));
  assert.equal(m, js.channels[0].length, "compacted length");
  assert.ok(m < n * 0.75 && m > n * 0.5, "the long silence and the quiet tail went, the breath stayed: " + m + " of " + n);
  assert.deepEqual(Array.from(new Uint8Array(x.memory.buffer, kept, nb)), Array.from(js.kept), "kept blocks");
  for (let c = 0; c < 2; c++) {
    const got = new Float32Array(x.memory.buffer, outs[c], m); let worst = 0;
    for (let i = 0; i < m; i++) worst = Math.max(worst, Math.abs(got[i] - js.channels[c][i]));
    assert.ok(worst < 1e-6, "channel " + c + " differs by " + worst);
  }
});
