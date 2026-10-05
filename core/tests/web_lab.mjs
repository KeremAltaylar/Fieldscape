/* The lab's audition bench (sample harmony, sub-project 1): a dropped recording is analysed, and the
   bench plays it retuned to exact chord ratios. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_lab.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = (process.env.FS_URL || "http://localhost:8765/") + "lab.html";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9266", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-lab-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
const WAV = (hz, secs, sr) => `(function(){ var sr = ${sr}, n = Math.round(sr * ${secs}), b = new ArrayBuffer(44 + n * 4), v = new DataView(b);
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 4, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 4, true);
  for (var i = 0; i < n; i++) { var x = Math.round(12000 * Math.sin(2 * Math.PI * ${hz} * i / sr)); v.setInt16(44 + i * 4, x, true); v.setInt16(46 + i * 4, x, true); }
  return new File([b], "tone.wav", { type: "audio/wav" }); })()`;
/* a pitched tone with overtones (1/k, six harmonics) - how a voice or an instrument records; the core's 3a level reference */
const RICH = (hz, secs, sr) => WAV(hz, secs, sr).replace("Math.sin(2 * Math.PI * " + hz + " * i / sr)",
  "(function () { var y = 0; for (var k = 1; k <= 6; k++) y += Math.sin(2 * Math.PI * " + hz + " * k * i / sr) / k; return 0.6 * y; })()");
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9266))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 60 && !(await ev("!!window.fsLab")); i++) await sleep(250);
  await ev("fsLab.ready");
  check("the bench loads its engine", await ev("!!fsLab.ready"), null);
  check("the lab's script is always fetched fresh (a versioned address)", await ev("[].some.call(document.scripts, function (s) { return s.src.indexOf('web/lab.js?v=') >= 0; })"), await ev("[].map.call(document.scripts, function (s) { return s.src; })"));
  /* Kerem, 2026-09-30: "I can not click to note scale chord progression buttons" - on a fresh page they
     looked ready and did nothing. Now they are disabled until there is something to play, and say why. */
  check("before a recording: the play buttons are disabled", await ev("[].every.call(document.querySelectorAll('[data-play]'), function (b) { return b.disabled; })"), null);
  check("... and the page says what to do", /load a pitched recording/i.test(await ev("document.querySelector('#lab-why').textContent")), await ev("document.querySelector('#lab-why').textContent"));
  check("published routes are listed", (await ev("fsLab.routes.length")) >= 1, await ev("fsLab.routes.length"));

  const a = await ev(`fsLab.load(${WAV(146.83, 2, 44100)}).then(function (a) { return a; })`);
  check("with a pitched recording the play buttons are enabled", await ev("[].every.call(document.querySelectorAll('[data-play]'), function (b) { return !b.disabled; })"), null);
  const lit = await ev("(function(){ var b = document.querySelector(\"[data-play='chord']\"); b.click(); var on = b.getAttribute('aria-pressed'); fsLab.stop(); return [on, b.getAttribute('aria-pressed')]; })()");
  check("the button playing is lit, and goes out on Stop", lit[0] === "true" && lit[1] === "false", lit);
  check("a 44.1 kHz stereo D3 is analysed as pitched, within 5 cents", a && a.verdict === "pitched" && Math.abs(1200 * Math.log2(a.f0 / 146.83)) < 5, a && [a.verdict, a.f0]);
  check("its pitch track is drawn", await ev("document.querySelector('#lab-track') && document.querySelector('#lab-track').dataset.points > 0"), null);

  const just = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })");
  await sleep(300); await ev("fsLab.stop()");
  const eq = await ev("fsLab.play('chord', { tuning: 'equal', step: 0 })");
  await sleep(300); await ev("fsLab.stop()");
  /* octaves aside (each note is played in the octave nearest the recording, spec D2) */
  const fold = (r) => r / Math.pow(2, Math.floor(Math.log2(r)));
  const ratio = fold(just.hz[2] / just.hz[0]);
  check("the chord in just intonation has an exact 3/2 fifth", Math.abs(ratio - 1.5) < 1e-6, ratio);
  check("equal temperament's fifth is 700 cents", Math.abs(((1200 * Math.log2(eq.hz[2] / eq.hz[0])) % 1200 + 1200) % 1200 - 700) < 0.01, eq.hz);
  check("each note's rate = its target / the measured f0", just.rates.every((r, i) => Math.abs(r - just.hz[i] / a.f0) < 1e-9), just.rates);

  const prog = await ev("fsLab.play('progression', { tuning: 'just' })");
  await sleep(500); await ev("fsLab.stop()");
  check("the progression plays 16 chords", prog && prog.chords === 16, prog);

  const long = await ev(`fsLab.load(${WAV(220, 120, 22050)}).then(function (a) { return { secs: a.frames / a.rate, note: document.querySelector('#lab-note').textContent }; })`);
  check("a long recording: only the first 30 s analysed, and the panel says so", Math.abs(long.secs - 30) < 0.01 && /first 30 s/.test(long.note), long);
  /* final review (2026-09-30): no clipping, Stop never lets a waiting voice start, unpitched says why, 30 s kept */
  const LOUD = `(function(){ var sr = 48000, n = sr * 2, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
    function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
    s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
    for (var i = 0; i < n; i++) v.setInt16(44 + i * 2, Math.round(32000 * Math.sin(2 * Math.PI * 146.83 * i / sr)), true);
    return new File([b], "loud.wav", { type: "audio/wav" }); })()`;
  await ev(`fsLab.load(${LOUD}).then(function () { return 1; })`);
  const loud = await ev("(function(){ var r = fsLab.play('chord', { tuning: 'just', step: 0 }); fsLab.stop(); return { sum: r && r.gains ? r.gains.reduce(function (a, g) { return a + g; }, 0) : null, peak: fsLab.peak }; })()");
  check("a loud chord's voices sum below full scale (no clipping)", loud.sum !== null && loud.sum * loud.peak <= 0.9, loud);
  const held = await ev("(function(){ fsLab.play('scale', { tuning: 'just', step: 0 }); fsLab.stop(); var now = fsLab.now(); return fsLab.voices().filter(function (v) { return v.start > now && v.stopAt > v.start; }).length; })()");
  check("Stop: no waiting voice starts afterwards", held === 0, held);
  const NOISE = `(function(){ var sr = 48000, n = sr * 2, b = new ArrayBuffer(44 + n * 2), v = new DataView(b), r = 3;
    function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
    s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
    for (var i = 0; i < n; i++) { r ^= r << 13; r ^= r >>> 17; r ^= r << 5; r >>>= 0; v.setInt16(44 + i * 2, Math.round((r / 4294967296 - 0.5) * 20000), true); }   /* xorshift32: white (an LCG in doubles was not) */
    return new File([b], "wind.wav", { type: "audio/wav" }); })()`;
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  const un = await ev("(function(){ var r = fsLab.play('chord'); return { r: r, note: document.querySelector('#lab-note').textContent }; })()");
  check("an unpitched recording says why it does not play yet", un.r && un.r.silent && /unpitched/i.test(un.note), un);
  check("... and its play buttons are disabled, with the reason beside them", await ev("document.querySelector(\"[data-play='chord']\").disabled && /unpitched/i.test(document.querySelector('#lab-why').textContent)"), null);
  await ev(`fsLab.load(${WAV(220, 120, 22050)}).then(function () { return 1; })`);
  const kept = await ev("fsLab.bufferSeconds");
  check("a long recording keeps only the 30 s it analysed", kept > 29.9 && kept <= 30.01, kept);
  /* Kerem, 2026-09-30: "the buttons are clickable but I don't hear any sound" - a 4 kHz bird was
     retuned to chord notes around 150-300 Hz (28x slower, inaudible). Each note is the chord note in the
     octave nearest the recording (spec D2 "any octave"), and sound really comes out. */
  const BIRD = `(function(){ var sr = 48000, n = sr * 2, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
    function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
    s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
    for (var i = 0; i < n; i++) { var t = i / sr, k = t % 0.25; v.setInt16(44 + i * 2, k < 0.08 ? Math.round(12000 * Math.sin(Math.PI * k / 0.08) * Math.sin(2 * Math.PI * 4000 * t)) : 0, true); }
    return new File([b], "bird.wav", { type: "audio/wav" }); })()`;
  const bird = await ev(`fsLab.load(${BIRD}).then(function (a) { return a.f0; })`);
  const br = await ev("(function(){ var r = fsLab.play('chord', { tuning: 'just', step: 0 }); fsLab.stop(); return r; })()");
  check("a 4 kHz bird is played near its own pitch (every rate within an octave either side)", br && br.rates && br.rates.every(function (r) { return r > 0.5 && r < 2; }), [bird, br && br.rates]);
  check("... on the chord's own notes, octaves aside (just fifth still 3/2 up to octaves)", br && br.hz && Math.abs(Math.log2(br.hz[2] / br.hz[0] / 1.5) - Math.round(Math.log2(br.hz[2] / br.hz[0] / 1.5))) < 1e-9, br && br.hz);
  await ev("fsLab.play('chord', { tuning: 'just', step: 0 }), 0");
  /* the test bird calls 80 ms in every 250: one 43 ms meter read can land between calls - the loudest of
     ten reads across a second */
  let lvl = -120;
  for (let i = 0; i < 10; i++) { await sleep(100); lvl = Math.max(lvl, await ev("fsLab.level()")); }
  await ev("fsLab.stop()");
  check("sound comes out while a chord plays (above -40 dBFS)", lvl > -40, lvl);
  /* Kerem, 2026-09-30: still silent on his machine - the page now shows what the audio is doing, so a
     silent run can be read: the engine's state and the level leaving it, live while it plays */
  await ev("document.querySelector(\"[data-play='chord']\").click(), 0");
  await sleep(700);
  const st = await ev("document.querySelector('#lab-audio').textContent");
  await ev("fsLab.stop()");
  check("the audio status line shows the engine running and the output level while playing", /running/.test(st) && /-?\d+ dB/.test(st), st);
  /* Kerem, 2026-09-30: "scale is too low in volume ... I want to hear the different pitches of each step ...
     I don't hear differences in progression ... make it a view that I can see". A bird that starts after
     0.6 s of quiet: notes start at its clear moment, a scale climbs, chords keep their shape, and the
     playing view draws every voice with its envelope. */
  const LATE = `(function(){ var sr = 48000, n = sr * 3, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
    function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
    s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
    for (var i = 0; i < n; i++) { var t = i / sr; v.setInt16(44 + i * 2, t < 0.6 ? 0 : Math.round(12000 * Math.sin(2 * Math.PI * 3800 * t)), true); }
    return new File([b], "late-bird.wav", { type: "audio/wav" }); })()`;
  await ev(`fsLab.load(${LATE}).then(function () { return 1; })`);
  const sc = await ev("(function(){ var r = fsLab.play('scale', { tuning: 'just', step: 0 }); var v = fsLab.voices(); fsLab.stop(); return { hz: r.hz, offs: v.map(function (x) { return x.offset; }), f0: fsLab.analysis.f0 }; })()");
  check("each note starts at the recording's clear moment, not its quiet start", sc.offs.every(function (o) { return o >= 0.6; }), sc.offs);
  check("the scale climbs, step by step", sc.hz.every(function (h, i) { return i === 0 || h > sc.hz[i - 1]; }), sc.hz);
  check("... around the recording's pitch (within an octave either side)", sc.hz.every(function (h) { return h > sc.f0 / 2 && h < sc.f0 * 2; }), [sc.f0, sc.hz]);
  const cd = await ev("(function(){ var r = fsLab.play('chord', { tuning: 'just', step: 0 }); fsLab.stop(); return r.hz; })()");
  check("a chord keeps its shape (its notes rise as written)", cd.every(function (h, i) { return i === 0 || h > cd[i - 1]; }), cd);
  await ev("fsLab.play('scale', { tuning: 'just', step: 0 }), 0");
  await sleep(1500);
  const lv2 = await ev("fsLab.level()");
  const roll = await ev("({ voices: +document.querySelector('#lab-roll').dataset.voices, now: document.querySelector('#lab-now').textContent })");
  await ev("fsLab.stop()");
  check("the scale is heard (above -40 dBFS)", lv2 > -40, lv2);
  check("the playing view draws every voice", roll.voices === 7, roll);
  check("... and names what is sounding now", /[A-G]#?\d/.test(roll.now), roll.now);
  /* Kerem, 2026-09-30: "can you add an octave adjusting attribute" - the whole block up or down by octaves */
  const oct = await ev("(function(){ var sel = document.querySelector('#lab-octave'); if (!sel) return null; var out = {}; ['0', '1', '-2'].forEach(function (o) { sel.value = o; sel.dispatchEvent(new Event('change')); var r = fsLab.play('chord', { tuning: 'just', step: 0 }); fsLab.stop(); out[o] = r.hz; }); sel.value = '0'; sel.dispatchEvent(new Event('change')); return out; })()");
  check("Octave +1 doubles every note, -2 quarters them", oct && oct["1"].every(function (h, i) { return Math.abs(h / oct["0"][i] - 2) < 1e-9; }) && oct["-2"].every(function (h, i) { return Math.abs(h / oct["0"][i] - 0.25) < 1e-9; }), oct);
  /* Kerem, 2026-09-30: "it is there but it does not affect the sound" - it only applied to the next press.
     Changed while a note sounds, the pitch coming out moves: measured at the output (loudest frequency). */
  await ev(`fsLab.load(${LATE}).then(function () { return 1; })`);
  await ev("document.querySelector(\"[data-play='note']\").click(), 0");
  await sleep(600);
  const before = await ev("fsLab.peakHz()");
  await ev("(function(){ var s = document.querySelector('#lab-octave'); s.value = '1'; s.dispatchEvent(new Event('change')); })(), 0");
  await sleep(600);
  const after = await ev("fsLab.peakHz()");
  await ev("fsLab.stop(); (function(){ var s = document.querySelector('#lab-octave'); s.value = '0'; s.dispatchEvent(new Event('change')); })(), 0");
  check("Octave changed while a note plays moves the sound an octave (measured at the output)", before > 0 && Math.abs(Math.log2(after / before) - 1) < 0.1, [before, after]);
  /* 2a: the Resonator on the engine - an unpitched "wind" file plays a chord whose notes stand out */
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  await ev("fsLab.setSynth('resonator'), fsLab.engineReady");
  check("with the Resonator, an unpitched recording is playable", await ev("!document.querySelector(\"[data-play='chord']\").disabled"), null);
  const rc = await ev("(function(){ var r = fsLab.play('chord', { tuning: 'just', step: 0 }); return r; })()");
  await sleep(1500);
  const made = await ev("fsLab.created()");
  let rl = -120; for (let i = 0; i < 5; i++) { await sleep(100); rl = Math.max(rl, await ev("fsLab.level()")); }
  const rnow = await ev("document.querySelector('#lab-now').textContent");
  await ev("fsLab.stop()");
  check("the Resonator's chord is heard (above -40 dBFS)", rl > -40, rl);
  check("each chord note stands >= 10 dB over the noise around it (pitch created)", made && made.length === rc.hz.length && made.every(function (d) { return d >= 10; }), made);
  /* final review #5: measuring it on the audio thread blocked a render ~70 ms per poll; the page measures it now */
  check("pitch created is measured on the page, never on the audio thread", await ev("fetch('web/core-worklet.js?v=' + Date.now()).then(function (r) { return r.text(); }).then(function (s) { return !/fs_bench_created/.test(s); })"), null);
  check("the Resonator plays the chord in its written register (octave 0)", rc.hz[0] > 60 && rc.hz[0] < 400, rc.hz);
  check("the playing view and Now line follow the Resonator", /[A-G]#?\d/.test(rnow), rnow);
  await ev("fsLab.setSynth('retune'), 0");
  /* Kerem 2026-10-01: "smooth cloudy transitions when release is longer than the note" - longer ranges, an S-curve
     attack, chord changes without a sudden drop, and the overlapping tails still below full scale */
  const set = (id, v) => ev(`(function(){ var s = document.querySelector('#${id}'); s.value = '${v}'; s.dispatchEvent(new Event('input')); return 1; })()`);
  const ranges = await ev("[document.querySelector('#lab-attack').max, document.querySelector('#lab-release').max]");
  check("Attack reaches 4 s and Release 10 s", ranges[0] === "4" && ranges[1] === "10", ranges);
  await ev(`fsLab.load(${WAV(220, 30, 22050)}).then(function () { return 1; })`);
  await set("lab-attack", 2); await set("lab-release", 8);
  await ev("fsLab.play('note', { tuning: 'just', step: 0 }), 0");
  const st0 = await ev("fsLab.voices()[0].start");
  while ((await ev("fsLab.now()")) < st0 + 0.5) { await sleep(5); }
  const quarter = await ev("fsLab.level()");
  while ((await ev("fsLab.now()")) < st0 + 2.6) { await sleep(20); }
  const full = await ev("fsLab.level()");
  await ev("fsLab.stop()");
  check("Retune's attack fades in as an S-curve (a quarter in: 13-21 dB under full; a line is 12, the old curve 60)", quarter - full < -13 && quarter - full > -21, [quarter, full]);
  await ev(`fsLab.load(${LOUD}).then(function () { return 1; })`);
  await set("lab-attack", 0.02);
  await ev("fsLab.play('progression', { tuning: 'just' }), 0");
  const changes = await ev("(function(){ var s = {}; fsLab.voices().forEach(function (v) { s[v.start.toFixed(3)] = 1; }); return Object.keys(s).map(Number).sort(function (a, b) { return a - b; }).slice(1, 4); })()");
  /* levels averaged over ~150 ms: notes a semitone apart across a change beat at ~10 Hz, and one 46 ms reading can
     sit in a beat's trough (a single-reading check failed 1 run in 5 at 6.06 dB) */
  const lv = []; let ppeak = 0;
  while ((await ev("fsLab.now()")) < changes[2] + 0.6) {
    lv.push([await ev("fsLab.now()"), await ev("fsLab.level()")]);
    ppeak = Math.max(ppeak, await ev("fsLab.peakOut()"));
    await sleep(40);
  }
  await ev("fsLab.stop()"); await set("lab-release", 0.6);
  const mean = (a, b) => { const s = lv.filter((p) => p[0] >= a && p[0] < b); return s.length ? s.reduce((m, p) => m + p[1], 0) / s.length : null; };
  let pdrop = 0;
  changes.forEach(function (c) {
    const before = mean(c - 0.3, c - 0.05);
    for (let s = c; s < c + 0.6; s += 0.05) { const m = mean(s, s + 0.15); if (before !== null && m !== null) { pdrop = Math.max(pdrop, before - m); } }
  });
  check("a progression with an 8 s release changes chord without a sudden drop (< 6 dB, 150 ms averages)", pdrop < 6, pdrop);
  check("... and its overlapping tails stay below full scale", ppeak > 0 && ppeak < 1, ppeak);
  /* Kerem 2026-10-01: "increase the master output twice, it is too low" - x2 (+6 dB) at the output, a limiter after it */
  await ev(`fsLab.load(${WAV(220, 30, 22050)}).then(function () { return 1; })`);
  await set("lab-attack", 0.02);
  const one = await ev("(function(){ var r = fsLab.play('note', { tuning: 'just', step: 0 }); return { g: r.gains[0], peak: fsLab.peak }; })()");
  await sleep(1200);
  let ol = -120; for (let i = 0; i < 5; i++) { await sleep(60); ol = Math.max(ol, await ev("fsLab.level()")); }
  await ev("fsLab.stop()");
  /* Kerem 2026-10-04: "general volume can be x2.5 since it's too low" */
  const want = 20 * Math.log10(2.5 * one.g * one.peak / Math.SQRT2);
  check("the master output is 2.5x the voice's level (+8 dB, within 1 dB)", Math.abs(ol - want) < 1, [ol, want]);
  /* 2b: the Harmonic filter and the Formant on the bench */
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  check("the Synth menu offers the Harmonic filter and the Formant", await ev("['harmonic','formant'].every(function (v) { return !!document.querySelector(\"#lab-synth option[value='\" + v + \"']\"); })"), null);
  await ev("fsLab.setSynth('harmonic'), fsLab.engineReady");
  check("... with Method and Mode beside Focus, and Body/Excite hidden", await ev("!!document.querySelector('#lab-part') && !document.querySelector('#lab-part').hidden && document.querySelector('#lab-res').hidden"), null);
  check("Colour reads Overtones for the Harmonic filter", /Overtones/.test(await ev("(document.querySelector('#lab-colour-label') || {}).textContent || ''")), null);
  /* Ringing: the pitched mode (Dry's bands are >= 73 Hz wide, so a low chord's notes blur - spec) */
  await ev("(function(){ var s = document.querySelector('#lab-mode'); s.value = '1'; s.dispatchEvent(new Event('change')); return 1; })()");
  const hc = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })");
  await sleep(2500);
  const hm = await ev("fsLab.created()");
  await ev("fsLab.stop()");
  check("the Harmonic filter creates pitch on a noise file (each note >= 10 dB)", hm && hc && hc.hz && hm.length === hc.hz.length && hm.every(function (d) { return d >= 10; }), hm);
  await ev("fsLab.setSynth('formant'), 0");
  await ev("(function(){ var s = document.querySelector('#lab-colour'); s.value = '0.2667'; s.dispatchEvent(new Event('input')); return 1; })()");
  const fl = await ev("(document.querySelector('#lab-colour-label') || {}).textContent || ''");
  check("Colour reads Partial 5 for the Formant at 0.27", /Partial 5/.test(fl), fl);
  await ev("(function(){ var s = document.querySelector('#lab-colour'); s.value = '0.5'; s.dispatchEvent(new Event('input')); return 1; })()");
  /* Kerem 2026-10-04: "when I add for voice, sections and third voice still digital synths play" - a route whose patch has no
     version / progression (lab2's offline default route, an old route) was read by the engine as an old patch: its default,
     digital, ignoring the roles. The panel's patch always carries them. */
  const rp = await ev("(function(){ fsLab.routes.push({ id: 'bare', name: 'Bare', patch: {} }); fsLab.setRoute('bare'); ['voice','sect','v3'].forEach(function (r) { var s = document.querySelector('#lab-rt-' + r); s.value = 's-freeze'; }); var p = fsLab.rtPatch(); fsLab.routes.pop(); fsLab.setRoute(fsLab.routes[0].id); ['voice','sect','v3'].forEach(function (r) { document.querySelector('#lab-rt-' + r).value = ''; }); return p; })()");
  check("a bare route's patch still carries the chosen sampler roles (a version and a progression added)", rp && rp.version >= 4 && rp.version <= 17 && rp.prog && rp.prog.length > 0 && rp.voice.synth === "s-freeze" && rp.sect.synth === "s-freeze" && rp.v3.synth === "s-freeze", rp);
  /* Kerem 2026-10-04: "I want to control the voice, sections and third voice parameters as well when I select the synths" -
     each role its own controls for the synth chosen, written into the route's patch (the same fields the route editor saves) */
  const setRole = (r, v) => ev(`(function(){ var s = document.querySelector('#lab-rt-${r}'); s.value = '${v}'; s.dispatchEvent(new Event('change')); return 1; })()`);
  const setCtl = (id, v, e) => ev(`(function(){ var s = document.querySelector('#${id}'); if (!s) return 0; s.value = '${v}'; s.dispatchEvent(new Event('${e || "input"}')); return 1; })()`);
  await setRole("voice", "s-resonator"); await setRole("sect", "fm"); await setRole("v3", "s-harmonic");
  const ctl = await ev("(function(){ var q = function (id) { return !!document.querySelector('#' + id); }; return { voiceBody: q('lab-rt-voice-body'), voiceMethod: q('lab-rt-voice-method'), v3Method: q('lab-rt-v3-method'), v3Body: q('lab-rt-v3-body'), sectHarm: q('lab-rt-sect-harm'), sectFocus: q('lab-rt-sect-focus'), gains: q('lab-rt-voice-gain') && q('lab-rt-sect-gain') && q('lab-rt-v3-gain') }; })()");
  check("each role shows the controls of its own synth (Resonator: Body; Harmonic filter: Method; FM: its two timbres; all: Gain)", ctl.voiceBody && !ctl.voiceMethod && ctl.v3Method && !ctl.v3Body && ctl.sectHarm && !ctl.sectFocus && ctl.gains, ctl);
  await setCtl("lab-rt-voice-focus", 0.2); await setCtl("lab-rt-v3-focus", 0.9); await setCtl("lab-rt-v3-mode", 1, "change"); await setCtl("lab-rt-sect-harm", 2); await setCtl("lab-rt-sect-gain", 0.3);
  const rp2 = await ev("fsLab.rtPatch()");
  check("... each role keeps its own values in the route's patch", rp2.voice.sampler.focus === 0.2 && rp2.v3.sampler.focus === 0.9 && rp2.v3.sampler.mode === 1 && rp2.sect.harm === 2 && rp2.sect.gain === 0.3 && !rp2.sect.sampler, rp2);
  await setRole("voice", ""); await setRole("sect", ""); await setRole("v3", "");
  /* 3a: a route played by the real route engine, each role Digital or Sampler */
  check("the Route panel offers Digital and Sampler sounds for each role", await ev("['voice','sect','v3'].every(function (r) { var s = document.querySelector('#lab-rt-' + r); return s && s.querySelector(\"optgroup[label='Digital'] option[value='fm']\") && s.querySelectorAll(\"optgroup[label='Sampler'] option\").length === 6; })"), null);
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  await ev("(function(){ var s = document.querySelector('#lab-rt-v3'); s.value = 's-harmonic'; s.dispatchEvent(new Event('change')); return 1; })()");
  await ev("document.querySelector('#lab-rt-play').click(), 0");
  let rtl = -120; for (let i = 0; i < 20; i++) { await sleep(200); rtl = Math.max(rtl, await ev("fsLab.level()")); }
  const rtNow = await ev("document.querySelector('#lab-rt-now').textContent");
  check("Play route: the route engine plays (above -40 dBFS) and shows its chord", rtl > -40 && /Chord \d+ of \d+/.test(rtNow), [rtl, rtNow]);
  await ev("(function(){ var s = document.querySelector('#lab-rt-v3'); s.value = 'fm'; s.dispatchEvent(new Event('change')); return 1; })()");
  let rtl2 = -120; for (let i = 0; i < 10; i++) { await sleep(200); rtl2 = Math.max(rtl2, await ev("fsLab.level()")); }
  check("... a role switched Sampler -> Digital while it plays keeps sounding", rtl2 > -40, rtl2);
  /* final review 3a I2/I3: a recording goes to the engine once - switching roles does not re-send it (each send was mixed on
     the audio thread, 50-130 ms, and kept for good) */
  await ev("(function(){ var s = document.querySelector('#lab-rt-v3'); s.value = 's-harmonic'; s.dispatchEvent(new Event('change')); return 1; })()");
  await sleep(300);
  const sent = await ev("fsLab.rtSent");
  check("switching roles does not re-send the recording (sent once)", sent === 1, sent);
  const sentBefore = await ev("fsLab.rtPatchSent");
  await ev("(function(){ var s = document.querySelector('#lab-rt-v3-focus'); s.value = '0.7'; s.dispatchEvent(new Event('input')); return 1; })()");
  await sleep(400);
  const afterSent = await ev("fsLab.rtPatchSent");
  check("moving a role's control while the route plays sends the new patch", afterSent > sentBefore, [sentBefore, afterSent]);
  await ev("document.querySelector('#lab-rt-stop').click(), 0");
  await sleep(3500);
  const rtq = await ev("fsLab.level()");
  check("Stop route: silence", rtq < -60, rtq);
  /* Kerem 2026-10-05: "the sampler synths volume is comparatively lower than the digital synths ... x2 or more": on a
     route's Voice (the other roles muted), each sampler's mean level sits at least 3 dB over FM's */
  const meanRt = async (syn) => {
    await setRole("voice", syn);
    for (let i = 0; i < 150 && (await ev("fsLab.level()")) > -90; i++) await sleep(200);
    await ev("document.querySelector('#lab-rt-play').click(), 0");
    await sleep(3000); let e = 0; for (let i = 0; i < 20; i++) { await sleep(200); e += Math.pow(10, (await ev("fsLab.level()")) / 10); }
    await ev("document.querySelector('#lab-rt-stop').click(), 0");
    return 10 * Math.log10(e / 20);
  };
  await setRole("sect", "fm"); await setRole("v3", "fm");
  await ev("['sect','v3'].forEach(function (r) { var s = document.querySelector('#lab-rt-' + r + '-gain'); s.value = '0'; s.dispatchEvent(new Event('input')); }), 0");
  const fmL = await meanRt("fm"), smpL = {};
  for (const v of ["s-resonator", "s-harmonic", "s-formant", "s-pulsar", "s-freeze"]) smpL[v] = +((await meanRt(v)) - fmL).toFixed(1);
  /* Retune needs a pitched recording (on noise the panel warns, 3c.1 F3): it is measured on one */
  await ev(`fsLab.load(${RICH(220, 20, 22050)}).then(function () { return 1; })`);
  smpL["s-retune"] = +((await meanRt("s-retune")) - fmL).toFixed(1);
  check("every sampler on a route's Voice sits >= 3 dB over FM (Kerem: x2 or more)", Object.values(smpL).every((d) => d >= 3), smpL);
  await setRole("voice", ""); await setRole("sect", ""); await setRole("v3", "");
  /* 2c: Pulsar and Freeze on the bench */
  check("the Synth menu offers Pulsar and Freeze", await ev("['pulsar','freeze'].every(function (v) { return !!document.querySelector(\"#lab-synth option[value='\" + v + \"']\"); })"), null);
  const setv = (id, v, evn) => ev(`(function(){ var s = document.querySelector('#${id}'); s.value = '${v}'; s.dispatchEvent(new Event('${evn || "input"}')); return 1; })()`);
  await ev("fsLab.setSynth('pulsar'), fsLab.engineReady");
  check("... with only Focus and Colour (no Method/Mode, no Body/Excite)", await ev("!!document.querySelector('#lab-part') && document.querySelector('#lab-part').hidden && document.querySelector('#lab-res').hidden && !document.querySelector('#lab-np').hidden"), null);
  await setv("lab-colour", 0.5);
  const pl = await ev("(document.querySelector('#lab-colour-label') || {}).textContent || ''");
  check("Colour reads Grain 53 % for Pulsar at 0.5", /Grain 53 %/.test(pl), pl);
  await setv("lab-focus", 1);
  const pc = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })"); await sleep(2500);
  const pm = await ev("fsLab.created()"); await ev("fsLab.stop()");
  check("Pulsar creates pitch on a noise file (each note >= 10 dB)", pm && pc && pc.hz && pm.length === pc.hz.length && pm.every(function (d) { return d >= 10; }), pm);
  await ev("fsLab.setSynth('freeze'), 0");
  const fz = await ev("(document.querySelector('#lab-colour-label') || {}).textContent || ''");
  check("Colour reads a Moment m:ss for Freeze", /^Moment \d+:\d\d$/.test(fz), fz);
  const fc = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })"); await sleep(2500);
  const fm = await ev("fsLab.created()"); await ev("fsLab.stop()");
  check("Freeze creates pitch on a noise file (each note >= 10 dB)", fm && fc && fc.hz && fm.length === fc.hz.length && fm.every(function (d) { return d >= 10; }), fm);
  await setv("lab-focus", 0.5);
  /* Kerem 2026-10-02: "clicks and clips ... chord and progression with release full and overtones full" - the master's
     oversampled curve rang past full scale (output peaks 1.14, measured): a dense Dry progression never passes -1 dBFS */
  await ev("fsLab.setSynth('harmonic'), fsLab.engineReady");
  await ev("(function(){ var s = document.querySelector('#lab-method'); s.value = '1'; s.dispatchEvent(new Event('change')); return 1; })()");
  await ev("(function(){ var s = document.querySelector('#lab-colour'); s.value = '1'; s.dispatchEvent(new Event('input')); s = document.querySelector('#lab-release'); s.value = '10'; s.dispatchEvent(new Event('input')); return 1; })()");
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  await ev("fsLab.play('progression', { tuning: 'just' }), 0");
  let lpk = 0; for (let i = 0; i < 40; i++) { await sleep(200); lpk = Math.max(lpk, await ev("fsLab.peakOut()")); }
  const lst = await ev("fsLab.stats()");
  await ev("fsLab.stop()");
  check("a dense progression (Release 10 s, Overtones full) never passes -1 dBFS at the output", lpk > 0.3 && lpk <= 0.892, lpk);
  check("... and the engine keeps time (no block over its time)", lst && lst.underruns === 0, lst);
  await ev("(function(){ var s = document.querySelector('#lab-release'); s.value = '0.6'; s.dispatchEvent(new Event('input')); s = document.querySelector('#lab-colour'); s.value = '0.5'; s.dispatchEvent(new Event('input')); s = document.querySelector('#lab-method'); s.value = '0'; s.dispatchEvent(new Event('change')); return 1; })()");
  await ev("(function(){ var s = document.querySelector('#lab-mode'); s.value = '0'; s.dispatchEvent(new Event('change')); return 1; })()");
  await ev("fsLab.setSynth('retune'), 0");
  check("no page errors", errors.length === 0, errors);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
