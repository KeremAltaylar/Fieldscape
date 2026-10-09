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

/* 3c.1 F1: the silence of a recording cut out (50 ms blocks, -40 dB of the loudest, gaps under 0.25 s kept) */
const calls = (sr) => {      /* three 0.3 s calls of a 5 kHz tone, 2 s of silence between, 0.5 s of silence first */
  const n = Math.round(sr * 7.4), x = new Float32Array(n);
  for (const s0 of [0.5, 2.8, 5.1]) for (let i = 0; i < 0.3 * sr; i++) x[Math.round(s0 * sr) + i] = 0.5 * Math.sin(2 * Math.PI * 5000 * i / sr);
  return x;
};
test("lab-roles: compactData keeps the calls, joined, and drops the silence between", () => {
  const sr = 48000, x = calls(sr);
  const track = Array.from({ length: Math.round(7.4 / 0.02) }, (_, i) => [i, 0.9, 5000]);   /* hop i carries its own index */
  const c = FsRoles.compactData([x], sr, { f0: 5000, hop_s: 0.02, track, frames: x.length });
  const secs = c.channels[0].length / sr;
  assert.ok(secs > 0.85 && secs < 1.25, String(secs));                       /* ~0.9 s of calls (+ block rounding) */
  let peak = 0; for (const v of c.channels[0]) peak = Math.max(peak, Math.abs(v));
  assert.ok(peak > 0.45);
  let step = 0; for (let i = 1; i < c.channels[0].length; i++) step = Math.max(step, Math.abs(c.channels[0][i] - c.channels[0][i - 1]));
  /* faded, no click: no step larger than the 5 kHz tone's own (2*pi*5000/sr * 0.5); an unfaded cut jumps up to 1.0 */
  assert.ok(step <= 1.02 * 2 * Math.PI * 5000 / sr * 0.5, "a join steps by " + step);
  assert.equal(c.analysis.frames, c.channels[0].length);
  /* the track keeps the hops inside kept blocks, in order: hop 25 (0.5 s) is the first call's first hop */
  assert.equal(c.analysis.track[0][0], 25);
  assert.ok(c.analysis.track.length > 40 && c.analysis.track.length < 65, String(c.analysis.track.length));
  assert.equal(x.length, Math.round(sr * 7.4));                               /* the input untouched */
});
test("lab-roles: compactData keeps a gap under 0.25 s, and leaves an all-silent or tiny buffer whole", () => {
  const sr = 48000, x = new Float32Array(sr * 2);
  for (let i = 0; i < 0.4 * sr; i++) x[i] = 0.5 * Math.sin(i / 3);
  for (let i = Math.round(0.6 * sr); i < sr; i++) x[i] = 0.5 * Math.sin(i / 3);   /* a 0.2 s gap, then 0.4 s more */
  assert.ok(FsRoles.compactData([x], sr, null).channels[0].length >= sr * 0.95);
  const z = new Float32Array(sr); assert.equal(FsRoles.compactData([z], sr, null).channels[0].length, sr);
  const click = new Float32Array(sr); click[24000] = 0.9;
  const cc = FsRoles.compactData([click], sr, null).channels[0];
  assert.ok(cc.length > 0 && cc.length <= sr);
});

/* final review 3d I3: Position's place in the (compacted) recording, as the core reads it - from the first confident frame
   of the pitch track, stopping 0.5 s (or half what is left) short of the end */
test("lab-roles: positionFrame maps Position as the core does", () => {
  const sr = 48000, len = 10 * sr;
  const track = Array.from({ length: 500 }, (_, i) => [220, i < 10 ? 0.3 : 0.9, 1000]);
  const from = 10 * 0.02 * sr, room = len - from - 2, tail = Math.min(0.5 * sr, 0.5 * room);
  assert.equal(FsRoles.positionFrame(0, len, { hop_s: 0.02, track }, sr), from);
  assert.equal(FsRoles.positionFrame(1, len, { hop_s: 0.02, track }, sr), from + room - tail);
  assert.equal(FsRoles.positionFrame(0.5, len, null, sr), 0.5 * (len - 2 - Math.min(0.5 * sr, 0.5 * (len - 2))));
});

