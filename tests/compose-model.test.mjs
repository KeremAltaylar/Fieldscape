import test from "node:test";
import assert from "node:assert/strict";
await import("../web/compose-model.js");      /* a browser script: it sets globalThis.FsCompose */
const M = globalThis.FsCompose;

test("defaults are today's sound, written out", () => {
  const c = M.defaults();
  assert.equal(c.voice.style, "free"); assert.equal(c.sect.style, "free"); assert.equal(c.v3.style, "free");
  assert.equal(c.bass.mode, "hold"); assert.deepEqual(c.over, { chord: {}, section: {}, zone: {} });
});
test("bars: a chord's later bars develop by default, and Generate writes it", () => {
  assert.equal(M.defaults().bars, "develop");
  const g = M.generate(M.defaults(), { form: "AABA", contrast: 0.5, seed: 4, nchords: 8 });
  assert.equal(g.bars, "develop");
  assert.equal(M.generate(Object.assign(M.defaults(), { bars: "repeat" }), { form: "AABA", contrast: 0.5, seed: 4, nchords: 8 }).bars, "develop");
});
test("a chord override holds only what it changes; clearing it removes the key and the empty scope", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "chord", i: 4 }, "voice", "style", "strum");
  assert.deepEqual(c.over.chord["4"], { voice: { style: "strum", tones: [0, 1, 2] } });   /* a style brings its tones (I2) */
  const e = M.effective(c, { kind: "chord", i: 4 }, "voice");
  assert.equal(e.values.style, "strum"); assert.equal(e.source.style, "here"); assert.equal(e.source.rate, "route");
  M.clearValue(c, { kind: "chord", i: 4 }, "voice", "style"); M.clearValue(c, { kind: "chord", i: 4 }, "voice", "tones");
  assert.equal(c.over.chord["4"], undefined);
});
test("a zone override carries when and blend", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "zone", id: "p1" }, "sect", "role", "off");
  assert.equal(c.over.zone.p1.when, "inside"); assert.equal(c.over.zone.p1.blend, 1);
});
test("lane cells: zone > chord > section > route", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "route" }, "voice", "style", "arpeggio");
  M.setValue(c, { kind: "section", i: 1 }, "voice", "style", "pedal");
  M.setValue(c, { kind: "chord", i: 2 }, "voice", "style", "strum");
  M.setValue(c, { kind: "zone", id: "z" }, "voice", "style", "swells");
  assert.equal(M.cell(c, 0, [])[0].style, "arpeggio");
  assert.deepEqual(M.cell(c, 0, [{ kind: "section", id: 1 }])[0], { style: "pedal", source: "section" });
  assert.deepEqual(M.cell(c, 2, [{ kind: "section", id: 1 }])[0], { style: "strum", source: "chord" });
  assert.deepEqual(M.cell(c, 2, [{ kind: "zone", id: "z" }])[0], { style: "swells", source: "zone" });
});
test("bands: where a zone and a section cross the route, as shares of its length", () => {
  const coords = [[29.0, 41.0], [29.01, 41.0]];
  const zones = [{ id: "z", lon: 29.005, lat: 41.0, r: 100 }];
  const sections = [{ id: 0, ring: [[28.999, 40.999], [29.003, 40.999], [29.003, 41.001], [28.999, 41.001], [28.999, 40.999]] }];
  const b = M.bands(coords, sections, zones);
  const z = b.find((x) => x.kind === "zone"), s = b.find((x) => x.kind === "section");
  assert.ok(Math.abs(z.t0 - 0.38) < 0.03 && Math.abs(z.t1 - 0.62) < 0.03, JSON.stringify(z));
  assert.ok(s.t0 === 0 && Math.abs(s.t1 - 0.3) < 0.03, JSON.stringify(s));
});
test("history: undo and redo walk the snapshots", () => {
  const h = M.history(50), c = M.defaults();
  h.push(c); const d = JSON.parse(JSON.stringify(c)); d.voice.style = "pedal"; h.push(d);
  assert.equal(h.undo().voice.style, "free"); assert.equal(h.redo().voice.style, "pedal"); assert.equal(h.canRedo(), false);
});
test("templates and the summary", () => {
  assert.ok(M.TEMPLATES.length >= 3);
  const t = M.TEMPLATES.find((x) => x.id === "arp-strum-pedal").comp();
  assert.equal(M.summary(t), "Arpeggio · Strum · Pedal");
  assert.equal(M.summary(M.defaults()), "Free · Free · Free");
});

// Fix round 1 regression tests

test("Fix 1: template arrays are cloned, not referenced", () => {
  const t1 = M.TEMPLATES.find((x) => x.id === "arp-strum-pedal").comp();
  t1.voice.grid[0] = 9;
  const t2 = M.TEMPLATES.find((x) => x.id === "arp-strum-pedal").comp();
  assert.equal(t2.voice.grid[0], 0, "editing one template instance corrupted another");
  assert.equal(M.FIELDS.arpeggio[0].def[0], 0, "editing a template corrupted FIELDS");
});

test("Fix 2: zone role-only override preserves style from chord/section", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "chord", i: 5 }, "voice", "style", "strum");
  M.setValue(c, { kind: "zone", id: "z" }, "voice", "role", "accomp");
  const cell = M.cell(c, 5, [{ kind: "zone", id: "z" }])[0];
  assert.equal(cell.style, "strum", "zone role-only override lost chord style");
});

test("Fix 3a: chord-level role off is honoured in cell", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "chord", i: 3 }, "voice", "role", "off");
  const cell = M.cell(c, 3, [])[0];
  assert.equal(cell.style, "off", "chord role off not resolved");
});

test("Fix 3b: route role off is honoured even with chord style set", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "route" }, "voice", "role", "off");
  M.setValue(c, { kind: "chord", i: 2 }, "voice", "style", "pedal");
  const cell = M.cell(c, 2, [])[0];
  assert.equal(cell.style, "off", "route role off was overridden by chord style");
});

test("Fix 4: a field the route leaves unset takes its own style's default", () => {
  const c = M.defaults();
  c.voice.style = "euclid";
  assert.deepEqual(M.effective(c, { kind: "route" }, "voice").values.grid, M.FIELDS.euclid.find((x) => x.k === "grid").def);
  c.sect.style = "swells";
  assert.deepEqual(M.effective(c, { kind: "route" }, "sect").values.tones, M.FIELDS.swells.find((x) => x.k === "tones").def);
});

test("a chord that turns a voice off is the source of the off cell", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "chord", i: 3 }, "voice", "role", "off");
  assert.deepEqual(M.cell(c, 3, [])[0], { style: "off", source: "chord" });
  M.setValue(c, { kind: "zone", id: "z" }, "sect", "role", "off");
  assert.deepEqual(M.cell(c, 0, [{ kind: "zone", id: "z" }])[1], { style: "off", source: "zone" });
});

test("Fix 5a: bands returns [] with fewer than 2 coordinates", () => {
  assert.deepEqual(M.bands([], [], []), []);
  assert.deepEqual(M.bands([[0, 0]], [], []), []);
  assert.deepEqual(M.bands(null, [], []), []);
});

test("Fix 5b: setValue guards against undefined scope.i", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "chord" }, "voice", "style", "pedal");
  assert.equal(c.over.chord.undefined, undefined, "setValue should not create entry with undefined key");
  M.setValue(c, { kind: "section" }, "voice", "style", "strum");
  assert.equal(c.over.section.undefined, undefined, "setValue should not create entry with undefined key");
});

