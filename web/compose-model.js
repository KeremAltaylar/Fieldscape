/* Composition step 1 (docs/superpowers/specs/2026-10-17-composition-design.md): the page's model of a route's "comp" - its
   defaults (today's sound), templates, overrides per chord / section / zone, what a scope inherits, the lanes' cells, where
   zones and sections cross the route, undo history and a one-line summary. No DOM: node-tested (tests/compose-model.test.mjs). */
(function (root) {
  var VOICES = ["voice", "sect", "v3"];
  var STYLES = [{ id: "free", label: "Free" }, { id: "arpeggio", label: "Arpeggio" }, { id: "pedal", label: "Pedal" },
                { id: "strum", label: "Strum" }, { id: "euclid", label: "Euclidean" }, { id: "swells", label: "Swells" }];
  var ROLES = [{ id: "accomp", label: "on" }, { id: "melody", label: "melody" }, { id: "off", label: "off" }];
  /* composition 2: the chain's operations (core/composer.hpp Op) */
  var OPS = [{ id: "repeat", label: "repeat" }, { id: "seq", label: "sequence", arg: { min: -7, max: 7, def: 1 } }, { id: "invert", label: "invert" },
             { id: "retro", label: "retrograde" }, { id: "augment", label: "augment" }, { id: "diminish", label: "diminish" },
             { id: "fragment", label: "fragment", arg: { min: 1, max: 8, def: 2 } }, { id: "cadence", label: "cadence" }];
  var RATES = ["16n", "8n", "8n.", "4n", "4n.", "2n", "2n.", "1n"];
  var COMMON = [
    { k: "style", label: "style", kind: "menu", options: STYLES, def: "free" },
    { k: "role", label: "role", kind: "menu", options: ROLES, def: "accomp" },
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
    return { seed: 1, bars: "develop", voice: voiceDefaults(), sect: voiceDefaults(), v3: voiceDefaults(), bass: { mode: "hold" }, over: { chord: {}, section: {}, zone: {} } };
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
  /* n: the route's chord count - a melody's label then takes its chain step's form letter (B inv), else A */
  function cell(comp, i, bandsAt, n) {
    return VOICES.map(function (v) {
      var style = comp[v].style, styleSource = "route", role = comp[v].role, roleSource = "route";
      (bandsAt || []).filter(function (b) { return b.kind === "section"; }).forEach(function (b) {
        var o = comp.over.section[String(b.id)]; if (o && o[v]) { if (o[v].style) { style = o[v].style; styleSource = "section"; } if (o[v].role) { role = o[v].role; roleSource = "section"; } } });
      var ch = comp.over.chord[String(i)]; if (ch && ch[v]) { if (ch[v].style) { style = ch[v].style; styleSource = "chord"; } if (ch[v].role) { role = ch[v].role; roleSource = "chord"; } }
      (bandsAt || []).filter(function (b) { return b.kind === "zone"; }).forEach(function (b) {
        var o = comp.over.zone[b.id]; if (o && o[v]) { if (o[v].style) { style = o[v].style; styleSource = "zone"; } if (o[v].role) { role = o[v].role; roleSource = "zone"; } } });
      /* an off cell is the doing of the layer that turned it off */
      if (role === "melody") { return { style: "melody", label: melodyLabel(comp, i, n), source: roleSource }; }
      return role === "off" ? { style: "off", source: roleSource } : { style: style, source: styleSource };
    });
  }
  function motifDefault() {
    return { basis: "chord", bars: 1, notes: [{ deg: 0, start: 0, len: 4 }, { deg: 2, start: 4, len: 4 }, { deg: 1, start: 8, len: 4 }, { deg: 0, oct: 1, start: 12, len: 4 }] };
  }
  function setMotif(comp, motif) { comp.motif = clone(motif); }
  function setChain(comp, chain) { comp.chain = clone(chain); }
  function clamp(x, a, b) { return Math.max(a, Math.min(b, x)); }
  /* the chain step covering a chord, the later one winning; null: none (the motif repeats) */
  function chainAt(comp, chord) {
    var ch = comp.chain || [];
    for (var i = ch.length - 1; i >= 0; i--) { if (chord >= ch[i].from && chord <= ch[i].to) { return ch[i]; } }
    return null;
  }
  /* the motif as developed for one chord - comp::motif_step (core/composer.cpp) in JS, with the core's parse clamps (motif_of):
     { op, notes: [{ deg, oct, start, len }], len16, last, cad, others, strength }; times in sixteenths from the phrase's start */
  function motifStep(comp, chord) {
    var m = comp.motif || {}, bars = clamp(Math.round(m.bars || 1), 1, 2), L = bars * 16, base = [];
    (m.notes || []).forEach(function (x) {
      var s0 = Math.round(x.start || 0);
      if (base.length >= 8 || s0 < 0 || s0 >= L) { return; }
      base.push({ deg: clamp(Math.round(x.deg || 0), -7, 13), oct: clamp(Math.round(x.oct || 0), -2, 2), start: s0, len: clamp(Math.round(Math.min(L - s0, x.len === undefined ? 4 : x.len)), 1, 32) });
    });
    var st = chainAt(comp, chord), op = st ? st.op : "repeat", arg = st ? Math.round(st.arg || 0) : 0, len16 = L, n = base.map(function (x) { return Object.assign({}, x); });
    if (op === "seq") { n.forEach(function (x) { x.deg += clamp(arg, -7, 7); }); }
    else if (op === "invert") { n.forEach(function (x, i) { x.deg = 2 * base[0].deg - base[i].deg; }); }
    else if (op === "retro") { n = base.slice().reverse().map(function (x) { return Object.assign({}, x, { start: Math.max(0, L - (x.start + x.len)) }); }); }
    else if (op === "augment") { len16 = Math.min(2 * L, 32); n.forEach(function (x) { x.start *= 2; x.len *= 2; }); }
    else if (op === "diminish") {
      n.forEach(function (x) { x.start = Math.floor(x.start / 2); x.len = Math.max(1, Math.floor(x.len / 2)); });
      n = n.concat(n.map(function (x) { return Object.assign({}, x, { start: x.start + L / 2 }); }));
    }
    else if (op === "fragment") { n = n.slice(0, clamp(arg, 1, 8)); }
    n = n.filter(function (x) { return x.start < len16; }).map(function (x) { x.len = Math.min(x.len, len16 - x.start); return x; });
    n = n.filter(function (x, i) { return !n.slice(i + 1).some(function (y) { return y.start === x.start; }); });   /* one note a start: the later kept */
    var last = !!st && chord === st.to;
    if (op === "cadence" && last && n.length) {   /* the latest note goes last, held to the phrase's end */
      var j = 0; n.forEach(function (x, i) { if (x.start >= n[j].start) { j = i; } });
      var t = n[j]; n[j] = n[n.length - 1]; n[n.length - 1] = t; t.len = len16 - t.start;
    }
    return { op: op, notes: n, len16: len16, last: last, cad: st && st.cad || "full", others: st && st.others || "thin", strength: st && st.strength !== undefined ? st.strength : 1 };
  }
  function melodyLabel(comp, i, n) {
    var st = chainAt(comp, i);
    if (!st) { return "A"; }
    var a = Math.round(st.arg || 0), L = "A";
    if (comp.form && n) { formSpans(comp.form.type, Math.min(n, NCHORD)).forEach(function (s) { if (st.from >= s.a && st.from < s.b) { L = s.letter.charAt(0).toUpperCase(); } }); }
    return { seq: L + (a < 0 ? "↓" + -a : "↑" + a), invert: L + " inv", retro: L + " retro", augment: L + " aug", diminish: L + " dim", fragment: L + " frag " + a, cadence: "cad." }[st.op] || L;
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
  /* a voice for its role: the mover high and quick, the held voice low and long, the support between at 4n or 4n. (or
     resting); `avoid` a style class it must not take (B's change from A). Registers never cross: held lo 38-46, hi to 62;
     support lo 50-55; mover lo 60-67, hi from 77 */
  var RANGE = { mover: ["16n", "8n"], held: ["2n", "2n.", "1n"], support: ["4n", "4n."] };
  function drawVoice(r, role, dense, avoid) {
    var o = voiceDefaults();
    o.style = one(r, ROLE_STYLES[role].filter(function (s) { return CLASS[s] !== avoid; }));
    if (role === "mover") { o.role = "melody"; o.rate = dense ? "16n" : "8n"; o.lo = int(r, 60, 67); o.hi = o.lo + int(r, 17, 24); }
    else if (role === "held") { o.rate = dense ? one(r, ["2n", "2n."]) : one(r, ["2n.", "1n"]); o.lo = int(r, 38, 46); o.hi = o.lo + int(r, 12, 16); }
    else { o.rate = dense ? "4n" : "4n."; o.lo = int(r, 50, 55); o.hi = o.lo + int(r, 14, 19);
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
  /* B or C: the mover and the held voice change together with probability `contrast` - they trade roles (and so registers),
     each taking a style from another class, the density the other way - and the support on its own; above 0, one of the two
     at least. Exactly one voice moves and one sustains in every letter. */
  var SWAP = { mover: "held", held: "mover", support: "support" };
  function contrastOf(r, A, c) {
    var t = { dense: !A.dense, roles: {}, v: {} }, up = r(), us = r(), pair = up < c, sup = us < c;
    if (c > 0 && !pair && !sup) { if (up <= us) { pair = true; } else { sup = true; } }
    VOICES.forEach(function (v) {
      if (A.roles[v] === "support" ? sup : pair) { t.roles[v] = SWAP[A.roles[v]]; t.v[v] = drawVoice(r, t.roles[v], t.dense, cls(A.v[v])); }
      else { t.roles[v] = A.roles[v]; t.v[v] = clone(A.v[v]); }
    });
    t.v.bass = r() < c ? (A.v.bass.mode === "hold" ? pedalBass(r) : { mode: "hold" }) : clone(A.v.bass);
    t.cad = r() < c ? int(r, 0, 2) : A.cad;
    return t;
  }
  /* a': A with one or two distinct changes - a voice's rate a step within its role's, a voice's pattern, or the whole texture
     an octave (down when it sits high, up when low: the voices keep their order) */
  function variantOf(r, A) {
    var t = clone(A), cands = ["octave"];
    VOICES.forEach(function (v) { if (t.v[v].role !== "off") { cands.push("rate:" + v); if (t.v[v].style !== "free") { cands.push("pattern:" + v); } } });
    for (var n = r() < 0.5 ? 1 : 2; n > 0; n--) {
      var c = cands.splice(Math.floor(r() * cands.length), 1)[0], v = c.split(":")[1], o = t.v[v];
      if (c === "octave") { var d = t.v[VOICES.filter(function (x) { return t.roles[x] === "mover"; })[0]].hi > 84 ? -12 : 12;
        VOICES.forEach(function (x) { t.v[x].lo += d; t.v[x].hi += d; }); }
      else if (c.indexOf("rate:") === 0) { var rg = RANGE[t.roles[v]], ri = rg.indexOf(o.rate); o.rate = rg[ri + 1 < rg.length ? ri + 1 : ri - 1]; }
      else if (o.style === "arpeggio") { var g = JSON.stringify(o.grid); o.grid = one(r, PRESETS.filter(function (p) { return JSON.stringify(p[1]) !== g; }))[1].slice(); }
      else if (o.style === "euclid") { o.rot = (o.rot + 1) % o.steps; }
      else if (o.style === "strum") { o.dir = o.dir === "up" ? "down" : "up"; }
      else if (o.style === "swells") { o.tones = o.tones.length > 2 ? o.tones.slice(0, 2) : [0, 1, 2, 3]; }
      else { o.pedal = o.pedal === "root" ? "fifth" : "root"; }
    }
    return t;
  }
  function held(o, st) {
    var x = Object.assign(voiceDefaults(), { style: st, rate: "1n", lo: o.lo, hi: o.hi });
    if (st === "swells") { x.tones = [0, 1, 2]; x.attack = 0.3; x.release = 0.5; } else { x.pedal = "root"; }
    return x;
  }
  /* a section's last chord: its moving voice swells or pedals at 1n, or rests */
  function thin(t) {
    var x = clone(t.v);
    VOICES.forEach(function (v) { if (t.roles[v] !== "mover") { return; }
      var mel = x[v].role === "melody";   /* the melody never rests: the cadence op ends it */
      if (t.cad === 2 && !mel) { x[v].role = "off"; } else { x[v] = held(x[v], t.cad ? "pedal" : "swells"); if (mel) { x[v].role = "melody"; } } });
    return x;
  }
  /* the route's last chord: every voice swells or pedals, the bass holds the root */
  function settle(r, A) {
    var x = { bass: { mode: "hold" } };
    VOICES.forEach(function (v) { x[v] = held(A.v[v], r() < 0.5 ? "swells" : "pedal"); if (A.v[v].role === "melody") { x[v].role = "melody"; } });
    return x;
  }
  /* playable as written: a strum's whole spread ((tones - 1) x spread) within 60% of its step at the route's tempo (96 bpm
     when the caller does not say), a swell's attack + release at most 0.95 */
  var STRIDE = { "16n": 1, "8n": 2, "8n.": 3, "4n": 4, "4n.": 6, "2n": 8, "2n.": 12, "1n": 16 };
  function tame(x, bpm) {
    VOICES.forEach(function (v) { var o = x[v];
      if (o.style === "strum" && o.tones.length > 1) { o.spread = Math.min(o.spread, Math.floor(0.6 * 15000 / bpm * STRIDE[o.rate] / (o.tones.length - 1) / 5) * 5); }
      if (o.style === "swells" && o.attack + o.release > 0.95) { o.release = Math.round((0.95 - o.attack) * 100) / 100; } });
    return x;
  }
  /* a new comp: the route's defaults are A; every chord the core reads (the first NCHORD) that differs holds only its
     differing keys - and, under a section that sets a voice's key (opt.at[i]: the bands over chord i, as cell() takes them),
     that key too, so the form plays over the section; old chord overrides go, sections' and zones' stay. A zone is above a
     chord (resolve: route < section < chord < zone), so nothing a chord writes can beat it: zones are left to win. */
  var NCHORD = 64;
  /* a motif of 3-6 chord-degree notes (0 root .. 4 9th) with a shape: starts on the root or 5th, steps of one degree with at
     most one leap, no degree three times running, 3+ distinct degrees, a rise and a fall, ending off where it began; a rhythm
     of two or more lengths over one bar. Drawn again until it fits (seeded, so deterministic). */
  var RHYTHMS = { 3: [[4, 4, 8], [6, 2, 8], [4, 8, 4], [8, 4, 4]], 4: [[6, 2, 4, 4], [4, 4, 6, 2], [2, 2, 4, 8], [4, 2, 2, 8]],
                  5: [[4, 2, 2, 4, 4], [2, 2, 4, 4, 4], [6, 2, 2, 2, 4]], 6: [[2, 2, 4, 2, 2, 4], [4, 2, 2, 2, 2, 4]] };
  function shaped(d) {
    var up = false, down = false, leaps = 0, uniq = {};
    d.forEach(function (x, i) { uniq[x] = 1; if (i) { up = up || x > d[i - 1]; down = down || x < d[i - 1]; leaps += Math.abs(x - d[i - 1]) > 1 ? 1 : 0; }
      if (i > 1 && x === d[i - 1] && x === d[i - 2]) { leaps += 9; } });
    return Object.keys(uniq).length >= 3 && up && down && leaps <= 1 && d[d.length - 1] !== d[0];
  }
  function drawMotif(r) {
    var k = int(r, 3, 6), len = one(r, RHYTHMS[k]), d;
    var tries = 0;   /* a capped retry: a shape every draw tested so far finds in a few tries; past 200, a known shape */
    do { d = [one(r, [0, 2])]; for (var i = 1; i < k; i++) { d.push(clamp(d[i - 1] + one(r, [-2, -1, -1, 0, 1, 1, 2]), 0, 4)); } } while (!shaped(d) && ++tries < 200);
    if (!shaped(d)) { d = [[2, 3, 1], [0, 1, 2, 1], [0, 1, 2, 3, 1], [0, 1, 2, 3, 2, 1]][k - 3]; }
    var at = 0;
    return { basis: "chord", bars: 1, notes: d.map(function (x, i) { var n = { deg: x, start: at, len: len[i] }; at += len[i]; return n; }) };
  }
  /* the chain: a letter's op over its chords (A repeats, A' sequences by 1 or 2 either way, B inverts or fragments, C
     retrogrades), then a cadence on its last chord - half inside the form, full at the route's end */
  function drawChain(r, spans, n) {
    var ops = {}, chain = [];
    spans.forEach(function (s) { var k = texKey(s.letter);
      if (!ops[k]) { ops[k] = k === "A" ? { op: "repeat" } : k === "A'" ? { op: "seq", arg: one(r, [-2, -1, 1, 2]) } : k === "C" ? { op: "retro" } :
        r() < 0.5 ? { op: "invert" } : { op: "fragment", arg: int(r, 2, 3) }; }
      if (s.b - s.a > 1) { chain.push(Object.assign({ from: s.a, to: s.b - 2 }, ops[k])); }
      chain.push({ op: "cadence", cad: s.b === n ? "full" : "half", others: "thin", from: s.b - 1, to: s.b - 1 });
    });
    return chain;
  }
  function generate(comp, opt) {
    var seed = Math.max(0, opt.seed | 0), c = Math.max(0, Math.min(1, +opt.contrast || 0)), type = formOf(opt.form).id;
    var n = Math.min(NCHORD, Math.max(0, opt.nchords | 0)), bpm = +opt.bpm > 0 ? +opt.bpm : 96;
    var r = rng(seed), A = textureA(r), tex = { A: A }, spans = formSpans(type, n), out = clone(comp), all = VOICES.concat(["bass"]);
    spans.forEach(function (s) { var k = texKey(s.letter); if (!tex[k]) { tex[k] = k === "A'" ? variantOf(r, A) : contrastOf(r, A, c); } });
    var end = tame(settle(r, A), bpm);
    tame(A.v, bpm); all.forEach(function (v) { out[v] = clone(A.v[v]); });
    out.motif = drawMotif(r); out.chain = drawChain(r, spans, n);
    out.seed = seed; out.bars = "develop"; out.form = { type: type, contrast: c, seed: seed };
    out.over = { chord: {}, section: (comp.over && out.over.section) || {}, zone: (comp.over && out.over.zone) || {} };
    spans.forEach(function (s) {
      var t = tex[texKey(s.letter)], body = tame(clone(t.v), bpm), cad = tame(thin(t), bpm);
      for (var i = s.a; i < s.b; i++) {
        var at = i === n - 1 ? end : i === s.b - 1 && s.b - s.a > 1 ? cad : body, o = {}, secs = {};
        ((opt.at || [])[i] || []).forEach(function (b) { var so = b.kind === "section" && out.over.section[String(b.id)];
          if (so) { all.forEach(function (v) { Object.keys(so[v] || {}).forEach(function (k) { secs[v + "." + k] = true; }); }); } });
        all.forEach(function (v) { var d = {};
          Object.keys(at[v]).forEach(function (k) { if (secs[v + "." + k] || JSON.stringify(at[v][k]) !== JSON.stringify(out[v][k])) { d[k] = clone(at[v][k]); } });
          if (Object.keys(d).length) { o[v] = d; } });
        if (Object.keys(o).length) { out.over.chord[String(i)] = o; }
      }
    });
    return out;
  }
  function label(id) { for (var i = 0; i < STYLES.length; i++) { if (STYLES[i].id === id) { return STYLES[i].label; } } return id; }
  function summary(comp) { return VOICES.map(function (v) { return comp[v].role === "off" ? "Off" : comp[v].role === "melody" ? "Motif" : label(comp[v].style); }).join(" · "); }
  var api = { VOICES: VOICES, STYLES: STYLES, ROLES: ROLES, OPS: OPS, motifDefault: motifDefault, motifStep: motifStep, setMotif: setMotif, setChain: setChain, chainAt: chainAt, RATES: RATES, FIELDS: FIELDS, BASS: BASS, defaults: defaults, TEMPLATES: TEMPLATES,
    scopeObj: scopeObj, setValue: setValue, clearValue: clearValue, clearScope: clearScope, effective: effective, cell: cell,
    bands: bands, history: history, summary: summary, clone: clone, label: label,
    PRESETS: PRESETS, FORMS: FORMS, NCHORD: NCHORD, formSpans: formSpans, generate: generate, roll: roll };
  if (typeof module === "object" && module.exports) { module.exports = api; } else { root.FsCompose = api; }
})(typeof globalThis !== "undefined" ? globalThis : this);