/* Sample harmony 6 (Kerem 2026-10-07): octave on every sampler synth, an EQ on every voice */
test("lab-roles 6: overlay puts a role's EQ on the patch for a digital role as well as a sampler one, octave with the sampler", () => {
  const eq = [[80, 3, 0.7], [250, 0, 1], [1000, -4, 1], [4000, 0, 1], [10000, 0, 0.7]];
  const patch = { version: 17, voice: { synth: "fm", gain: 0.4, harm: 1, index: 4 }, sect: { synth: "fm", gain: 0.8 }, v3: { synth: "am" } };
  const roles = { voice: { synth: "s-resonator", sampler: { focus: 0.5, octave: -1 }, eq }, sect: { synth: "am", eq }, v3: { synth: "am" } };
  const out = FsRoles.overlay(patch, roles);
  assert.deepEqual(out.voice.eq, eq);
  assert.equal(out.voice.sampler.octave, -1);
  assert.deepEqual(out.sect.eq, eq);
  assert.equal(out.sect.synth, "fm");                         /* a digital role: the live patch's synth, only the EQ added */
  assert.ok(!("eq" in out.v3));
  assert.ok(!("eq" in patch.sect));                           /* the original untouched */
});

test("lab-roles 6: a session keeps octave and EQ in its saved roles and reads them back", async () => {
  const eq = [[80, 3, 0.7], [250, 0, 1], [1000, -4, 1], [4000, 0, 1], [10000, 0, 0.7]];
  const rows = { A: { sect: { synth: "fm", eq }, voice: { synth: "s-retune", sampler: { octave: 2 } } } };
  const sb = { from: () => ({ select: () => ({ eq: (k, id) => ({ maybeSingle: async () => ({ data: rows[id] ? { roles: rows[id] } : null, error: null }) }) }) }) };
  const s = FsRoles.session({ sb, ctx: () => null, analyse: () => null });
  await s.load("A");
  assert.deepEqual(s.state("sect").eq, eq);
  assert.equal(s.state("voice").octave, 2);
  assert.equal(s.state("v3").octave, 0);
  const j = s.json();
  assert.deepEqual(j.sect.eq, eq);
  assert.equal(j.voice.sampler.octave, 2);
  assert.ok(!("eq" in j.v3) || j.v3.eq === null);
});

test("lab-roles 6: eqResponse - flat is 0 dB, a +6 dB bell at 1 kHz reads +6 there and ~0 a decade away", () => {
  const flat = FsRoles.eqDefault();
  for (const hz of [50, 1000, 12000]) assert.ok(Math.abs(FsRoles.eqResponse(flat, hz, 48000)) < 1e-9);
  const b = FsRoles.eqDefault(); b[2] = [1000, 6, 1];
  assert.ok(Math.abs(FsRoles.eqResponse(b, 1000, 48000) - 6) < 0.01);
  assert.ok(Math.abs(FsRoles.eqResponse(b, 100, 48000)) < 0.1 && Math.abs(FsRoles.eqResponse(b, 10000, 48000)) < 0.1);
  const sh = FsRoles.eqDefault(); sh[0] = [16000, -12, 0.7];  /* a low shelf high up: everything under it -12 (the core's route check) */
  assert.ok(Math.abs(FsRoles.eqResponse(sh, 500, 48000) + 12) < 0.1);
});