/* final review I2: what the inspector shows is what plays - choosing a style writes its pattern or tones where the scope has none */
test("choosing a style writes its grid or tones, as the inspector shows them, and keeps any already set", () => {
  for (const scope of [{ kind: "route" }, { kind: "chord", i: 2 }, { kind: "zone", id: "z" }]) {
    for (const st of ["arpeggio", "euclid", "strum", "swells"]) {
      const c = M.defaults();
      M.setValue(c, scope, "sect", "style", st);
      const k = st === "arpeggio" || st === "euclid" ? "grid" : "tones";
      const written = M.scopeObj(c, scope, false).sect[k], shown = M.effective(c, scope, "sect").values[k];
      assert.deepEqual(written, M.FIELDS[st].filter((f) => f.k === k)[0].def, st + " " + scope.kind);
      assert.deepEqual(shown, written);
    }
  }
  const c = M.defaults();
  M.setValue(c, { kind: "route" }, "voice", "grid", [3, 2, 1, 0]);
  M.setValue(c, { kind: "route" }, "voice", "style", "arpeggio");
  assert.deepEqual(c.voice.grid, [3, 2, 1, 0]);
  M.setValue(c, { kind: "route" }, "voice", "style", "free");
  assert.equal(c.voice.tones, undefined);
});
test("the bass's fixed pedal has a note knob (12-72, D2)", () => {
  const f = M.BASS.filter((x) => x.k === "note")[0];
  assert.ok(f && f.kind === "knob" && f.min === 12 && f.max === 72 && f.def === 38);
  const c = M.defaults(); M.setValue(c, { kind: "route" }, "bass", "note", 45);
  assert.equal(M.effective(c, { kind: "route" }, "bass").values.note, 45);
});
/* final review M7: a chord inside a section inherits the section's settings in the inspector, as its lane cell shows them */
test("a chord's inspector inherits route < section, source 'section'", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "route" }, "voice", "rate", "8n");
  M.setValue(c, { kind: "section", i: 1 }, "voice", "style", "pedal");
  M.setValue(c, { kind: "section", i: 1 }, "voice", "rate", "4n");
  const at = [{ kind: "section", id: 1, t0: 0, t1: 0.5 }];
  const e = M.effective(c, { kind: "chord", i: 2 }, "voice", at);
  assert.equal(e.style, "pedal"); assert.equal(e.values.rate, "4n"); assert.equal(e.source.rate, "section"); assert.equal(e.source.lo, "route");
  assert.equal(M.cell(c, 2, at)[0].style, "pedal");
  M.setValue(c, { kind: "chord", i: 2 }, "voice", "rate", "16n");
  const e2 = M.effective(c, { kind: "chord", i: 2 }, "voice", at);
  assert.equal(e2.values.rate, "16n"); assert.equal(e2.source.rate, "here");
  assert.equal(M.effective(c, { kind: "chord", i: 3 }, "voice").source.rate, "route");   /* no bands: the route's */
});

/* Generate form (2026-10-19 brief, .superpowers/sdd/2026-10-17-composition-1-foundation/generate-brief.md) */
import { readFileSync } from "node:fs";
const CORE = readFileSync(new URL("../core/composer.cpp", import.meta.url), "utf8");
/* what the core parses: every key it asks a voice/bass object for, and the strings its tables know */
const CORE_KEYS = new Set([...CORE.matchAll(/(?:has|get)\("(\w+)"\)/g)].map((m) => m[1]));
const strTable = (re) => [...CORE.match(re)[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]);
const CORE_STR = {
  style: strTable(/STYLES\[NSTYLE\] = \{([^}]*)\}/), role: strTable(/ROLES\[5\] = \{([^}]*)\}/), rate: strTable(/T\[\] = \{(.*)\};/),
  pedal: strTable(/P\[3\] = \{([^}]*)\}/), dir: strTable(/D\[3\] = \{([^}]*)\}/), mode: strTable(/M\[4\] = \{([^}]*)\}/),
  op: strTable(/OPS\[NOP\] = \{([^}]*)\}/), cad: strTable(/CAD\[3\] = \{([^}]*)\}/), others: strTable(/OTH\[3\] = \{([^}]*)\}/)
};
const CORE_NOTE_KEYS = new Set([...CORE.matchAll(/\bx->[ns]\("(\w+)"/g)].map((m) => m[1]));   /* a motif note's and a chain step's keys */
const moving = (o) => o.role !== "off" && ["16n", "8n"].includes(o.rate) && !["pedal", "swells"].includes(o.style);
const gen = (o, c = M.defaults()) => M.generate(c, Object.assign({ form: "AABA", contrast: 0.6, seed: 7, nchords: 16 }, o));
const voicesAt = (c, i) => ["voice", "sect", "v3", "bass"].map((v) => Object.assign({}, c[v], (c.over.chord[String(i)] || {})[v] || {}));
const fieldOf = (k) => M.FIELDS.common.concat(...Object.keys(M.FIELDS).filter((s) => s !== "common").map((s) => M.FIELDS[s]), M.BASS).find((f) => f.k === k);
function checkParsable(c) {
  const objs = [c.voice, c.sect, c.v3].map((o) => ["route", o]).concat(Object.entries(c.over.chord).flatMap(([i, o]) => Object.entries(o).map(([v, x]) => [i + ":" + v, x])));
  for (const [where, o] of objs.concat([["bass", c.bass]])) {
    for (const [k, v] of Object.entries(o)) {
      assert.ok(CORE_KEYS.has(k), where + " writes " + k + ", which the core never reads");
      if (CORE_STR[k] && typeof v === "string") assert.ok(CORE_STR[k].includes(v), where + " " + k + "=" + v);
      const f = fieldOf(k);
      if (f && f.kind === "knob") assert.ok(typeof v === "number" && v >= f.min && v <= f.max, where + " " + k + "=" + v);
      if (k === "grid") assert.ok(v.length === 16 && v.every((x) => Number.isInteger(x) && x >= -1 && x <= 4), where + " grid " + v);
      if (k === "tones") assert.ok(v.length && v.every((x) => Number.isInteger(x) && x >= 0 && x <= 4), where + " tones " + v);
    }
  }
  if (c.motif) {
    assert.ok(CORE.includes('"scale"') && ["chord", "scale"].includes(c.motif.basis), "basis " + c.motif.basis);
    assert.ok(c.motif.bars >= 1 && c.motif.bars <= 2 && c.motif.notes.length >= 1 && c.motif.notes.length <= 8);
    for (const n of c.motif.notes) {
      for (const k of Object.keys(n)) assert.ok(CORE_NOTE_KEYS.has(k), "motif note key " + k);
      assert.ok(n.start >= 0 && n.start < c.motif.bars * 16 && n.deg >= -7 && n.deg <= 13 && (n.oct || 0) >= -2 && (n.oct || 0) <= 2, JSON.stringify(n));
    }
  }
  for (const s of c.chain || []) {
    for (const k of Object.keys(s)) assert.ok(CORE_NOTE_KEYS.has(k), "chain key " + k);
    assert.ok(CORE_STR.op.includes(s.op) && CORE_STR.cad.includes(s.cad || "full") && CORE_STR.others.includes(s.others || "thin"), JSON.stringify(s));
    assert.ok(s.from >= 0 && s.to >= s.from && s.to < 64, JSON.stringify(s));
  }
  assert.ok((c.chain || []).length <= 16);
}
test("generate: the forms and how a route's chords split among their letters", () => {
  assert.deepEqual(M.FORMS.map((f) => f.id), ["AABA", "ABAB", "ABAC", "sentence", "rondo"]);
  const sp = (f, n) => M.formSpans(f, n).map((s) => s.letter + (s.b - s.a));
  assert.deepEqual(sp("AABA", 16), ["A4", "A4", "B4", "A4"]);
  assert.deepEqual(sp("sentence", 16), ["a4", "a′4", "b8"]);
  assert.deepEqual(sp("rondo", 16), ["A4", "B3", "A3", "C3", "A3"]);
  assert.deepEqual(sp("AABA", 3), ["A1", "A1", "B1"]);       /* fewer chords than letters: its first n letters */
  assert.deepEqual(sp("AABA", 1), ["A1"]);
  assert.deepEqual(sp("ABAC", 13), ["A4", "B3", "A3", "C3"]);
});
test("generate: pure and deterministic; a new seed is a new piece", () => {
  const c = M.defaults(), before = JSON.stringify(c);
  assert.deepEqual(gen({}, c), gen({}, c));
  assert.equal(JSON.stringify(c), before, "generate changed its input");
  assert.notDeepEqual(gen({ seed: 8 }), gen({ seed: 7 }));
  const g = gen({});
  assert.equal(g.seed, 7); assert.deepEqual(g.form, { type: "AABA", contrast: 0.6, seed: 7, relations: "some" });
});
test("generate: a repeated A is identical; B differs from A at contrast 1 and equals it at 0", () => {
  for (const seed of [1, 2, 3, 4, 5]) {
    const g = gen({ seed });
    for (let j = 0; j < 4; j++) assert.deepEqual(voicesAt(g, j), voicesAt(g, 4 + j), "seed " + seed + " chord " + j);
    const g0 = gen({ seed, contrast: 0 });
    for (let j = 0; j < 4; j++) assert.deepEqual(voicesAt(g0, 8 + j), voicesAt(g0, j), "contrast 0: B is A, seed " + seed);
    const g1 = gen({ seed, contrast: 1 }), cls = (o) => o.role === "off" ? "off" : ({ arpeggio: "move", euclid: "move", pedal: "held", swells: "held" })[o.style] || "chord";
    const A = voicesAt(g1, 1), B = voicesAt(g1, 9);
    for (let v = 0; v < 3; v++) assert.notEqual(cls(B[v]), cls(A[v]), "contrast 1: voice " + v + " keeps its class, seed " + seed);
  }
});
test("generate: textures - one voice moves above one that sustains", () => {
  const ri = (r) => M.RATES.indexOf(r);
  for (let seed = 0; seed < 20; seed++) {
    const g = gen({ seed }), A = [g.voice, g.sect, g.v3];
    const mover = A.find((o) => o.role !== "off" && ["arpeggio", "euclid", "strum"].includes(o.style) && ri(o.rate) <= ri("8n"));
    const held = A.find((o) => o !== mover && o.role !== "off" && ["pedal", "swells", "strum"].includes(o.style) && ri(o.rate) >= ri("2n"));
    assert.ok(mover && held, "seed " + seed + ": " + JSON.stringify(A.map((o) => [o.style, o.rate, o.role])));
    assert.ok(mover.lo >= held.lo && mover.hi > held.hi, "seed " + seed + " registers");
  }
});
test("generate: each section's last chord thins; the route's last chord settles", () => {
  /* fix round 1: strict - the section's end has fewer moving voices than the chord before it, over many seeds and forms */
  for (let seed = 0; seed < 300; seed++) for (const form of ["AABA", "rondo"]) {
    const g = gen({ seed, form }), cnt = (i) => voicesAt(g, i).slice(0, 3).filter(moving).length;
    for (const s of M.formSpans(form, 16).slice(0, -1)) assert.ok(cnt(s.b - 1) < cnt(s.b - 2), form + " seed " + seed + " chord " + (s.b - 1) + " thins");
  }
  for (const seed of [1, 2, 3]) {
    const g = gen({ seed });
    const last = voicesAt(g, 15);
    assert.ok(last.slice(0, 3).every((o) => o.role !== "off" && ["pedal", "swells"].includes(o.style)), JSON.stringify(last));
    assert.equal(last[3].mode, "hold");
  }
});
test("generate: zones and sections untouched, old chord overrides cleared, only differing keys written, all parsable", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "section", i: 1 }, "voice", "style", "pedal");
  M.setValue(c, { kind: "zone", id: "z" }, "sect", "role", "off");
  M.setValue(c, { kind: "chord", i: 20 }, "voice", "rate", "4n");
  for (const form of M.FORMS.map((f) => f.id)) for (const n of [1, 3, 12, 16]) for (const contrast of [0, 0.6, 1]) {
    const g = gen({ form, nchords: n, contrast, seed: n * 31 + 1 }, c);
    assert.deepEqual(g.over.section, c.over.section); assert.deepEqual(g.over.zone, c.over.zone);
    assert.ok(Object.keys(g.over.chord).every((i) => +i < n), "a chord past the route, or an old override");
    for (const [i, o] of Object.entries(g.over.chord)) for (const [v, x] of Object.entries(o)) {
      assert.ok(Object.keys(x).length, "empty voice " + i + v);
      for (const k of Object.keys(x)) assert.notDeepEqual(x[k], g[v][k], i + " " + v + "." + k + " equals the route's");
    }
    checkParsable(g);
  }
});
test("generate: the route's last chord, with one chord", () => {
  const g = gen({ nchords: 1 }), v = voicesAt(g, 0);
  assert.ok(v.slice(0, 3).every((o) => o.role !== "off" && ["pedal", "swells"].includes(o.style)), JSON.stringify(v));
  assert.equal(v[3].mode, "hold"); checkParsable(g);
});

