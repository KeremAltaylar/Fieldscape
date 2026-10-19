/* Composition step 1 (docs/superpowers/specs/2026-10-17-composition-design.md): the page's model of a route's "comp" - its
   defaults (today's sound), templates, overrides per chord / section / zone, what a scope inherits, the lanes' cells, where
   zones and sections cross the route, undo history and a one-line summary. No DOM: node-tested (tests/compose-model.test.mjs). */
(function (root) {
  var VOICES = ["voice", "sect", "v3"];
  var STYLES = [{ id: "free", label: "Free" }, { id: "arpeggio", label: "Arpeggio" }, { id: "pedal", label: "Pedal" },
                { id: "strum", label: "Strum" }, { id: "euclid", label: "Euclidean" }, { id: "swells", label: "Swells" }];
  var ROLES = [{ id: "accomp", label: "on" }, { id: "off", label: "off" }];
  var RATES = ["16n", "8n", "8n.", "4n", "4n.", "2n", "2n.", "1n"];
  var COMMON = [
    { k: "style", label: "style", kind: "menu", options: STYLES, def: "free" },
    { k: "role", label: "voice", kind: "menu", options: ROLES, def: "accomp" },
    { k: "rate", label: "rate", kind: "menu", options: RATES.map(function (r) { return { id: r, label: r }; }), def: "8n" },
    { k: "lo", label: "lowest", kind: "knob", min: 24, max: 96, step: 1, def: 48, fmt: "note" },
    { k: "hi", label: "highest", kind: "knob", min: 36, max: 108, step: 1, def: 84, fmt: "note" },
    { k: "len", label: "length", kind: "knob", min: 0.05, max: 1, step: 0.05, def: 0.9, fmt: "pct" },
    { k: "variation", label: "variation", kind: "knob", min: 0, max: 1, step: 0.05, def: 0, fmt: "pct" }
  ];
  var FIELDS = {
    common: COMMON,
    free: [],
    arpeggio: [{ k: "grid", label: "pattern", kind: "grid", def: [0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3] },
               { k: "span", label: "octaves", kind: "knob", min: 1, max: 3, step: 1, def: 1 },
               { k: "acc", label: "accents", kind: "accents", def: [70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70] }],
    pedal: [{ k: "pedal", label: "note", kind: "menu", options: [{ id: "root", label: "root" }, { id: "fifth", label: "fifth" }, { id: "fixed", label: "fixed" }], def: "root" },
            { k: "pedal_note", label: "fixed note", kind: "knob", min: 24, max: 96, step: 1, def: 50, fmt: "note" },
            { k: "follow", label: "follows chords", kind: "toggle", def: true }],
    strum: [{ k: "tones", label: "tones", kind: "tones", def: [0, 1, 2] },
            { k: "dir", label: "direction", kind: "menu", options: [{ id: "up", label: "up" }, { id: "down", label: "down" }, { id: "alt", label: "alternating" }], def: "up" },
            { k: "spread", label: "spread", kind: "knob", min: 0, max: 200, step: 5, def: 40, fmt: "ms" }],
    euclid: [{ k: "pulses", label: "pulses", kind: "knob", min: 0, max: 16, step: 1, def: 5 },
             { k: "steps", label: "steps", kind: "knob", min: 1, max: 16, step: 1, def: 8 },
             { k: "rot", label: "rotation", kind: "knob", min: 0, max: 15, step: 1, def: 0 },
             { k: "grid", label: "pitches", kind: "grid", def: [0, 2, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] }],
    swells: [{ k: "tones", label: "tones", kind: "tones", def: [0, 1, 2, 3] },
             { k: "attack", label: "attack", kind: "knob", min: 0.01, max: 0.95, step: 0.01, def: 0.3, fmt: "pct" },
             { k: "release", label: "release", kind: "knob", min: 0.01, max: 0.95, step: 0.01, def: 0.4, fmt: "pct" },
             { k: "peak", label: "level", kind: "knob", min: 0, max: 1, step: 0.05, def: 0.8, fmt: "pct" }]
  };
  var BASS = [{ k: "mode", label: "bass", kind: "menu", options: [{ id: "hold", label: "holds the root" }, { id: "pedal", label: "pedal" }, { id: "off", label: "off" }], def: "hold" },
              { k: "pedal", label: "pedal note", kind: "menu", options: [{ id: "root", label: "key root" }, { id: "fifth", label: "key fifth" }, { id: "fixed", label: "fixed" }], def: "root" },
              { k: "note", label: "fixed note", kind: "knob", min: 12, max: 72, step: 1, def: 38, fmt: "note" }];
  function clone(x) { return JSON.parse(JSON.stringify(x)); }
  function voiceDefaults() { return { style: "free", role: "accomp", rate: "8n", lo: 48, hi: 84, len: 0.9, variation: 0 }; }
  function defaults() {
    return { seed: 1, voice: voiceDefaults(), sect: voiceDefaults(), v3: voiceDefaults(), bass: { mode: "hold" }, over: { chord: {}, section: {}, zone: {} } };
  }
  function tmpl(mod) { return function () { var c = defaults(); mod(c); return c; }; }
  var TEMPLATES = [
    { id: "today", label: "Today's sound", comp: tmpl(function () {}) },
    { id: "arp-strum-pedal", label: "Arpeggio · Strum · Pedal", comp: tmpl(function (c) {
        c.voice = Object.assign(c.voice, { style: "arpeggio", rate: "16n", lo: 55, hi: 84, grid: FIELDS.arpeggio[0].def.slice() });
        c.sect = Object.assign(c.sect, { style: "strum", rate: "2n", tones: [0, 1, 2, 3], spread: 50, lo: 50, hi: 74 });
        c.v3 = Object.assign(c.v3, { style: "pedal", rate: "1n", pedal: "fifth", lo: 43, hi: 62 }); }) },
    { id: "pulse", label: "Euclidean pulse", comp: tmpl(function (c) {
        c.voice = Object.assign(c.voice, { style: "euclid", rate: "8n", pulses: 5, steps: 8, grid: [0, 2, 1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] });
        c.sect = Object.assign(c.sect, { style: "euclid", rate: "16n", pulses: 3, steps: 8, rot: 2, grid: [2, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1], lo: 62, hi: 86 }); }) },
    { id: "ambient", label: "Swells (ambient)", comp: tmpl(function (c) {
        c.voice = Object.assign(c.voice, { style: "swells", rate: "1n", len: 1, tones: [0, 2, 4] });
        c.sect = Object.assign(c.sect, { style: "swells", rate: "2n.", len: 1, tones: [1, 3], lo: 60, hi: 84, attack: 0.6, release: 0.5 });
        c.v3.role = "off"; c.bass.mode = "pedal"; }) }
  ];
  function key(scope) { return scope.kind === "zone" ? scope.id : String(scope.i); }
  function scopeObj(comp, scope, create) {
    if (scope.kind === "route") { return comp; }
    if (scope.kind !== "zone" && scope.i === undefined) { return null; }
    var m = comp.over[scope.kind], k = key(scope);
    if (!m[k] && create) { m[k] = scope.kind === "zone" ? { when: "inside", blend: 1 } : {}; }
    return m[k] || null;
  }
  function setValue(comp, scope, voice, k, v) {
    var o = scopeObj(comp, scope, true);
    if (!o) { return; }
    o[voice] = o[voice] || {};
    o[voice][k] = v;
    /* a style chosen here plays its pattern or tones as the inspector shows them: the model's, written out where this scope
       has none (the core's own defaults differ - a grid of 0 1 2, tones 1 3 5) */
    if (k === "style" && FIELDS[v]) {
      FIELDS[v].forEach(function (f) { if ((f.k === "grid" || f.k === "tones") && o[voice][f.k] === undefined) { o[voice][f.k] = f.def.slice(); } });
    }
  }
  function emptyOver(o) { return Object.keys(o).every(function (k) { return k === "when" || k === "blend"; }); }
  function clearValue(comp, scope, voice, k) {
    if (scope.kind === "route") { comp[voice][k] = (voice === "bass" ? { mode: "hold" } : voiceDefaults())[k]; if (comp[voice][k] === undefined) { delete comp[voice][k]; } return; }
    var o = scopeObj(comp, scope, false); if (!o || !o[voice]) { return; }
    delete o[voice][k];
    if (!Object.keys(o[voice]).length) { delete o[voice]; }
    if (emptyOver(o)) { delete comp.over[scope.kind][key(scope)]; }
  }
  function clearScope(comp, scope) { if (scope.kind !== "route") { delete comp.over[scope.kind][key(scope)]; } }
  function fieldDef(k, style) {
    var searchList = COMMON.concat(BASS);
    if (style && FIELDS[style]) { searchList = searchList.concat(FIELDS[style]); }
    Object.keys(FIELDS).forEach(function (s) { if (!style || s !== style) { searchList = searchList.concat(FIELDS[s]); } });
    for (var i = 0; i < searchList.length; i++) { if (searchList[i].k === k) { return searchList[i].def; } }
    return undefined;
  }
  /* what a scope shows for a voice: its own values ("here") over the route's ("route"); every field of the voice's style. A
     chord also inherits from the sections over it (bandsAt, as cell() takes it): route < section < chord, source "section" */
  function effective(comp, scope, voice, bandsAt) {
    var base = comp[voice] || {}, own = scope.kind === "route" ? {} : ((scopeObj(comp, scope, false) || {})[voice] || {});
    var values = {}, source = {}, from = {};
    if (scope.kind === "chord") {
      base = Object.assign({}, base);
      (bandsAt || []).filter(function (b) { return b.kind === "section"; }).forEach(function (b) {
        var o = comp.over.section[String(b.id)]; if (o && o[voice]) { Object.keys(o[voice]).forEach(function (k) { base[k] = o[voice][k]; from[k] = "section"; }); } });
    }
    var style = own.style || base.style || "free";
    var keys = voice === "bass" ? BASS.map(function (f) { return f.k; }) : COMMON.concat(FIELDS[style] || []).map(function (f) { return f.k; });
    keys.forEach(function (k) {
      var v;
      if (own[k] !== undefined) { v = own[k]; source[k] = "here"; }
      else { v = base[k] !== undefined ? base[k] : fieldDef(k, style); source[k] = from[k] || "route"; }
      if (Array.isArray(v)) { v = v.slice(); }
      values[k] = v;
    });
    return { values: values, source: source, style: style };
  }
  /* a lane cell: chord i, with the bands over it; zone > chord > section > route */
  function cell(comp, i, bandsAt) {
    return VOICES.map(function (v) {
      var style = comp[v].style, styleSource = "route", role = comp[v].role, roleSource = "route";
      (bandsAt || []).filter(function (b) { return b.kind === "section"; }).forEach(function (b) {
        var o = comp.over.section[String(b.id)]; if (o && o[v]) { if (o[v].style) { style = o[v].style; styleSource = "section"; } if (o[v].role) { role = o[v].role; roleSource = "section"; } } });
      var ch = comp.over.chord[String(i)]; if (ch && ch[v]) { if (ch[v].style) { style = ch[v].style; styleSource = "chord"; } if (ch[v].role) { role = ch[v].role; roleSource = "chord"; } }
      (bandsAt || []).filter(function (b) { return b.kind === "zone"; }).forEach(function (b) {
        var o = comp.over.zone[b.id]; if (o && o[v]) { if (o[v].style) { style = o[v].style; styleSource = "zone"; } if (o[v].role) { role = o[v].role; roleSource = "zone"; } } });
      /* an off cell is the doing of the layer that turned it off */
      return role === "off" ? { style: "off", source: roleSource } : { style: style, source: styleSource };
    });
  }
  function dist(a, b) { var R = 6371008.8, la = (a[1] + b[1]) / 2 * Math.PI / 180;
    var dx = (b[0] - a[0]) * Math.PI / 180 * Math.cos(la) * R, dy = (b[1] - a[1]) * Math.PI / 180 * R; return Math.sqrt(dx * dx + dy * dy); }
  function inRing(p, ring) { var c = false;
    for (var i = 0, j = ring.length - 1; i < ring.length; j = i++) {
      if ((ring[i][1] > p[1]) !== (ring[j][1] > p[1]) && p[0] < (ring[j][0] - ring[i][0]) * (p[1] - ring[i][1]) / (ring[j][1] - ring[i][1]) + ring[i][0]) { c = !c; } }
    return c; }
  /* where each section and zone crosses the route, as [t0, t1] shares of its length (sampled every ~2 m) */
  function bands(coords, sections, zones) {
    if (!coords || coords.length < 2) { return []; }
    var cum = [0]; for (var i = 1; i < coords.length; i++) { cum.push(cum[i - 1] + dist(coords[i - 1], coords[i])); }
    var total = cum[cum.length - 1] || 1, n = Math.max(20, Math.min(2000, Math.round(total / 2))), pts = [];
    for (var s = 0; s <= n; s++) { var d = s / n * total, k = 1; while (k < cum.length - 1 && cum[k] < d) { k++; }
      var f = (d - cum[k - 1]) / Math.max(1e-9, cum[k] - cum[k - 1]);
      pts.push([coords[k - 1][0] + (coords[k][0] - coords[k - 1][0]) * f, coords[k - 1][1] + (coords[k][1] - coords[k - 1][1]) * f]); }
    var out = [];
    function scan(kind, id, inside) { var start = -1;
      for (var s = 0; s <= n; s++) { var on = inside(pts[s]);
        if (on && start < 0) { start = s; }
        if ((!on || s === n) && start >= 0) { out.push({ kind: kind, id: id, t0: start / n, t1: (on ? s : s - 1) / n }); start = -1; } } }
    (sections || []).forEach(function (sc) { scan("section", sc.id, function (p) { return inRing(p, sc.ring); }); });
    (zones || []).forEach(function (z) { scan("zone", z.id, function (p) { return dist(p, [z.lon, z.lat]) <= z.r; }); });
    return out;
  }
  function history(limit) {
    var past = [], future = [];
    return {
      push: function (c) { past.push(JSON.stringify(c)); if (past.length > limit) { past.shift(); } future = []; },
      undo: function () { if (past.length < 2) { return null; } future.push(past.pop()); return JSON.parse(past[past.length - 1]); },
      redo: function () { if (!future.length) { return null; } var s = future.pop(); past.push(s); return JSON.parse(s); },
      canUndo: function () { return past.length > 1; }, canRedo: function () { return future.length > 0; }
    };
  }
  /* the arpeggio's patterns: chord degrees per step (0 root, 1 3rd, 2 5th, 3 7th, 4 9th); 8 and 10 are the root and 3rd again
     (the octave is the voice's range and its octaves setting) */
  var PRESETS = [["up", [0, 1, 2, 3]], ["down", [3, 2, 1, 0]], ["up-down", [0, 1, 2, 3, 2, 1]], ["1-5-8-10", [0, 2, 0, 1]], ["Alberti", [0, 2, 1, 2]]]
    .map(function (p) { var g = []; for (var i = 0; i < 16; i++) { g.push(p[1][i % p[1].length]); } return [p[0], g]; });

  /* Generate form (.superpowers/sdd/2026-10-17-composition-1-foundation/generate-brief.md): a form's letters cover the route's
     chords in order; each letter is a texture of the three voices - one moves (high, 8n-16n), one sustains (low, 2n-1n), one
     supports or rests. A repeated A is A, a' is A with one or two changes, B and C move from A by `contrast`; each section's last
     chord thins, the route's last settles. Pure and seeded (mulberry32): the same seed, form and contrast write the same comp. */
  var FORMS = [{ id: "AABA", label: "AABA", letters: ["A", "A", "B", "A"] }, { id: "ABAB", label: "ABAB", letters: ["A", "B", "A", "B"] },
               { id: "ABAC", label: "ABAC", letters: ["A", "B", "A", "C"] }, { id: "sentence", label: "a a′ b", letters: ["a", "a′", "b"], weights: [1, 1, 2] },
               { id: "rondo", label: "ABACA", letters: ["A", "B", "A", "C", "A"] }];
  function rng(seed) {
    var a = seed >>> 0;
    return function () { a = (a + 0x6D2B79F5) | 0; var t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  }
  function roll(seed) { return 1 + Math.floor(rng((seed | 0) + 1)() * 99998); }   /* the next seed Generate takes, 1..99998 */
  function formOf(id) { return FORMS.filter(function (f) { return f.id === id; })[0] || FORMS[0]; }
  /* n chords among the letters as evenly as their weights allow, earlier letters taking the remainder; fewer chords than
     letters: the first n letters, a chord each. [{ letter, a, b }], b exclusive */
  function formSpans(id, n) {
    var f = formOf(id), L = f.letters.slice(0, Math.min(f.letters.length, n)), counts;
    if (n < f.letters.length) { counts = L.map(function () { return 1; }); }
    else {
      var w = L.map(function (x, i) { return (f.weights || [])[i] || 1; }), W = w.reduce(function (s, x) { return s + x; }, 0);
      counts = w.map(function (x) { return Math.floor(n * x / W); });
      for (var i = 0, rem = n - counts.reduce(function (s, x) { return s + x; }, 0); i < rem; i++) { counts[i % counts.length]++; }
    }
    var a = 0; return L.map(function (x, i) { var s = { letter: x, a: a, b: a + counts[i] }; a += counts[i]; return s; });
  }
  /* a letter's texture: A for A and a, A' for a', B, C */
  function texKey(letter) { return letter.charAt(0).toUpperCase() + (letter.length > 1 ? "'" : ""); }
  var CLASS = { arpeggio: "move", euclid: "move", pedal: "held", swells: "held", strum: "chord", free: "chord" };
  var ROLE_STYLES = { mover: ["arpeggio", "euclid", "strum"], held: ["pedal", "swells", "strum"], support: ["free", "arpeggio", "pedal", "strum", "euclid", "swells"] };
  var TONES = { strum: [[0, 1, 2], [0, 1, 2, 3], [0, 2, 4], [0, 1, 3]], swells: [[0, 1, 2, 3], [0, 2, 4], [0, 2], [1, 3], [0, 1, 2]] };
  function cls(o) { return o.role === "off" ? "off" : CLASS[o.style]; }
  function one(r, a) { return a[Math.floor(r() * a.length)]; }
  function int(r, a, b) { return a + Math.floor(r() * (b - a + 1)); }
  function styleFields(r, o) {   /* a style's own settings, drawn */
    var st = o.style;
    if (st === "arpeggio") { o.grid = one(r, PRESETS)[1].slice(); o.span = one(r, [1, 1, 2]); }
    else if (st === "strum") { o.tones = one(r, TONES.strum).slice(); o.dir = one(r, ["up", "down", "alt"]); o.spread = 5 * int(r, 4, 24); }
    else if (st === "euclid") { o.steps = one(r, [8, 12, 16]); o.pulses = int(r, 3, Math.ceil(o.steps * 0.6)); o.rot = int(r, 0, o.steps - 1);
      o.grid = []; for (var i = 0, k = int(r, 2, 4); i < 16; i++) { o.grid.push(i < k ? int(r, 0, 4) : -1); } }
    else if (st === "swells") { o.tones = one(r, TONES.swells).slice(); o.attack = Math.round((0.2 + 0.5 * r()) * 20) / 20; o.release = Math.round((0.3 + 0.4 * r()) * 20) / 20; }
    else if (st === "pedal") { o.pedal = one(r, ["root", "fifth"]); }
    return o;
  }
  /* a voice for its role: the mover high and quick, the held voice low and long, the support between, slower, or resting;
     `avoid` a style class it must not take (B's change from A) */
  function drawVoice(r, role, dense, avoid) {
    var o = voiceDefaults();
    o.style = one(r, ROLE_STYLES[role].filter(function (s) { return CLASS[s] !== avoid; }));
    if (role === "mover") { o.rate = dense ? "16n" : "8n"; o.lo = int(r, 60, 67); o.hi = o.lo + int(r, 17, 24); }
    else if (role === "held") { o.rate = dense ? one(r, ["2n", "2n."]) : one(r, ["2n.", "1n"]); o.lo = int(r, 38, 46); o.hi = o.lo + int(r, 14, 20); }
    else { o.rate = dense ? one(r, ["4n", "4n.", "2n"]) : one(r, ["2n", "1n"]); o.lo = int(r, 50, 55); o.hi = o.lo + int(r, 14, 19);
      if (avoid !== "off" && r() < (dense ? 0.2 : 0.45)) { o.role = "off"; } }
    return styleFields(r, o);
  }
  function pedalBass(r) { return { mode: "pedal", pedal: r() < 0.5 ? "root" : "fifth" }; }
  function textureA(r) {
    var roles = ["mover", "held", "support"];
    for (var i = 2; i > 0; i--) { var j = Math.floor(r() * (i + 1)), x = roles[i]; roles[i] = roles[j]; roles[j] = x; }
    var t = { dense: r() < 0.5, roles: {}, v: {} };
    VOICES.forEach(function (v, i) { t.roles[v] = roles[i]; t.v[v] = drawVoice(r, roles[i], t.dense, null); });
    t.v.bass = r() < 0.65 ? { mode: "hold" } : pedalBass(r);
    t.cad = int(r, 0, 2);
    return t;
  }
  /* B or C: each voice changes with probability `contrast` (one at least, above 0) - its role swapped (the mover sustains, the held voice moves: the
     registers trade), its style from another class, the density the other way */
  var SWAP = { mover: "held", held: "mover", support: "support" };
  function contrastOf(r, A, c) {
    var t = { dense: !A.dense, roles: {}, v: {} }, u = VOICES.map(function () { return r(); }), least = Math.min.apply(null, u);
    VOICES.forEach(function (v, i) {
      if (u[i] < c || (c > 0 && u[i] === least)) {   /* any contrast changes a voice at least */ t.roles[v] = SWAP[A.roles[v]]; t.v[v] = drawVoice(r, t.roles[v], t.dense, cls(A.v[v])); }
      else { t.roles[v] = A.roles[v]; t.v[v] = clone(A.v[v]); }
    });
    t.v.bass = r() < c ? (A.v.bass.mode === "hold" ? pedalBass(r) : { mode: "hold" }) : clone(A.v.bass);
    t.cad = r() < c ? int(r, 0, 2) : A.cad;
    return t;
  }
  /* a': A with one or two changes - a voice's rate a step, its register an octave, or its pattern */
  function variantOf(r, A) {
    var t = clone(A), on = VOICES.filter(function (v) { return t.v[v].role !== "off"; });
    for (var n = r() < 0.5 ? 1 : 2; n > 0; n--) {
      var o = t.v[one(r, on)], kind = int(r, 0, 2), ri = RATES.indexOf(o.rate), step = RATES[ri + 1 < RATES.length ? ri + 1 : ri - 1];
      if (kind === 0) { o.rate = step; }
      else if (kind === 1) { var d = o.hi + 12 <= 108 ? 12 : -12; o.lo += d; o.hi += d; }
      else if (o.style === "arpeggio") { var g = JSON.stringify(o.grid); o.grid = one(r, PRESETS.filter(function (p) { return JSON.stringify(p[1]) !== g; }))[1].slice(); }
      else if (o.style === "euclid") { o.rot = (o.rot + 1) % o.steps; }
      else if (o.style === "strum") { o.dir = o.dir === "up" ? "down" : "up"; }
      else if (o.style === "swells") { o.tones = o.tones.length > 2 ? o.tones.slice(0, 2) : [0, 1, 2, 3]; }
      else if (o.style === "pedal") { o.pedal = o.pedal === "root" ? "fifth" : "root"; }
      else { o.rate = step; }
    }
    return t;
  }
  function held(o, st) {
    var x = Object.assign(voiceDefaults(), { style: st, rate: "1n", lo: o.lo, hi: o.hi });
    if (st === "swells") { x.tones = [0, 1, 2]; x.attack = 0.3; x.release = 0.5; } else { x.pedal = "root"; }
    return x;
  }
  /* a section's last chord: its moving voice (with none, its quickest busy one) swells or pedals at 1n, or rests */
  function thin(t) {
    var x = clone(t.v), busy = function (v) { return x[v].role !== "off" && CLASS[x[v].style] !== "held"; };
    var movers = VOICES.filter(function (v) { return t.roles[v] === "mover" && busy(v); });
    if (!movers.length) { movers = VOICES.filter(busy).sort(function (a, b) { return RATES.indexOf(x[a].rate) - RATES.indexOf(x[b].rate); }).slice(0, 1); }
    movers.forEach(function (v) { if (t.cad === 2) { x[v].role = "off"; } else { x[v] = held(x[v], t.cad ? "pedal" : "swells"); } });
    return x;
  }
  /* the route's last chord: every voice swells or pedals, the bass holds the root */
  function settle(r, A) {
    var x = { bass: { mode: "hold" } };
    VOICES.forEach(function (v) { x[v] = held(A.v[v], r() < 0.5 ? "swells" : "pedal"); });
    return x;
  }
  /* a new comp: the route's defaults are A; every chord that differs holds only its differing keys; old chord overrides go,
     sections' and zones' stay */
  function generate(comp, opt) {
    var seed = Math.max(0, opt.seed | 0), c = Math.max(0, Math.min(1, +opt.contrast || 0)), n = Math.max(0, opt.nchords | 0), type = formOf(opt.form).id;
    var r = rng(seed), A = textureA(r), tex = { A: A }, spans = formSpans(type, n), out = clone(comp), all = VOICES.concat(["bass"]);
    spans.forEach(function (s) { var k = texKey(s.letter); if (!tex[k]) { tex[k] = k === "A'" ? variantOf(r, A) : contrastOf(r, A, c); } });
    var end = settle(r, A);
    all.forEach(function (v) { out[v] = clone(A.v[v]); });
    out.seed = seed; out.form = { type: type, contrast: c, seed: seed };
    out.over = { chord: {}, section: (comp.over && out.over.section) || {}, zone: (comp.over && out.over.zone) || {} };
    spans.forEach(function (s) {
      var t = tex[texKey(s.letter)], cad = thin(t);
      for (var i = s.a; i < s.b; i++) {
        var at = i === n - 1 ? end : i === s.b - 1 ? cad : t.v, o = {};
        all.forEach(function (v) { var d = {};
          Object.keys(at[v]).forEach(function (k) { if (JSON.stringify(at[v][k]) !== JSON.stringify(out[v][k])) { d[k] = clone(at[v][k]); } });
          if (Object.keys(d).length) { o[v] = d; } });
        if (Object.keys(o).length) { out.over.chord[String(i)] = o; }
      }
    });
    return out;
  }
  function label(id) { for (var i = 0; i < STYLES.length; i++) { if (STYLES[i].id === id) { return STYLES[i].label; } } return id; }
  function summary(comp) { return VOICES.map(function (v) { return comp[v].role === "off" ? "Off" : label(comp[v].style); }).join(" · "); }
  var api = { VOICES: VOICES, STYLES: STYLES, ROLES: ROLES, RATES: RATES, FIELDS: FIELDS, BASS: BASS, defaults: defaults, TEMPLATES: TEMPLATES,
    scopeObj: scopeObj, setValue: setValue, clearValue: clearValue, clearScope: clearScope, effective: effective, cell: cell,
    bands: bands, history: history, summary: summary, clone: clone, label: label,
    PRESETS: PRESETS, FORMS: FORMS, formSpans: formSpans, generate: generate, roll: roll };
  if (typeof module === "object" && module.exports) { module.exports = api; } else { root.FsCompose = api; }
})(typeof globalThis !== "undefined" ? globalThis : this);