/* Sample harmony 6a: a route's role setups live in its own patch (K4) */
test("6a: toPatch writes sampler roles and EQ into a copy of the patch, never an inline analysis", () => {
  const patch = { version: 17, voice: { synth: "fm", gain: 0.4, harm: 1, index: 4 }, sect: { synth: "fm" }, v3: { synth: "am" } };
  const eq = [[80, 3, 0.7], [250, 0, 1], [1000, 0, 1], [4000, 0, 1], [10000, 0, 0.7]];
  const roles = { voice: { synth: "s-retune", sampler: { focus: 0.3, octave: 1 }, eq, sample: { path: "R/voice-1.wav", name: "a.wav", analysis_path: "R/voice-1.json", f0: 220, analysis: { track: [1] } } },
    sect: { synth: "fm", eq }, v3: { synth: "am" } };
  const out = FsRoles.toPatch(patch, roles);
  assert.equal(out.voice.synth, "s-retune");
  assert.deepEqual(out.voice.sampler, { focus: 0.3, octave: 1 });
  assert.deepEqual(out.voice.sample, { path: "R/voice-1.wav", name: "a.wav", analysis_path: "R/voice-1.json", f0: 220 });
  assert.equal(out.voice.gain, 0.4);
  assert.deepEqual(out.sect.eq, eq); assert.equal(out.sect.synth, "fm");
  assert.ok(!("eq" in out.v3) && !("sample" in out.v3));
  assert.equal(patch.voice.synth, "fm");                       /* the original untouched */
});
test("6a: fromPatch reads them back", () => {
  const patch = { voice: { synth: "s-freeze", gain: 0.5, sampler: { focus: 0.7 }, sample: { path: "R/voice-1.wav", name: "a", analysis_path: "R/voice-1.json", f0: 0 } }, sect: { synth: "fm", eq: [[80, 1, 0.7]] } };
  const r = FsRoles.fromPatch(patch);
  assert.equal(r.voice.synth, "s-freeze"); assert.equal(r.voice.sampler.focus, 0.7); assert.equal(r.voice.sample.analysis_path, "R/voice-1.json");
  assert.deepEqual(r.sect.eq, [[80, 1, 0.7]]); assert.equal(r.sect.synth, "fm");
});
test("6a: decodeSample fetches the analysis file when the sample has none inline", async () => {
  const got = [];
  const sb = { storage: { from: () => ({ download: async (p) => { got.push(p); return { data: p.endsWith(".json") ? new Blob([JSON.stringify({ f0: 220, hop_s: 0.02, track: [] })]) : new Blob([new Uint8Array(8)]) }; } }) } };
  const chan = new Float32Array(4800);
  const buf = { numberOfChannels: 1, length: chan.length, sampleRate: 48000, duration: 0.1, getChannelData: () => chan, copyToChannel() {} };
  const ctx = { sampleRate: 48000, decodeAudioData: async () => buf, createBuffer: () => buf };
  const p = await FsRoles.decodeSample(sb, ctx, { path: "R/voice-1.wav", analysis_path: "R/voice-1.json" });
  assert.deepEqual(got.sort(), ["R/voice-1.json", "R/voice-1.wav"]);
  assert.equal(p.analysis.f0, 220);
});
test("6a: a session saving to the route's folder uploads the WAV and its analysis JSON beside it", async () => {
  const ups = [];
  const sb = { storage: { from: () => ({ upload: async (p, b) => { ups.push([p, b.type]); return { error: null }; }, download: async () => ({ error: { message: "none" } }) }) } };
  const chan = new Float32Array(48000);
  const buf = { numberOfChannels: 1, length: chan.length, sampleRate: 48000, duration: 1, getChannelData: () => chan, copyToChannel() {} };
  const ctx = { sampleRate: 48000, decodeAudioData: async () => buf, createBuffer: () => buf };
  const s = FsRoles.session({ sb, ctx: () => ctx, analyse: () => ({ f0: 330, hop_s: 0.02, track: [] }), folder: (id) => id + "/", analysisFiles: true });
  s.fromPatch("R", {});
  await s.upload("v3", { name: "b.wav", arrayBuffer: async () => new ArrayBuffer(8) }, "R");
  const x = s.state("v3").sample;
  assert.ok(/^R\/v3-\d+\.wav$/.test(x.path) && x.analysis_path === x.path.replace(/\.wav$/, ".json") && x.f0 === 330, JSON.stringify(x));
  assert.deepEqual(ups.map((u) => u[0]).sort(), [x.analysis_path, x.path].sort());
  assert.equal(s.loadedFor, "R");
});
test("6a: a voice moved to a sampler remembers its digital synth (its timbres kept when chosen again: 3c I2)", () => {
  const p1 = FsRoles.toPatch({ v3: { synth: "pluck", harm: 8000 } }, { v3: { synth: "s-freeze", sampler: {} } });
  assert.equal(p1.v3.synth, "s-freeze"); assert.equal(p1.v3.digital, "pluck"); assert.equal(p1.v3.harm, 8000);
  const p2 = FsRoles.toPatch(p1, { v3: { synth: "s-pulsar", sampler: {} } });
  assert.equal(p2.v3.digital, "pluck");                         /* sampler to sampler: the digital one kept */
});
