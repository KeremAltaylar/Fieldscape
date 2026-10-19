/* Composition step 1: the composing dock (docs/superpowers/specs/2026-10-17-composition-design.md, layout C, approved mock
   design/compose/dock-mock.html). The map keeps the top half; the dock below holds the chord strip, the section and zone
   bands, the three voice lanes and voice 1's bass row; the inspector (B) at its right shows every setting of the selection,
   inherited ones dimmed with their source. Edits go through ctx.save (the route's patch): heard at once. Desk only. */
(function () {
  var M = window.FsCompose, ctx = null, comp = null, hist = null, sel = { kind: "route" }, voice = "voice", grown = false, saveErr = null, nowTimer = null, statsTimer = null, audio = [], polls = 0;
  var gen = false, genForm = "AABA", genContrast = 0.6;   /* the Generate form panel: open, its form and contrast */
  var VS = ["voice", "sect", "v3", "bass"];
  var NAME = { voice: "voice 1", sect: "voice 2", v3: "voice 3", bass: "voice 1 bass" };
  var TAB = { voice: "voice 1", sect: "voice 2", v3: "voice 3", bass: "bass" };
  /* the cell's word (short), the tooltip's (long) */
  var WORD = { free: "free", arpeggio: "arp", strum: "strum", pedal: "pedal", euclid: "euclid", swells: "swell", off: "off", hold: "root", bpedal: "pedal" };
  var LONG = { free: "free", arpeggio: "arpeggio", strum: "strum", pedal: "pedal", euclid: "euclidean", swells: "swells", off: "off", hold: "holds the root", bpedal: "pedal" };
  /* the small picture in each cell: what the style plays, at a glance (the mock's) */
  function dots(xs, ys) { return xs.map(function (x, k) { return '<circle cx="' + x + '" cy="' + ys[k % ys.length] + '" r="2" fill="currentColor"/>'; }).join(""); }
  var GLYPH = {
    free: dots([6, 20, 33, 47, 60, 74, 87], [14, 6, 17, 9, 4, 15, 10]),
    arpeggio: '<polyline points="2,19 14,14 26,9 38,4 50,19 62,14 74,9 86,4 98,19" fill="none" stroke="currentColor" stroke-width="1.5"/>' + dots([2, 14, 26, 38, 50, 62, 74, 86, 98], [19, 14, 9, 4]),
    strum: [8, 58].map(function (x) { return [0, 1, 2].map(function (k) { return '<line x1="' + (x + k * 5) + '" y1="' + (18 - k * 6) + '" x2="' + (x + k * 5 + 30) + '" y2="' + (18 - k * 6) + '" stroke="currentColor" stroke-width="1.6"/>'; }).join(""); }).join(""),
    pedal: '<line x1="2" y1="16" x2="98" y2="16" stroke="currentColor" stroke-width="2"/><circle cx="3" cy="16" r="2.4" fill="currentColor"/>',
    euclid: [1, 0, 1, 1, 0, 1, 1, 0].map(function (on, k) { return '<circle cx="' + (7 + k * 12.3) + '" cy="11" r="' + (on ? 3.2 : 1.6) + '" fill="currentColor" opacity="' + (on ? 1 : 0.45) + '"/>'; }).join(""),
    swells: '<path d="M2,20 C30,20 34,3 50,3 C66,3 70,20 98,20" fill="none" stroke="currentColor" stroke-width="1.6"/>',
    off: '<line x1="40" y1="11" x2="60" y2="11" stroke="currentColor" stroke-width="1.5" opacity=".6"/>',
    hold: '<line x1="2" y1="5" x2="98" y2="5" stroke="currentColor" stroke-width="2"/>',
    bpedal: '<line x1="2" y1="5" x2="98" y2="5" stroke="currentColor" stroke-width="2"/><circle cx="3" cy="5" r="2.6" fill="currentColor"/>',
    boff: '<line x1="40" y1="5" x2="60" y2="5" stroke="currentColor" stroke-width="1.5" opacity=".6"/>'
  };
  var ICON = {
    undo: '<path d="M5 4 L2 7 L5 10 M2 7 H10 A4 4 0 0 1 10 15 H7" fill="none" stroke="currentColor" stroke-width="1.5"/>',
    redo: '<path d="M11 4 L14 7 L11 10 M14 7 H6 A4 4 0 0 0 6 15 H9" fill="none" stroke="currentColor" stroke-width="1.5"/>',
    grow: '<path d="M3 10 L8 5 L13 10" fill="none" stroke="currentColor" stroke-width="1.5"/>',
    shrink: '<path d="M3 6 L8 11 L13 6" fill="none" stroke="currentColor" stroke-width="1.5"/>',
    close: '<path d="M4 4 L12 12 M12 4 L4 12" fill="none" stroke="currentColor" stroke-width="1.5"/>'
  };
  var RATE = { "16n": "1/16", "8n": "1/8", "8n.": "1/8 .", "4n": "1/4", "4n.": "1/4 .", "2n": "1/2", "2n.": "1/2 .", "1n": "1 bar" };
  var PRESETS = M.PRESETS;   /* the arpeggio's patterns (the model's: Generate draws from them too) */

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) { e.className = cls; } if (text != null) { e.textContent = text; } return e; }
  function svg(vb, inner) { return '<svg viewBox="0 0 ' + vb + '" preserveAspectRatio="none" aria-hidden="true">' + inner + "</svg>"; }
  function noteName(m) { return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1); }
  function fmt(f, v) { return f.fmt === "note" ? noteName(v) : f.fmt === "pct" ? Math.round(v * 100) + " %" : f.fmt === "ms" ? Math.round(v) + " ms" : String(v); }
  function scopeName(s) { return s.kind === "route" ? "the route" : s.kind === "chord" ? "chord " + (s.i + 1) : s.kind === "section" ? "section " + (s.i + 1) : s.name; }
  function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
  /* ctx.save says true, or why it could not store (shown with Retry until a save goes through) */
  function store() { var r = ctx.save(M.clone(comp)); saveErr = r === true || r === undefined ? null : String(r || "not stored"); }
  /* a hand edit to the route or a chord after Generate: the form row says "edited" (a section's or zone's edit is not the form's) */
  function commit(fresh) {
    if (comp.form && !fresh && sel.kind !== "section" && sel.kind !== "zone") { comp.form.edited = true; }
    store(); hist.push(comp); render();
  }
  function resize() { if (window.__fa && __fa.map) { setTimeout(function () { __fa.map.resize(); }, 0); } }

  function open(c) {
    ctx = c; comp = M.clone(ctx.comp()); hist = M.history(100); hist.push(comp); sel = { kind: "route" }; voice = "voice"; saveErr = null; gen = false;
    var d = document.getElementById("compose-dock");
    if (!d) { d = el("section", "cd"); d.id = "compose-dock"; d.setAttribute("aria-label", "Compose"); document.body.appendChild(d); }
    document.body.classList.add("composing"); d.hidden = false; resize();
    render();
    if (ctx.show) { setTimeout(function () { if (ctx) { ctx.show(null); } }, 60); }   /* the route in view, in the map's new half */
    clearInterval(nowTimer); nowTimer = setInterval(markNow, 500);
    audio = []; clearInterval(statsTimer); statsTimer = setInterval(pollAudio, 1000); pollAudio();
  }
  function close() {
    var d = document.getElementById("compose-dock"); if (d) { d.hidden = true; }
    document.body.classList.remove("composing", "composing-grown"); grown = false; clearInterval(nowTimer); clearInterval(statsTimer);
    if (ctx) { ctx.light(null); } ctx = null; resize();
  }

  /* a re-render keeps keyboard focus: the control at the same place (its [data-fk] holder, its tag, its index there) */
  function focusAt(d) {
    var a = document.activeElement, k = a && d.contains(a) ? a.closest("[data-fk]") : null;
    return k ? { fk: k.dataset.fk, tag: a.tagName, i: k === a ? -1 : [].indexOf.call(k.querySelectorAll(a.tagName), a) } : null;
  }
  function refocus(d, f) {
    if (!f) { return; }
    var k = [].filter.call(d.querySelectorAll("[data-fk]"), function (x) { return x.dataset.fk === f.fk; })[0];
    var t = k && (f.i < 0 ? k : k.querySelectorAll(f.tag)[f.i]);
    if (t && t.tagName === f.tag) { t.focus({ preventScroll: true }); }
  }
  function render() {
    var d = document.getElementById("compose-dock"), was = focusAt(d); d.textContent = "";
    draw(d); refocus(d, was);
  }
  function back() { var c = ctx; close(); if (c && c.back) { c.back(); } }
  function draw(d) {
    var n = ctx.chords.length;
    if (!n) {   /* C-5 empty: a route with no progression */
      var e = el("div", "cd-empty"); e.dataset.fk = "empty";
      e.appendChild(el("p", "cd-empty-t", "This route has no chords yet."));
      e.appendChild(el("p", "hint", "The voices follow the route’s chords, so they come first. Add a progression in its patch, then compose."));
      var b = el("button", "ghost", "Open the patch"); b.type = "button"; b.addEventListener("click", back); e.appendChild(b); d.appendChild(e); return;
    }
    var main = el("div", "cd-main"), insp = el("aside", "cd-insp"); insp.id = "cd-insp";
    var secs = ctx.sections(), zs = ctx.zones(), bs = M.bands(ctx.coords, secs || [], zs);
    main.appendChild(header()); if (comp.form) { main.appendChild(formRow(n)); } main.appendChild(strip(n));
    bandRows(main, bs, secs === null, zs); main.appendChild(el("div", "cd-sep"));
    main.appendChild(lanes(n, bs)); main.appendChild(foot());
    d.appendChild(main); inspector(insp, bs, n); d.appendChild(insp);
    /* C-8, one screen: a style with many settings, in a zone, is taller than the dock - the rows close up, in two steps, rather
       than anything scrolling or being cut off */
    ["tight", "tighter"].forEach(function (k) { if (insp.scrollHeight > insp.clientHeight + 1) { insp.classList.add(k); } });
    markNow();
  }
  function header() {
    var h = el("div", "cd-head"), t = el("h2", "cd-title");   /* not a <header>: the listener hides those */
    h.dataset.fk = "head";
    t.appendChild(el("i", null, "Compose")); t.appendChild(document.createTextNode(ctx.route.properties.name || "Unnamed route")); h.appendChild(t);
    var sv = el("span", "cd-saved" + (saveErr ? " warn" : ""), saveErr ? "Not saved: " + saveErr : "saved");
    if (saveErr) { var rt = el("button", "cd-retry", "Retry"); rt.type = "button"; rt.title = "Save these settings again";
      rt.addEventListener("click", function () { store(); render(); }); sv.appendChild(rt); }
    sv.setAttribute("role", "status"); h.appendChild(sv);
    var tl = el("label"); tl.appendChild(el("span", "cd-lbl", "start from"));
    var s = el("select"); s.id = "cd-template"; s.setAttribute("aria-label", "Start from a template");
    var cur = M.TEMPLATES.filter(function (x) { var y = x.comp(); return M.VOICES.concat(["bass"]).every(function (v) { return same(y[v], comp[v]); }); })[0];
    s.appendChild(new Option(cur ? "—" : "your own", ""));
    M.TEMPLATES.forEach(function (x) { s.appendChild(new Option(x.label, x.id)); });
    s.value = cur ? cur.id : "";
    s.addEventListener("change", function () { var x = M.TEMPLATES.filter(function (y) { return y.id === s.value; })[0];
      if (x) { var nc = x.comp(); nc.seed = comp.seed;
        nc.over = comp.form ? { chord: {}, section: comp.over.section, zone: comp.over.zone } : comp.over;   /* a generated piece's chords go with its form */
        comp = nc; commit(); } });
    tl.appendChild(s); h.appendChild(tl);
    var sd = el("label"); sd.appendChild(el("span", "cd-lbl", "seed"));
    var si = el("input"); si.type = "number"; si.min = 0; si.max = 99999; si.value = comp.seed; si.setAttribute("aria-label", "Seed");
    si.addEventListener("change", function () { var v = Math.max(0, Math.min(99999, +si.value | 0));
      if (gen) { generate(v); } else { comp.seed = v; commit(); } });   /* the panel open: that seed's piece */
    sd.appendChild(si); h.appendChild(sd);
    var gb = el("button", "cd-gen", "Generate"); gb.appendChild(el("span", "cd-lbl", " form")); gb.type = "button"; gb.setAttribute("aria-pressed", String(gen));
    gb.title = "Write a whole piece in a form: its sections' voices, styles and settings (one Ctrl+Z takes it back)";
    gb.addEventListener("click", function () { gen = !gen;
      if (gen && comp.form) { genForm = comp.form.type; genContrast = comp.form.contrast; } render(); });
    h.appendChild(gb);
    [[ICON.undo, "Undo (Ctrl+Z)", undo, hist.canUndo()], [ICON.redo, "Redo (Ctrl+Shift+Z)", redo, hist.canRedo()],
     [grown ? ICON.shrink : ICON.grow, grown ? "Show the map" : "Grow the dock", function () { grown = !grown; document.body.classList.toggle("composing-grown", grown); resize(); render(); }, true],
     [ICON.close, "Close", close, true]].forEach(function (x) {
      var b = el("button", "cd-icon"); b.type = "button"; b.innerHTML = '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">' + x[0] + "</svg>";
      b.title = x[1]; b.setAttribute("aria-label", x[1]); b.disabled = !x[3]; b.addEventListener("click", x[2]); h.appendChild(b); });
    return h;
  }
  /* the form's letters over their chords (a generated piece's), a repeat dashed */
  function formRow(n) {
    var ed = !!comp.form.edited, r = el("div", "cd-row cd-formrow" + (ed ? " edited" : ""));
    r.appendChild(el("span", "cd-name", ed ? "form · edited" : "form"));
    var t = el("div", "cd-track cd-form"), seen = {};
    t.title = (ed ? "generated · edited: " : "generated: ") + M.FORMS.filter(function (f) { return f.id === comp.form.type; })[0].label + " · seed " + comp.form.seed +
      " · contrast " + Math.round(comp.form.contrast * 100) + " %" + (n > M.NCHORD ? " · over chords 1–" + M.NCHORD + " of " + n : "");
    r.title = t.title;
    M.formSpans(comp.form.type, Math.min(n, M.NCHORD)).forEach(function (x) {
      var b = el("span", "cd-band form" + (seen[x.letter] ? " rep" : ""), x.letter); seen[x.letter] = true;
      b.style.left = (x.a / n * 100) + "%"; b.style.width = "calc(" + ((x.b - x.a) / n * 100) + "% - 3px)";   /* the strip's gap */ t.appendChild(b); });
    r.appendChild(t); return r;
  }
  /* the bands over each chord (as the lanes take them): a section's keys are written on its chords, so the form plays over it */
  function generate(seed) {
    var n = ctx.chords.length, bs = M.bands(ctx.coords, ctx.sections() || [], ctx.zones()), at = [];
    for (var i = 0; i < n; i++) { at.push(bs.filter(function (b) { return b.t0 < (i + 1) / n && b.t1 > i / n; })); }
    comp = M.generate(comp, { form: genForm, contrast: genContrast, seed: seed, nchords: n, at: at,
      bpm: ((ctx.route.properties || {}).patch || {}).tempo || 72 });   /* the core's own default tempo */
    commit(true);
  }
  function strip(n) {
    var r = el("div", "cd-row"); r.appendChild(el("span", "cd-name", "chords"));
    var s = el("div", "cd-strip"); s.id = "cd-strip"; s.style.gridTemplateColumns = "repeat(" + n + ", minmax(0, 1fr))";
    ctx.chords.forEach(function (c, i) {
      var b = el("button", "cd-chord"); b.type = "button"; b.dataset.fk = "chord:" + i; b.style.background = c.colour; b.title = (i + 1) + " · " + c.label;
      b.appendChild(el("b", null, String(i + 1))); b.appendChild(el("span", null, c.label));
      b.setAttribute("aria-label", "chord " + (i + 1) + ", " + c.label); if (sel.kind === "chord" && sel.i === i) { b.classList.add("on"); }
      b.addEventListener("click", function () { pick({ kind: "chord", i: i }); }); s.appendChild(b); });
    r.appendChild(s); return r;
  }
  function bandName(b, zs) { return b.kind === "zone" ? ((zs.filter(function (z) { return z.id === b.id; })[0] || {}).name || "zone") : "section " + (b.id + 1); }
  function isSel(kind, id) { return sel.kind === kind && String(kind === "zone" ? sel.id : sel.i) === String(id); }
  function bandRows(main, bs, noSections, zs) {
    var n = ctx.chords.length;
    [["section", "sections"], ["zone", "zones"]].forEach(function (k) {
      var r = el("div", "cd-row"); r.appendChild(el("span", "cd-name", k[1]));
      var t = el("div", "cd-track");
      for (var i = 1; i < n; i++) { var tk = el("i", "tick"); tk.style.left = (i / n * 100) + "%"; t.appendChild(tk); }
      var mine = bs.filter(function (b) { return b.kind === k[0]; });
      if (k[0] === "section" && noSections) { t.appendChild(el("span", "cd-hint", "walk onto the route to see its sections")); }
      else if (!mine.length) { t.appendChild(el("span", "cd-hint", k[0] === "zone" ? "no zone crosses this route" : "no sections cross this route")); }
      mine.forEach(function (b) {
        var name = bandName(b, zs);
        var x = el("button", "cd-band " + k[0], name); x.type = "button"; x.dataset.fk = "band:" + k[0] + ":" + b.id; x.style.left = (b.t0 * 100) + "%"; x.style.width = Math.max(1.5, (b.t1 - b.t0) * 100) + "%";
        x.title = name + (comp.over[k[0]][String(b.id)] ? " · changes the voices" : " · changes nothing yet");
        if (!comp.over[k[0]][String(b.id)]) { x.classList.add("plain"); }
        if (isSel(k[0], b.id)) { x.classList.add("on"); }
        x.addEventListener("click", function () { pick(k[0] === "zone" ? { kind: "zone", id: b.id, name: name } : { kind: "section", i: b.id }); });
        t.appendChild(x); });
      r.appendChild(t); main.appendChild(r); });
  }
  /* bass: route < section < chord < zone, as the voices' cells */
  function bassAt(i, at) {
    var mode = comp.bass.mode || "hold", src = "route";
    function take(o, s) { if (o && o.bass && o.bass.mode) { mode = o.bass.mode; src = s; } }
    at.filter(function (b) { return b.kind === "section"; }).forEach(function (b) { take(comp.over.section[String(b.id)], "section"); });
    take(comp.over.chord[String(i)], "chord");
    at.filter(function (b) { return b.kind === "zone"; }).forEach(function (b) { take(comp.over.zone[b.id], "zone"); });
    return { style: mode === "hold" ? "hold" : mode === "pedal" ? "bpedal" : "boff", source: src };
  }
  function laneSub(v) {
    if (v === "bass") { var o = M.BASS[0].options.filter(function (x) { return x.id === (comp.bass.mode || "hold"); })[0]; return o ? o.label : ""; }
    var c = comp[v]; if (c.role === "off") { return "off"; }
    return M.label(c.style) + (c.style === "pedal" && c.pedal && c.pedal !== "root" ? " · " + (c.pedal === "fifth" ? "5th" : "fixed") : "");
  }
  function lanes(n, bs) {
    var w = el("div", "cd-lanes"); w.id = "cd-lanes";
    var cells = [];
    for (var i = 0; i < n; i++) { (function (i) { var at = bs.filter(function (b) { return b.t0 < (i + 1) / n && b.t1 > i / n; });
      var c = M.cell(comp, i, at); c.push(bassAt(i, at)); c.at = at; cells.push(c); })(i); }
    VS.forEach(function (v, vi) {
      var r = el("div", "cd-row cd-lane" + (v === "bass" ? " bass" : "")); r.dataset.voice = v;
      var nm = el("span", "cd-name", NAME[v]); nm.appendChild(el("small", null, laneSub(v))); r.appendChild(nm);
      var g = el("div", "cd-cells"); g.style.gridTemplateColumns = "repeat(" + n + ", minmax(0, 1fr))";
      for (var i = 0; i < n; i++) {
        var c = cells[i][vi], prev = i ? cells[i - 1][vi] : null, st = c.style === "off" && v === "bass" ? "boff" : c.style;
        var b = el("button", "cd-cell src-" + c.source + (c.style === "off" || st === "boff" ? " off" : ""));
        var on = sel.kind === "chord" && sel.i === i && voice === v;
        if (on) { b.classList.add("on"); }
        else if (prev && prev.style === c.style && prev.source === c.source && c.source !== "chord") { b.classList.add("cont"); }   /* the selected cell keeps its word */
        if ((sel.kind === "zone" || sel.kind === "section") && c.source === sel.kind && cells[i].at.some(function (x) { return x.kind === sel.kind && isSel(x.kind, x.id); })) { b.classList.add("lit"); }
        b.innerHTML = "<span>" + (WORD[st] || st) + "</span>" + svg(v === "bass" ? "100 10" : "100 22", GLYPH[st] || GLYPH.free);
        b.type = "button"; b.dataset.i = i; b.dataset.fk = "cell:" + v + ":" + i;
        b.title = (i + 1) + " · " + ctx.chords[i].label + " · " + (LONG[st] || st) + " (" + c.source + ")";
        b.setAttribute("aria-label", NAME[v] + ", chord " + (i + 1) + ": " + (LONG[st] || st) + (c.source !== "route" ? ", set by the " + c.source : ""));
        (function (i, v) { b.addEventListener("click", function () { voice = v; pick({ kind: "chord", i: i }); });
          b.addEventListener("dblclick", function () { listen((i + 0.5) / n); }); })(i, v);
        g.appendChild(b);
      }
      r.appendChild(g); w.appendChild(r); });
    return w;
  }
  /* the walker's chord: a dot on voice 1's cell it plays, kept current while the dock is open */
  function markNow() {
    if (!ctx) { return; }
    var t = ctx.now ? ctx.now() : null, n = ctx.chords.length, k = t == null ? -1 : Math.min(n - 1, Math.floor(t * n));
    [].forEach.call(document.querySelectorAll('#cd-lanes .cd-lane[data-voice="voice"] .cd-cell'), function (c, i) { c.classList.toggle("now", i === k); });
  }
  /* the foot's dropout readout: the engine's slow blocks (web/core-worklet.js), asked once a second while the dock is open;
     "audio ok", or how many in the last minute (warn), the slowest block in its title. Tests read __fa.dropouts */
  function pollAudio() {
    polls++;
    if (!ctx || !ctx.stats || !ctx.stats(function (m) {
      var now = Date.now(), last = audio[audio.length - 1];
      if (last && m.underruns < last.u) { audio = []; }          /* a new engine (lab mode turned): its count starts again */
      audio.push({ at: now, u: m.underruns, ms: m.maxMs });
      while (audio.length > 1 && now - audio[1].at >= 60000) { audio.shift(); }
      showAudio();
    })) { showAudio(); }
  }
  function showAudio() {
    var n = audio.length > 1 ? audio[audio.length - 1].u - audio[0].u : 0, slow = 0;
    audio.forEach(function (x) { slow = Math.max(slow, x.ms); });
    window.__fa = window.__fa || {}; __fa.dropouts = { n: n, slowestMs: slow, polls: polls, answers: audio.length };
    var e = document.getElementById("cd-audio"); if (!e) { return; }
    e.classList.toggle("warn", n > 0);
    e.lastChild.textContent = n ? n + (n === 1 ? " dropout" : " dropouts") + " in the last minute" : "audio ok";
    e.title = audio.length ? "slowest block in the last minute: " + slow + " ms (a block lasts 2.7 ms)" : "slowest block: none yet - no sound is playing";
  }
  function foot() {
    var f = el("div", "cd-foot"), lg = el("div", "cd-legend");
    [["route", ""], ["by a section", " by-section"], ["by a zone", " by-zone"], ["by a chord", " by-chord"]].forEach(function (x) {
      var s = el("span"); s.appendChild(el("i", x[1].trim() || null)); s.appendChild(document.createTextNode(x[0])); lg.appendChild(s); });
    f.appendChild(lg);
    var k = el("span", "cd-keys");
    k.innerHTML = "<kbd>←</kbd><kbd>→</kbd> cells <kbd>Enter</kbd> open <kbd>Del</kbd> clear <kbd>Space</kbd> sound";
    f.appendChild(k);
    var au = el("span", "cd-audio"); au.id = "cd-audio"; au.setAttribute("role", "status"); au.appendChild(el("i")); au.appendChild(el("span"));
    f.appendChild(au); setTimeout(showAudio, 0); return f;
  }
  function pick(s) { sel = s; gen = false; ctx.light(s.kind === "chord" ? { chord: s.i } : s.kind === "zone" ? { zone: s.id } : null); render(); }
  function listen(t) { ctx.listenAt(t); render(); }

  function ownOf(s, v) { return s.kind === "route" ? null : ((M.scopeObj(comp, s, false) || {})[v] || null); }
  function changes(s) { return VS.filter(function (v) { var o = ownOf(s, v); return o && Object.keys(o).length; }); }
  function span(bs, kind, id) {   /* the chords a band covers, "11–12" */
    var n = ctx.chords.length, b = bs.filter(function (x) { return x.kind === kind && String(x.id) === String(id); });
    if (!b.length) { return null; }
    var a = Math.floor(Math.min.apply(null, b.map(function (x) { return x.t0; })) * n) + 1, z = Math.min(n, Math.ceil(Math.max.apply(null, b.map(function (x) { return x.t1; })) * n));
    return a === z ? "chord " + a : "chords " + a + "–" + z;
  }
  function voicesList(vs) { var w = vs.map(function (v) { return v === "bass" ? "the bass" : TAB[v].replace("voice ", ""); });
    if (!w.length) { return "changes nothing yet"; }
    var nums = w.filter(function (x) { return x !== "the bass"; }), bass = w.length > nums.length;
    var s = nums.length ? (nums.length > 1 ? "voices " + nums.slice(0, -1).join(", ") + " and " + nums[nums.length - 1] : "voice " + nums[0]) : "";
    return s + (bass ? (s ? " and the bass" : "the bass") : ""); }

  function inspector(w, bs, n) {
    if (gen) { genPanel(w, n); return; }
    var ih = el("div", "cd-ih"), title; ih.dataset.fk = "ih";
    var cap = TAB[voice].charAt(0).toUpperCase() + TAB[voice].slice(1);
    if (sel.kind === "chord") { title = cap + " · chord " + (sel.i + 1); }
    else if (sel.kind === "route") { title = cap + " · the route"; }
    else { title = (sel.kind === "zone" ? "Zone · " : "Section · ") + scopeName(sel).replace("section ", ""); }
    var h = el("h3", null, title); h.id = "cd-insp-title"; ih.appendChild(h);
    if (sel.kind !== "route") { var cl = el("button", "ghost cd-clear", "clear"); cl.type = "button"; cl.title = "Clear " + scopeName(sel) + "’s settings";
      cl.disabled = !changes(sel).length; cl.addEventListener("click", function () { M.clearScope(comp, sel); commit(); }); ih.appendChild(cl); }
    w.appendChild(ih);
    var sub;
    if (sel.kind === "chord") { var own = Object.keys(ownOf(sel, voice) || {}).length;
      sub = ctx.chords[sel.i].label + " · " + (own ? own + " of its own · the rest from the route" : "nothing of its own · all from the route"); }
    else if (sel.kind === "route") { sub = "every chord, unless a section, chord or zone says otherwise"; }
    else { sub = (span(bs, sel.kind, sel.kind === "zone" ? sel.id : sel.i) || "off the route") + " · " + voicesList(changes(sel)); }
    w.appendChild(el("p", "cd-isub", sub));
    if (sel.kind === "zone") { zoneLines(w); }
    var tabs = el("div", "cd-seg cd-vtabs"); tabs.dataset.fk = "vtabs"; tabs.style.setProperty("--n", 4); tabs.setAttribute("role", "group"); tabs.setAttribute("aria-label", "Voice");
    var ch = changes(sel);
    VS.forEach(function (v) { var b = el("button", ch.indexOf(v) >= 0 ? "has" : null, TAB[v]); b.type = "button"; b.setAttribute("aria-pressed", String(v === voice));
      b.addEventListener("click", function () { voice = v; render(); }); tabs.appendChild(b); });
    w.appendChild(tabs);
    var e = M.effective(comp, sel, voice, sel.kind === "chord" ? bs.filter(function (b) { return b.t0 < (sel.i + 1) / n && b.t1 > sel.i / n; }) : null);
    var style = voice === "bass" ? null : e.style;
    var fields = voice === "bass" ? M.BASS : M.FIELDS.common.concat(M.FIELDS[style] || []);
    fields.filter(function (f) { return f.kind !== "grid" && f.kind !== "accents"; }).forEach(function (f) { w.appendChild(row(f, e.values[f.k], e.source[f.k])); });
    var grid = fields.filter(function (f) { return f.kind === "grid"; })[0];
    if (grid) { patternBlock(w, grid, fields.filter(function (f) { return f.kind === "accents"; })[0], e); }
    var tools = el("div", "cd-itools"); tools.dataset.fk = "tools";
    var li = el("button", "cd-listen", "Listen here"); li.type = "button"; li.title = "Put the walker on this stretch of the route";
    li.addEventListener("click", function () {
      var b = sel.kind === "zone" || sel.kind === "section" ? bs.filter(function (x) { return x.kind === sel.kind && isSel(x.kind, x.id); })[0] : null;
      listen(sel.kind === "chord" ? (sel.i + 0.5) / n : b ? (b.t0 + b.t1) / 2 : 0.02); });
    tools.appendChild(li);
    var sm = el("button", null, "Show on map"); sm.type = "button"; sm.title = "Bring the selection into view on the map";
    sm.addEventListener("click", function () { ctx.show(sel.kind === "chord" ? { chord: sel.i } : sel.kind === "zone" ? { zone: sel.id } : null); });
    tools.appendChild(sm);
    w.appendChild(tools);
  }
  /* the Generate form panel (in the inspector's place): the form, how far B and C move from A, Generate - each press a new seed */
  function genPanel(w, n) {
    var ih = el("div", "cd-ih"); ih.dataset.fk = "ih";
    var h = el("h3", null, "Generate form"); h.id = "cd-insp-title"; ih.appendChild(h);
    var cl = el("button", "ghost cd-clear", "close"); cl.type = "button"; cl.title = "Close Generate form";
    cl.addEventListener("click", function () { gen = false; render(); }); ih.appendChild(cl); w.appendChild(ih);
    var f = M.FORMS.filter(function (x) { return x.id === (comp.form || {}).type; })[0];
    w.appendChild(el("p", "cd-isub", f ? "now " + f.label + " · seed " + comp.form.seed + " · contrast " + Math.round(comp.form.contrast * 100) + " %" : "writes every chord’s voices · sections and zones stay"));
    var r = el("div", "cd-row2 wide mine"); r.dataset.k = "form"; r.dataset.fk = "row:form"; r.appendChild(el("span", "cd-k", "form"));
    var c = el("span", "cd-ctl"); c.appendChild(seg(M.FORMS.map(function (x) { return [x.id, x.label]; }), genForm, function (v) { genForm = v; render(); }, "form")); r.appendChild(c); w.appendChild(r);
    var k = { fmt: "pct" };
    var q = el("div", "cd-row2 mine"); q.dataset.k = "contrast"; q.dataset.fk = "row:contrast"; q.appendChild(el("span", "cd-k", "contrast"));
    var kc = el("span", "cd-ctl"), ki = el("input"), out = el("span", "cd-v", fmt(k, genContrast));
    ki.type = "range"; ki.min = 0; ki.max = 1; ki.step = 0.05; ki.value = genContrast; ki.setAttribute("aria-label", "contrast");
    ki.title = "How far B and C move from A: 0 the same, 100 % every voice another kind";
    var fill = function () { ki.style.setProperty("--fill", (+ki.value * 100) + "%"); }; fill();
    ki.addEventListener("input", function () { genContrast = +ki.value; out.textContent = fmt(k, genContrast); fill(); });
    kc.appendChild(ki); q.appendChild(kc); q.appendChild(out); q.appendChild(el("span", "cd-src")); w.appendChild(q);
    /* how the chords split: "A A B A · 4 4 4 4 chords" */
    var m = Math.min(n, M.NCHORD), sp = M.formSpans(genForm, m);
    w.appendChild(el("p", "cd-isub", sp.map(function (x) { return x.letter; }).join(" ") + " · " + sp.map(function (x) { return x.b - x.a; }).join(" ") + (m === 1 ? " chord" : " chords")));
    if (n > m) { var lim = el("p", "cd-isub", "form over chords 1–" + m + " of " + n); lim.title = "The engine reads " + m + " chords' own settings; the rest play the route's"; w.appendChild(lim); }
    w.appendChild(el("p", "cd-isub", "a new seed each press · Ctrl+Z undoes"));
    var tools = el("div", "cd-itools"); tools.dataset.fk = "tools";
    var g = el("button", "cd-listen", "Generate"); g.type = "button"; g.id = "cd-generate"; g.title = "Write a new piece in this form, with a new seed";
    g.addEventListener("click", function () { generate(M.roll(comp.seed)); }); tools.appendChild(g); w.appendChild(tools);
  }
  function seg(opts, value, onPick, label) {
    var s = el("span", "cd-seg"); s.style.setProperty("--n", opts.length); s.setAttribute("role", "group"); if (label) { s.setAttribute("aria-label", label); }
    opts.forEach(function (o) { var b = el("button", null, o[1]); b.type = "button"; b.setAttribute("aria-pressed", String(String(o[0]) === String(value)));
      b.addEventListener("click", function () { onPick(o[0]); }); s.appendChild(b); });
    return s;
  }
  function zoneLines(w) {
    var o = M.scopeObj(comp, sel, false) || { when: "inside", blend: 1 };
    [["when", [["inside", "while inside"], ["enter", "on entering"]]], ["blend", [[1, "next bar"], [2, "over 2 bars"], [4, "over 4 bars"]]]].forEach(function (k) {
      var r = el("div", "cd-row2 wide" + (M.scopeObj(comp, sel, false) ? " mine" : " inh")); r.dataset.k = k[0]; r.dataset.fk = "row:" + k[0];
      r.appendChild(el("span", "cd-k", k[0]));
      var c = el("span", "cd-ctl"); c.appendChild(seg(k[1], o[k[0]], function (v) { var z = M.scopeObj(comp, sel, true); z[k[0]] = v; commit(); }, k[0])); r.appendChild(c);
      w.appendChild(r); });
  }
  function srcCell(src, f, keys) {
    var s = el("span", "cd-src");
    if (sel.kind === "route") { s.textContent = "route"; return s; }
    if (src !== "here") { s.textContent = src === "section" ? "section" : "route"; return s; }
    s.appendChild(document.createTextNode("here"));
    var x = el("button", "cd-reset", "×"); x.type = "button"; x.title = "Back to the route’s"; x.setAttribute("aria-label", "Reset " + f.label + " to the route’s");
    x.addEventListener("click", function () { keys.forEach(function (k) { M.clearValue(comp, sel, voice, k); }); commit(); }); s.appendChild(x);
    return s;
  }
  function row(f, v, src) {
    var mine = sel.kind === "route" || src === "here";
    var r = el("div", "cd-row2 " + (mine ? "mine" : "inh")); r.dataset.k = f.k; r.dataset.fk = "row:" + f.k;
    r.appendChild(el("span", "cd-k", f.label));
    var set = function (val) { M.setValue(comp, sel, voice, f.k, val); commit(); };
    var ctl = el("span", "cd-ctl"), out = el("span", "cd-v", "");
    if (f.kind === "menu") {
      var m = el("select"); m.setAttribute("aria-label", f.label);
      f.options.forEach(function (o) { m.appendChild(new Option(f.k === "rate" ? RATE[o.id] || o.label : o.label, o.id)); }); m.value = v;
      m.addEventListener("change", function () { set(m.value); }); ctl.appendChild(m);
    } else if (f.kind === "toggle") {
      ctl.appendChild(seg([[true, "on"], [false, "off"]], !!v, function (x) { set(x); }, f.label));
    } else if (f.kind === "knob") {
      var k = el("input"); k.type = "range"; k.min = f.min; k.max = f.max; k.step = f.step; k.value = v; k.setAttribute("aria-label", f.label);
      var fill = function () { k.style.setProperty("--fill", ((+k.value - f.min) / (f.max - f.min) * 100) + "%"); }; fill();
      out.textContent = fmt(f, v);
      k.addEventListener("input", function () { out.textContent = fmt(f, +k.value); fill(); });
      k.addEventListener("change", function () { set(+k.value); });
      k.addEventListener("dblclick", function () { set(f.def); });
      ctl.appendChild(k);
    } else if (f.kind === "tones") {
      var t = el("span", "cd-seg cd-tones"); t.style.setProperty("--n", 5); t.setAttribute("role", "group"); t.setAttribute("aria-label", f.label);
      ["1", "3", "5", "7", "9"].forEach(function (lab, d) { var b = el("button", null, lab); b.type = "button"; b.setAttribute("aria-pressed", String((v || []).indexOf(d) >= 0));
        b.addEventListener("click", function () { var a = (v || []).slice(), i = a.indexOf(d); if (i >= 0) { a.splice(i, 1); } else { a.push(d); } a.sort(); set(a.length ? a : [0]); }); t.appendChild(b); });
      ctl.appendChild(t);
    }
    r.appendChild(ctl); r.appendChild(out); r.appendChild(srcCell(src, f, [f.k]));
    return r;
  }
  /* the pattern grid: rows chord degrees (9 7 5 3 root), columns steps; one degree per column, click again to rest. Arpeggio adds
     presets and the accents; a euclid's pitches past its steps are dimmed. */
  function patternBlock(w, grid, acc, e) {
    var src = e.source[grid.k] === "here" || (acc && e.source[acc.k] === "here") ? "here" : e.source[grid.k] || "route";
    var eb = el("p", "cd-ieb" + (src === "here" || sel.kind === "route" ? " mine" : "")); eb.dataset.fk = "ieb";
    eb.appendChild(el("span", null, grid.label + " · ")); var sc = srcCell(src, grid, acc ? [grid.k, acc.k] : [grid.k]); eb.appendChild(sc);
    w.appendChild(eb);
    var cur = (e.values[grid.k] || []).slice(); while (cur.length < 16) { cur.push(-1); }
    var set = function (val) { M.setValue(comp, sel, voice, grid.k, val); commit(); };
    if (acc) {
      var p = el("div", "cd-presets"); p.dataset.fk = "presets"; p.setAttribute("role", "group"); p.setAttribute("aria-label", "Patterns");
      PRESETS.forEach(function (x) { var b = el("button", null, x[0]); b.type = "button"; b.setAttribute("aria-pressed", String(same(x[1], cur))); b.addEventListener("click", function () { set(x[1].slice()); }); p.appendChild(b); });
      w.appendChild(p);
    }
    var accv = acc ? (e.values[acc.k] || []).slice() : null; if (accv) { while (accv.length < 16) { accv.push(70); } }
    var steps = e.style === "euclid" ? e.values.steps : 16;
    var g = el("div", "cd-pgrid" + (src === "here" || sel.kind === "route" ? "" : " inh")); g.dataset.fk = "pgrid";
    ["9", "7", "5", "3", "root"].forEach(function (lab, ri) { var d = 4 - ri; g.appendChild(el("span", "cd-gk", lab));
      for (var c = 0; c < 16; c++) { (function (c) {
        var b = el("button", "cd-gc" + (c % 4 === 0 ? " beat" : "") + (cur[c] === d ? " on" + (accv && accv[c] >= 85 ? " acc" : "") : "") + (c >= steps ? " past" : ""));
        b.type = "button"; b.setAttribute("aria-label", "step " + (c + 1) + ", " + lab); b.setAttribute("aria-pressed", String(cur[c] === d));
        b.addEventListener("click", function () { var a = cur.slice(); a[c] = a[c] === d ? -1 : d; set(a); }); g.appendChild(b); })(c); } });
    w.appendChild(g);
    if (acc) {
      var a = el("div", "cd-accs" + (src === "here" || sel.kind === "route" ? "" : " inh")); a.dataset.fk = "accs"; a.appendChild(el("span", "cd-gk", "acc"));
      accv.forEach(function (lv, c) { var b = el("button", "cd-ab"); b.type = "button"; b.style.setProperty("--h", lv + "%"); b.setAttribute("aria-label", "accent, step " + (c + 1) + ", " + lv);
        b.addEventListener("click", function (ev) { var r = b.getBoundingClientRect(), x = accv.slice();
          x[c] = Math.round(Math.max(0, Math.min(1, 1 - (ev.clientY - r.top) / r.height)) * 100); M.setValue(comp, sel, voice, acc.k, x); commit(); });
        a.appendChild(b); });
      w.appendChild(a);
    }
  }
  function undo() { var c = hist.undo(); if (c) { comp = c; store(); render(); } }
  function redo() { var c = hist.redo(); if (c) { comp = c; store(); render(); } }
  /* the keyboard (the dock's own keys only where they mean something; a focused button, select, range or field keeps its
     native keys): arrows move between cells, Enter opens the inspector and Delete clears from a cell, chord or band, Space is
     the sound from the page or a cell or chord, Ctrl+Z / Ctrl+Shift+Z outside a field, Esc back to the patch */
  function on(t, sel) { return !!(t && t.closest && t.closest(sel)); }
  document.addEventListener("keydown", function (e) {
    if (!ctx) { return; }
    var t = e.target, field = /INPUT|SELECT|TEXTAREA/.test((t && t.tagName) || "") || (t && t.isContentEditable);
    var grid = on(t, "#compose-dock .cd-cell, #compose-dock .cd-chord, #compose-dock .cd-band");
    var page = !t || t === document || t === document.body || t === document.documentElement;
    if (e.key === "Escape") { if (field && !on(t, "#compose-dock")) { return; } e.preventDefault(); e.stopPropagation(); back(); return; }
    if (field) { return; }
    if ((e.ctrlKey || e.metaKey) && !e.altKey && e.key.toLowerCase() === "z") { e.preventDefault(); if (e.shiftKey) { redo(); } else { undo(); } return; }
    if (e.ctrlKey || e.metaKey || e.altKey || !ctx.chords.length) { return; }
    var n = ctx.chords.length;
    if (e.key === " ") {
      if (!page && !on(t, "#compose-dock .cd-cell, #compose-dock .cd-chord")) { return; }
      var pb = document.getElementById("patch-play"); if (pb) { pb.click(); } e.preventDefault(); return;
    }
    if (e.key === "Enter") { if (!grid) { return; } var f = document.querySelector("#cd-insp select, #cd-insp input, #cd-insp .cd-row2 button"); if (f) { f.focus(); } e.preventDefault(); return; }
    if (e.key === "Delete" || e.key === "Backspace") { if (!grid || sel.kind === "route") { return; } M.clearScope(comp, sel); commit(); e.preventDefault(); return; }
    if (!/^Arrow/.test(e.key) || !(page || on(t, "#compose-dock"))) { return; }
    if (on(t, "#compose-dock") && !grid) { return; }      /* arrows on the dock's other buttons: nothing of ours */
    e.preventDefault();
    if (sel.kind !== "chord") { pick({ kind: "chord", i: 0 }); }
    else {
      var vi = VS.indexOf(voice);
      if (e.key === "ArrowRight") { pick({ kind: "chord", i: Math.min(n - 1, sel.i + 1) }); }
      else if (e.key === "ArrowLeft") { pick({ kind: "chord", i: Math.max(0, sel.i - 1) }); }
      else if (e.key === "ArrowDown") { voice = VS[Math.min(VS.length - 1, vi + 1)]; render(); }
      else if (e.key === "ArrowUp") { voice = VS[Math.max(0, vi - 1)]; render(); }
    }
    var c = document.querySelector("#cd-lanes .cd-cell.on"); if (c) { c.focus({ preventScroll: true }); }
  }, true);
  /* a zone picked on the map while composing (the page's own selection path calls this) */
  function pickZone(id, name) {
    if (!ctx || !ctx.chords.length) { return false; }
    var z = (ctx.zones() || []).filter(function (x) { return x.id === id; })[0];
    pick({ kind: "zone", id: id, name: (z && z.name) || name || "zone" }); return true;
  }
  window.FsComposeDock = { open: open, close: close, pickZone: pickZone, isOpen: function () { return !!ctx; } };
})();
