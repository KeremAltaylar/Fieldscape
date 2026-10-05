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

/* final review 3c I1: an upload on route A finishing after route B is shown must never save B's roles into A's row */
test("lab-roles: a save for a route other than the one loaded is refused, even after a slow upload", async () => {
  const rows = { A: { voice: { synth: "s-freeze", sampler: { focus: 0.9 }, sample: null } }, B: { voice: { synth: "fm" } } };
  const upserts = [];
  let releaseUpload;
  const sb = {
    from: () => ({
      select: () => ({ eq: (k, id) => ({ maybeSingle: async () => ({ data: rows[id] ? { roles: rows[id] } : null, error: null }) }) }),
      upsert: async (row) => { upserts.push(row); return { error: null }; }
    }),
    storage: { from: () => ({ upload: () => new Promise((r) => { releaseUpload = () => r({ error: null }); }), download: async () => ({ error: { message: "none" } }) }) }
  };
  const chan = new Float32Array(48000);
  const buf = { numberOfChannels: 1, length: chan.length, sampleRate: 48000, duration: 1, getChannelData: () => chan, copyToChannel() {} };
  const ctx = { sampleRate: 48000, decodeAudioData: async () => buf, createBuffer: () => buf };
  const s = FsRoles.session({ sb, ctx: () => ctx, analyse: () => ({ f0: 0 }) });
  await s.load("A");
  const up = s.upload("voice", { name: "a.wav", arrayBuffer: async () => new ArrayBuffer(8) }, "A");
  for (let i = 0; i < 20 && !releaseUpload; i++) await new Promise((r) => setTimeout(r, 5));
  await s.load("B");                                   /* the setter opens route B while A's sample uploads */
  releaseUpload(); await up;
  const m = await s.save("A", "user-1");
  assert.ok(m && /Not saved/.test(m), m);
  assert.equal(upserts.length, 0, JSON.stringify(upserts));
  assert.equal(await s.save("B", "user-1"), null);
  assert.equal(upserts.length, 1);
  assert.equal(upserts[0].route_id, "B");
  assert.deepEqual(s.lastSaved, upserts[0].roles);
});