/* Generate form, fix round 1 */
test("fix 1: the form covers the chords the core reads (NCHORD 64); the settle lands on the last of them", () => {
  assert.equal(M.NCHORD, 64);
  const k24 = Object.keys(gen({ nchords: 24 }).over.chord).map(Number);
  assert.ok(k24.includes(23) && k24.every((i) => i < 24), String(k24));   /* the settle lands on chord 24, past the old 16 */
  for (const n of [70, 100]) for (const seed of [1, 2, 3]) {
    const g = gen({ nchords: n, seed });
    assert.ok(Object.keys(g.over.chord).every((i) => +i < 64), n + ": " + Object.keys(g.over.chord));
    const last = voicesAt(g, 63);
    assert.ok(last.slice(0, 3).every((o) => o.role !== "off" && ["pedal", "swells"].includes(o.style)) && last[3].mode === "hold", JSON.stringify(last));
    for (let j = 0; j < 3; j++) assert.deepEqual(voicesAt(g, j), voicesAt(g, 16 + j), "the spans are over 64 chords");
  }
});
test("fix 2: a chord under a section that sets a voice's key writes the generated value, so the form plays", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "section", i: 1 }, "voice", "style", "pedal");
  M.setValue(c, { kind: "section", i: 1 }, "sect", "rate", "1n");
  M.setValue(c, { kind: "section", i: 1 }, "bass", "mode", "off");
  const at = []; for (let i = 0; i < 16; i++) at.push(i >= 4 && i < 12 ? [{ kind: "section", id: 1, t0: 0.25, t1: 0.75 }] : []);
  const res = (g, i, v, sec) => Object.assign({}, g[v], sec ? (g.over.section["1"] || {})[v] || {} : {}, (g.over.chord[String(i)] || {})[v] || {});
  for (const seed of [1, 2, 3, 4, 5, 6, 7, 8]) {
    const g = gen({ seed, at }, c), plain = gen({ seed });
    for (let i = 4; i < 12; i++) for (const [v, k] of [["voice", "style"], ["sect", "rate"], ["bass", "mode"]])
      assert.deepEqual(res(g, i, v, true)[k], res(plain, i, v, false)[k], "seed " + seed + " chord " + i + " " + v + "." + k);
    assert.deepEqual(g.over.section, c.over.section);
    checkParsable(g);
  }
});
test("fix 3: a' differs from A in one or two changes, never none, the octave either way", () => {
  const dirs = new Set();
  for (let seed = 0; seed < 500; seed++) {
    const g = gen({ seed, form: "sentence" }), A = voicesAt(g, 0).slice(0, 3), P = voicesAt(g, 4).slice(0, 3);
    let changes = 0;
    const dlo = A.map((o, i) => P[i].lo - o.lo);
    if (dlo[0] !== 0) { assert.ok(Math.abs(dlo[0]) === 12 && dlo.every((d, i) => d === dlo[0] && P[i].hi - A[i].hi === d), "seed " + seed + " octave " + dlo); changes++; dirs.add(dlo[0]); }
    A.forEach((o, i) => { const ks = Object.keys(Object.assign({}, o, P[i])).filter((k) => k !== "lo" && k !== "hi" && JSON.stringify(o[k]) !== JSON.stringify(P[i][k]));
      changes += ks.filter((k) => !(k === "spread" && ks.includes("rate"))).length; });   /* a strum's spread follows its rate (fix 5) */
    assert.ok(changes >= 1 && changes <= 2, "seed " + seed + ": " + changes + " changes");
  }
  assert.deepEqual([...dirs].sort(), [-12, 12]);
});
test("fix 4: every letter has exactly one mover above one sustainer, the support between", () => {
  const cnt = { mover: ["16n", "8n"], held: ["2n", "2n.", "1n"], support: ["4n", "4n."] };
  for (let seed = 0; seed < 1000; seed++) for (const contrast of [0.25, 0.5, 0.75]) for (const f of M.FORMS) {
    const g = gen({ seed, contrast, form: f.id });
    for (const s of M.formSpans(f.id, 16)) {
      const on = voicesAt(g, s.a).slice(0, 3).filter((o) => o.role !== "off"), by = (k) => on.filter((o) => cnt[k].includes(o.rate));
      const where = f.id + " " + contrast + " seed " + seed + " " + s.letter + ": " + JSON.stringify(on.map((o) => [o.style, o.rate, o.lo, o.hi]));
      assert.ok(by("mover").length === 1 && by("held").length === 1 && by("support").length === on.length - 2, where);
      const m = by("mover")[0], h = by("held")[0];
      assert.ok(m.lo > h.lo && m.hi > h.hi && by("support").every((x) => x.lo > h.lo && x.lo < m.lo), where);
    }
  }
});
test("fix 5 and 6: a strum's spread within 60% of its step; a swell's attack + release at most 0.95", () => {
  const STRIDE = { "16n": 1, "8n": 2, "8n.": 3, "4n": 4, "4n.": 6, "2n": 8, "2n.": 12, "1n": 16 };
  for (const bpm of [undefined, 72, 140]) for (let seed = 0; seed < 300; seed++) for (const form of ["AABA", "sentence"]) {
    const g = gen({ seed, form, bpm, contrast: 1 }), step = (r) => 60000 / (bpm || 96) / 4 * STRIDE[r];
    for (let i = 0; i < 16; i++) for (const o of voicesAt(g, i).slice(0, 3)) {
      if (o.style === "strum") assert.ok((o.tones.length - 1) * o.spread <= 0.6 * step(o.rate) + 1e-9, "seed " + seed + " " + JSON.stringify(o));
      if (o.style === "swells") assert.ok(o.attack + o.release <= 0.95 + 1e-9, "seed " + seed + " " + JSON.stringify(o));
    }
  }
});
test("fix 7: a one-chord section does not thin (the last chord still settles)", () => {
  for (let seed = 0; seed < 50; seed++) {
    const g = gen({ seed, nchords: 4 });
    assert.equal(g.over.chord["0"], undefined, "chord 1 is A"); assert.equal(g.over.chord["1"], undefined, "chord 2 is A");
    assert.deepEqual(voicesAt(g, 3).slice(0, 3).map((o) => ["pedal", "swells"].includes(o.style) && o.role !== "off"), [true, true, true]);
  }
});

