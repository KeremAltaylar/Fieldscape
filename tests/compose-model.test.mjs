import test from "node:test";
import assert from "node:assert/strict";
await import("../web/compose-model.js");      /* a browser script: it sets globalThis.FsCompose */
const M = globalThis.FsCompose;

test("defaults are today's sound, written out", () => {
  const c = M.defaults();
  assert.equal(c.voice.style, "free"); assert.equal(c.sect.style, "free"); assert.equal(c.v3.style, "free");
  assert.equal(c.bass.mode, "hold"); assert.deepEqual(c.over, { chord: {}, section: {}, zone: {} });
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
  pedal: strTable(/P\[3\] = \{([^}]*)\}/), dir: strTable(/D\[3\] = \{([^}]*)\}/), mode: strTable(/M\[3\] = \{([^}]*)\}/)
};
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
  assert.equal(g.seed, 7); assert.deepEqual(g.form, { type: "AABA", contrast: 0.6, seed: 7 });
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
  const busy = (o) => o.role !== "off" && !["pedal", "swells"].includes(o.style);
  for (const seed of [1, 2, 3]) {
    const g = gen({ seed }), cnt = (i) => voicesAt(g, i).slice(0, 3).filter(busy).length;
    for (const end of [3, 7, 11]) assert.ok(cnt(end) < cnt(end - 1) || cnt(end - 1) === 0, "seed " + seed + " chord " + end + " thins");
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
