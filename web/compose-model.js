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
              { k: "pedal", label: "pedal note", kind: "menu", options: [{ id: "root", label: "key root" }, { id: "fifth", label: "key fifth" }, { id: "fixed", label: "fixed" }], def: "root" }];
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
  /* what a scope shows for a voice: its own values ("here") over the route's ("route"); every field of the voice's style */
  function effective(comp, scope, voice) {
    var base = comp[voice] || {}, own = scope.kind === "route" ? {} : ((scopeObj(comp, scope, false) || {})[voice] || {});
    var values = {}, source = {};
    var style = own.style || base.style || "free";
    var keys = voice === "bass" ? BASS.map(function (f) { return f.k; }) : COMMON.concat(FIELDS[style] || []).map(function (f) { return f.k; });
    keys.forEach(function (k) {
      var v;
      if (own[k] !== undefined) { v = own[k]; source[k] = "here"; }
      else { v = base[k] !== undefined ? base[k] : fieldDef(k, style); source[k] = "route"; }
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
  function label(id) { for (var i = 0; i < STYLES.length; i++) { if (STYLES[i].id === id) { return STYLES[i].label; } } return id; }
  function summary(comp) { return VOICES.map(function (v) { return comp[v].role === "off" ? "Off" : label(comp[v].style); }).join(" · "); }
  var api = { VOICES: VOICES, STYLES: STYLES, ROLES: ROLES, RATES: RATES, FIELDS: FIELDS, BASS: BASS, defaults: defaults, TEMPLATES: TEMPLATES,
    scopeObj: scopeObj, setValue: setValue, clearValue: clearValue, clearScope: clearScope, effective: effective, cell: cell,
    bands: bands, history: history, summary: summary, clone: clone, label: label };
  if (typeof module === "object" && module.exports) { module.exports = api; } else { root.FsCompose = api; }
})(typeof globalThis !== "undefined" ? globalThis : this);