/* Composition 2: motifs, chains, the melody voice (the cases of core/tests/composer_test.cpp's test_melody / test_cadence) */
const tri = (r) => r.notes.map((n) => [n.deg, n.start, n.len, n.oct || 0]);
const withChain = (chain, motif = M.motifDefault()) => { const c = M.defaults(); M.setMotif(c, motif); M.setChain(c, chain); return c; };
const step = (op, extra = {}, from = 0, to = 3) => [Object.assign({ op, from, to }, extra)];
test("tables: OPS, the melody role and the default motif", () => {
  assert.deepEqual(M.OPS.map((o) => o.id), CORE_STR.op);
  assert.deepEqual(M.OPS.filter((o) => o.arg).map((o) => [o.id, o.arg.min, o.arg.max, o.arg.def]), [["seq", -7, 7, 1], ["fragment", 1, 8, 2]]);
  assert.ok(M.ROLES.some((r) => r.id === "melody" && r.label === "melody") && M.ROLES.every((r) => CORE_STR.role.includes(r.id)));
  assert.deepEqual(M.motifDefault(), { basis: "chord", bars: 1, notes: [{ deg: 0, start: 0, len: 4 }, { deg: 2, start: 4, len: 4 }, { deg: 1, start: 8, len: 4 }, { deg: 0, oct: 1, start: 12, len: 4 }] });
});
test("motifStep: each op on the default motif, as comp::motif_step", () => {
  const at = (chain, chord = 0) => M.motifStep(withChain(chain), chord);
  assert.equal(at([]).op, "repeat");
  assert.deepEqual(tri(at([])), [[0, 0, 4, 0], [2, 4, 4, 0], [1, 8, 4, 0], [0, 12, 4, 1]]);
  assert.deepEqual(tri(at(step("seq", { arg: 1 }))), [[1, 0, 4, 0], [3, 4, 4, 0], [2, 8, 4, 0], [1, 12, 4, 1]]);
  assert.deepEqual(tri(at(step("invert"))), [[0, 0, 4, 0], [-2, 4, 4, 0], [-1, 8, 4, 0], [0, 12, 4, 1]]);   /* around the first note's degree */
  assert.deepEqual(tri(at(step("retro"))), [[0, 0, 4, 1], [1, 4, 4, 0], [2, 8, 4, 0], [0, 12, 4, 0]]);
  assert.deepEqual(tri(at(step("fragment", { arg: 2 }))), [[0, 0, 4, 0], [2, 4, 4, 0]]);
  const aug = at(step("augment")); assert.equal(aug.len16, 32);
  assert.deepEqual(tri(aug), [[0, 0, 8, 0], [2, 8, 8, 0], [1, 16, 8, 0], [0, 24, 8, 1]]);
  assert.deepEqual(tri(at(step("diminish"))), [[0, 0, 2, 0], [2, 2, 2, 0], [1, 4, 2, 0], [0, 6, 2, 1], [0, 8, 2, 0], [2, 10, 2, 0], [1, 12, 2, 0], [0, 14, 2, 1]]);
  assert.equal(at(step("invert", {}, 4, 5)).op, "repeat", "a chord outside the step");
  assert.equal(at(step("seq", { arg: 2 })).op, "seq");
});
test("motifStep: later chain step wins; invert uses the first note; clamps; one note a start; clip to the phrase", () => {
  const two = [{ op: "seq", arg: 1, from: 0, to: 5 }, { op: "retro", from: 2, to: 3 }];
  assert.equal(M.chainAt(withChain(two), 2).op, "retro"); assert.equal(M.chainAt(withChain(two), 4).op, "seq"); assert.equal(M.chainAt(withChain(two), 6), null);
  const inv = { bars: 1, notes: [{ deg: 2, start: 0, len: 4 }, { deg: 4, start: 4, len: 4 }] };
  assert.deepEqual(tri(M.motifStep(withChain(step("invert"), inv), 0)), [[2, 0, 4, 0], [0, 4, 4, 0]]);
  /* parse clamps: len to the bar's end (12 + 8 -> 4), deg -7..13, oct -2..2, more than 8 notes: 8, a start past the bar dropped */
  const wild = { bars: 1, notes: [{ deg: 99, oct: 9, start: 12, len: 8 }, { deg: -99, start: 16 }].concat([1, 2, 3, 4, 5, 6, 7, 8, 9].map((i) => ({ start: i }))) };
  const w = M.motifStep(withChain([], wild), 0); assert.equal(w.notes.length, 8);
  assert.deepEqual(tri(w)[0], [13, 12, 4, 2]);
  /* diminish: starts 0 and 1 land both on 0 and on 8, the later kept */
  const d = { notes: [{ deg: 0, start: 0, len: 4 }, { deg: 2, start: 1, len: 4 }] };
  assert.deepEqual(tri(M.motifStep(withChain(step("diminish", {}, 0, 0), d), 0)), [[2, 0, 2, 0], [2, 8, 2, 0]]);
  /* a two-bar motif augmented: the phrase stays 32, a note starting at 40 is dropped, one held past the end is clipped */
  const long = { bars: 2, notes: [{ deg: 0, start: 0, len: 4 }, { deg: 1, start: 12, len: 8 }, { deg: 2, start: 20, len: 4 }] };
  assert.deepEqual(tri(M.motifStep(withChain(step("augment"), long), 0)), [[0, 0, 8, 0], [1, 24, 8, 0]]);
  assert.equal(M.motifStep(withChain([], { notes: [] }), 0).notes.length, 0);
  assert.equal(M.motifStep(M.defaults(), 0).notes.length, 0, "no motif: the core has none");
});
test("motifStep: a cadence's last chord puts the latest note last, held to the phrase's end", () => {
  const m = { notes: [{ deg: 0, start: 8, len: 2 }, { deg: 2, start: 0, len: 4 }, { deg: 1, start: 4, len: 2 }] };
  const c = withChain([{ op: "cadence", cad: "half", others: "stop", strength: 0.5, from: 0, to: 1 }], m);
  const in0 = M.motifStep(c, 0), end = M.motifStep(c, 1);
  assert.equal(in0.last, false); assert.deepEqual(tri(in0), [[0, 8, 2, 0], [2, 0, 4, 0], [1, 4, 2, 0]]);
  assert.equal(end.last, true); assert.equal(end.cad, "half"); assert.equal(end.others, "stop");
  assert.deepEqual(tri(end), [[1, 4, 2, 0], [2, 0, 4, 0], [0, 8, 8, 0]]);
});
test("cell: a melody voice shows its development; summary says Motif", () => {
  const c = withChain([{ op: "seq", arg: 2, from: 0, to: 0 }, { op: "seq", arg: -1, from: 1, to: 1 }, { op: "invert", from: 2, to: 2 }, { op: "fragment", arg: 2, from: 3, to: 3 },
    { op: "cadence", from: 4, to: 4 }, { op: "retro", from: 5, to: 5 }, { op: "augment", from: 6, to: 6 }, { op: "diminish", from: 7, to: 7 }]);
  c.voice.role = "melody";
  const lab = (i) => M.cell(c, i)[0].label;
  assert.deepEqual([0, 1, 2, 3, 4, 5, 6, 7, 8].map(lab), ["A↑2", "A↓1", "A inv", "A frag 2", "cad.", "A retro", "A aug", "A dim", "A"]);
  assert.deepEqual(M.cell(c, 0)[0], { style: "melody", label: "A↑2", source: "route" });
  assert.deepEqual(M.cell(c, 0)[1], { style: "free", source: "route" });
  M.setValue(c, { kind: "chord", i: 0 }, "voice", "role", "off"); assert.deepEqual(M.cell(c, 0)[0], { style: "off", source: "chord" });
  M.setValue(c, { kind: "chord", i: 0 }, "voice", "role", "melody"); assert.equal(M.cell(c, 0)[0].source, "chord");
  const s = M.defaults(); s.voice.role = "melody"; s.sect.style = "strum"; s.v3.style = "pedal";
  assert.equal(M.summary(s), "Motif · Strum · Pedal");
});
test("cell: a melody's label takes its chain step's form letter (the mock: A, A↑2, B inv, cad.) when told the chord count", () => {
  const c = withChain([{ op: "seq", arg: 2, from: 4, to: 7 }, { op: "invert", from: 8, to: 10 }, { op: "cadence", from: 11, to: 11 }]);
  c.voice.role = "melody"; c.form = { type: "AABA", contrast: 0.6, seed: 1 };
  const lab = (i) => M.cell(c, i, [], 16)[0].label;
  assert.deepEqual([0, 4, 8, 11, 12].map(lab), ["A", "A↑2", "B inv", "cad.", "A"]);
  assert.equal(M.cell(c, 8)[0].label, "A inv");            /* no chord count: no form letters */
  c.form = { type: "sentence", contrast: 0.6, seed: 1 };    /* a a′ b: the letter's first, upper case */
  assert.deepEqual([0, 4, 8].map(lab), ["A", "A↑2", "B inv"]);
  assert.equal(M.FIELDS.common.filter((f) => f.k === "role")[0].label, "role");
});
test("generate: a motif and a chain follow the form; one melody voice at a time", () => {
  for (const form of M.FORMS) for (const n of [1, 3, 8, 16, 40]) for (let seed = 1; seed < 40; seed++) {
    const g = gen({ form: form.id, nchords: n, seed });
    assert.equal(g.motif.basis, "chord"); assert.equal(g.motif.bars, 1);
    assert.ok(g.motif.notes.length >= 3 && g.motif.notes.length <= 6, JSON.stringify(g.motif));
    assert.ok(g.motif.notes.every((x) => Number.isInteger(x.deg) && x.deg >= 0 && x.deg <= 4), JSON.stringify(g.motif));
    const starts = g.motif.notes.map((x) => x.start); assert.ok(starts.every((x, i) => x >= 0 && x < 16 && (i === 0 || x > starts[i - 1])), String(starts));
    for (let i = 0; i < n; i++) assert.equal(voicesAt(g, i).slice(0, 3).filter((o) => o.role === "melody").length, 1, form.id + " n" + n + " seed " + seed + " chord " + i);
    checkParsable(g);
    const last = M.chainAt(g, n - 1); assert.deepEqual([last.op, last.cad, last.to], ["cadence", "full", n - 1]);
    for (const sp of M.formSpans(form.id, n)) {
      assert.ok(sp.b - sp.a === 1 || M.chainAt(g, sp.a).from === sp.a);
      if (sp.b < n) { const e = M.chainAt(g, sp.b - 1); assert.deepEqual([e.op, e.cad], ["cadence", "half"]); }
    }
  }
});
test("generate: A repeats, A' sequences by 1 or 2, B inverts or fragments, C retrogrades", () => {
  const seen = new Set();
  for (let seed = 1; seed < 60; seed++) {
    const g = gen({ form: "ABAC", seed }), sp = M.formSpans("ABAC", 16), op = (k) => M.chainAt(g, sp[k].a);
    assert.deepEqual([op(0).op, op(2).op], ["repeat", "repeat"]);
    assert.ok(["invert", "fragment"].includes(op(1).op)); assert.equal(op(3).op, "retro");
    seen.add(op(1).op);
    const gs = gen({ form: "sentence", seed }), s = M.chainAt(gs, M.formSpans("sentence", 16)[1].a);
    assert.ok(s.op === "seq" && [-2, -1, 1, 2].includes(s.arg), JSON.stringify(s)); seen.add("seq" + s.arg);
    assert.equal(M.chainAt(gs, 0).op, "repeat");
  }
  assert.deepEqual([...seen].sort(), ["fragment", "invert", "seq-1", "seq-2", "seq1", "seq2"]);
});
test("generate: no chords, no chain (the motif is still drawn)", () => {
  const e = gen({ nchords: 0 }); assert.deepEqual(e.chain, []); assert.ok(e.motif.notes.length >= 3);
});
test("generate: a motif has a shape (500 seeds)", () => {
  for (let seed = 0; seed < 500; seed++) {
    const g = gen({ seed }), n = g.motif.notes, d = n.map((x) => x.deg), w = "seed " + seed + " " + d;
    assert.ok(new Set(d).size >= 3, "distinct " + w);
    assert.ok(d.every((x, i) => i < 2 || !(x === d[i - 1] && x === d[i - 2])), "3 in a row " + w);
    assert.ok(d.some((x, i) => i && x > d[i - 1]) && d.some((x, i) => i && x < d[i - 1]), "contour " + w);
    assert.ok([0, 2].includes(d[0]) && d[d.length - 1] !== d[0], "ends " + w);
    assert.ok(d.filter((x, i) => i && Math.abs(x - d[i - 1]) > 1).length <= 1, "leaps " + w);
    assert.ok(new Set(n.map((x) => x.len)).size >= 2, "rhythm " + w);
    assert.ok(n.every((x, i) => x.start >= 0 && x.start + x.len <= 16 && (!i || x.start > n[i - 1].start)), "grid " + w);
  }
});

