/* Modulation FX (docs/superpowers/specs/2026-11-17-modulation-fx-design.md): the page's copy of core/modfx.hpp's PARAMS table
   and cfg_of rules - parameters, divisions, sources, defaults, clamping, readouts. tests/fx-model.test.mjs reads the C++ rows
   and fails if the two drift. min/max/def are the core's numbers (a switch holds its option index, a division holds beats);
   the stored JSON holds division strings, "ssb"/"ring", "chord"/"free", booleans (zero, freeze) and the stage count. No DOM. */
(function (root) {
  var DIVISIONS = [["32m", 128], ["16m", 64], ["8m", 32], ["4m", 16], ["2m", 8], ["1n", 4], ["2n", 2], ["4n", 1], ["8n", 0.5], ["16n", 0.25], ["32n", 0.125],
    ["2n.", 3], ["4n.", 1.5], ["8n.", 0.75], ["16n.", 0.375], ["4t", 2 / 3], ["8t", 1 / 3], ["16t", 1 / 6]]
    .map(function (d) { return { id: d[0], beats: d[1] }; });
  var SOURCES = ["lfo1", "lfo2", "lfo3", "lfo4", "env", "chord"].map(function (s) { return { id: s, label: s === "env" ? "Envelope" : s === "chord" ? "Chord" : "LFO " + s[3] }; });
  var LFO_SHAPES = ["sine", "tri", "up", "down", "square", "steps", "drift"].map(function (s) { return { id: s, label: s[0].toUpperCase() + s.slice(1) }; });
  var LFO_RATES = ["1n", "2n", "2m", "4m"];
  var BPM = 72;   /* a point before any route */

  /* [key, label, min, max, def, taper, unit, kind, options] - core row order */
  var SPEC = [
    ["shift", "Shift", "frequency shifter / ring modulator", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["hz", "shift", -2000, 2000, 0, "cubic", "Hz"],
      ["mode", "mode", 0, 1, 0, "lin", "", "switch", [["ssb", "SSB"], ["ring", "Ring"]]], ["fb", "feedback", 0, 0.9, 0, "lin", "pct"]]],
    ["swarm", "Swarm", "chorus, ensemble, flanger", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["rate", "rate", 0.125, 128, 4, "exp", "div", "division"], ["depth", "depth", 0, 1, 0.5, "lin", "pct"],
      ["delay", "delay", 0.1, 30, 8, "exp", "ms"], ["fb", "feedback", -0.9, 0.9, 0, "lin", "pct"], ["spread", "spread", 0, 1, 1, "lin", "pct"],
      ["zero", "through zero", 0, 1, 0, "lin", "", "switch", [[false, "Off"], [true, "On"]]]]],
    ["phase", "Phase", "phaser, 2 to 12 stages", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["stages", "stages", 2, 12, 6, "lin", "", "switch", [[2, "2"], [4, "4"], [6, "6"], [8, "8"], [12, "12"]]],
      ["hz", "centre", 100, 8000, 800, "exp", "Hz"], ["span", "span", 0, 4, 2, "lin", "oct"], ["rate", "rate", 0.125, 128, 2, "exp", "div", "division"],
      ["fb", "feedback", -0.95, 0.95, 0.3, "lin", "pct"]]],
    ["comb", "Comb", "tuned combs on the chord", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["tune", "tune", 0, 1, 0, "lin", "", "switch", [["chord", "Chord"], ["free", "Free"]]],
      ["hz", "pitch", 40, 2000, 110, "exp", "Hz"], ["decay", "decay", 0.05, 20, 2, "exp", "s"], ["damp", "damping", 500, 16000, 6000, "exp", "Hz"],
      ["detune", "detune", 0, 50, 8, "lin", "ct"]]],
    ["vowel", "Vowel", "formants, a to u", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["pos", "vowel", 0, 4, 0, "lin", "n1"], ["q", "sharpness", 2, 20, 8, "exp", "n1"], ["size", "size", 0.5, 2, 1, "exp", "x"]]],
    ["cloud", "Cloud", "grains of the last 2 s", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["size", "size", 20, 500, 120, "exp", "ms"], ["dens", "density", 1, 40, 12, "exp", "/s"], ["pitch", "pitch", -24, 24, 0, "lin", "st"],
      ["spray", "spray", 0, 1, 0, "lin", "pct"], ["scatter", "scatter", 0, 1, 0.3, "lin", "pct"], ["rev", "reverse", 0, 1, 0, "lin", "pct"],
      ["freeze", "freeze", 0, 1, 0, "lin", "", "switch", [[false, "Off"], [true, "On"]]]]],
    ["tape", "Tape", "wow, flutter, saturation", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["wow", "wow", 0, 1, 0.3, "lin", "pct"], ["flutter", "flutter", 0, 1, 0.2, "lin", "pct"],
      ["drive", "drive", 0, 1, 0.2, "lin", "pct"], ["tone", "tone", 500, 16000, 9000, "exp", "Hz"]]],
    ["echo", "Echo", "feedback delay that transforms every repeat", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["time", "time", 0.125, 128, 0.75, "exp", "div", "division"], ["fb", "feedback", 0, 0.98, 0.45, "lin", "pct"],
      ["tone", "tone", -1, 1, 0, "lin", "n2"], ["shift", "shift", -200, 200, 0, "lin", "Hz"], ["wobble", "wobble", 0, 1, 0, "lin", "pct"], ["spread", "spread", 0, 1, 0.5, "lin", "pct"]]],
    ["space", "Space", "eight-line reverb, decay to a minute", [
      ["mix", "mix", 0, 1, 0, "lin", "pct"], ["size", "size", 0.2, 1, 0.7, "lin", "n2"], ["decay", "decay", 0.3, 60, 4, "exp", "s"],
      ["damp", "damping", 500, 16000, 7000, "exp", "Hz"], ["mod", "motion", 0, 1, 1, "lin", "pct"]]]
  ];
  var MODULES = SPEC.map(function (m) {
    return { id: m[0], label: m[1], note: m[2], params: m[3].map(function (p) {
      var o = { key: m[0] + "." + p[0], label: p[1], min: p[2], max: p[3], def: p[4], taper: p[5], unit: p[6], kind: p[7] || "knob" };
      if (o.kind === "switch") { o.options = p[8].map(function (x) { return { id: x[0], label: x[1] }; }); }
      return o;
    }) };
  });
  var BY = {};
  MODULES.forEach(function (m) { m.params.forEach(function (p) { BY[p.key] = p; }); });
  /* route / point scalars outside the modules: [min, max, default] */
  var TOP = { ret: [0, 1, 0], send1: [0, 1, 1], send2: [0, 1, 1], send3: [0, 1, 1], wet: [0, 1, 0] };

  function num(v) { return typeof v === "number" && isFinite(v); }
  function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
  function divOf(v) {   /* a division id from an id, or the nearest one to a beat count; null if neither */
    var i, d, b = Infinity, best = null;
    if (typeof v === "string") { return DIVISIONS.some(function (x) { return x.id === v; }) ? v : null; }
    if (!num(v) || v <= 0) { return null; }
    for (i = 0; i < DIVISIONS.length; i++) { d = Math.abs(Math.log(DIVISIONS[i].beats / v)); if (d < b) { b = d; best = DIVISIONS[i].id; } }
    return best;
  }
  function defaultOf(p) {   /* the stored form of a row's default */
    var i;
    if (p.kind === "division") { for (i = 0; i < DIVISIONS.length; i++) { if (Math.abs(DIVISIONS[i].beats - p.def) < 1e-6) { return DIVISIONS[i].id; } } }
    if (p.kind === "switch") { return p.key === "phase.stages" ? p.def : p.options[p.def].id; }
    return p.def;
  }
  /* any value -> a stored value for the row, or undefined if it cannot be one (the core keeps the default then) */
  function fit(p, v) {
    var i;
    if (p.kind === "knob") { return num(v) ? clamp(v, p.min, p.max) : undefined; }
    if (p.kind === "division") { return typeof v === "string" ? divOf(v) || undefined : undefined; }
    if (p.key === "phase.stages") {   /* the nearest of 2/4/6/8/12 */
      if (!num(v)) { return undefined; }
      var best = 6; p.options.forEach(function (o) { if (Math.abs(o.id - v) < Math.abs(best - v)) { best = o.id; } });
      return best;
    }
    /* as cfg_of: a string by name (mode ssb/ring, every other switch the core's chord/free), a boolean or a number >= 0.5 its index */
    i = typeof v === "string" ? (p.key === "shift.mode" ? ["ssb", "ring"] : ["chord", "free"]).indexOf(v)
      : typeof v === "boolean" ? +v : num(v) ? +(v >= 0.5) : -1;
    return i >= 0 ? p.options[i].id : undefined;
  }

  function lfoDefault(i) { return { shape: "sine", rate: LFO_RATES[i], phase: 0 }; }
  function defaults(kind) {
    var o = kind === "point" ? { wet: 0, on: false } : { ret: 0, send: [1, 1, 1] };
    MODULES.forEach(function (m) { o[m.id] = {}; m.params.forEach(function (p) { o[m.id][p.key.split(".")[1]] = defaultOf(p); }); });
    o.lfo = [0, 1, 2, 3].map(lfoDefault);
    o.env = { attack: 10, release: 300, sens: 1 };
    o.mods = [];
    return o;
  }
  function trim(a) { while (a.length && !Object.keys(a[a.length - 1]).length) { a.pop(); } return a; }
  /* what the core would read from obj, minus everything at its default: a dry FX stores as {} */
  function clean(obj) {
    var out = {}, o = obj && typeof obj === "object" ? obj : {};
    ["ret", "wet"].forEach(function (k) { var n = num(o[k]) ? clamp(o[k], 0, 1) : 0; if (n) { out[k] = n; } });
    if (o.on === true || (num(o.on) && o.on !== 0)) { out.on = true; }
    if (Array.isArray(o.send)) {
      var s = [0, 1, 2].map(function (q) { return num(o.send[q]) ? clamp(o.send[q], 0, 1) : 1; });
      if (s.some(function (x) { return x !== 1; })) { out.send = s; }
    }
    MODULES.forEach(function (m) {
      var src = o[m.id], dst = {};
      if (!src || typeof src !== "object") { return; }
      m.params.forEach(function (p) {
        var k = p.key.split(".")[1], v = fit(p, src[k]);
        if (v !== undefined && v !== defaultOf(p)) { dst[k] = v; }
      });
      if (Object.keys(dst).length) { out[m.id] = dst; }
    });
    if (Array.isArray(o.lfo)) {
      var l = [0, 1, 2, 3].map(function (i) {
        var x = o.lfo[i], d = {}, df = lfoDefault(i);
        if (!x || typeof x !== "object") { return d; }
        var sh = typeof x.shape === "string" ? x.shape : num(x.shape) && x.shape >= 0 && x.shape < LFO_SHAPES.length ? LFO_SHAPES[Math.floor(x.shape)].id : null;
        if (LFO_SHAPES.some(function (z) { return z.id === sh; }) && sh !== df.shape) { d.shape = sh; }
        if (typeof x.rate === "string" && divOf(x.rate) && x.rate !== df.rate) { d.rate = x.rate; }
        if (num(x.phase) && clamp(x.phase, 0, 1) !== 0) { d.phase = clamp(x.phase, 0, 1); }
        return d;
      });
      if (trim(l).length) { out.lfo = l; }
    }
    if (o.env && typeof o.env === "object") {
      var e = {}, E = { attack: [1, 200, 10], release: [20, 2000, 300], sens: [0, 4, 1] };
      Object.keys(E).forEach(function (k) { if (num(o.env[k])) { var v = clamp(o.env[k], E[k][0], E[k][1]); if (v !== E[k][2]) { e[k] = v; } } });
      if (Object.keys(e).length) { out.env = e; }
    }
    if (Array.isArray(o.mods)) {   /* a bad slot stays an empty slot: positions are kept */
      var m = o.mods.slice(0, 8).map(function (x) {
        if (!x || typeof x !== "object" || !SOURCES.some(function (s) { return s.id === x.src; }) || !BY[x.dst] || BY[x.dst].kind !== "knob") { return {}; }
        return { src: x.src, dst: x.dst, amt: num(x.amt) ? clamp(x.amt, -1, 1) : 0 };
      });
      if (trim(m).length) { out.mods = m; }
    }
    return out;
  }

  /* keys: "phase.hz" ... or ret / send1..3 / wet */
  function get(obj, key) {
    var p = BY[key], k, v;
    if (TOP[key]) { v = key.slice(0, 4) === "send" ? obj && obj.send && obj.send[key[4] - 1] : obj && obj[key]; return num(v) ? clamp(v, 0, 1) : TOP[key][2]; }
    if (!p) { return undefined; }
    k = key.split(".");
    v = obj && obj[k[0]] ? fit(p, obj[k[0]][k[1]]) : undefined;
    return v === undefined ? defaultOf(p) : v;
  }
  function set(obj, key, value) {   /* clamps (a bad value is ignored); mutates and returns obj */
    var p = BY[key], k, v;
    if (TOP[key]) {
      if (!num(value)) { return obj; }
      v = clamp(value, 0, 1);
      if (key.slice(0, 4) === "send") { obj.send = obj.send || [1, 1, 1]; obj.send[key[4] - 1] = v; } else { obj[key] = v; }
      return obj;
    }
    if (!p) { return obj; }
    v = p.kind === "division" && num(value) ? divOf(value) : fit(p, value);
    if (v === undefined || v === null) { return obj; }
    k = key.split(".");
    obj[k[0]] = obj[k[0]] || {};
    obj[k[0]][k[1]] = v;
    return obj;
  }

  function divSeconds(div, bpm) {
    var d = DIVISIONS.filter(function (x) { return x.id === div; })[0];
    return (d ? d.beats : 1) * 60 / (bpm || BPM);
  }
  function divLabel(id) {
    var m = /^(\d+)([nmt])(\.?)$/.exec(id);
    return m[2] === "m" ? m[1] + " bars" : "1/" + m[1] + (m[2] === "t" ? "t" : "") + m[3];
  }
  function trimZeros(s) { return s.indexOf(".") < 0 ? s : s.replace(/0+$/, "").replace(/\.$/, ""); }
  function sgn(v, s) { return (v > 0 ? "+" : "") + s; }
  /* a value in its real unit; a division (id or beats) is "1/8. · 0.50 s" at the route's bpm */
  function fmt(key, value, tempo) {
    var p = BY[key], v = value, u;
    if (TOP[key]) { return Math.round(v * 100) + " %"; }
    if (!p) { return String(value); }
    u = p.unit;
    if (p.kind === "division") {
      var id = divOf(v), s = divSeconds(id, tempo);
      return divLabel(id) + " · " + (s < 10 ? s.toFixed(2) : s.toFixed(1)) + " s";
    }
    if (p.kind === "switch") { var o = p.options.filter(function (x) { return x.id === v; })[0]; return o ? o.label : String(v); }
    if (u === "pct") { return (p.min < 0 ? sgn(v, String(Math.round(v * 100))) : String(Math.round(v * 100))) + " %"; }
    if (u === "Hz") { return Math.abs(v) >= 1000 && p.min >= 0 ? trimZeros((v / 1000).toFixed(1)) + " kHz" : (p.min < 0 ? sgn(v, String(Math.round(v))) : String(Math.round(v))) + " Hz"; }
    if (u === "ms") { return trimZeros(v.toFixed(v >= 10 ? 0 : 1)) + " ms"; }
    if (u === "s") { return trimZeros(v.toFixed(v < 1 ? 2 : v < 10 ? 1 : 0)) + " s"; }
    if (u === "st") { return sgn(v, String(Math.round(v))) + " st"; }
    if (u === "ct") { return Math.round(v) + " ct"; }
    if (u === "oct") { return trimZeros(v.toFixed(1)) + " oct"; }
    if (u === "/s") { return trimZeros(v.toFixed(v < 10 ? 1 : 0)) + " /s"; }
    if (u === "x") { return "×" + trimZeros(v.toFixed(2)); }
    var t = trimZeros(v.toFixed(u === "n2" ? 2 : 1));
    return p.min < 0 ? sgn(v, t) : t;   /* only a bipolar row (echo.tone) carries a "+" */
  }

  function destinations() {   /* the continuous rows (44), for the matrix and the LFO targets */
    var out = [];
    MODULES.forEach(function (m) { m.params.forEach(function (p) {
      if (p.kind === "knob") { out.push({ key: p.key, label: m.label + " " + p.label, module: m.id, unit: p.unit, min: p.min, max: p.max, taper: p.taper }); }
    }); });
    return out;
  }
  function summary(obj) {
    var on = MODULES.filter(function (m) { return get(obj, m.id + ".mix") > 0; }).map(function (m) { return m.label; });
    return on.length ? on.join(" · ") : "Off";
  }

  function freeze(o) { Object.freeze(o); Object.keys(o).forEach(function (k) { if (o[k] && typeof o[k] === "object" && !Object.isFrozen(o[k])) { freeze(o[k]); } }); return o; }
  [MODULES, DIVISIONS, SOURCES, LFO_SHAPES, BY, TOP].forEach(freeze);
  var api = { MODULES: MODULES, DIVISIONS: DIVISIONS, SOURCES: SOURCES, LFO_SHAPES: LFO_SHAPES, defaults: defaults, clean: clean, get: get, set: set,
    fmt: fmt, divSeconds: divSeconds, destinations: destinations, summary: summary };
  if (typeof module === "object" && module.exports) { module.exports = api; } else { root.FsFxModel = api; }
})(typeof globalThis !== "undefined" ? globalThis : this);
