/* Sample harmony 3c: the pure parts of the shared role-setup module (web/lab-roles.js) */
import { test } from "node:test";
import assert from "node:assert/strict";
await import("../web/lab-roles.js");      /* a browser script: it sets globalThis.FsRoles */
const FsRoles = globalThis.FsRoles;

test("lab-roles: overlay puts a sampler role's synth and controls on a copy, leaving gain and the original alone", () => {
  const patch = { version: 17, voice: { synth: "fm", gain: 0.4, harm: 1, index: 4 }, sect: { synth: "fm", gain: 0.8, harm: 2, index: 7 } };
  const roles = { voice: { synth: "s-freeze", gain: 0.9, sampler: { focus: 0.8, colour: 0.2, tune: 1, body: 0, excite: 0, method: 0, mode: 0 } },
    sect: { synth: "am", gain: 0.1 } };
  const out = FsRoles.overlay(patch, roles);
  assert.equal(out.voice.synth, "s-freeze");
  assert.equal(out.voice.sampler.focus, 0.8);
  assert.equal(out.voice.gain, 0.4);
  assert.ok(!("harm" in out.voice) && !("index" in out.voice));
  assert.deepEqual(out.sect, patch.sect);                     /* a digital role in the row: the live patch's */
  assert.equal(patch.voice.synth, "fm");                      /* the original untouched */
  assert.deepEqual(FsRoles.overlay(patch, null), patch);
});

test("lab-roles: wavBytes writes a 16-bit PCM WAV, clipped", () => {
  const b = FsRoles.wavBytes([new Float32Array([0, 0.5, -1, 2])], 48000);
  const v = new DataView(b.buffer);
  assert.equal(String.fromCharCode(...b.slice(0, 4)), "RIFF");
  assert.equal(v.getUint32(24, true), 48000);
  assert.equal(v.getUint16(34, true), 16);
  assert.equal(v.getUint32(40, true), 8);
  assert.deepEqual([v.getInt16(44, true), v.getInt16(46, true), v.getInt16(48, true), v.getInt16(50, true)], [0, 16384, -32767, 32767]);
});

test("lab-roles: names are escaped, samplers recognised", () => {
  assert.equal(FsRoles.esc(`<img src="x">&'`), "&lt;img src=&quot;x&quot;&gt;&amp;&#39;");
  assert.ok(FsRoles.isSampler("s-pulsar") && !FsRoles.isSampler("fm") && !FsRoles.isSampler(""));
  assert.equal(FsRoles.sampleLabel("a.wav", { f0: 220.4 }, "saved"), "a.wav · 220 Hz · saved");
  assert.equal(FsRoles.colourName("s-freeze", 0.5, 62), "Moment 0:30");
});
