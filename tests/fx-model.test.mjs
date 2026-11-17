import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
await import("../web/fx-model.js");      /* a browser script: it sets globalThis.FsFxModel */
const M = globalThis.FsFxModel;
const hpp = readFileSync(new URL("../core/modfx.hpp", import.meta.url), "utf8");
const f32 = Math.fround;

const ROWS = [...hpp.matchAll(/^\s*\{ "([a-z]+\.[a-z]+)", [A-Z]+, ([-\d.]+)f?, ([-\d.]+)f?, ([-\d.]+)f?, (LIN|EXP|CUBIC), (true|false) \},$/gm)]
  .map(m => ({ key: m[1], min: +m[2], max: +m[3], def: +m[4], taper: m[5].toLowerCase(), cont: m[6] === "true" }));
const modelRows = () => M.MODULES.flatMap(m => m.params);

test("the model's parameters are the C++ PARAMS rows: key, min, max, default, taper, in order", () => {
  assert.equal(ROWS.length, 52);
  const got = modelRows();
  assert.deepEqual(got.map(p => p.key), ROWS.map(r => r.key));
  ROWS.forEach((r, i) => {
    const p = got[i];
    assert.equal(f32(p.min), f32(r.min), r.key + " min"); assert.equal(f32(p.max), f32(r.max), r.key + " max");
    assert.equal(f32(p.def), f32(r.def), r.key + " def"); assert.equal(p.taper, r.taper, r.key + " taper");
    assert.equal(p.kind === "knob", r.cont, r.key + " continuous");
    if (!r.cont) assert.ok(p.kind === "switch" || p.kind === "division", r.key);
  });
  assert.equal(M.destinations().length, 44);
  assert.deepEqual(M.destinations().map(d => d.key), ROWS.filter(r => r.cont).map(r => r.key));
});

test("DIVISIONS carry the core's beats (test_div) and the rows' defaults name a division", () => {
  const core = [...hpp.slice(hpp.indexOf("DIVISIONS[] = {"), hpp.indexOf("inline const Division *division")).matchAll(/\{ "(\d+[nmt]\.?)", ([^}]+?) \}/g)].map(m => [m[1], Function("return " + m[2])()]);
  assert.equal(core.length, 18);
  assert.deepEqual(M.DIVISIONS.map(d => [d.id, d.beats]), core);
  const b = id => M.DIVISIONS.find(d => d.id === id).beats;
  assert.equal(b("4n"), 1); assert.equal(b("8n."), 0.75); assert.ok(Math.abs(b("8t") - 1 / 3) < 1e-9);
  assert.equal(b("2m"), 8); assert.equal(b("32m"), 128); assert.equal(b("1n"), 4);
  for (const p of modelRows().filter(p => p.kind === "division")) assert.ok(M.DIVISIONS.some(d => f32(d.beats) === f32(p.def)), p.key);
  assert.deepEqual(M.SOURCES.map(s => s.id), ["lfo1", "lfo2", "lfo3", "lfo4", "env", "chord"]);
  assert.deepEqual(M.LFO_SHAPES.map(s => s.id), ["sine", "tri", "up", "down", "square", "steps", "drift"]);
  assert.deepEqual(M.MODULES.map(m => m.id), ["shift", "swarm", "phase", "comb", "vowel", "cloud", "tape", "echo", "space"]);
});

test("defaults round-trip through clean to {}", () => {
  for (const kind of ["route", "point"]) assert.deepEqual(M.clean(M.defaults(kind)), {}, kind);
  const r = M.defaults("route"), p = M.defaults("point");
  assert.deepEqual(r.send, [1, 1, 1]); assert.equal(r.ret, 0); assert.equal(r.echo.time, "8n."); assert.equal(r.phase.rate, "2n");
  assert.deepEqual(r.lfo.map(l => l.rate), ["1n", "2n", "2m", "4m"]); assert.equal(r.space.mod, 1);
  assert.equal(p.wet, 0); assert.equal(p.on, false); assert.ok(!("ret" in p) && !("send" in p));
});

