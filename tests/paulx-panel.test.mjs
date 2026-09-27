// tests/paulx-panel.test.mjs — the soundscape panel's PaulXStretch controls.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const html = readFileSync("index.html", "utf8").replace(/\r\n/g, "\n");
function src(name) {
  const s = html.indexOf("function " + name + "(");
  assert.ok(s !== -1, "missing " + name);
  let i = html.indexOf("{", html.indexOf(")", s)), d = 0;
  const from = i;
  for (; i < html.length; i++) { if (html[i] === "{") d++; else if (html[i] === "}") { d--; if (!d) break; } }
  return html.slice(from + 1, i);
}

/* 2026-09-27: PaulXStretch's modules (none of which the core engine ever read) gave way to
   Fieldscape's own shaping - Kerem: pitch & harmony, movement & space, a region. */
test("the panel has Fieldscape's own sections and controls, and none of Paul's modules", () => {
  const p = src("renderSoundscapePanel");
  ["Time", "Pitch & harmony", "Movement & space"].forEach((h) => assert.match(p, new RegExp('heading\\("' + h + '"[,)]'), h));
  ["stretch", "fft", "onset", "start", "end", "transpose", "tune", "focus", "partials", "layers", "harmony", "glide",
   "drift", "blur", "width", "grit"].forEach((k) => assert.match(p, new RegExp('k: "' + k + '"'), k));
  assert.match(p, /buildPxToggle\("freeze"/);
  assert.doesNotMatch(p, /buildPxModule\(/, "no PaulXStretch module is left in the panel");
  ["warp", "morph", "field", "xfade"].forEach((k) => assert.doesNotMatch(p, new RegExp('k: "' + k + '"'), k));
});

test("the shaping is stored in properties.sound.shape, dry by default (A-8)", () => {
  const p = src("renderSoundscapePanel");
  assert.match(p, /if \(!q\.shape\) \{ q\.shape = \{\}; \}/);
  assert.match(p, /transpose: 0, tune: 0, focus: 0\.5, partials: 8, layers: 0, harmony: 0\.5, glide: 2,/);
  assert.match(p, /drift: 0, blur: 0, width: 1, start: 0, end: 1/);
});

test("readouts are in real units", () => {
  const p = src("renderSoundscapePanel");
  [/" st"/, /fx\.x\(/, /fx\.pct/, /fx\.sec/].forEach((re) => assert.match(p, re));
});

test("a module heading is a real toggle bound to .on, and an off module dims", () => {
  const m = src("buildPxModule");
  assert.match(m, /aria-pressed/);
  assert.match(m, /obj\.on = !obj\.on/);
  assert.match(m, /classList\.toggle\("off", !obj\.on\)/);
  assert.match(html, /\.pxmod\.off \.pprow \{ opacity:/);
});

test("rows declare data-def, so the global double-click reset and shift-fine drag apply", () => {
  assert.match(src("buildPxRow"), /dataset\.def/);
});

test("log sliders keep the stored value in real units", () => {
  const b = src("buildPxRow");
  assert.match(b, /Math\.log\(v\)/);
  assert.match(b, /Math\.exp\(u\)/);
});

test("narrow or short screens get tabs, not a scrolling panel", () => {
  /* Measured: 1428x729 three columns, 501x695 five tabs, 832x390 five tabs with two sub-columns
     — every one 0 px of panel scroll. Three tabs had scrolled 23-168 px. */
  /* 2026-09-19: the tab frame moved into soundColumns, shared by all three sound panels, so
     Rhythm and Grains got the same fallback Stretch already had. */
  const p = src("renderSoundscapePanel"), cols = src("soundColumns"), tabbed = src("soundTabbed");
  assert.match(cols, /setAttribute\("role", "tablist"\)/);
  assert.match(tabbed, /innerWidth < 1000 \|\| innerHeight < 700/);
  assert.match(cols, /twoUp = tabbed && innerWidth >= 700/);
  assert.match(p, /var tabbed = soundTabbed\(\);/);
  assert.match(p, /soundColumns\(body, "soundscape", tabbed \?/);
  ["Point", "Time", "Harmony", "Space"].forEach((t) => assert.match(p, new RegExp('title: "' + t + '"')));
  assert.match(p, /innerHeight < 500\) \{ wave\.classList\.add\("short"\); mhead\.hidden = true; \}/);
  for (const panel of ["renderRhythmPanel", "renderGrainPanel"]) {
    assert.match(src(panel), /soundColumns\(body,/, panel + " uses the same frame");
  }
});

test("every drag updates the caption first, whether or not a voice is playing, then posts pxParams", () => {
  const p = src("renderSoundscapePanel");
  const live = p.slice(p.indexOf("var commitLive = function ()"));
  const caption = live.indexOf("updateWaveCaption();"), guard = live.search(/if \(!v \|\| !v\.ready/);
  assert.ok(caption !== -1 && guard !== -1 && caption < guard);
  assert.match(live, /pxParams\(q, v\)/);
});

test("the copy names the engine it is: Fieldscape's own, not PaulXStretch", () => {
  assert.match(src("renderSoundscapePanel"), /Fieldscape's stretch engine/);
  assert.doesNotMatch(src("renderSoundscapePanel"), /PaulXStretch engine/);
});

test("the waveform dims what lies outside the play range", () => {
  assert.match(src("soundscapeWaveDraw"), /var rg = q\.shape \|\| \{ start: 0, end: 1 \};/);
});
