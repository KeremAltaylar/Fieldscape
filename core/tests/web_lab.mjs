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
  check("published routes are listed", (await ev("fsLab.routes.length")) >= 1, await ev("fsLab.routes.length"));

  const a = await ev(`fsLab.load(${WAV(146.83, 2, 44100)}).then(function (a) { return a; })`);
  check("a 44.1 kHz stereo D3 is analysed as pitched, within 5 cents", a && a.verdict === "pitched" && Math.abs(1200 * Math.log2(a.f0 / 146.83)) < 5, a && [a.verdict, a.f0]);
  check("its pitch track is drawn", await ev("document.querySelector('#lab-track') && document.querySelector('#lab-track').dataset.points > 0"), null);

  const just = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })");
  await sleep(300); await ev("fsLab.stop()");
  const eq = await ev("fsLab.play('chord', { tuning: 'equal', step: 0 })");
  await sleep(300); await ev("fsLab.stop()");
  const ratio = just.hz[2] / just.hz[0];
  check("the chord in just intonation has an exact 3/2 fifth", Math.abs(ratio - 1.5) < 1e-6, ratio);
  check("equal temperament's fifth is 700 cents", Math.abs(1200 * Math.log2(eq.hz[2] / eq.hz[0]) - 700) < 0.01, eq.hz);
  check("each note's rate = its target / the measured f0", just.rates.every((r, i) => Math.abs(r - just.hz[i] / a.f0) < 1e-9), just.rates);

  const prog = await ev("fsLab.play('progression', { tuning: 'just' })");
  await sleep(500); await ev("fsLab.stop()");
  check("the progression plays 16 chords", prog && prog.chords === 16, prog);

  const long = await ev(`fsLab.load(${WAV(220, 120, 22050)}).then(function (a) { return { secs: a.frames / a.rate, note: document.querySelector('#lab-note').textContent }; })`);
  check("a long recording: only the first 30 s analysed, and the panel says so", Math.abs(long.secs - 30) < 0.01 && /first 30 s/.test(long.note), long);
  check("no page errors", errors.length === 0, errors);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