test("set clamps, get reads, clean drops defaults and junk", () => {
  const o = {};
  M.set(o, "echo.fb", 5); assert.equal(M.get(o, "echo.fb"), 0.98);
  M.set(o, "shift.hz", -9999); assert.equal(M.get(o, "shift.hz"), -2000);
  M.set(o, "phase.stages", 9); assert.equal(M.get(o, "phase.stages"), 8);   /* a tie keeps the earlier one, as cfg_of does */
  M.set(o, "phase.stages", 7); assert.equal(M.get(o, "phase.stages"), 6);
  M.set(o, "echo.time", "32m"); assert.equal(M.get(o, "echo.time"), "32m");
  M.set(o, "echo.time", "bogus"); assert.equal(M.get(o, "echo.time"), "32m");
  M.set(o, "ret", 2); assert.equal(M.get(o, "ret"), 1);
  assert.equal(M.get({}, "space.decay"), 4);
  assert.deepEqual(M.clean({ echo: { fb: "loud", mix: 3, nope: 1 }, ghost: { a: 1 }, ret: 0, send: [1, 1, 1] }), { echo: { mix: 1 } });
});

test("fmt: real units, divisions with seconds", () => {
  assert.equal(M.fmt("phase.hz", 2400), "2.4 kHz");
  assert.equal(M.fmt("phase.hz", 800), "800 Hz");
  assert.equal(M.fmt("swarm.delay", 8), "8 ms");
  assert.equal(M.fmt("echo.time", "8n.", 90), "1/8. · 0.50 s");
  assert.equal(M.fmt("echo.time", 0.75, 90), "1/8. · 0.50 s");
  assert.equal(M.fmt("cloud.pitch", 7), "+7 st");
  assert.equal(M.fmt("cloud.pitch", 0), "0 st");
  assert.equal(M.fmt("echo.fb", 0.45), "45 %");
  assert.equal(M.fmt("comb.decay", 2), "2 s");
  assert.equal(M.fmt("comb.detune", 8), "8 ct");
  assert.equal(M.fmt("shift.mode", "ring"), "Ring");
  assert.equal(M.fmt("swarm.zero", true), "On");
  assert.equal(M.divSeconds("4n", 120), 0.5); assert.equal(M.divSeconds("1n", 60), 4);
});

test("the spec's example survives clean (walk slots dropped, defaults dropped)", () => {
  const ex = { ret: 0.6, send: [1, 0.5, 1],
    phase: { mix: 1, stages: 8, hz: 900, span: 3, rate: "2m", fb: 0.5 },
    echo: { mix: 0.7, time: "8n.", fb: 0.7, shift: 6, tone: -0.3 },
    lfo: [{ shape: "drift", rate: "4m", phase: 0 }],
    env: { attack: 10, release: 300, sens: 1 }, smooth: 2,
    mods: [{ src: "lfo1", dst: "echo.shift", amt: 0.4 }, { src: "pace", dst: "phase.span", amt: 0.5 }] };
  assert.deepEqual(M.clean(ex), { ret: 0.6, send: [1, 0.5, 1],
    phase: { mix: 1, stages: 8, hz: 900, span: 3, rate: "2m", fb: 0.5 },
    echo: { mix: 0.7, fb: 0.7, shift: 6, tone: -0.3 },
    lfo: [{ shape: "drift", rate: "4m" }],
    mods: [{ src: "lfo1", dst: "echo.shift", amt: 0.4 }] });
  assert.deepEqual(M.clean({ mods: [{ src: "pace", dst: "x" }, { src: "env", dst: "shift.mode", amt: 1 }, { src: "env", dst: "tape.drive", amt: 9 }] }),
    { mods: [{}, {}, { src: "env", dst: "tape.drive", amt: 1 }] });
});

test("summary names the modules that are on", () => {
  assert.equal(M.summary({}), "Off");
  assert.equal(M.summary({ phase: { mix: 1 }, echo: { mix: 0.3 }, space: { mix: 0.2 }, comb: { mix: 0 } }), "Phase · Echo · Space");
});
