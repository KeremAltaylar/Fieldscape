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
  assert.deepEqual(c.over.chord["4"], { voice: { style: "strum" } });
  const e = M.effective(c, { kind: "chord", i: 4 }, "voice");
  assert.equal(e.values.style, "strum"); assert.equal(e.source.style, "here"); assert.equal(e.source.rate, "route");
  M.clearValue(c, { kind: "chord", i: 4 }, "voice", "style");
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
