/* Composition step 1: the composing dock (docs/superpowers/specs/2026-10-17-composition-design.md, layout C, approved mock
   design/compose/dock-mock.html). The map keeps the top half; the dock below holds the chord strip, the section and zone
   bands, the three voice lanes and voice 1's bass row; the inspector (B) at its right shows every setting of the selection,
   inherited ones dimmed with their source. Edits go through ctx.save (the route's patch): heard at once. Desk only. */
(function () {
  var M = window.FsCompose, ctx = null, comp = null, hist = null, sel = { kind: "route" }, voice = "voice", grown = false, saved = true, nowTimer = null;
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
  /* the arpeggio's patterns: chord degrees per step (0 root, 1 3rd, 2 5th, 3 7th, 4 9th); 8 and 10 are the root and 3rd again
     (the octave is the voice's range and its octaves setting) */
  var PRESETS = [["up", [0, 1, 2, 3]], ["down", [3, 2, 1, 0]], ["up-down", [0, 1, 2, 3, 2, 1]], ["1-5-8-10", [0, 2, 0, 1]], ["Alberti", [0, 2, 1, 2]]]
    .map(function (p) { var g = []; for (var i = 0; i < 16; i++) { g.push(p[1][i % p[1].length]); } return [p[0], g]; });

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) { e.className = cls; } if (text != null) { e.textContent = text; } return e; }
  function svg(vb, inner) { return '<svg viewBox="0 0 ' + vb + '" preserveAspectRatio="none" aria-hidden="true">' + inner + "</svg>"; }
  function noteName(m) { return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1); }
  function fmt(f, v) { return f.fmt === "note" ? noteName(v) : f.fmt === "pct" ? Math.round(v * 100) + " %" : f.fmt === "ms" ? Math.round(v) + " ms" : String(v); }
  function scopeName(s) { return s.kind === "route" ? "the route" : s.kind === "chord" ? "chord " + (s.i + 1) : s.kind === "section" ? "section " + (s.i + 1) : s.name; }
  function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
  function commit() { saved = ctx.save(M.clone(comp)) !== false; hist.push(comp); render(); }
  function resize() { if (window.__fa && __fa.map) { setTimeout(function () { __fa.map.resize(); }, 0); } }

  function open(c) {
    ctx = c; comp = M.clone(ctx.comp()); hist = M.history(100); hist.push(comp); sel = { kind: "route" }; voice = "voice"; saved = true;
    var d = document.getElementById("compose-dock");
    if (!d) { d = el("section", "cd"); d.id = "compose-dock"; d.setAttribute("aria-label", "Compose"); document.body.appendChild(d); }
    document.body.classList.add("composing"); d.hidden = false; resize();
    render();
    if (ctx.show) { setTimeout(function () { if (ctx) { ctx.show(null); } }, 60); }   /* the route in view, in the map's new half */
    clearInterval(nowTimer); nowTimer = setInterval(markNow, 500);
  }
  function close() {
    var d = document.getElementById("compose-dock"); if (d) { d.hidden = true; }
    document.body.classList.remove("composing", "composing-grown"); grown = false; clearInterval(nowTimer);
    if (ctx) { ctx.light(null); } ctx = null; resize();
  }

  function render() {
    var d = document.getElementById("compose-dock"); d.textContent = "";
    var n = ctx.chords.length;
    if (!n) {   /* C-5 empty: a route with no progression */
      var e = el("div", "cd-empty");
      e.appendChild(el("p", "cd-empty-t", "This route has no chords yet."));
      e.appendChild(el("p", "hint", "The voices follow the route’s chords, so they come first. Add a progression in its patch, then compose."));
      var b = el("button", "ghost", "Close"); b.type = "button"; b.addEventListener("click", close); e.appendChild(b); d.appendChild(e); return;
    }
    var main = el("div", "cd-main"), insp = el("aside", "cd-insp"); insp.id = "cd-insp";
    var secs = ctx.sections(), zs = ctx.zones(), bs = M.bands(ctx.coords, secs || [], zs);
    main.appendChild(header()); main.appendChild(strip(n));
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
    t.appendChild(el("i", null, "Compose")); t.appendChild(document.createTextNode(ctx.route.properties.name || "Unnamed route")); h.appendChild(t);
    h.appendChild(el("span", "cd-saved" + (saved ? "" : " warn"), saved ? "saved" : "Not saved on this device"));
    var tl = el("label"); tl.appendChild(el("span", "cd-lbl", "start from"));
    var s = el("select"); s.id = "cd-template"; s.setAttribute("aria-label", "Start from a template");
    var cur = M.TEMPLATES.filter(function (x) { var y = x.comp(); return M.VOICES.concat(["bass"]).every(function (v) { return same(y[v], comp[v]); }); })[0];
    s.appendChild(new Option(cur ? "—" : "your own", ""));
    M.TEMPLATES.forEach(function (x) { s.appendChild(new Option(x.label, x.id)); });
    s.value = cur ? cur.id : "";
    s.addEventListener("change", function () { var x = M.TEMPLATES.filter(function (y) { return y.id === s.value; })[0];
      if (x) { var nc = x.comp(); nc.over = comp.over; nc.seed = comp.seed; comp = nc; commit(); } });
    tl.appendChild(s); h.appendChild(tl);
    var sd = el("label"); sd.appendChild(el("span", "cd-lbl", "seed"));
    var si = el("input"); si.type = "number"; si.min = 0; si.max = 99999; si.value = comp.seed; si.setAttribute("aria-label", "Seed");
    si.addEventListener("change", function () { comp.seed = Math.max(0, Math.min(99999, +si.value | 0)); commit(); });
    sd.appendChild(si); h.appendChild(sd);
    [[ICON.undo, "Undo (Ctrl+Z)", undo, hist.canUndo()], [ICON.redo, "Redo (Ctrl+Shift+Z)", redo, hist.canRedo()],
     [grown ? ICON.shrink : ICON.grow, grown ? "Show the map" : "Grow the dock", function () { grown = !grown; document.body.classList.toggle("composing-grown", grown); resize(); render(); }, true],
     [ICON.close, "Close", close, true]].forEach(function (x) {
      var b = el("button", "cd-icon"); b.type = "button"; b.innerHTML = '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">' + x[0] + "</svg>";
      b.title = x[1]; b.setAttribute("aria-label", x[1]); b.disabled = !x[3]; b.addEventListener("click", x[2]); h.appendChild(b); });
    return h;
  }
  function strip(n) {
    var r = el("div", "cd-row"); r.appendChild(el("span", "cd-name", "chords"));
    var s = el("div", "cd-strip"); s.id = "cd-strip"; s.style.gridTemplateColumns = "repeat(" + n + ", minmax(0, 1fr))";
    ctx.chords.forEach(function (c, i) {
      var b = el("button", "cd-chord"); b.type = "button"; b.style.background = c.colour; b.title = (i + 1) + " · " + c.label;
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
        var x = el("button", "cd-band " + k[0], name); x.type = "button"; x.style.left = (b.t0 * 100) + "%"; x.style.width = Math.max(1.5, (b.t1 - b.t0) * 100) + "%";
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
        if (prev && prev.style === c.style && prev.source === c.source && c.source !== "chord") { b.classList.add("cont"); }
        if (sel.kind === "chord" && sel.i === i && voice === v) { b.classList.add("on"); }
        if ((sel.kind === "zone" || sel.kind === "section") && c.source === sel.kind && cells[i].at.some(function (x) { return x.kind === sel.kind && isSel(x.kind, x.id); })) { b.classList.add("lit"); }
        b.innerHTML = "<span>" + (WORD[st] || st) + "</span>" + svg(v === "bass" ? "100 10" : "100 22", GLYPH[st] || GLYPH.free);
        b.type = "button"; b.dataset.i = i;
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
  function foot() {
    var f = el("div", "cd-foot"), lg = el("div", "cd-legend");
    [["route", ""], ["a section changed it", " by-section"], ["a zone changed it", " by-zone"], ["a chord changed it", " by-chord"]].forEach(function (x) {
      var s = el("span"); s.appendChild(el("i", x[1].trim() || null)); s.appendChild(document.createTextNode(x[0])); lg.appendChild(s); });
    f.appendChild(lg);
    var k = el("span", "cd-keys");
    k.innerHTML = "<kbd>←</kbd><kbd>→</kbd> cells · <kbd>Enter</kbd> open · <kbd>Del</kbd> clear · <kbd>Space</kbd> sound";
    f.appendChild(k); return f;
  }
  function pick(s) { sel = s; ctx.light(s.kind === "chord" ? { chord: s.i } : s.kind === "zone" ? { zone: s.id } : null); render(); }
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
    var ih = el("div", "cd-ih"), title;
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
    var tabs = el("div", "cd-seg cd-vtabs"); tabs.style.setProperty("--n", 4); tabs.setAttribute("role", "group"); tabs.setAttribute("aria-label", "Voice");
    var ch = changes(sel);
    VS.forEach(function (v) { var b = el("button", ch.indexOf(v) >= 0 ? "has" : null, TAB[v]); b.type = "button"; b.setAttribute("aria-pressed", String(v === voice));
      b.addEventListener("click", function () { voice = v; render(); }); tabs.appendChild(b); });
    w.appendChild(tabs);
    var e = M.effective(comp, sel, voice);
    var style = voice === "bass" ? null : e.style;
    var fields = voice === "bass" ? M.BASS : M.FIELDS.common.concat(M.FIELDS[style] || []);
    fields.filter(function (f) { return f.kind !== "grid" && f.kind !== "accents"; }).forEach(function (f) { w.appendChild(row(f, e.values[f.k], e.source[f.k])); });
    var grid = fields.filter(function (f) { return f.kind === "grid"; })[0];
    if (grid) { patternBlock(w, grid, fields.filter(function (f) { return f.kind === "accents"; })[0], e); }
    var tools = el("div", "cd-itools");
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
  function seg(opts, value, onPick, label) {
    var s = el("span", "cd-seg"); s.style.setProperty("--n", opts.length); s.setAttribute("role", "group"); if (label) { s.setAttribute("aria-label", label); }
    opts.forEach(function (o) { var b = el("button", null, o[1]); b.type = "button"; b.setAttribute("aria-pressed", String(String(o[0]) === String(value)));
      b.addEventListener("click", function () { onPick(o[0]); }); s.appendChild(b); });
    return s;
  }
  function zoneLines(w) {
    var o = M.scopeObj(comp, sel, false) || { when: "inside", blend: 1 };
    [["when", [["inside", "while inside"], ["enter", "on entering"]]], ["blend", [[0, "at once"], [1, "next bar"], [2, "2 bars"], [4, "4 bars"]]]].forEach(function (k) {
      var r = el("div", "cd-row2 wide" + (M.scopeObj(comp, sel, false) ? " mine" : " inh")); r.dataset.k = k[0];
      r.appendChild(el("span", "cd-k", k[0]));
      var c = el("span", "cd-ctl"); c.appendChild(seg(k[1], o[k[0]], function (v) { var z = M.scopeObj(comp, sel, true); z[k[0]] = v; commit(); }, k[0])); r.appendChild(c);
      w.appendChild(r); });
  }
  function srcCell(src, f, keys) {
    var s = el("span", "cd-src");
    if (sel.kind === "route") { s.textContent = "route"; return s; }
    if (src !== "here") { s.textContent = "route"; return s; }
    s.appendChild(document.createTextNode("here"));
    var x = el("button", "cd-reset", "×"); x.type = "button"; x.title = "Back to the route’s"; x.setAttribute("aria-label", "Reset " + f.label + " to the route’s");
    x.addEventListener("click", function () { keys.forEach(function (k) { M.clearValue(comp, sel, voice, k); }); commit(); }); s.appendChild(x);
    return s;
  }
  function row(f, v, src) {
    var mine = sel.kind === "route" || src === "here";
    var r = el("div", "cd-row2 " + (mine ? "mine" : "inh")); r.dataset.k = f.k;
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
    var src = e.source[grid.k] === "here" || (acc && e.source[acc.k] === "here") ? "here" : "route";
    var eb = el("p", "cd-ieb" + (src === "here" || sel.kind === "route" ? " mine" : ""));
    eb.appendChild(el("span", null, grid.label + " · ")); var sc = srcCell(src, grid, acc ? [grid.k, acc.k] : [grid.k]); eb.appendChild(sc);
    w.appendChild(eb);
    var cur = (e.values[grid.k] || []).slice(); while (cur.length < 16) { cur.push(-1); }
    var set = function (val) { M.setValue(comp, sel, voice, grid.k, val); commit(); };
    if (acc) {
      var p = el("div", "cd-presets"); p.setAttribute("role", "group"); p.setAttribute("aria-label", "Patterns");
      PRESETS.forEach(function (x) { var b = el("button", null, x[0]); b.type = "button"; b.setAttribute("aria-pressed", String(same(x[1], cur))); b.addEventListener("click", function () { set(x[1].slice()); }); p.appendChild(b); });
      w.appendChild(p);
    }
    var accv = acc ? (e.values[acc.k] || []).slice() : null; if (accv) { while (accv.length < 16) { accv.push(70); } }
    var steps = e.style === "euclid" ? e.values.steps : 16;
    var g = el("div", "cd-pgrid" + (src === "here" || sel.kind === "route" ? "" : " inh"));
    ["9", "7", "5", "3", "root"].forEach(function (lab, ri) { var d = 4 - ri; g.appendChild(el("span", "cd-gk", lab));
      for (var c = 0; c < 16; c++) { (function (c) {
        var b = el("button", "cd-gc" + (c % 4 === 0 ? " beat" : "") + (cur[c] === d ? " on" + (accv && accv[c] >= 85 ? " acc" : "") : "") + (c >= steps ? " past" : ""));
        b.type = "button"; b.setAttribute("aria-label", "step " + (c + 1) + ", " + lab); b.setAttribute("aria-pressed", String(cur[c] === d));
        b.addEventListener("click", function () { var a = cur.slice(); a[c] = a[c] === d ? -1 : d; set(a); }); g.appendChild(b); })(c); } });
    w.appendChild(g);
    if (acc) {
      var a = el("div", "cd-accs" + (src === "here" || sel.kind === "route" ? "" : " inh")); a.appendChild(el("span", "cd-gk", "acc"));
      accv.forEach(function (lv, c) { var b = el("button", "cd-ab"); b.type = "button"; b.style.setProperty("--h", lv + "%"); b.setAttribute("aria-label", "accent, step " + (c + 1) + ", " + lv);
        b.addEventListener("click", function (ev) { var r = b.getBoundingClientRect(), x = accv.slice();
          x[c] = Math.round(Math.max(0, Math.min(1, 1 - (ev.clientY - r.top) / r.height)) * 100); M.setValue(comp, sel, voice, acc.k, x); commit(); });
        a.appendChild(b); });
      w.appendChild(a);
    }
  }
  function undo() { var c = hist.undo(); if (c) { comp = c; saved = ctx.save(M.clone(comp)) !== false; render(); } }
  function redo() { var c = hist.redo(); if (c) { comp = c; saved = ctx.save(M.clone(comp)) !== false; render(); } }
  /* the keyboard: arrows move between cells, Enter opens, Delete clears, Ctrl+Z / Ctrl+Shift+Z, Space the sound, Esc closes */
  document.addEventListener("keydown", function (e) {
    if (!ctx || !ctx.chords.length || /INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) { return; }
    var n = ctx.chords.length;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") { e.preventDefault(); if (e.shiftKey) { redo(); } else { undo(); } return; }
    if (e.ctrlKey || e.metaKey || e.altKey) { return; }
    if (e.key === " ") { var pb = document.getElementById("patch-play"); if (pb) { pb.click(); } e.preventDefault(); return; }
    if (e.key === "Escape") { close(); return; }
    if (sel.kind !== "chord" && /^Arrow/.test(e.key)) { pick({ kind: "chord", i: 0 }); e.preventDefault(); return; }
    if (sel.kind === "chord") {
      var vi = VS.indexOf(voice);
      if (e.key === "ArrowRight") { pick({ kind: "chord", i: Math.min(n - 1, sel.i + 1) }); }
      else if (e.key === "ArrowLeft") { pick({ kind: "chord", i: Math.max(0, sel.i - 1) }); }
      else if (e.key === "ArrowDown") { voice = VS[Math.min(VS.length - 1, vi + 1)]; render(); }
      else if (e.key === "ArrowUp") { voice = VS[Math.max(0, vi - 1)]; render(); }
      else if (e.key === "Delete" || e.key === "Backspace") { M.clearScope(comp, sel); commit(); }
      else if (e.key === "Enter") { var f = document.querySelector("#cd-insp select, #cd-insp input, #cd-insp .cd-row2 button"); if (f) { f.focus(); } }
      else { return; }
      e.preventDefault();
      var on = document.querySelector("#cd-lanes .cd-cell.on"); if (on && e.key !== "Enter") { on.focus(); }
    }
  }, true);
  window.FsComposeDock = { open: open, close: close, isOpen: function () { return !!ctx; } };
})();
