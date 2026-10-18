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