/* composition 3, task 7: relations (core/composer.cpp rel_of is the authority) */
const REL_SRC = CORE.slice(CORE.indexOf("static void rel_of"), CORE.indexOf("int relations_in_force"));
const REL_STR = {
  type: strTable(/TYPES\[NREL\] = \{([^}]*)\}/), transform: strTable(/TR\[3\] = \{([^}]*)\}/), source: strTable(/SRC\[3\] = \{([^}]*)\}/),
  split: strTable(/SPL\[3\] = \{([^}]*)\}/), motion: strTable(/MOT\[4\] = \{([^}]*)\}/), meet: strTable(/MEET\[3\] = \{([^}]*)\}/)
};
const REL_KEYS = new Set([...REL_SRC.matchAll(/\bx->(?:n|s|get)\("(\w+)"/g)].map((m) => m[1]));
const allFields = () => Object.values(M.RELATIONS).flatMap((t) => t.fields);
test("relations: the types, strings, keys, ranges and defaults are the core's", () => {
  assert.deepEqual(Object.keys(M.RELATIONS), REL_STR.type);
  for (const [type, t] of Object.entries(M.RELATIONS)) {
    assert.ok(t.label && t.fields.length, type);
    for (const f of t.fields) {
      assert.ok(REL_KEYS.has(f.k), type + " writes " + f.k + ", which the core never reads");
      if (REL_STR[f.k]) assert.deepEqual(f.options.map((o) => o.id), REL_STR[f.k], f.k);
      const m = REL_SRC.match(new RegExp('x->[ns]\\("' + f.k + '", ("[^"]*"|-?[\\d.]+)\\)'));
      if (m && f.kind === "knob") assert.equal(f.def, +m[1], f.k + " default");
      if (m && f.kind === "menu") assert.equal(f.def, JSON.parse(m[1]), f.k + " default");
    }
  }
  const by = (k) => allFields().find((f) => f.k === k);
  const rng = { delay: [1, 32], trans: [-7, 7], gap: [1, 16], every: [2, 4], offset: [0, 120], ornament: [0, 1], strength: [0, 1], cap: [1, 32] };
  for (const [k, [lo, hi]] of Object.entries(rng)) assert.deepEqual([by(k).min, by(k).max], [lo, hi], k);
  assert.deepEqual(by("trigger").options.map((o) => o.id), ["phrase", "enter"]); assert.equal(by("trigger").def, "phrase");
  assert.deepEqual(by("octave").options.map((o) => o.id), ["unison", "octave"]); assert.equal(by("octave").def, "unison");
  assert.equal(by("mask").def, "x.x.x.x.x.x.x.x.");   /* the core's 0x5555 */
  assert.deepEqual(by("weight").def, [1, 1, 1]); assert.deepEqual([by("weight").min, by("weight").max], [0, 4]);
  for (const k of ["relations", "a", "b", "c", "from", "to", "zone"]) assert.ok(CORE.includes('"' + k + '"'), k);
});
const base = () => { const c = M.defaults(); c.voice.role = "melody"; return c; };
test("relations: add writes the type's defaults and the span; a == b and bad voices are refused", () => {
  const c = base();
  assert.equal(M.addRelation(c, "canon", 0, 1, { from: 2, to: 5 }), 0);
  assert.deepEqual(c.relations[0], { type: "canon", a: 0, b: 1, from: 2, to: 5, delay: 8, trans: 0, transform: "none" });
  assert.equal(M.addRelation(c, "hocket", 0, 1), 1);
  assert.deepEqual([c.relations[1].from, c.relations[1].to, c.relations[1].split, c.relations[1].mask], [0, 63, "alt", "x.x.x.x.x.x.x.x."]);
  const n = c.relations.length;
  for (const bad of [["canon", 1, 1], ["canon", 0, 3], ["canon", -1, 1], ["nope", 0, 1]]) assert.equal(M.addRelation(c, ...bad), -1, bad.join());
  assert.equal(c.relations.length, n);
  const d = base(); assert.equal(M.addRelation(d, "density", 0, 1), 0);
  assert.equal(M.addRelation(d, "density", 0, 1), -1, "only the first density relation acts, so a second is refused"); assert.equal(d.relations.length, 1);
  for (let i = 0; i < 20; i++) M.addRelation(c, "canon", 0, 1);
  assert.equal(c.relations.length, 12, "REL_MAX");
});
test("relations: set validates and clamps; remove; each edit is one history step", () => {
  const c = base(), h = M.history(50); h.push(c);
  M.addRelation(c, "canon", 0, 1, { from: 0, to: 7 }); h.push(c);
  assert.ok(M.setRelation(c, 0, "delay", 99)); assert.equal(c.relations[0].delay, 32); h.push(c);
  assert.ok(M.setRelation(c, 0, "trans", -3)); assert.ok(M.setRelation(c, 0, "transform", "invert"));
  assert.equal(M.setRelation(c, 0, "transform", "sideways"), false); assert.equal(c.relations[0].transform, "invert");
  assert.equal(M.setRelation(c, 0, "b", 0), false); assert.equal(M.setRelation(c, 0, "a", 5), false);
  assert.equal(M.setRelation(c, 0, "to", -1), false); assert.equal(M.setRelation(c, 0, "from", 9), false);
  assert.ok(M.setRelation(c, 0, "b", 2)); assert.ok(M.setRelation(c, 0, "c", 1)); assert.equal(c.relations[0].c, 1);
  assert.equal(M.setRelation(c, 0, "c", 2), false, "c repeats b");
  assert.ok(M.setRelation(c, 0, "c", null)); assert.ok(!("c" in c.relations[0]));
  assert.ok(M.setRelation(c, 0, "zone", "p1")); assert.equal(c.relations[0].zone, "p1"); assert.ok(M.setRelation(c, 0, "zone", null)); assert.ok(!("zone" in c.relations[0]));
  assert.equal(M.setRelation(c, 3, "delay", 2), false);
  M.addRelation(c, "hocket", 0, 1); M.setRelation(c, 1, "split", "mask");
  assert.equal(M.setRelation(c, 1, "mask", "xx"), false); assert.ok(M.setRelation(c, 1, "mask", "xxxx............"));
  M.addRelation(c, "density", 0, 1); assert.ok(M.setRelation(c, 2, "weight", [1, 9, 0])); assert.deepEqual(c.relations[2].weight, [1, 4, 0]);
  M.removeRelation(c, 1); assert.deepEqual(c.relations.map((r) => r.type), ["canon", "density"]);
  assert.equal(M.removeRelation(c, 7), false);
  h.push(c); assert.equal(h.undo().relations.length, 1); assert.equal(h.undo().relations[0].delay, 8, "the delay edit is one step");
  assert.equal(h.undo().relations, undefined, "the add is one step");
});
test("relations: relationsAt honours the span and the zone", () => {
  const c = base();
  M.addRelation(c, "canon", 0, 1, { from: 2, to: 4 }); M.addRelation(c, "call", 0, 1); M.setRelation(c, 1, "zone", "z1");
  assert.deepEqual([1, 2, 4, 5].map((i) => M.relationsAt(c, i, [])), [[], [0], [0], []]);
  assert.deepEqual(M.relationsAt(c, 3, ["z1"]), [0, 1]); assert.deepEqual(M.relationsAt(c, 3, ["z2"]), [0]);
  assert.deepEqual(M.relationsAt(M.defaults(), 0, []), []);
});
test("relations: links for drawing, and cells marked", () => {
  const c = base();
  M.addRelation(c, "canon", 0, 1, { from: 2, to: 5 }); M.setRelation(c, 0, "trans", 2);
  M.addRelation(c, "hocket", 1, 2, { from: 0, to: 1 }); M.addRelation(c, "call", 0, 2, { from: 4, to: 63 }); M.setRelation(c, 2, "source", "half");
  M.addRelation(c, "density", 0, 1, { from: 6, to: 7 });
  assert.deepEqual(M.links(c, 8).map((l) => [l.i, l.type, l.a, l.b, l.from, l.to, l.label]), [
    [0, "canon", 0, 1, 2, 5, "canon · ½ bar · +2"], [1, "hocket", 1, 2, 0, 1, "hocket · alt"], [2, "call", 0, 2, 4, 7, "answers · half"], [3, "density", 0, 1, 6, 7, "density · 8"]]);
  assert.equal(M.links(c, 3).length, 2); assert.equal(M.links(c, 2).length, 1, "a relation starting past the route is not drawn");
  assert.equal(M.links(c, 8)[1].c, undefined);
  const at = (i) => M.cell(c, i, [], 8);
  assert.equal(at(3)[1].rel, "canon"); assert.equal(at(3)[1].style, "free"); assert.equal(at(3)[0].rel, undefined);
  assert.equal(at(0)[1].rel, "hocket"); assert.equal(at(0)[2].rel, "hocket"); assert.equal(at(6)[2].rel, "answer");
  assert.equal(M.cell(M.defaults(), 3, [], 8)[1].rel, undefined);
  assert.equal(M.cell(c, 3, [], 8)[0].label, "A", "the melody keeps its label");
});
import { createHash } from "node:crypto";
const relsOf = (g) => g.relations || [];
const letterSpans = (form, n = 16) => M.formSpans(form, n);
const FORM_IDS = ["AABA", "ABAB", "ABAC", "sentence", "rondo"];
test("generate relations: 'some' writes one relation per letter span (cadence chord left out) and a unison on each cadence chord", () => {
  for (const form of FORM_IDS) for (let seed = 1; seed < 40; seed++) {
    const g = gen({ form, seed, contrast: 0.7 }), sp = letterSpans(form), rs = relsOf(g), un = rs.filter((r) => r.type === "unison"), rest = rs.filter((r) => r.type !== "unison");
    assert.equal(g.form.relations, "some"); assert.equal(rs.length, 2 * sp.length, form + seed);
    assert.equal(un.length, sp.length);
    un.forEach((u, i) => { assert.deepEqual([u.meet, u.octave, u.from, u.to], ["cadence", "octave", sp[i].b - 1, sp[i].b - 1]); assert.equal(voicesAt(g, u.from)[u.a].role, "melody"); assert.equal(new Set([u.a, u.b, u.c]).size, 3); });
    sp.forEach((s, i) => assert.deepEqual([rest[i].from, rest[i].to], [s.a, s.b - 2], "over the letter, not its cadence chord"));
    assert.ok(!rs.some((r) => r.type === "density"));
  }
});
test("generate relations: A letters gentle, a repeated A the same, A' one parameter off, B/C by contrast", () => {
  const seenHi = new Set(), body = (x) => { const y = Object.assign({}, x); delete y.from; delete y.to; return y; };
  for (let seed = 1; seed < 150; seed++) {
    const g = gen({ form: "AABA", seed, contrast: 0.7 }), r = relsOf(g).filter((x) => x.type !== "unison");
    assert.ok(["hetero", "motion"].includes(r[0].type), "A is gentle"); assert.deepEqual(body(r[0]), body(r[1]), "A repeated"); assert.deepEqual(body(r[0]), body(r[3]));
    if (r[0].type === "hetero") assert.ok(r[0].offset >= 30 && r[0].offset <= 60 && r[0].ornament >= 0 && r[0].ornament <= 0.3 + 1e-9);
    else assert.ok(r[0].motion === "contrary" && r[0].strength >= 0.6 && r[0].strength <= 1);
    seenHi.add(r[2].type); const b = r[2];
    if (b.type === "canon") { assert.ok(b.delay >= 4 && b.delay <= 8 && Math.abs(b.trans) === 2 && b.transform === "none", "invert only at contrast >= 0.8"); }
    if (b.type === "call") assert.ok(["half", "mirror"].includes(b.source));
    if (b.type === "hocket") assert.ok(b.split === "alt" || (b.split === "every" && b.every === 2));
    const lo = relsOf(gen({ form: "AABA", seed, contrast: 0.4 })).filter((x) => x.type !== "unison")[2];
    assert.ok(lo.type === "motion" && ["par3", "par6"].includes(lo.motion), "low contrast: par3/par6, no canon/call/hocket");
  }
  assert.deepEqual([...seenHi].sort(), ["call", "canon", "hocket"]);
  const inv = new Set(); for (let seed = 1; seed < 150; seed++) relsOf(gen({ form: "AABA", seed, contrast: 0.9 })).forEach((x) => x.type === "canon" && inv.add(x.transform));
  assert.deepEqual([...inv].sort(), ["invert", "none"]);
  for (let seed = 1; seed < 60; seed++) {
    const r = relsOf(gen({ form: "sentence", seed, contrast: 0.7 })).filter((x) => x.type !== "unison"), vary = ["offset", "ornament", "strength"].filter((k) => r[0][k] !== r[1][k]);
    assert.equal(r[0].type, r[1].type); assert.equal(vary.length, 1, "a' varies exactly one parameter"); assert.equal(r[0].a, r[1].a);
  }
});
test("generate relations: 'rich' adds one shared density and a second relation in B; 'none' writes none", () => {
  let second = 0;
  for (const form of FORM_IDS) for (let seed = 1; seed < 40; seed++) {
    const some = relsOf(gen({ form, seed, contrast: 0.6 })), g = gen({ form, seed, contrast: 0.6, relations: "rich" }), rs = relsOf(g), d = rs.filter((r) => r.type === "density");
    assert.equal(g.form.relations, "rich"); assert.equal(d.length, 1); assert.deepEqual([d[0].from, d[0].to], [0, 15]);
    assert.ok(d[0].cap >= 10 && d[0].cap <= 14); assert.deepEqual([...d[0].weight].sort(), [1, 1, 2]); assert.equal(d[0].weight[d[0].a], 2, "the melody weighs 2");
    assert.ok(rs.length > some.length && rs.length <= 12, form + seed); if (rs.length > some.length + 1) second++;
    for (const r of some) assert.ok(rs.some((x) => JSON.stringify(x) === JSON.stringify(r)), "'rich' keeps what 'some' writes");
    const none = gen({ form, seed, contrast: 0.6, relations: "none" }); assert.equal(none.relations, undefined); assert.equal(none.form.relations, "none");
  }
  assert.ok(second > 100, "a second relation in B when its third voice sounds: " + second);
});
test("generate relations: valid for the model, leaders never Free, deterministic, earlier output unchanged", () => {
  for (const form of FORM_IDS) for (const relations of ["some", "rich"]) for (let seed = 1; seed < 60; seed++) for (const contrast of [0, 0.45, 0.5, 0.85]) {
    const g = gen({ form, seed, contrast, relations }), c = M.defaults();
    assert.equal(JSON.stringify(g), JSON.stringify(gen({ form, seed, contrast, relations })), "deterministic");
    assert.ok(g.relations.length <= 12); assert.ok(g.relations.filter((r) => r.type === "density").length <= 1);
    for (const r of g.relations) {
      const i = M.addRelation(c, r.type, r.a, r.b, { from: r.from, to: r.to }); assert.ok(i >= 0, "addRelation took it");
      if (r.c !== undefined) assert.ok(M.setRelation(c, i, "c", r.c));
      for (const k of Object.keys(r)) { if (!["type", "a", "b", "c", "from", "to"].includes(k)) assert.ok(M.setRelation(c, i, k, r[k]), k); }
      assert.deepEqual(c.relations[i], r); assert.notEqual(r.a, r.b);
      for (let ch = r.from; ch <= r.to; ch++) { const v = voicesAt(g, ch)[r.a]; assert.ok(v.role === "melody" || v.style !== "free", "leader " + r.a + " at " + ch); }
    }
  }
  const out = [];
  for (const form of M.FORMS.map((f) => f.id)) for (const contrast of [0, 0.3, 0.5, 0.8, 1]) for (let seed = 1; seed <= 40; seed++) for (const nchords of [1, 3, 16]) {
    const g = M.generate(M.defaults(), { form, contrast, seed, nchords }); delete g.relations; delete g.form.relations; out.push(g);
  }
  assert.equal(createHash("sha256").update(JSON.stringify(out)).digest("hex"), "01fd863e9cb1f585220bae1d67ea17c646af67b446a975e15800cb2c314d6662", "textures, motif, chain as before relations were drawn");
  assert.equal(gen({ form: "AABA", nchords: 0, contrast: 1 }).relations, undefined);
  assert.equal(gen({ form: "AABA", seed: 3 }).form.relations, "some", "the default");
});

/* composition 4, task 7: the four deeper styles, the walking bass, ornaments, glides and micro-tuning (core/composer.cpp voice_of / bass_of are the authority) */
const HPP = readFileSync(new URL("../core/composer.hpp", import.meta.url), "utf8");
const VOICE_SRC = CORE.slice(CORE.indexOf("static void voice_of"), CORE.indexOf("static void bass_of")), BASS_SRC = CORE.slice(CORE.indexOf("static void bass_of"), CORE.indexOf("static const char *const VKEYS"));
const tbl = (name) => [...CORE.match(new RegExp("\\b" + name + "\\[\\d\\] = \\{([^}]*)\\}"))[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]);
const NEW_STYLES = ["ostinato", "chorale", "tintinnabuli", "pointillist"];
const f4 = (list, k) => list.find((f) => f.k === k);
const ids = (f) => f.options.map((o) => o.id);
/* a number key's parse: [fallback, lo, hi] from clamp8/clampf(j->n("k", d), lo, hi) */
const numParse = (src, k) => { const m = src.match(new RegExp('clamp(?:8|f)\\(j->n\\("' + k + '", ([-\\d.]+)\\), ([-\\d.]+), ([-\\d.]+)\\)')); return m && m.slice(1).map(Number); };
const strParse = (src, k) => src.match(new RegExp('j->s\\("' + k + '", "([^"]*)"\\)'))[1];
const hppDef = (name) => +HPP.match(new RegExp("\\b" + name + " = ([-\\d.]+)"))[1];
test("composition 4: the styles, their strings and the table order are the core's", () => {
  assert.deepEqual(M.STYLES.map((s) => s.id), strTable(/STYLES\[NSTYLE\] = \{([^}]*)\}/));
  assert.deepEqual(NEW_STYLES.map(M.label), ["Ostinato", "Chorale", "Tintinnabuli", "Pointillist"]);
  assert.deepEqual(ids(f4(M.FIELDS.ostinato, "refit")), tbl("RF")); assert.deepEqual(ids(f4(M.FIELDS.chorale, "voicing")), tbl("VO"));
  assert.deepEqual(ids(f4(M.FIELDS.tintinnabuli, "tmode")), tbl("TM")); assert.deepEqual(ids(f4(M.ORNAMENTS, "orn")), tbl("OR"));
  assert.deepEqual(ids(f4(M.ORNAMENTS, "glide_iv")), tbl("GI")); assert.deepEqual(ids(f4(M.BASS, "approach")), tbl("A"));
  assert.deepEqual(ids(f4(M.BASS, "mode")), tbl("M"));
  assert.deepEqual(ids(f4(M.FIELDS.chorale, "restrike")), CORE_STR.rate); assert.deepEqual(ids(f4(M.BASS, "wrate")), CORE_STR.rate);
  assert.equal(M.summary(Object.assign(M.defaults(), { voice: { style: "chorale", role: "accomp" }, sect: { style: "tintinnabuli", role: "accomp" }, v3: { style: "pointillist", role: "accomp" } })), "Chorale · Tintinnabuli · Pointillist");
});
test("composition 4: every new key is one the core reads; its range and default are the core's", () => {
  const fields = [].concat(...NEW_STYLES.map((s) => M.FIELDS[s]), M.ORNAMENTS), bassNew = M.BASS.slice(3);
  const keys = new Set(fields.map((f) => f.k));
  ["grid", "acc", "refit", "voicing", "voices", "restrike", "strict", "answers", "tmode", "silence", "plen", "orn", "orn_every", "orn_prob", "orn_speed", "glide", "glide_iv", "micro"].forEach((k) => assert.ok(keys.has(k), k));
  assert.deepEqual(bassNew.map((f) => f.k), ["approach", "before", "wrate"]);
  for (const f of fields) assert.ok(CORE_KEYS.has(f.k) || VOICE_SRC.includes('get("' + f.k + '")'), f.k);
  for (const f of bassNew) assert.ok(BASS_SRC.includes('get("' + f.k + '")'), f.k);
  for (const f of fields.concat(bassNew)) {
    const src = bassNew.includes(f) ? BASS_SRC : VOICE_SRC;
    if (f.kind === "knob") { const [d, lo, hi] = numParse(src, f.k); assert.deepEqual([f.def, f.min, f.max], [d, lo, hi], f.k); }
    else if (f.kind === "menu" && f.k !== "restrike" && f.k !== "wrate") assert.equal(f.def, strParse(src, f.k), f.k);
  }
  assert.equal(f4(M.FIELDS.chorale, "restrike").def, strParse(VOICE_SRC, "restrike") === "1n" ? "1n" : "?"); assert.equal(f4(M.BASS, "wrate").def, strParse(BASS_SRC, "wrate"));
  /* the struct defaults of composer.hpp */
  const d = (s, k) => f4(M.FIELDS[s], k).def, STRIDE = { "16n": 1, "1n": 16, "4n": 4 };
  assert.equal(d("chorale", "voices"), hppDef("nvoices")); assert.equal(STRIDE[d("chorale", "restrike")], hppDef("restrike")); assert.equal(d("chorale", "strict"), hppDef("strict"));
  assert.equal(d("tintinnabuli", "answers"), hppDef("tvoice")); assert.equal(d("pointillist", "plen"), hppDef("plen"));
  assert.deepEqual(d("pointillist", "silence"), [hppDef("sil_min"), hppDef("sil_max")]);
  assert.equal(f4(M.ORNAMENTS, "orn_prob").def, hppDef("orn_prob")); assert.equal(f4(M.ORNAMENTS, "orn_speed").def, hppDef("orn_speed")); assert.equal(f4(M.ORNAMENTS, "orn_every").def, hppDef("orn_every"));
  assert.equal(f4(M.ORNAMENTS, "glide").def, hppDef("glide_ms")); assert.deepEqual(f4(M.ORNAMENTS, "micro").def, [0, 0, 0, 0, 0]);
  assert.equal(f4(M.BASS, "before").def, hppDef("before")); assert.equal(STRIDE[f4(M.BASS, "wrate").def], hppDef("wrate"));
  /* silence 0..8 s (min, max each), micro -50..50 cents, as the core clamps them */
  const s = f4(M.FIELDS.pointillist, "silence"), m = f4(M.ORNAMENTS, "micro");
  assert.deepEqual([s.kind, s.min, s.max], ["range", 0, 8]); assert.ok(VOICE_SRC.includes("x->num : v.sil_min, 0, 8)")); assert.deepEqual([m.min, m.max], [-50, 50]); assert.ok(VOICE_SRC.includes("x->num + 50, 0, 100) - 50"));
  /* the ostinato plays as an arpeggio: the same kinds, the core's own grid (0 1 2 ...) */
  assert.equal(f4(M.FIELDS.ostinato, "grid").kind, "grid"); assert.equal(f4(M.FIELDS.ostinato, "acc").kind, "accents");
  assert.deepEqual(f4(M.FIELDS.ostinato, "grid").def, Array.from({ length: 16 }, (_, i) => i % 3)); assert.deepEqual(f4(M.FIELDS.ostinato, "acc").def, Array(16).fill(70));
});
test("composition 4: effective shows each style's fields and defaults; ornaments and glides on every voice", () => {
  for (const st of NEW_STYLES) {
    const c = M.defaults(); M.setValue(c, { kind: "route" }, "sect", "style", st);
    const e = M.effective(c, { kind: "route" }, "sect");
    for (const f of M.FIELDS[st].concat(M.ORNAMENTS)) assert.deepEqual(e.values[f.k], f.def, st + " " + f.k);
    assert.equal(e.values.tones, undefined); assert.equal(e.values.grid !== undefined, st === "ostinato");
  }
  const e = M.effective(M.defaults(), { kind: "route" }, "voice"); assert.equal(e.values.orn, "none"); assert.deepEqual(e.values.micro, [0, 0, 0, 0, 0]);
  const b = M.effective(M.defaults(), { kind: "route" }, "bass"); assert.equal(b.values.approach, "chromatic"); assert.equal(b.values.before, 1); assert.equal(b.values.wrate, "4n");
  const c = M.defaults(); M.setValue(c, { kind: "chord", i: 3 }, "voice", "orn", "trill");
  const x = M.effective(c, { kind: "chord", i: 3 }, "voice"); assert.equal(x.values.orn, "trill"); assert.equal(x.source.orn, "here"); assert.equal(x.source.glide, "route");
  M.clearValue(c, { kind: "chord", i: 3 }, "voice", "orn"); assert.equal(c.over.chord["3"], undefined);
  const r = M.defaults(); M.setValue(r, { kind: "route" }, "voice", "micro", [10, 0, 0, 0, -5]); M.clearValue(r, { kind: "route" }, "voice", "micro");
  assert.deepEqual(M.effective(r, { kind: "route" }, "voice").values.micro, [0, 0, 0, 0, 0]);
});
test("composition 4: choosing a new style writes its pattern, as the inspector shows it", () => {
  for (const scope of [{ kind: "route" }, { kind: "chord", i: 2 }, { kind: "zone", id: "z" }]) {
    const c = M.defaults(); M.setValue(c, scope, "voice", "style", "ostinato");
    assert.deepEqual(M.scopeObj(c, scope, false).voice.grid, f4(M.FIELDS.ostinato, "grid").def);
    assert.deepEqual(M.effective(c, scope, "voice").values.grid, M.scopeObj(c, scope, false).voice.grid);
    for (const st of ["chorale", "tintinnabuli", "pointillist"]) { M.setValue(c, scope, "sect", "style", st); const o = M.scopeObj(c, scope, false).sect; assert.equal(o.style, st); assert.equal(o.grid, undefined); assert.equal(o.tones, undefined); }
  }
  const c = M.defaults(); M.setValue(c, { kind: "route" }, "bass", "mode", "walk");
  assert.equal(M.effective(c, { kind: "route" }, "bass").values.mode, "walk");
  M.setValue(c, { kind: "chord", i: 1 }, "voice", "style", "pointillist");
  assert.equal(M.cell(c, 1, [])[0].style, "pointillist");
});
test("composition 4: generate is byte-identical to before the new styles", () => {
  const out = [];
  for (const form of M.FORMS.map((f) => f.id)) for (const contrast of [0, 0.3, 0.5, 0.8, 1]) for (const relations of ["none", "some", "rich"]) for (let seed = 1; seed <= 30; seed++) for (const nchords of [0, 1, 3, 16, 70])
    out.push(M.generate(M.defaults(), { form, contrast, seed, nchords, relations, bpm: seed % 2 ? 96 : 140 }));
  assert.equal(createHash("sha256").update(JSON.stringify(out)).digest("hex"), "8194807cddc3947b3cd25c907091e5a477128924d676df3986b592272ab9a6fe");
});
