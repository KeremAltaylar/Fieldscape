/* 8b final review I1: a voice without its own envelope shows - and a first edit starts from - its instrument's own envelope
   (as the core's synths and samplers set it), not one role default for every instrument. */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const html = readFileSync(new URL("../index.html", import.meta.url), "utf8");
function slice(from, to) {
  const a = html.indexOf(from), b = html.indexOf(to, a);
  assert.ok(a >= 0 && b > a, "markers " + from + " / " + to);
  return html.slice(a, b);
}
const env = new Function(slice("var ENV_W = 320", "function envOf(") + "\nreturn { envOwnOf: envOwnOf, ENV_RANGE: ENV_RANGE };")();

test("a sampler voice's own envelope holds at full and releases in 0.6 s", () => {
  for (const s of ["s-resonator", "s-harmonic", "s-retune", "s-freeze"]) {
    const e = env.envOwnOf(s, "sect");
    assert.equal(e[2], 1, s + " sustain"); assert.equal(e[3], 0.6, s + " release"); assert.equal(e[0], 0.02, s + " attack");
  }
});

test("a self-voiced synth's own envelope is its own, whatever voice plays it", () => {
  assert.deepEqual(env.envOwnOf("metal", "voice").slice(1), [1.8, 0, 2.6]);
  assert.deepEqual(env.envOwnOf("membrane", "v3").slice(1), [1, 0.02, 2]);
  assert.deepEqual(env.envOwnOf("wavetable", "sect"), [0.06, 0.4, 0.62, 2]);
});

test("the role-enveloped synths show their role's", () => {
  assert.deepEqual(env.envOwnOf("fm", "sect"), [0.012, 0.35, 0.18, 1.1]);
  assert.deepEqual(env.envOwnOf("mono", "v3"), [0.5, 1.2, 0.6, 3.2]);
});

test("every own envelope is inside the graph's ranges", () => {
  for (const s of ["fm", "am", "duo", "mono", "simple", "pluck", "metal", "membrane", "wavetable", "comb", "formant", "s-resonator"])
    for (const r of ["voice", "sect", "v3"])
      env.envOwnOf(s, r).forEach((v, i) => assert.ok(v >= env.ENV_RANGE[i][0] && v <= env.ENV_RANGE[i][1], s + " " + r + " " + i + " " + v));
});
