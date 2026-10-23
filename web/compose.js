/* Composition step 1: the composing dock (docs/superpowers/specs/2026-10-17-composition-design.md, layout C, approved mock
   design/compose/dock-mock.html). The map keeps the top half; the dock below holds the chord strip, the section and zone
   bands, the three voice lanes and voice 1's bass row; the inspector (B) at its right shows every setting of the selection,
   inherited ones dimmed with their source. Edits go through ctx.save (the route's patch): heard at once. Desk only. */
(function () {
  var M = window.FsCompose, ctx = null, comp = null, hist = null, sel = { kind: "route" }, voice = "voice", grown = false, saveErr = null, nowTimer = null, statsTimer = null, audio = [], polls = 0;
  var gen = false, genForm = "AABA", genContrast = 0.6, genRel = "some";   /* the Generate form panel: open, its form and contrast */
  /* composition 2: the selected chain step and motif note (sel.kind "sent" puts them in the inspector), the roll's cursor, the
     lane whose layers are shown, the motif generator's seed */
  var si = -1, ni = -1, cur = { row: 0, col: 0 }, lay = null, mseed = 0;
  /* want: where focus goes after the next render when its control is gone (selectors, the first found); capped: the 8-note cap was hit */
  var ornOpen = null, want = null, capped = false, CURSOR = "#cd-motif .cd-mcell.cur", relMenu = false, ri = {}, rzT = 0;
  var VS = ["voice", "sect", "v3", "bass"];
  var NAME = { voice: "voice 1", sect: "voice 2", v3: "voice 3", bass: "voice 1 bass" };
  var TAB = { voice: "voice 1", sect: "voice 2", v3: "voice 3", bass: "bass" };
  /* the cell's word (short), the tooltip's (long) */
  var WORD = { free: "free", arpeggio: "arp", strum: "strum", pedal: "pedal", euclid: "euclid", swells: "swell", off: "off", hold: "root", bpedal: "pedal",
    ostinato: "ostin.", chorale: "chor.", tintinnabuli: "tinti.", pointillist: "point.", bwalk: "walk" };
  var LONG = { free: "free", arpeggio: "arpeggio", strum: "strum", pedal: "pedal", euclid: "euclidean", swells: "swells", off: "off", hold: "holds the root", bpedal: "pedal",
    ostinato: "ostinato", chorale: "chorale", tintinnabuli: "tintinnabuli", pointillist: "pointillist", bwalk: "walking bass" };
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
    boff: '<line x1="40" y1="5" x2="60" y2="5" stroke="currentColor" stroke-width="1.5" opacity=".6"/>',
    /* composition 4 (design/compose/styles-mock.html): a looping cell, stacked bars, a line and its answer, scattered dots, the walking bass's line */
    ostinato: '<path d="M6,16 C6,4 30,4 30,12 S54,20 54,10" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M58,16 C58,4 82,4 82,12" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M86,6 l8,4 -8,4 z" fill="currentColor"/>',
    chorale: [10, 40, 70].map(function (x) { return [4, 9, 14, 19].map(function (y) { return '<rect x="' + x + '" y="' + (y - 1.5) + '" width="20" height="3" rx="1" fill="currentColor"/>'; }).join(""); }).join(""),
    tintinnabuli: '<polyline points="2,14 26,10 50,14 74,8 98,12" fill="none" stroke="currentColor" stroke-width="1.5"/><polyline points="2,20 26,17 50,20 74,15 98,18" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2 2"/>',
    pointillist: [[8, 6], [22, 16], [41, 9], [63, 18], [77, 5], [93, 13]].map(function (p) { return '<circle cx="' + p[0] + '" cy="' + p[1] + '" r="2.2" fill="currentColor"/>'; }).join(""),
    bwalk: '<polyline points="2,8 26,6 50,4 74,6 98,3" fill="none" stroke="currentColor" stroke-width="1.6"/>',
    /* composition 2 (design/compose/sentence-mock.html): the motif's line, and a cadence settling on its long last note */
    mel: '<polyline points="2,18 22,8 46,13 70,4 98,10" fill="none" stroke="currentColor" stroke-width="1.6"/>' +
      [[2, 18], [22, 8], [46, 13], [70, 4]].map(function (p) { return '<rect x="' + p[0] + '" y="' + (p[1] - 2) + '" width="16" height="4" rx="1" fill="currentColor"/>'; }).join(""),
    cad: '<polyline points="2,6 30,10 60,14 98,18" fill="none" stroke="currentColor" stroke-width="1.6"/><rect x="60" y="16" width="38" height="4" rx="1" fill="currentColor"/>'
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
  /* sixteenths as a note value, and a start as beat.sixteenth (bar 2's beats go on: 5.1 ...) */
  var DUR = { 1: "1/16", 2: "1/8", 3: "1/8 .", 4: "1/4", 6: "1/4 .", 8: "1/2", 12: "1/2 .", 16: "1 bar", 32: "2 bars" };
  function sec(v) { return String(Math.round(v * 100) / 100); }
  function fmt(f, v) { if (f.fmt === "rbar") { return barsWord(v); } if (f.fmt === "perbar") { return v + " a bar"; }
    return f.fmt === "note" ? noteName(v) : f.fmt === "pct" ? Math.round(v * 100) + " %" : f.fmt === "ms" ? Math.round(v) + " ms" : f.fmt === "s" ? sec(v) + " s" : f.fmt === "x" ? sec(v) + "×" : f.fmt === "vn" ? "v" + (v + 1) :
    f.fmt === "sgn" ? (v > 0 ? "+" + v : v < 0 ? "−" + -v : "0") : f.fmt === "dur" ? DUR[v] || v + "/16" : f.fmt === "beat" ? (Math.floor(v / 4) + 1) + "." + (v % 4 + 1) : String(v); }
  function scopeName(s) { return s.kind === "route" ? "the route" : s.kind === "chord" ? "chord " + (s.i + 1) : s.kind === "section" ? "section " + (s.i + 1) : s.name; }
  function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
  /* ctx.save says true, or why it could not store (shown with Retry until a save goes through) */
  function store() { var r = ctx.save(M.clone(comp)); saveErr = r === true || r === undefined ? null : String(r || "not stored"); }
  /* a hand edit to the route or a chord after Generate: the form row says "edited" (a section's or zone's edit is not the form's) */
  function commit(fresh) {
    if (comp.form && !fresh && sel.kind !== "section" && sel.kind !== "zone") { comp.form.edited = true; }
    capped = false; relMenu = false; store(); hist.push(comp); render();
  }
  /* composition 2: the motif (none yet: an empty one, written by the first edit) and the chain, each edit one undo step */
  function motif() { return comp.motif || { basis: "chord", bars: 1, notes: [] }; }
  function editMotif(fn) { var m = M.clone(motif()); fn(m); M.setMotif(comp, m); capped = false; commit(); }
  function editChain(fn) { var c = M.clone(comp.chain || []); fn(c); M.setChain(comp, c); commit(); }
  function rowsOf(m) { return m.basis === "scale" ? ["1", "2", "3", "4", "5", "6", "7"] : ["root", "3", "5", "7", "9"]; }
  function mod(a, n) { return ((a % n) + n) % n; }
  var DEG = ["root", "3rd", "5th", "7th", "9th"];
  /* a motif note's name over chord i: the chord's tone (or the key's scale degree) as the core takes it, ′ an octave up */
  function pitchName(i, x, basis) {
    var t = basis === "scale" ? ctx.scale || [] : (ctx.chords[i] || {}).tones || [];
    if (!t.length) { return "?"; }
    var o = Math.floor(x.deg / t.length), up = o + (x.oct || 0);
    return noteName(t[x.deg - o * t.length]).replace(/-?\d+$/, "") + (up > 0 ? "′" : up < 0 ? "‚" : "");
  }
  function stepWord(s) {
    if (!s) { return "repeat"; }
    var a = Math.round(s.arg || 0), o = M.OPS.filter(function (x) { return x.id === s.op; })[0];
    return s.op === "cadence" ? "cadence · " + (s.cad || "full") : s.op === "seq" ? "sequence " + fmt({ fmt: "sgn" }, a) : s.op === "fragment" ? "fragment " + a : o ? o.label + (s.op === "augment" && motif().bars === 2 ? " · bar 1" : "") : s.op;
  }
  function shortWord(s) {
    var a = Math.round(s.arg || 0);
    return { repeat: "rep", seq: "seq " + fmt({ fmt: "sgn" }, a), invert: "inv", retro: "retro", augment: "aug", diminish: "dim", fragment: "frag " + a, cadence: "cad. " + (s.cad || "full") }[s.op] || s.op;
  }
  /* the letter of the form over a chord (a generated piece's), its span's index */
  function letterAt(i, n) {
    if (!comp.form) { return null; }
    var sp = M.formSpans(comp.form.type, Math.min(n, M.NCHORD));
    for (var k = 0; k < sp.length; k++) { if (i >= sp[k].a && i < sp[k].b) { return { letter: sp[k].letter, k: k }; } }
    return null;
  }
  /* the motif as it plays over chord i; a cadence's last chord ends on the key's tonic, 5th or 6th (the core's play_melody) */
  function plays(i) { var st = M.motifStep(comp, i), b = motif().basis;
    var out = st.notes.slice().sort(function (a, c) { return a.start - c.start; }).map(function (x) { return pitchName(i, x, b); });
    if (st.op === "cadence" && st.last && out.length && (ctx.scale || []).length) { out[out.length - 1] = pitchName(i, { deg: { full: 0, half: 4, deceptive: 5 }[st.cad] || 0 }, "scale"); }
    return out; }
  function hasMelody() { return /"role":"melody"/.test(JSON.stringify(comp)); }
  function resize() { if (window.__fa && __fa.map) { setTimeout(function () { __fa.map.resize(); }, 0); } }

  function open(c) {
    ctx = c; comp = M.clone(ctx.comp()); hist = M.history(100); hist.push(comp); sel = { kind: "route" }; voice = "voice"; ornOpen = null; saveErr = null; gen = false; si = ni = -1; lay = null; capped = false; relMenu = false;
    var d = document.getElementById("compose-dock");
    if (!d) { d = el("section", "cd"); d.id = "compose-dock"; d.setAttribute("aria-label", "Compose"); document.body.appendChild(d); }
    document.body.classList.add("composing"); d.hidden = false; resize();
    render();
    if (ctx.show) { setTimeout(function () { if (ctx) { ctx.show(null); } }, 60); }   /* the route in view, in the map's new half */
    clearInterval(nowTimer); nowTimer = setInterval(markNow, 500);
    audio = []; gapLog = []; clearInterval(statsTimer); statsTimer = setInterval(pollAudio, 1000); pollAudio();
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
    if (si >= (comp.chain || []).length) { si = -1; }            /* an undo can take the selected step or note away */
    if (ni >= motif().notes.length) { ni = -1; }
    if (sel.kind === "sent" && si < 0 && ni < 0) { sel = { kind: "route" }; }
    if (sel.kind === "rel" && !(comp.relations || [])[sel.i]) { sel = { kind: "route" }; }
    draw(d); refocus(d, was);
    if (want) { var q = want.map(function (x) { return d.querySelector(x); }).filter(Boolean)[0]; want = null; if (q) { q.focus({ preventScroll: true }); } }
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
    /* composition 2: the form · chain row under the chords (step 1's form row folded into it); grown, the sentence's editors
       take the bands' place (the mock's) */
    main.appendChild(header()); main.appendChild(strip(n));
    if (comp.form || (comp.chain || []).length || hasMelody()) { main.appendChild(chainRow(n)); }
    ri = relInfo(bs, n);
    if (!grown) { bandRows(main, bs, secs === null, zs); }
    main.appendChild(relRow(n, zs));   /* grown too: the sentence's editors sit under the lanes, the relations stay over them */
    main.appendChild(el("div", "cd-sep"));
    main.appendChild(lanes(n, bs)); if (grown) { main.appendChild(editor(n)); } main.appendChild(foot());
    d.appendChild(main); inspector(insp, bs, n); d.appendChild(insp);
    /* C-8, one screen: a style with many settings, in a zone, is taller than the dock - the rows close up, in two steps, rather
       than anything scrolling or being cut off */
    ["tight", "tighter", "tightest", "snug", "tiny"].forEach(function (k) { if (insp.scrollHeight > insp.clientHeight + 1) { insp.classList.add(k); } });   /* tightest: the role's helper line goes; snug and tiny (composition 4): rows and dials smaller, then the sub-line goes */
    /* the lanes too: the form · chain row (and a lane's layers) cost height - the cells close up, in two steps */
    ["tight", "tighter", "tightest"].forEach(function (k) { if (main.scrollHeight > main.clientHeight + 1) { main.classList.add(k); } });
    /* with ornaments & glides open the voice's own rows give way last: they fold into one line (their heading opens them again) */
    if (insp.scrollHeight > insp.clientHeight + 1 && insp.querySelector('.cd-orn[aria-expanded="true"]')) { insp.classList.add("fold"); }
    var run = main.querySelector(".cd-chiprun");   /* the chips give way in steps rather than being cut off */
    ["short", "shorter", "squeeze"].forEach(function (k) { if (run && run.scrollWidth > run.clientWidth + 1) { run.classList.add(k); } });
    var rr = main.querySelector(".cd-relrun");   /* the relation chips the same way */
    ["short", "shorter", "tiny", "squeeze"].forEach(function (k) { if (rr && rr.scrollWidth > rr.clientWidth + 1) { rr.classList.add(k); } });
    drawLinks(main, n, zs); markNow();
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
      if (x) { var nc = x.comp(); nc.seed = comp.seed; nc.bars = comp.bars || "develop";
        if (comp.relations) { nc.relations = comp.relations; }   /* the relations are not the template's */
        nc.over = comp.form ? { chord: {}, section: comp.over.section, zone: comp.over.zone } : comp.over;   /* a generated piece's chords go with its form */
        comp = nc; commit(); } });
    tl.appendChild(s); h.appendChild(tl);
    var sd = el("label"); sd.appendChild(el("span", "cd-lbl", "seed"));
    var si = el("input"); si.type = "number"; si.min = 0; si.max = 99999; si.value = comp.seed; si.setAttribute("aria-label", "Seed");
    si.addEventListener("change", function () { var v = Math.max(0, Math.min(99999, +si.value | 0));
      if (gen) { generate(v); } else { comp.seed = v; commit(); } });   /* the panel open: that seed's piece */
    sd.appendChild(si); h.appendChild(sd);
    /* composition 2: a chord of several bars develops its bar (variants) or repeats it as step 1 did */
    var bb = seg([["develop", "develop"], ["repeat", "repeat"]], comp.bars === "repeat" ? "repeat" : "develop", function (v) { comp.bars = v; commit(true); }, "Bars within a chord");
    [].forEach.call(bb.children, function (b, i) { b.textContent = ""; b.appendChild(el("span", "cd-w", i ? "repeat" : "develop")); b.appendChild(el("span", "cd-n", i ? "rep" : "dev")); });   /* the narrow desk reads dev | rep */
    bb.classList.add("cd-bars"); bb.title = "A chord that lasts several bars: develop varies each bar (answer, sequence, held ending, inversion; the arpeggio reverses, the euclid turns), repeat plays the first bar again";
    h.appendChild(bb);
    var gb = el("button", "cd-gen", "Generate"); gb.appendChild(el("span", "cd-lbl", " form")); gb.type = "button"; gb.setAttribute("aria-pressed", String(gen));
    gb.title = "Write a whole piece in a form: its sections' voices, styles and settings (one Ctrl+Z takes it back)";
    gb.addEventListener("click", function () { gen = !gen;
      if (gen && comp.form) { genForm = comp.form.type; genContrast = comp.form.contrast; genRel = comp.form.relations || "some"; } render(); });
    h.appendChild(gb);
    [[ICON.undo, "Undo (Ctrl+Z)", undo, hist.canUndo()], [ICON.redo, "Redo (Ctrl+Shift+Z)", redo, hist.canRedo()],
     [grown ? ICON.shrink : ICON.grow, grown ? "Show the map" : "Grow the dock", growDock, true],
     [ICON.close, "Close", close, true]].forEach(function (x) {
      var b = el("button", "cd-icon"); b.type = "button"; b.innerHTML = '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">' + x[0] + "</svg>";
      b.title = x[1]; b.setAttribute("aria-label", x[1]); b.disabled = !x[3]; b.addEventListener("click", x[2]); h.appendChild(b); });
    return h;
  }
  /* the form · chain row: the chain's steps over their chords, each with its form letter (a generated piece's; dimmed once
     edited by hand); a form with no chain (step 1's pieces) its letters, a repeat dashed; a melody with no chain says so */
  function chainRow(n) {
    var f = comp.form, ed = !!(f && f.edited), ch = comp.chain || [], r = el("div", "cd-row cd-formrow" + (ed ? " edited" : ""));
    r.appendChild(el("span", "cd-name", f ? (ed ? "form · edited" : "form · chain") : "chain"));
    var t = el("div", "cd-track cd-form"), seen = {}; t.id = "cd-chain";
    if (f) { t.title = (ed ? "generated · edited: " : "generated: ") + M.FORMS.filter(function (x) { return x.id === f.type; })[0].label + " · seed " + f.seed +
      " · contrast " + Math.round(f.contrast * 100) + " %" + (n > M.NCHORD ? " · over chords 1–" + M.NCHORD + " of " + n : ""); r.title = t.title; }
    var place = function (b, a, z) { b.style.left = (a / n * 100) + "%"; b.style.width = "calc(" + ((z - a) / n * 100) + "% - 3px)"; };   /* the strip's gap */
    if (!ch.length && f) {
      M.formSpans(f.type, Math.min(n, M.NCHORD)).forEach(function (x) {
        var b = el("span", "cd-band form" + (seen[x.letter] ? " rep" : ""), x.letter); seen[x.letter] = true; place(b, x.a, x.b); t.appendChild(b); });
    } else if (!ch.length) { t.appendChild(el("span", "cd-hint", "the motif repeats on every chord · grow the dock to develop it")); }
    ch.forEach(function (s, k) {
      var L = letterAt(s.from, n), b = el("button", "cd-step" + (s.op === "cadence" ? " cad" : "") + (sel.kind === "sent" && si === k ? " on" : ""));
      b.type = "button"; b.dataset.i = k; b.dataset.fk = "step:" + k;
      if (L) { b.dataset.letter = L.letter; b.dataset.span = L.k;
        if (s.op !== "cadence") { b.appendChild(el("em", null, L.letter)); b.appendChild(document.createTextNode(" · ")); } }   /* a cadence's chord is narrow: "cad." */
      b.appendChild(document.createTextNode(s.op === "cadence" ? "cad." : stepWord(s)));
      b.title = "step " + (k + 1) + " · " + stepWord(s) + " · chords " + (s.from + 1) + (s.to > s.from ? "–" + (s.to + 1) : "");
      b.setAttribute("aria-label", "chain step " + (k + 1) + ", " + stepWord(s) + ", chord " + (s.from + 1) + (s.to > s.from ? " to " + (s.to + 1) : ""));
      place(b, s.from, s.to + 1); b.addEventListener("click", function () { pickStep(k); }); t.appendChild(b); });
    r.appendChild(t); return r;
  }
  function pickStep(k) { si = k; sel = { kind: "sent" }; gen = false; var s = (comp.chain || [])[k]; ctx.light(s ? { chord: s.from } : null); render(); }
  function pickNote(i) { ni = i; sel = { kind: "sent" }; gen = false; render(); }
  /* the bands over each chord (as the lanes take them): a section's keys are written on its chords, so the form plays over it */
  function generate(seed) {
    var n = ctx.chords.length, bs = M.bands(ctx.coords, ctx.sections() || [], ctx.zones()), at = [];
    for (var i = 0; i < n; i++) { at.push(bs.filter(function (b) { return b.t0 < (i + 1) / n && b.t1 > i / n; })); }
    comp = M.generate(comp, { form: genForm, contrast: genContrast, relations: genRel, seed: seed, nchords: n, at: at,
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
        if (isSel(k[0], b.id) || (k[0] === "zone" && sel.kind === "rel" && ((comp.relations || [])[sel.i] || {}).zone === b.id)) { x.classList.add("on"); }
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
    return { style: mode === "hold" ? "hold" : mode === "pedal" ? "bpedal" : mode === "walk" ? "bwalk" : "boff", source: src };
  }
  function laneSub(v) {
    if (v === "bass") { var o = M.BASS[0].options.filter(function (x) { return x.id === (comp.bass.mode || "hold"); })[0]; return o ? o.label : ""; }
    var c = comp[v]; if (c.role === "off") { return "off"; }
    if (c.role === "melody") { return "Melody · motif"; }
    return M.label(c.style) + (c.style === "pedal" && c.pedal && c.pedal !== "root" ? " · " + (c.pedal === "fifth" ? "5th" : "fixed") : "");
  }
  /* where the core ignores them (Piece::ornament, note): strum, swells and chorale play no ornament or glide, tintinnabuli's answers no micro either */
  var CHORDY = ["strum", "swells", "chorale"];
  function noOrn(style) { return CHORDY.indexOf(style) >= 0 || style === "tintinnabuli"; }
  function ornSet(o, style) { return !!o && (!noOrn(style) && ((!!o.orn && o.orn !== "none") || o.glide > 0) || style !== "tintinnabuli" && (o.micro || []).some(Boolean)); }
  /* a voice's ornament settings over chord i, layer on layer as the cell takes its style: route < section < chord < zone */
  function ornAt(v, i, at, style) {
    var o = Object.assign({}, comp[v]), take = function (x) { if (x && x[v]) { M.ORNAMENTS.forEach(function (f) { if (x[v][f.k] !== undefined) { o[f.k] = x[v][f.k]; } }); } };
    at.filter(function (b) { return b.kind === "section"; }).forEach(function (b) { take(comp.over.section[String(b.id)]); });
    take(comp.over.chord[String(i)]);
    at.filter(function (b) { return b.kind === "zone"; }).forEach(function (b) { take(comp.over.zone[b.id]); });
    return ornSet(o, style);
  }
  function lanes(n, bs) {
    var w = el("div", "cd-lanes"); w.id = "cd-lanes";
    var cells = [];
    for (var i = 0; i < n; i++) { (function (i) { var at = bs.filter(function (b) { return b.t0 < (i + 1) / n && b.t1 > i / n; });
      var c = M.cell(comp, i, at, n); c.push(bassAt(i, at)); c.at = at; cells.push(c); })(i); }
    var empty = !motif().notes.length;
    if (lay && !grown) { w.classList.add("layered"); }
    VS.forEach(function (v, vi) {
      var r = el("div", "cd-row cd-lane" + (v === "bass" ? " bass" : "")); r.dataset.voice = v;
      var nm = el("span", "cd-name"), nl = el("span", "cd-nl", NAME[v]), sub = laneSub(v); nm.appendChild(nl);
      nm.appendChild(el("small", sub === "Melody · motif" ? "mel" : null, sub)); r.appendChild(nm);
      if (v !== "bass" && !grown) {   /* Show layers: what each layer says for this voice, chord by chord (on the name's line: the lane keeps its height) */
        var lb = el("button", "cd-layers", "layers"); lb.type = "button"; lb.dataset.fk = "layers:" + v; lb.setAttribute("aria-pressed", String(lay === v));
        lb.title = "Show what the zone, chord, section and route each say for " + NAME[v] + ", the one that plays lit";
        lb.addEventListener("click", function () { lay = lay === v ? null : v; render(); }); nl.appendChild(lb);
      }
      var g = el("div", "cd-cells"); g.style.gridTemplateColumns = "repeat(" + n + ", minmax(0, 1fr))";
      for (var i = 0; i < n; i++) {
        var c = cells[i][vi], prev = i ? cells[i - 1][vi] : null, mel = c.style === "melody";
        var st = mel ? (c.label === "cad." ? "cad" : "mel") : c.style === "off" && v === "bass" ? "boff" : c.style, word = mel ? c.label : WORD[st] || st;
        var long = mel ? "melody, " + stepWord(M.chainAt(comp, i)) + (empty ? " (no motif yet: plays as Free)" : "") : LONG[st] || st;
        var chained = mel && M.chainAt(comp, i) && M.chainAt(comp, i).op !== "repeat";
        var b = el("button", "cd-cell src-" + c.source + (c.style === "off" || st === "boff" ? " off" : "") + (mel ? " mel" : "") + (chained ? " src-chain" : ""));
        var on = sel.kind === "chord" && sel.i === i && voice === v;
        if (on) { b.classList.add("on"); }
        else if (prev && prev.style === c.style && prev.label === c.label && prev.source === c.source && c.source !== "chord") { b.classList.add("cont"); }   /* the selected cell keeps its word */
        if ((sel.kind === "zone" || sel.kind === "section") && c.source === sel.kind && cells[i].at.some(function (x) { return x.kind === sel.kind && isSel(x.kind, x.id); })) { b.classList.add("lit"); }
        var dr = c.rel && v !== "bass" ? drivenBy(i, vi, cells[i].at) : -1;   /* composition 3: a relation drives this voice here */
        if (dr >= 0 && st !== "off" && ((ri[dr] || {}).free || []).indexOf(i) < 0) { b.classList.add("relx"); b.classList.remove("cont"); b.style.setProperty("--rc", relCol(dr)); word = c.rel; long += " · " + c.rel;
          if (sel.kind === "rel" && sel.i === dr) { b.classList.add("relsel"); } }
        var orn = v !== "bass" && st !== "off" && ornAt(v, i, cells[i].at, st);   /* composition 4: an ornament, a glide or a micro-tuning here: a small ~ */
        if (orn) { b.classList.add("orn"); long += " · ornaments"; }
        b.innerHTML = "<span></span>" + svg(v === "bass" ? "100 10" : "100 22", GLYPH[st] || GLYPH.free); b.firstChild.textContent = word;
        b.type = "button"; b.dataset.i = i; b.dataset.fk = "cell:" + v + ":" + i;
        b.title = (i + 1) + " · " + ctx.chords[i].label + " · " + long + " (" + c.source + ")";
        b.setAttribute("aria-label", NAME[v] + ", chord " + (i + 1) + ": " + long + (c.source !== "route" ? ", set by the " + c.source : ""));
        (function (i, v) { b.addEventListener("click", function () { voice = v; pick({ kind: "chord", i: i }); });
          b.addEventListener("dblclick", function () { listen((i + 0.5) / n); }); })(i, v);
        g.appendChild(b);
      }
      if (lay === v && !grown) { r.classList.add("layers"); var wr = el("div", "cd-lanewrap"); wr.appendChild(g); layerRows(wr, v, vi, n, cells); r.appendChild(wr); }
      else { r.appendChild(g); }
      w.appendChild(r); });
    return w;
  }
  /* a lane's layers, zone over chord over section over route: what each says for the voice at each chord (its role, else its
     style), the one the cell takes (the model's resolve, M.cell's source) lit */
  function sayOf(o) { if (!o) { return ""; } return o.role === "melody" ? "melody" : o.role === "off" ? "off" : o.style ? WORD[o.style] || o.style : o.role === "accomp" ? "on" : ""; }
  function layerRows(wr, v, vi, n, cells) {
    ["zone", "chord", "section", "route"].forEach(function (k) {
      var row = el("div", "cd-layer"); row.dataset.k = k; row.style.gridTemplateColumns = "repeat(" + n + ", minmax(0, 1fr))";
      row.appendChild(el("span", "cd-lk", k));
      for (var i = 0; i < n; i++) {
        var at = cells[i].at, say = "";
        if (k === "route") { say = sayOf(comp[v]) || WORD[comp[v].style]; }
        else if (k === "chord") { say = sayOf((comp.over.chord[String(i)] || {})[v]); }
        else { at.filter(function (b) { return b.kind === k; }).forEach(function (b) { say = sayOf((comp.over[k][String(b.id)] || {})[v]) || say; }); }
        var x = el("i", say ? (cells[i][vi].source === k ? "win" : null) : "none", say || "·"); x.dataset.i = i;
        x.title = "chord " + (i + 1) + " · " + k + ": " + (say || "says nothing") + (say && cells[i][vi].source === k ? " · plays" : "");
        row.appendChild(x);
      }
      wr.appendChild(row); });
  }
  /* the grown dock's sentence editor (the mock's frame 1): the motif on its roll - rows the chord's tones (or the key's scale),
     columns sixteenths, a note a bar sized by its length, accented brighter, its end dragged to lengthen it - its accents, how
     it fits the first chords, and the development chain as chips (drag to reorder: a later step wins where two cover a chord) */
  function editor(n) {
    var m = motif(), names = rowsOf(m), R = names.length, L = (m.bars || 1) * 16, w = el("div", "cd-ed"), scale = m.basis === "scale";
    /* the scale roll's guide: the chord's own tones lit on the key's scale (the selected step's chord, the selected chord, or the first) */
    var gi = (comp.chain || [])[si] ? comp.chain[si].from : sel.kind === "chord" ? sel.i : 0, tones = ((ctx.chords[gi] || {}).tones || []).map(function (x) { return mod(x, 12); });
    var lit = function (d) { return scale && ctx.scale && ctx.scale.length && tones.indexOf(mod(ctx.scale[d], 12)) >= 0; };
    var h = el("div", "cd-edh"); h.dataset.fk = "edh";
    var t = el("h3", null, "Motif");
    t.appendChild(el("i", null, m.notes.length + (m.notes.length === 1 ? " note" : " notes") + " · " + (L / 16) + (L > 16 ? " bars" : " bar") + " · " +
      (scale ? "scale degrees of " + modeName() + " · lit: " + ((ctx.chords[gi] || {}).label || "") + "’s tones" : "chord degrees")));
    h.appendChild(t);
    if (capped) { var cp = el("span", "cd-mcap", "8 notes at most · remove one to add another"); cp.setAttribute("role", "status"); h.appendChild(cp); }
    var bs = seg([["chord", "chord"], ["scale", "scale"]], m.basis || "chord", function (v) { editMotif(function (x) { x.basis = v; var R = rowsOf(x).length;   /* fold each degree into the new rows, its octaves carried (the core wraps by the new count) */
  x.notes.forEach(function (y) { var o = Math.floor(y.deg / R); y.deg -= o * R; y.oct = Math.max(-2, Math.min(2, (y.oct || 0) + o)); if (!y.oct) { delete y.oct; } }); }); }, "Degrees of"); bs.id = "cd-basis";
    bs.title = "Rows: the chord's own tones, or the key's scale"; h.appendChild(bs);
    h.appendChild(seg([[1, "1 bar"], [2, "2 bars"]], L / 16, function (v) { editMotif(function (x) { x.bars = +v;
      x.notes = x.notes.filter(function (y) { return y.start < 16 * v; }).map(function (y) { y.len = Math.min(y.len, 16 * v - y.start); return y; }); }); }, "Length"));
    var gb = el("button", null, "generate"); gb.type = "button"; gb.title = "Draw a new motif (Ctrl+Z takes it back)"; gb.addEventListener("click", function () { newMotif(n); }); h.appendChild(gb);
    var cb = el("button", null, "clear"); cb.type = "button"; cb.title = "Clear the motif's notes"; cb.disabled = !m.notes.length;
    cb.addEventListener("click", function () { ni = -1; want = [CURSOR]; editMotif(function (x) { x.notes = []; }); }); h.appendChild(cb);
    w.appendChild(h);
    var roll = el("div", "cd-roll"); roll.id = "cd-motif"; roll.dataset.fk = "roll"; roll.style.setProperty("--rows", R);
    var keys = el("div", "cd-mkeys"); for (var r = R - 1; r >= 0; r--) { keys.appendChild(el("span", "cd-mrk" + (lit(r) ? " tone" : ""), names[r])); } roll.appendChild(keys);
    var g = el("div", "cd-mgrid"); g.dataset.cols = L; g.style.gridTemplateColumns = "repeat(" + L + ", minmax(0, 1fr))"; g.style.gridTemplateRows = "repeat(" + R + ", minmax(0, 1fr))";
    cur.row = Math.min(cur.row, R - 1); cur.col = Math.min(cur.col, L - 1);
    var full = m.notes.length >= 8;
    for (var vr = 0; vr < R; vr++) { for (var c = 0; c < L; c++) { (function (d, c) {
      var b = el("button", "cd-mcell" + (c % 4 === 0 ? " beat" : "") + (lit(d) ? " tone" : "") + (cur.row === d && cur.col === c ? " cur" : "")); b.type = "button";
      b.dataset.row = d; b.dataset.col = c; b.dataset.fk = "mc:" + d + ":" + c; b.tabIndex = cur.row === d && cur.col === c ? 0 : -1;   /* the roll: one tab stop, its cursor */
      b.setAttribute("aria-label", names[d] + ", " + fmt({ fmt: "beat" }, c)); if (full) { b.title = "The motif holds 8 notes at most"; }
      b.addEventListener("click", function () { toggleNote(d, c); });
      b.addEventListener("focus", function () { cur = { row: d, col: c }; });
      g.appendChild(b); })(R - 1 - vr, c); } }
    var pitch = "(100% + 2px)";   /* a column's pitch: the grid's width and one gap, over the columns */
    m.notes.forEach(function (x, i) {
      if (x.start >= L) { return; }
      var row = mod(x.deg, R), up = Math.floor(x.deg / R) + (x.oct || 0), acc = x.acc === undefined ? 70 : x.acc;
      var b = el("button", "cd-mnote" + (acc >= 85 ? " acc" : "") + (sel.kind === "sent" && ni === i ? " sel" : ""), names[row] + (up > 0 ? "′" : up < 0 ? "‚" : ""));
      b.type = "button"; b.dataset.i = i; b.tabIndex = -1;   /* reached by the cursor: Enter on its first cell removes it, Delete too */
      b.style.left = "calc(" + pitch + " * " + (x.start / L) + ")"; b.style.width = "calc(" + pitch + " * " + (Math.min(x.len, L - x.start) / L) + " - 2px)";
      b.style.top = "calc(" + pitch + " * " + ((R - 1 - row) / R) + ")"; b.style.height = "calc(" + pitch + " / " + R + " - 2px)";
      b.setAttribute("aria-label", "note " + (i + 1) + ", " + names[row] + ", " + fmt({ fmt: "beat" }, x.start) + ", " + fmt({ fmt: "dur" }, x.len));
      b.title = names[row] + " · " + fmt({ fmt: "beat" }, x.start) + " · " + fmt({ fmt: "dur" }, x.len) + " · drag its end to lengthen it";
      b.addEventListener("click", function () { if (!dragged) { cur = { row: row, col: x.start }; pickNote(i); } });
      g.appendChild(b); });
    g.addEventListener("pointerdown", function (e) { lengthen(e, g, m, L); });
    if (!m.notes.length) {   /* C-5: no motif yet - a melody voice plays as Free until it has notes */
      var em = el("div", "cd-mempty"); em.appendChild(el("p", "cd-mempty-t", "Draw a motif, or generate one."));
      em.appendChild(el("p", "hint", "A melody voice plays the motif along the chain. Until it has notes it plays as Free."));
      var eb = el("div", "cd-mempty-b"); eb.dataset.fk = "mempty";
      [["Generate", function () { want = [CURSOR]; newMotif(n); }], ["Use a starter", function () { want = [CURSOR]; editMotif(function (x) { x.notes = M.motifDefault().notes; }); }]].forEach(function (k) {
        var b = el("button", null, k[0]); b.type = "button"; b.addEventListener("click", k[1]); eb.appendChild(b); });
      em.appendChild(eb); g.appendChild(em);
    }
    roll.appendChild(g); w.appendChild(roll);
    var ac = el("div", "cd-macc"); ac.dataset.fk = "macc"; ac.appendChild(el("span", "cd-mrk", "accent"));
    var ag = el("div", "cd-maccs"); ag.style.gridTemplateColumns = "repeat(" + L + ", minmax(0, 1fr))";
    for (var k = 0; k < L; k++) { (function (k) {
      var i = -1; m.notes.forEach(function (x, j) { if (x.start === k) { i = j; } });
      var a = i < 0 ? 0 : m.notes[i].acc === undefined ? 70 : m.notes[i].acc, b = el(i < 0 ? "i" : "button", i < 0 ? null : "cd-mab" + (a >= 85 ? " acc" : ""));
      if (i >= 0) { b.type = "button"; b.style.setProperty("--h", a + "%"); b.setAttribute("aria-label", "accent, note " + (i + 1) + ", " + a); b.title = "accent " + a + " · click to set, ↑ ↓ by 10";
        var setA = function (v) { ni = i; sel = { kind: "sent" }; editMotif(function (x) { x.notes[i].acc = Math.max(0, Math.min(100, v)); }); };
        b.addEventListener("click", function (ev) { if (!ev.detail) { return; }   /* Enter or Space: a keyboard click has no height to read */
          var r = b.getBoundingClientRect(); setA(Math.round(Math.max(0, Math.min(1, 1 - (ev.clientY - r.top) / r.height)) * 100)); });
        b.addEventListener("keydown", function (ev) { if (ev.key === "ArrowUp" || ev.key === "ArrowDown") { ev.preventDefault(); setA(a + (ev.key === "ArrowUp" ? 10 : -10)); } }); }
      ag.appendChild(b); })(k); }
    ac.appendChild(ag); w.appendChild(ac);
    /* how it fits: the developed motif over the first two chords and the selected step's */
    var fit = el("p", "cd-fit"), at = [0, 1], s = (comp.chain || [])[si];
    if (s && at.indexOf(s.from) < 0) { at.push(s.from); }
    at.filter(function (i) { return i < n; }).forEach(function (i, k) {
      if (k) { fit.appendChild(document.createTextNode("  ·  ")); }
      var st = M.chainAt(comp, i); if (st && st.op !== "repeat") { fit.appendChild(document.createTextNode(stepWord(st) + " ")); }
      fit.appendChild(document.createTextNode("over ")); fit.appendChild(el("b", null, ctx.chords[i].label)); fit.appendChild(document.createTextNode(" " + (plays(i).join(" ") || "—"))); });
    fit.title = fit.textContent; w.appendChild(fit);
    var ch = comp.chain || [], chh = el("div", "cd-edh");
    var t2 = el("h3", "cd-edh2", "Development chain"); t2.appendChild(el("i", null, ch.length ? "drag or Alt+← → to reorder · each step covers its chords · a later one wins" : "no steps: the motif repeats")); chh.appendChild(t2); w.appendChild(chh);
    /* the chips in a run that gives way (the ranges go, then the long words, then they shrink with an ellipsis: draw measures);
       + step stays at the row's end */
    var bar = el("div", "cd-chips"), cl = el("div", "cd-chiprun");
    ch.forEach(function (x, k) {
      if (k) { cl.appendChild(el("span", "cd-arw", "→")); }
      var L2 = letterAt(x.from, n), pre = L2 ? L2.letter + " · " : "", b = el("button", "cd-chip" + (sel.kind === "sent" && si === k ? " on" : "")); b.type = "button"; b.draggable = true; b.dataset.i = k; b.dataset.fk = "chip:" + k;
      var rng = (x.from + 1) + (x.to > x.from ? "–" + (x.to + 1) : "");
      b.title = pre + stepWord(x) + " · chords " + rng + " · drag, or Alt+← →, to move it";
      b.appendChild(el("span", "cd-grip", "⋮⋮")); b.appendChild(el("b", "cd-wl", pre + stepWord(x))); b.appendChild(el("b", "cd-ws", (x.op === "cadence" ? "" : pre) + shortWord(x)));
      b.appendChild(el("small", null, rng));
      b.addEventListener("click", function () { pickStep(k); });
      b.addEventListener("keydown", function (e) { if (!e.altKey || (e.key !== "ArrowLeft" && e.key !== "ArrowRight")) { return; }
        e.preventDefault(); var j = k + (e.key === "ArrowLeft" ? -1 : 1); if (j < 0 || j >= ch.length) { return; } moveStep(k, j); });
      b.addEventListener("dragstart", function (e) { e.dataTransfer.setData("text/plain", String(k)); e.dataTransfer.effectAllowed = "move"; });
      b.addEventListener("dragover", function (e) { e.preventDefault(); });
      b.addEventListener("drop", function (e) { e.preventDefault(); var f = +e.dataTransfer.getData("text/plain"); if (f === k || isNaN(f)) { return; } moveStep(f, k); });
      cl.appendChild(b); });
    bar.appendChild(cl);
    var add = el("select", "cd-addstep"); add.id = "cd-addstep"; add.dataset.fk = "addstep"; add.setAttribute("aria-label", "Add a chain step"); add.disabled = ch.length >= 16;
    add.title = ch.length >= 16 ? "The chain holds 16 steps" : "Add a step: an operation on the motif over a chord or more";
    add.appendChild(new Option("+ step", "")); M.OPS.forEach(function (o) { add.appendChild(new Option(o.label, o.id)); });
    add.addEventListener("change", function () { if (add.value) { addStep(add.value, n); } }); bar.appendChild(add);
    w.appendChild(bar);
    var hint = el("p", "cd-edhint"); hint.innerHTML = "Click an empty cell to add a note, drag its end to lengthen it, <kbd>Del</kbd> removes it; arrows move in the roll, <kbd>Enter</kbd> adds or removes."; w.appendChild(hint);
    return w;
  }
  /* a chain step moved (a later step wins where two cover a chord): its chords go with it, the selection and focus follow */
  function moveStep(f, k) { si = k; sel = { kind: "sent" }; want = ['[data-fk="chip:' + k + '"]']; editChain(function (c) { var y = c.splice(f, 1)[0]; c.splice(k, 0, y); }); }
  function modeName() { var sc = ctx.scale || []; return sc.length ? noteName(sc[0]).replace(/-?\d+$/, "") + " " + (mod(sc[2] - sc[0], 12) === 3 ? "aeolian" : "ionian") : "the key"; }
  function newMotif(n) { mseed = M.roll(mseed || comp.seed || 1); var g = M.generate(comp, { form: "AABA", contrast: 0, seed: mseed, nchords: Math.min(n, M.NCHORD) });
    ni = -1; editMotif(function (x) { x.notes = g.motif.notes; x.bars = g.motif.bars; }); }
  function addStep(op, n) {
    var ch = comp.chain || [], m = Math.min(n, M.NCHORD), last = ch[ch.length - 1];
    var at = sel.kind === "chord" ? Math.min(sel.i, m - 1) : last ? Math.min(last.to + 1, m - 1) : 0, o = M.OPS.filter(function (x) { return x.id === op; })[0];
    var s = { op: op, from: at, to: at }; if (o.arg) { s.arg = o.arg.def; } if (op === "cadence") { s.cad = "full"; s.others = "thin"; }
    si = ch.length; sel = { kind: "sent" }; editChain(function (c) { c.push(s); });
  }
  /* a step removed: focus on the step now in its place, the one before, + step, or the chords */
  function removeStep(k) { si = -1; want = ['#cd-chain .cd-step[data-i="' + k + '"]', '#cd-chain .cd-step[data-i="' + (k - 1) + '"]', "#cd-addstep", "#cd-strip .cd-chord"];
    editChain(function (c) { c.splice(k, 1); }); }
  /* a note added where the roll is empty (one note a start: the note there goes; a note sounding on is cut short), or removed */
  function toggleNote(d, c) {
    var m = motif(), R = rowsOf(m).length, L = (m.bars || 1) * 16, hit = -1;
    m.notes.forEach(function (x, i) { if (x.start === c && mod(x.deg, R) === d) { hit = i; } });
    cur = { row: d, col: c };
    if (hit >= 0) { ni = -1; editMotif(function (x) { x.notes.splice(hit, 1); }); return; }
    var keep = m.notes.filter(function (x) { return x.start !== c; });
    if (keep.length >= 8) { capped = true; render(); return; }   /* the core plays 8 notes at most (MOTIF_MAX): C-5, say so */
    editMotif(function (x) {
      x.notes = keep.map(function (y) { y = M.clone(y); if (y.start < c && y.start + y.len > c) { y.len = c - y.start; } return y; });
      var next = L; x.notes.forEach(function (y) { if (y.start > c) { next = Math.min(next, y.start); } });
      x.notes.push({ deg: d, start: c, len: Math.max(1, Math.min(4, next - c)) }); ni = x.notes.length - 1;
    });
  }
  /* a note's end dragged (its last 8 px): its length follows to the nearest sixteenth, up to the next note (as adding one cuts
     the note before), stored once on release (one undo step) */
  var dragged = false;
  function lengthen(e, g, m, L) {
    var nb = e.target.closest && e.target.closest(".cd-mnote"); if (!nb || e.button) { return; }
    var r = nb.getBoundingClientRect(); if (e.clientX < r.right - 8) { return; }
    e.preventDefault(); dragged = true;
    var i = +nb.dataset.i, x = m.notes[i], gr = g.getBoundingClientRect(), pc = (gr.width + 2) / L, len = x.len, end = L;
    m.notes.forEach(function (y) { if (y.start > x.start) { end = Math.min(end, y.start); } });
    var mv = function (ev) { len = Math.max(1, Math.min(end - x.start, Math.round((ev.clientX - gr.left + 1) / pc) - x.start));
      nb.style.width = "calc((100% + 2px) * " + (len / L) + " - 2px)"; };
    try { nb.setPointerCapture(e.pointerId); } catch (_) { /* no live pointer (a synthetic event): the handle still hears its own */ }
    var up = function () { ["pointermove", "pointerup", "pointercancel"].forEach(function (t) { nb.removeEventListener(t, t === "pointermove" ? mv : up); });
      setTimeout(function () { dragged = false; }, 0); ni = i; sel = { kind: "sent" };
      if (len !== x.len) { editMotif(function (y) { y.notes[i].len = len; }); } else { render(); } };
    nb.addEventListener("pointermove", mv); nb.addEventListener("pointerup", up); nb.addEventListener("pointercancel", up);
  }
  /* the walker's chord: a dot on voice 1's cell it plays, kept current while the dock is open */
  function markNow() {
    if (!ctx) { return; }
    var t = ctx.now ? ctx.now() : null, n = ctx.chords.length, k = t == null ? -1 : Math.min(n - 1, Math.floor(t * n));
    [].forEach.call(document.querySelectorAll('#cd-lanes .cd-lane[data-voice="voice"] .cd-cell'), function (c, i) { c.classList.toggle("now", i === k); });
  }
  /* the foot's dropout readout: the engine's slow blocks (web/core-worklet.js), asked once a second while the dock is open;
     "audio ok", or how many in the last minute (warn), the slowest block in its title. Tests read __fa.dropouts */
  var gapLog = [];   /* the device's gap count, stamped: the last minute's are the ones shown */
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
    /* slow blocks are the engine running late; the device's own gap count (Chrome's playbackStats) says whether it was heard */
    var as = window.__fa.audioStats, gaps = as && typeof as.underruns === "number" ? as.underruns : null;
    var heard = null;
    if (gaps != null) {
      var nowT = Date.now(), lastG = gapLog[gapLog.length - 1];
      if (lastG && gaps < lastG.g) { gapLog = []; }   /* a new engine: its count starts again */
      gapLog.push({ at: nowT, g: gaps });
      while (gapLog.length > 1 && nowT - gapLog[1].at >= 60000) { gapLog.shift(); }
      heard = gaps - gapLog[0].g;
    }
    e.classList.toggle("warn", n > 0 || heard > 0);
    e.lastChild.textContent = heard > 0 ? heard + (heard === 1 ? " gap heard" : " gaps heard") + " · " + n + " slow" :
      n ? n + (n === 1 ? " slow block" : " slow blocks") + " in the last minute" : "audio ok";
    e.title = audio.length ? "slowest block in the last minute: " + slow + " ms (a block lasts 2.7 ms)" : "slowest block: none yet - no sound is playing";
  }
  function foot() {
    var f = el("div", "cd-foot"), lg = el("div", "cd-legend");
    [["route", ""], ["by a section", " by-section"], ["by a zone", " by-zone"], ["by a chord", " by-chord"]].concat(hasMelody() || (comp.chain || []).length ? [["by the chain", " by-chain"]] : []).forEach(function (x) {
      var s = el("span"); s.appendChild(el("i", x[1].trim() || null)); s.appendChild(document.createTextNode(x[0])); lg.appendChild(s); });
    f.appendChild(lg);
    var k = el("span", "cd-keys");
    k.innerHTML = "<kbd>←</kbd><kbd>→</kbd> cells <kbd>Enter</kbd> open <kbd>Del</kbd> clear <kbd>Space</kbd> sound";
    f.appendChild(k);
    var au = el("span", "cd-audio"); au.id = "cd-audio"; au.setAttribute("role", "status"); au.appendChild(el("i")); au.appendChild(el("span"));
    f.appendChild(au); setTimeout(showAudio, 0); return f;
  }
  function pick(s) { sel = s; gen = false; relMenu = false; ctx.light(s.kind === "chord" ? { chord: s.i } : s.kind === "zone" ? { zone: s.id } : null); render(); }
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
    if (sel.kind === "sent") { sentPanel(w, n); return; }
    if (sel.kind === "rel") { relPanel(w, bs, n); return; }
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
    var style = voice === "bass" ? null : e.style, mel = voice !== "bass" && e.values.role === "melody";
    /* a melody plays the motif: its style and rate are not what it plays (the chain is). The new styles play no rate of their own
       (a chorale's is its re-strike), a pointillist's note length is its own; the walking bass swaps its pedal's rows for the walk's */
    var HIDE = { chorale: ["rate"], tintinnabuli: ["rate"], pointillist: ["rate", "len"] }[style] || [];
    var fields = voice === "bass" ? M.BASS.filter(function (f) { return f.k === "mode" || (e.values.mode === "walk") === (["approach", "before", "wrate"].indexOf(f.k) >= 0); }) :
      (mel ? M.FIELDS.common.filter(function (f) { return f.k !== "style" && f.k !== "rate"; }) : M.FIELDS.common.concat(M.FIELDS[style] || [])).filter(function (f) { return HIDE.indexOf(f.k) < 0; });
    var vr = el("div", "cd-vrows");   /* the voice's own rows: one holder, so they can fold away as one */
    fields.filter(function (f) { return f.kind !== "grid" && f.kind !== "accents" && f.k !== "refit"; }).forEach(function (f) { fieldRow(vr, f, e);
      if (f.k === "role") { vr.appendChild(el("p", "cd-isub wrap cd-rolehint", "“melody” plays the motif; “on” plays the style; “off” is silent.")); } });
    var grid = fields.filter(function (f) { return f.kind === "grid"; })[0];
    if (grid) { patternBlock(vr, grid, fields.filter(function (f) { return f.kind === "accents"; })[0], e); }
    fields.filter(function (f) { return f.k === "refit"; }).forEach(function (f) { fieldRow(vr, f, e);
      vr.appendChild(el("p", "cd-isub wrap cd-rolehint", "It repeats every bar on purpose; “keep its shape” moves the whole cell with the root.")); });
    w.appendChild(vr);
    if (voice !== "bass") { ornSection(w, e, style, mel); }
    if (mel && sel.kind === "chord") {   /* what it plays here */
      w.appendChild(el("p", "cd-ieb", "plays here"));
      var st = M.chainAt(comp, sel.i), pl = plays(sel.i);
      w.appendChild(el("p", "cd-isub", (motif().notes.length ? "motif · " + stepWord(st) + " → " + pl.join(" · ") : "no motif yet · plays as Free")));
      var at = bs.filter(function (b) { return b.t0 < (sel.i + 1) / n && b.t1 > sel.i / n; }), say = function (k) { var x = "";
        if (k === "route") { return sayOf(comp[voice]) || "—"; }
        if (k === "chord") { return sayOf((comp.over.chord[String(sel.i)] || {})[voice]) || "—"; }
        at.filter(function (b) { return b.kind === k; }).forEach(function (b) { x = sayOf((comp.over[k][String(b.id)] || {})[voice]) || x; }); return x || "—"; };
      w.appendChild(el("p", "cd-ieb", "layers"));
      w.appendChild(el("p", "cd-isub", ["zone", "chord", "section", "route"].map(function (k) { return k + " " + say(k); }).join(" · ")));
    }
    var tools = el("div", "cd-itools"); tools.dataset.fk = "tools";
    var li = el("button", "cd-listen", "Listen here"); li.type = "button"; li.title = "Put the walker on this stretch of the route";
    li.addEventListener("click", function () {
      var b = sel.kind === "zone" || sel.kind === "section" ? bs.filter(function (x) { return x.kind === sel.kind && isSel(x.kind, x.id); })[0] : null;
      listen(sel.kind === "chord" ? (sel.i + 0.5) / n : b ? (b.t0 + b.t1) / 2 : 0.02); });
    tools.appendChild(li);
    if (mel && !grown) {
      var emb = el("button", null, "Edit motif ▴"); emb.type = "button"; emb.title = "Grow the dock: the motif's roll and the chain";
      emb.addEventListener("click", growDock); tools.appendChild(emb);
    } else {
      var sm = el("button", null, "Show on map"); sm.type = "button"; sm.title = "Bring the selection into view on the map";
      sm.addEventListener("click", function () { ctx.show(sel.kind === "chord" ? { chord: sel.i } : sel.kind === "zone" ? { zone: sel.id } : null); });
      tools.appendChild(sm);
    }
    w.appendChild(tools);
  }
  /* composition 4: fields drawn as the mock draws them. SEGS: a menu's options in one line; ADAPT: a shorter label, a format, a menu's own options */
  var SEGS = {
    orn: { label: "orn.", o: [["none", "none"], ["grace", "grace"], ["trill", "trill"], ["mordent", "mord."], ["slide", "slide"]] },
    glide_iv: { label: "glides", o: [["all", "all moves"], ["steps", "steps"], ["leaps", "leaps"]] },
    approach: { label: "to next", o: [["chromatic", "half steps"], ["scale", "scale"], ["fifth", "by the 5th"]] },
    refit: { label: "refit", o: [["degree", "by chord step"], ["transpose", "keep its shape"]] },
    tmode: { label: "tone", o: [["above", "above"], ["below", "below"], ["alt", "alternate"]] }
  };
  var EVERY = [{ id: 0, label: "by chance" }, { id: 1, label: "every note" }].concat([2, 3, 4, 5, 6, 7, 8].map(function (n) { return { id: n, label: n + (n === 2 ? "nd" : n === 3 ? "rd" : "th") + " note" }; }));
  var ADAPT = { orn_every: { kind: "menu", label: "every", options: EVERY }, orn_prob: { label: "chance" }, restrike: { label: "re-strike" }, strict: { label: "leading" }, answers: { label: "answers", fmt: "vn" },
    plen: { label: "length" }, before: { label: "last beats", title: "How many of the chord's last beats head for the next chord's root" } };
  function adapt(f) { var s = SEGS[f.k]; return s ? Object.assign({}, f, { kind: "seg", o: s.o, label: s.label }) : ADAPT[f.k] ? Object.assign({}, f, ADAPT[f.k]) : f; }
  var VOIC = [["close", "close", [36, 29, 22, 15]], ["open", "open", [40, 26, 19, 8]], ["drop2", "drop-2", [43, 29, 22, 15]]];
  /* one field's row in the inspector (v: the effective value, e.source: where it comes from), whatever its kind */
  function fieldRow(into, f0, e) {
    var f = adapt(f0), v = e.values[f.k], src = e.source[f.k];
    if (f.kind === "range") { rangeRow(into, f, v, src); return; }
    if (f.kind === "micro") { microBlock(into, f, v, src); return; }
    if (f.k === "voicing") { voicingBlock(into, f, v, src); return; }
    var r = row(f, v, src); if (f.title) { r.title = f.title; } into.appendChild(r);
    if (f.k === "voices" && !isSamp(voice)) {   /* a chorale asks no more voices than its synth plays at once (the role's own and its pool) */
      var sy = voiceSynth(voice) || "fm", cap = sy === "metal" ? 3 : 5;
      into.appendChild(el("p", "cd-isub wrap cd-rolehint", sy + " plays " + cap + " at once" + (+v > cap ? " · the extra voices are left out" : "")));
    }
    if (f.k === "strict") {
      var p = Math.round(v * 100);
      into.appendChild(el("p", "cd-isub", p + " %: " + (p >= 50 ? "nearest-note voice-leading" : p > 0 ? "stays near root position" : "root position every time")));
    }
    if (f.k === "answers") {
      var t = comp[VS[v]] || {}, free = t.role !== "melody" && t.role !== "off" && (t.style || "free") === "free";
      into.appendChild(el("p", "cd-isub wrap cd-rolehint", v === VS.indexOf(voice) ? "a voice cannot answer itself · pick another" :
        free ? "voice " + (v + 1) + " plays Free · its notes get the nearest chord tone" : "answers voice " + (v + 1) + " · the nearest root, 3rd or 5th"));
    }
  }
  function sourceHead(into, label, f, src) {   /* a block's heading: its name, and (off the route) where its value comes from */
    var eb = el("p", "cd-ieb" + (sel.kind === "route" || src === "here" ? " mine" : "")); eb.dataset.fk = "ieb:" + f.k; eb.appendChild(el("span", null, label));
    if (sel.kind !== "route") { eb.appendChild(srcCell(src, f, [f.k])); }
    into.appendChild(eb);
  }
  /* a full-line row has no source column: off the route, its own value gets a lit × (back to the inherited), an inherited one its source in its title */
  function wideReset(r, f, src) {
    if (sel.kind === "route") { return; }
    r.classList.add("rs");
    if (src !== "here") { r.title = "from the " + (src === "section" ? "section" : "route"); return; }
    var x = el("button", "cd-reset", "×"); x.type = "button"; x.title = "Back to the inherited"; x.setAttribute("aria-label", "Reset " + f.label + " to the inherited");
    x.addEventListener("click", function () { M.clearValue(comp, sel, voice, f.k); commit(); }); r.appendChild(x);
  }
  var set1 = function (f) { return function (val) { M.setValue(comp, sel, voice, f.k, val); commit(); }; };
  /* a range of two handles (the pointillist's silence): two native sliders on one track, so the arrows work on a focused handle */
  function rangeRow(into, f, v, src) {
    var mine = sel.kind === "route" || src === "here", set = set1(f), lo = v[0], hi = v[1];
    var r = el("div", "cd-row2 wide " + (mine ? "mine" : "inh")); r.dataset.k = f.k; r.dataset.fk = "row:" + f.k; r.appendChild(el("span", "cd-k", f.label));
    var ctl = el("span", "cd-ctl"), box = el("span", "cd-range"), fill = el("i"), hint = el("p", "cd-isub");
    var mk = function (x, name) { var i = el("input"); i.type = "range"; i.min = f.min; i.max = f.max; i.step = f.step; i.value = x; i.setAttribute("aria-label", f.label + ", " + name); i.title = name + " silence · ←→ by " + f.step + " s"; return i; };
    var a = mk(lo, "shortest"), b = mk(hi, "longest");
    var paint = function () { var x = +a.value, y = +b.value, q = function (z) { return (z - f.min) / (f.max - f.min); };
      box.style.setProperty("--l", q(x)); box.style.setProperty("--h", q(y)); a.style.zIndex = q(x) > 0.5 ? 3 : 1; hint.textContent = sec(x) + " – " + sec(y) + " s between notes"; };
    a.addEventListener("input", function () { if (+a.value > +b.value) { a.value = b.value; } paint(); });
    b.addEventListener("input", function () { if (+b.value < +a.value) { b.value = a.value; } paint(); });
    var done = function () { if (+a.value > +b.value) { a.value = b.value; } set([+a.value, +b.value]); };
    a.addEventListener("change", done); b.addEventListener("change", done);
    [a, b].forEach(function (i) { i.addEventListener("dblclick", function () { set(f.def.slice()); }); });
    box.appendChild(fill); box.appendChild(a); box.appendChild(b); paint(); ctl.appendChild(box); r.appendChild(ctl); wideReset(r, f, src); into.appendChild(r); into.appendChild(hint);
  }
  /* micro-tuning: five little dials, the root and the chord's 3rd, 5th, 7th, 9th, in cents. Each is a native slider (arrows move it a cent)
     drawn as a dial that a drag up or down turns; a double click puts it back to 0 */
  function microBlock(into, f, v, src) {
    var vals = (v || []).slice(); while (vals.length < 5) { vals.push(0); }
    var mine = sel.kind === "route" || src === "here", set = set1(f), ins = [];
    sourceHead(into, "micro-tuning · cents", f, src);
    var g = el("div", "cd-micro" + (mine ? "" : " inh")); g.dataset.k = f.k; g.dataset.fk = "row:" + f.k; g.setAttribute("role", "group"); g.setAttribute("aria-label", f.label + ", cents per chord tone");
    ["root", "3", "5", "7", "9"].forEach(function (nm, i) {
      var k = el("div", "cd-mk" + (vals[i] ? " set" : "")), dial = el("span", "cd-mdial"), inp = el("input"), val = el("b", null, fmt({ fmt: "sgn" }, vals[i]));
      inp.type = "range"; inp.min = f.min; inp.max = f.max; inp.step = f.step; inp.value = vals[i]; inp.setAttribute("aria-label", f.label + ", " + (nm === "root" ? "root" : "chord " + nm) + ", cents");
      inp.title = (nm === "root" ? "The root" : "The chord's " + nm) + " in cents · drag the dial, ←→ by 1, double-click for 0";
      var show = function () { var x = +inp.value; val.textContent = fmt({ fmt: "sgn" }, x); k.classList.toggle("set", x !== 0); dial.style.setProperty("--a", x * 2.4 + "deg"); };
      show(); ins.push(inp);
      inp.addEventListener("input", show);
      inp.addEventListener("change", function () { set(ins.map(function (q) { return +q.value; })); });
      dial.addEventListener("dblclick", function () { if (+inp.value) { inp.value = 0; show(); inp.dispatchEvent(new Event("change", { bubbles: true })); } });
      dial.addEventListener("pointerdown", function (ev) {
        if (ev.button) { return; }
        ev.preventDefault(); inp.focus({ preventScroll: true });
        var y0 = ev.clientY, v0 = +inp.value, moved = false;
        try { dial.setPointerCapture(ev.pointerId); } catch (_) { /* no live pointer (a synthetic event): the dial still hears its own */ }
        var mv = function (m) { var x = Math.max(f.min, Math.min(f.max, v0 + Math.round(y0 - m.clientY))); if (x !== +inp.value) { inp.value = x; show(); moved = true; } };
        var up = function () { ["pointermove", "pointerup", "pointercancel"].forEach(function (t) { dial.removeEventListener(t, t === "pointermove" ? mv : up); });
          if (moved) { inp.dispatchEvent(new Event("change", { bubbles: true })); } };
        dial.addEventListener("pointermove", mv); dial.addEventListener("pointerup", up); dial.addEventListener("pointercancel", up);
      });
      k.appendChild(dial); k.appendChild(inp); k.appendChild(val); k.appendChild(el("small", null, nm)); g.appendChild(k);
    });
    into.appendChild(g);
  }
  /* the chorale's voicing: three little staves, the chosen one lit */
  function voicingBlock(into, f, v, src) {
    var mine = sel.kind === "route" || src === "here", set = set1(f);
    sourceHead(into, f.label, f, src);
    var g = el("div", "cd-vrow" + (mine ? "" : " inh")); g.dataset.k = f.k; g.dataset.fk = "row:" + f.k; g.setAttribute("role", "group"); g.setAttribute("aria-label", f.label);
    VOIC.forEach(function (x) {
      var b = el("button", "cd-vbtn" + (x[0] === v ? " on" : "")); b.type = "button"; b.dataset.v = x[0]; b.setAttribute("aria-pressed", String(x[0] === v)); b.title = x[1];
      var lines = [0, 1, 2, 3, 4].map(function (l) { return '<line x1="2" x2="32" y1="' + (8 + l * 7) + '" y2="' + (8 + l * 7) + '" stroke="currentColor" stroke-width=".6" opacity=".45"/>'; }).join("");
      b.innerHTML = '<svg viewBox="0 0 34 46" class="cd-vsvg" aria-hidden="true">' + lines + x[2].map(function (y) { return '<ellipse cx="17" cy="' + y + '" rx="4.2" ry="3" fill="currentColor"/>'; }).join("") + "</svg><span></span>";
      b.lastChild.textContent = x[1]; b.addEventListener("click", function () { set(x[0]); }); g.appendChild(b); });
    into.appendChild(g);
  }
  /* ornaments & glides (a collapsible section: closed unless something is set, its heading's dot lit when something is) */
  function voiceSynth(vk) { return (((ctx.route.properties || {}).patch || {})[vk] || {}).synth; }   /* the route's synth for a voice */
  function isSamp(vk) { var syn = voiceSynth(vk); return typeof syn === "string" && syn.indexOf("s-") === 0; }   /* the page's own test (lab-roles isSampler) */
  function ornSection(w, e, style, mel) {
    var v = e.values, set = ornSet(v), open = ornOpen === null ? set : ornOpen;
    var fs = {}; M.ORNAMENTS.forEach(function (f) { fs[f.k] = f; });
    var sm = el("button", "cd-foldsum"); sm.type = "button"; sm.setAttribute("aria-expanded", "false"); sm.dataset.fk = "foldsum";
    sm.appendChild(el("span", null, "settings · " + M.label(style))); sm.appendChild(el("span", "cd-chev", "▸")); sm.title = "Show this voice's settings (closes ornaments & glides)";
    sm.addEventListener("click", function () { ornOpen = false; render(); }); w.appendChild(sm);
    var hd = el("button", "cd-orn"); hd.type = "button"; hd.dataset.fk = "orn"; hd.setAttribute("aria-expanded", String(open)); hd.title = open ? "Close ornaments & glides" : "Ornaments, glides and micro-tuning";
    hd.appendChild(el("span", null, "ornaments & glides")); if (set) { hd.appendChild(el("i", "cd-odot")); } hd.appendChild(el("span", "cd-chev", open ? "▾" : "▸"));
    hd.addEventListener("click", function () { ornOpen = !open; render(); }); w.appendChild(hd);
    if (!open) { return; }
    var box = el("div", "cd-fx"), g = function (k) { fieldRow(box, fs[k], e); };
    var ign = mel ? null : style, tint = ign === "tintinnabuli", hint = function (t) { box.appendChild(el("p", "cd-isub wrap cd-rolehint", t)); };
    g("orn");
    if (v.orn !== "none") { g("orn_every"); if (v.orn_every === 0) { g("orn_prob"); } g("orn_speed");
      if (tint) { hint("tintinnabuli answers play no ornaments"); } else if (CHORDY.indexOf(ign) >= 0) { hint("ornaments don't apply to chord styles"); } }
    g("glide");
    if (v.glide > 0) { g("glide_iv");
      var sam = isSamp(voice);
      var why = tint ? "tintinnabuli answers don't glide" : sam ? "a sampler voice changes pitch without gliding" : CHORDY.indexOf(ign) >= 0 ? "glides don't apply to chord styles" : "";
      if (why) { hint(why); } }
    g("micro");
    if (tint && (v.micro || []).some(Boolean)) { hint("tintinnabuli answers aren't micro-tuned"); }
    w.appendChild(box);
  }
  function growDock() { grown = !grown; document.body.classList.toggle("composing-grown", grown); resize(); render(); }
  /* the sentence's inspector: the selected chain step (operation, by, from, to; a cadence's type, the others, its strength) and
     the selected motif note (degree, octave, start, length, accent) */
  function sentPanel(w, n) {
    var ch = comp.chain || [], s = ch[si], m = motif(), x = m.notes[ni], names = rowsOf(m), L = (m.bars || 1) * 16, nc = Math.min(n, M.NCHORD);
    var ih = el("div", "cd-ih"); ih.dataset.fk = "ih";
    var lt = s ? letterAt(s.from, n) : null;
    var h = el("h3", null, s ? "Chain step " + (si + 1) + (lt ? " · " + lt.letter : "") : "Motif · note " + (ni + 1)); h.id = "cd-insp-title"; ih.appendChild(h);
    var rm = el("button", "ghost cd-clear", "remove"); rm.type = "button"; rm.title = s ? "Remove this chain step" : "Remove this note";
    var dropNote = function () { var j = ni; ni = -1; want = [CURSOR, "#cd-insp-title"]; editMotif(function (y) { y.notes.splice(j, 1); }); };
    rm.addEventListener("click", function () { if (s) { removeStep(si); } else { dropNote(); } });
    ih.appendChild(rm); w.appendChild(ih);
    if (s) {
      w.appendChild(el("p", "cd-isub", (s.to > s.from ? "chords " + (s.from + 1) + "–" + (s.to + 1) : "chord " + (s.from + 1)) + (s.op === "cadence" ? " · the phrase closes" : " · the motif, developed")));
      var setS = function (k) { return function (v) { var j = si; editChain(function (c) { var y = c[j]; y[k] = v;
        if (k === "op") { var o = M.OPS.filter(function (z) { return z.id === v; })[0]; if (o.arg && (y.arg === undefined || y.arg < o.arg.min || y.arg > o.arg.max)) { y.arg = o.arg.def; }
          if (v === "cadence") { y.cad = y.cad || "full"; y.others = y.others || "thin"; } }
        if (k === "from" && y.to < v) { y.to = v; } }); }; };
      var opd = M.OPS.filter(function (z) { return z.id === s.op; })[0] || {};
      var HERE = { src: "here" }, own = function (set, k, def) { return set ? { src: "here", reset: function () { setS(k)(def); } } : { src: "default" }; };
      w.appendChild(row({ k: "op", label: "operation", kind: "menu", options: M.OPS }, s.op, "", setS("op"), HERE));
      if (opd.arg) { w.appendChild(row({ k: "arg", label: s.op === "seq" ? "by" : "notes", kind: "knob", min: opd.arg.min, max: opd.arg.max, step: 1, def: opd.arg.def, fmt: s.op === "seq" ? "sgn" : null }, Math.round(s.arg || 0), "",
        function (v) { setS("arg")(+v); }, own(Math.round(s.arg || 0) !== opd.arg.def, "arg", opd.arg.def))); }
      var chords = []; for (var i = 0; i < nc; i++) { chords.push({ id: i, label: "chord " + (i + 1) }); }
      w.appendChild(row({ k: "from", label: "from", kind: "menu", options: chords }, s.from, "", function (v) { setS("from")(+v); }, HERE));
      w.appendChild(row({ k: "to", label: "to", kind: "menu", options: chords.slice(s.from) }, s.to, "", function (v) { setS("to")(+v); }, HERE));
      if (s.op === "cadence") {
        [["cad", "type", ["full", "half", "deceptive"], s.cad || "full"], ["others", "others", ["hold", "thin", "stop"], s.others || "thin"]].forEach(function (k) {
          var r = el("div", "cd-row2 wide mine"); r.dataset.k = k[0]; r.dataset.fk = "row:" + k[0]; r.appendChild(el("span", "cd-k", k[1]));
          var c = el("span", "cd-ctl"); c.appendChild(seg(k[2].map(function (v) { return [v, v]; }), k[3], setS(k[0]), k[1])); r.appendChild(c); w.appendChild(r); });
        w.appendChild(row({ k: "strength", label: "strength", kind: "knob", min: 0, max: 1, step: 0.05, def: 1, fmt: "pct" }, s.strength === undefined ? 1 : s.strength, "", function (v) { setS("strength")(+v); },
          own(s.strength !== undefined && s.strength !== 1, "strength", 1)));
        w.appendChild(el("p", "cd-isub wrap", "full ends on the tonic, half on the 5th, deceptive on the 6th"));
      }
    }
    if (x) {
      var R = names.length, row0 = mod(x.deg, R), lab = m.basis === "scale" ? names[row0] : DEG[row0];
      var eb = el("p", "cd-ieb", "selected note · " + lab); eb.dataset.fk = "ieb"; w.appendChild(eb);
      if (s) { var rn = el("button", "cd-reset", "remove"); rn.type = "button"; rn.title = "Remove this note"; rn.addEventListener("click", dropNote); eb.appendChild(rn); }   /* the head's remove is the step's */
      var setN = function (k) { return function (v) { var j = ni; editMotif(function (y) { var z = y.notes[j]; z[k] = +v;
        if (k === "start") { z.len = Math.min(z.len, L - z.start); } }); }; };
      var NH = { src: "here" }, oc = Math.floor(x.deg / R) + (x.oct || 0);
      var unset = function (k) { var j = ni; return function () { editMotif(function (y) { var z = y.notes[j]; if (k === "oct") { z.deg = mod(z.deg, R); } delete z[k]; }); }; };
      w.appendChild(row({ k: "deg", label: "degree", kind: "menu", options: names.map(function (q, d) { return { id: d, label: m.basis === "scale" ? q : DEG[d] }; }) }, row0, "",
        function (v) { var j = ni; editMotif(function (y) { var z = y.notes[j]; z.oct = Math.max(-2, Math.min(2, Math.floor(z.deg / R) + (z.oct || 0))); z.deg = +v; if (!z.oct) { delete z.oct; } }); }, NH));
      w.appendChild(row({ k: "oct", label: "octave", kind: "knob", min: -2, max: 2, step: 1, def: 0, fmt: "sgn" }, oc, "",
        function (v) { var j = ni; editMotif(function (y) { var z = y.notes[j]; z.oct = +v - Math.floor(z.deg / R); }); }, oc ? { src: "here", reset: unset("oct") } : { src: "motif" }));
      w.appendChild(row({ k: "start", label: "start", kind: "knob", min: 0, max: L - 1, step: 1, def: 0, fmt: "beat" }, x.start, "", setN("start"), NH));
      w.appendChild(row({ k: "len", label: "length", kind: "knob", min: 1, max: L - x.start, step: 1, def: 4, fmt: "dur" }, x.len, "", setN("len"), NH));
      w.appendChild(row({ k: "acc", label: "accent", kind: "knob", min: 0, max: 100, step: 5, def: 70 }, x.acc === undefined ? 70 : x.acc, "", setN("acc"), x.acc !== undefined ? { src: "here", reset: unset("acc") } : { src: "motif" }));
    }
    var tools = el("div", "cd-itools"); tools.dataset.fk = "tools";
    var li = el("button", "cd-listen", "Listen here"); li.type = "button"; li.title = "Put the walker on this step's chords";
    li.addEventListener("click", function () { listen(((s ? s.from : 0) + 0.5) / n); }); tools.appendChild(li);
    var sm = el("button", null, grown ? "Show on map" : "Edit motif ▴"); sm.type = "button";
    sm.addEventListener("click", function () { if (grown) { ctx.show(s ? { chord: s.from } : null); } else { growDock(); } }); tools.appendChild(sm);
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
    var rr = el("div", "cd-row2 wide mine"); rr.dataset.k = "relations"; rr.dataset.fk = "row:relations"; rr.appendChild(el("span", "cd-k", "relations"));
    var rc = el("span", "cd-ctl"); rc.appendChild(seg([["none", "none"], ["some", "some"], ["rich", "rich"]], genRel, function (v) { genRel = v; render(); }, "relations")); rr.appendChild(rc); w.appendChild(rr);
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
    w.appendChild(el("p", "cd-isub", "replaces the motif, chain and relations · Ctrl+Z undoes"));
    var tools = el("div", "cd-itools"); tools.dataset.fk = "tools";
    var g = el("button", "cd-listen", "Generate"); g.type = "button"; g.id = "cd-generate"; g.title = "Write a new piece in this form, with a new seed";
    g.addEventListener("click", function () { generate(M.roll(comp.seed)); }); tools.appendChild(g); w.appendChild(tools);
  }
  function seg(opts, value, onPick, label) {
    var s = el("span", "cd-seg"); s.style.setProperty("--n", opts.length); s.setAttribute("role", "group"); if (label) { s.setAttribute("aria-label", label); }
    opts.forEach(function (o) { var b = el("button", null, o[1]); b.type = "button"; b.dataset.v = o[0]; b.setAttribute("aria-pressed", String(String(o[0]) === String(value)));
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
  /* the sentence's source column: "here x" (x back to the default), else where it comes from (the motif's, the default) */
  function ownSrc(f, so) {
    var s = el("span", "cd-src", so.src);
    if (so.reset) { var x = el("button", "cd-reset", "×"); x.type = "button"; x.title = "Back to the " + (so.src === "here" ? "default" : so.src); x.setAttribute("aria-label", "Reset " + f.label);
      x.addEventListener("click", so.reset); s.appendChild(x); }
    return s;
  }
  function row(f, v, src, own, so) {   /* own: the sentence's setter (a chain step's, a note's); so: its source, { src, reset } */
    var mine = own ? so.src === "here" : sel.kind === "route" || src === "here";
    var r = el("div", "cd-row2 " + (f.kind === "seg" ? "wide " : "") + (mine ? "mine" : "inh")); r.dataset.k = f.k; r.dataset.fk = "row:" + f.k;
    r.appendChild(el("span", "cd-k", f.label));
    /* a voice made the melody gets a motif to play (the starter) in the same undo step */
    var set = own || function (val) { M.setValue(comp, sel, voice, f.k, val); if (f.k === "role" && val === "melody" && !comp.motif) { M.setMotif(comp, M.motifDefault()); } commit(); };
    var ctl = el("span", "cd-ctl"), out = el("span", "cd-v", "");
    if (f.kind === "menu") {
      var m = el("select"); m.setAttribute("aria-label", f.label);
      var opts = f.k === "role" ? ["melody", "accomp", "off"].map(function (id) { return f.options.filter(function (o) { return o.id === id; })[0]; }) : f.options;   /* the mock's order */
      opts.forEach(function (o) { m.appendChild(new Option(["rate", "restrike", "wrate"].indexOf(f.k) >= 0 ? RATE[o.id] || o.label : o.label, o.id)); }); m.value = v;
      m.addEventListener("change", function () { var o = opts.filter(function (x) { return String(x.id) === m.value; })[0]; set(o ? o.id : m.value); }); ctl.appendChild(m);   /* a menu of numbers writes a number */
    } else if (f.kind === "seg") {
      ctl.appendChild(seg(f.o, v, function (x) { set(x); }, f.label));
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
    r.appendChild(ctl); if (f.kind === "seg") { if (!own) { wideReset(r, f, src); } return r; }   /* a switch takes the line: no value, and a source only as a × */
    r.appendChild(out); r.appendChild(own ? ownSrc(f, so) : srcCell(src, f, [f.k]));
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
    if (acc && e.style === "arpeggio") {   /* the presets are the arpeggio's: an ostinato draws its own cell */
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
  function undo() { var c = hist.undo(); if (c) { comp = c; capped = false; store(); render(); } }
  function redo() { var c = hist.redo(); if (c) { comp = c; capped = false; store(); render(); } }
  /* the keyboard (the dock's own keys only where they mean something; a focused button, select, range or field keeps its
     native keys): arrows move between cells, Enter opens the inspector and Delete clears from a cell, chord or band, Space is
     the sound from the page or a cell or chord, Ctrl+Z / Ctrl+Shift+Z outside a field, Esc back to the patch */
  function on(t, sel) { return !!(t && t.closest && t.closest(sel)); }
  document.addEventListener("keydown", function (e) {
    if (!ctx) { return; }
    var t = e.target, field = /INPUT|SELECT|TEXTAREA/.test((t && t.tagName) || "") || (t && t.isContentEditable);
    var grid = on(t, "#compose-dock .cd-cell, #compose-dock .cd-chord, #compose-dock .cd-band");
    var roll = on(t, "#cd-motif .cd-mcell, #cd-motif .cd-mnote"), step = on(t, "#compose-dock .cd-step"), rel = on(t, "#compose-dock .cd-rchip, #compose-dock .cd-link");
    var page = !t || t === document || t === document.body || t === document.documentElement;
    if (e.key === "Escape" && relMenu) { e.preventDefault(); e.stopPropagation(); relMenu = false; want = ["#cd-addrel"]; render(); return; }
    if (e.key === "Escape") { if (field && !on(t, "#compose-dock")) { return; } e.preventDefault(); e.stopPropagation(); back(); return; }
    if (field) { return; }
    if ((e.ctrlKey || e.metaKey) && !e.altKey && e.key.toLowerCase() === "z") { e.preventDefault(); if (e.shiftKey) { redo(); } else { undo(); } return; }
    if (e.ctrlKey || e.metaKey || e.altKey || !ctx.chords.length) { return; }
    var n = ctx.chords.length;
    if (e.key === " ") {
      if (!page && !roll && !step && !rel && !on(t, "#compose-dock .cd-cell, #compose-dock .cd-chord")) { return; }
      var pb = document.getElementById("patch-play"); if (pb) { pb.click(); } e.preventDefault(); return;
    }
    /* the roll: arrows move its cursor, Enter adds or removes the note there, Delete removes the selected note */
    if (roll) {
      var m = motif(), R = rowsOf(m).length, L = (m.bars || 1) * 16, nb = t.closest(".cd-mnote"), x = nb ? m.notes[+nb.dataset.i] : null;
      var rr = x ? mod(x.deg, R) : +t.dataset.row, cc = x ? x.start : +t.dataset.col;
      var go = function (r, c) { var q = document.querySelector('#cd-motif .cd-mcell[data-row="' + r + '"][data-col="' + c + '"]'); if (q) { q.focus({ preventScroll: true }); } };
      if (e.key === "Enter") { e.preventDefault(); toggleNote(rr, cc); go(rr, cc); return; }
      if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); var j = nb ? +nb.dataset.i : ni;
        m.notes.forEach(function (y, q) { if (!nb && y.start === cc && mod(y.deg, R) === rr) { j = q; } });
        if (j >= 0) { ni = -1; editMotif(function (y) { y.notes.splice(j, 1); }); go(rr, cc); } return; }
      if (/^Arrow/.test(e.key)) { e.preventDefault();
        if (e.key === "ArrowRight") { cc = Math.min(L - 1, cc + 1); } else if (e.key === "ArrowLeft") { cc = Math.max(0, cc - 1); }
        else if (e.key === "ArrowUp") { rr = Math.min(R - 1, rr + 1); } else { rr = Math.max(0, rr - 1); }
        [].forEach.call(document.querySelectorAll("#cd-motif .cd-mcell.cur"), function (q) { q.classList.remove("cur"); q.tabIndex = -1; });
        var q = document.querySelector('#cd-motif .cd-mcell[data-row="' + rr + '"][data-col="' + cc + '"]'); if (q) { q.classList.add("cur"); q.tabIndex = 0; }
        cur = { row: rr, col: cc }; go(rr, cc); return; }
      return;
    }
    /* a chain step: Enter its inspector, Delete removes it */
    if (step) {
      var k = +t.closest(".cd-step").dataset.i;
      if (e.key === "Enter") { e.preventDefault(); pickStep(k); var f0 = document.querySelector("#cd-insp select, #cd-insp input"); if (f0) { f0.focus(); } return; }
      if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); removeStep(k); return; }
      return;
    }
    /* the + relation menu: arrows move between its items; a relation chip or link: Enter opens it, Delete removes it */
    if (relMenu && on(t, "#cd-relmenu") && /^Arrow(Up|Down)$/.test(e.key)) { var mi = [].slice.call(document.querySelectorAll("#cd-relmenu button:not(:disabled)")), at = mi.indexOf(t.closest("button")) + (e.key === "ArrowDown" ? 1 : -1);
      e.preventDefault(); if (mi[(at + mi.length) % mi.length]) { mi[(at + mi.length) % mi.length].focus(); } return; }
    if (rel) {
      var cur0 = t.closest(".cd-rchip, .cd-link"), rk = +cur0.dataset.i;
      if (e.key === "ArrowLeft" || e.key === "ArrowRight") { e.preventDefault();   /* along the chips, or along the links, as cells go along a lane */
        var sib = [].slice.call(document.querySelectorAll(cur0.classList.contains("cd-rchip") ? "#compose-dock .cd-rchip" : "#compose-dock .cd-link")), nx = sib[sib.indexOf(cur0) + (e.key === "ArrowRight" ? 1 : -1)];
        if (nx) { nx.focus({ preventScroll: true }); } return; }
      if (e.key === "Enter") { e.preventDefault(); pickRel(rk); var f1 = document.querySelector("#cd-insp select, #cd-insp input"); if (f1) { f1.focus(); } return; }
      if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); removeRel(rk); return; }
      return;
    }
    if (e.key === "Enter") { if (!grid) { return; } var f = document.querySelector("#cd-insp select, #cd-insp input, #cd-insp .cd-row2 button"); if (f) { f.focus(); } e.preventDefault(); return; }
    if (e.key === "Delete" || e.key === "Backspace") { if (!grid || sel.kind === "route" || sel.kind === "sent") { return; } M.clearScope(comp, sel); commit(); e.preventDefault(); return; }
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
  /* composition 3: relations (design/compose/relations-mock.html). The row of chips under the zones, the links drawn over the lanes, the
     "+ relation" menu and the relation's inspector. Every edit goes through commit(true): one undo step, the form row left alone. */
  var RTYPES = [["canon", "canon", "follows, later and moved"], ["call", "call & response", "answers in the rests"], ["hocket", "hocket", "shares one line"],
    ["hetero", "heterophony", "doubles, a little late"], ["motion", "motion rule", "contrary, 3rds, 6ths"], ["unison", "unison", "voices meet"], ["density", "shared density", "a cap a bar, by weight"]];
  var RHEAD = { canon: "canon", call: "answer", hocket: "hocket", hetero: "hetero", motion: "motion", unison: "unison", density: "density" };
  function relCol(j) { return "var(--rel-" + (j % 6 + 1) + ")"; }
  function vn(x) { return "voice " + (x + 1); }
  function barsWord(d) { return d === 16 ? "1 bar" : d === 8 ? "½ bar" : d === 4 ? "¼ bar" : d === 12 ? "¾ bar" : d + "/16"; }
  function zoneIdsAt(at) { return at.filter(function (b) { return b.kind === "zone"; }).map(function (b) { return b.id; }); }
  /* the relation that drives voice v (0..2) at chord i, as the model's cell reads it (the last one wins), or -1 */
  function drivenBy(i, v, at) {
    var hit = -1;
    M.relationsAt(comp, i, zoneIdsAt(at)).forEach(function (j) { var r = comp.relations[j], both = r.type === "hocket" || r.type === "density";
      if (v === r.b || v === r.c || (both && v === r.a)) { hit = j; } });
    return hit;
  }
  /* per relation: the chords it is in force on, those where one of its voices is off or Free (it is silent there), dimmed when it is silent everywhere */
  function relInfo(bs, n) {
    var out = {};
    M.links(comp, n).forEach(function (l) {
      var r = comp.relations[l.i], vs = r.type === "density" ? [] : [r.a, r.b], x = { inforce: 0, off: [], voices: [], free: [], fv: [] };
      var need = r.type === "density" ? [0, 1, 2] : [r.a];   /* the voices it needs to have a line: a pair's leader, every member of a density */
      var bad = 0;
      for (var i = l.from; i <= l.to; i++) {
        var at = bs.filter(function (b) { return b.t0 < (i + 1) / n && b.t1 > i / n; });
        if (M.relationsAt(comp, i, zoneIdsAt(at)).indexOf(l.i) < 0) { continue; }
        x.inforce++;
        var c = M.cell(comp, i, at, n), gone = vs.filter(function (v) { return c[v].style === "off"; });
        if (gone.length) { x.off.push(i); gone.forEach(function (v) { if (x.voices.indexOf(v) < 0) { x.voices.push(v); } }); }
        /* a Free voice's notes never enter the ring the relations read, so a relation that needs its line does nothing there */
        var fr = need.filter(function (v) { return c[v].style === "free"; });
        if (r.type === "density" && fr.length < need.length) { fr = []; }   /* a density caps the composed voices; it is idle only when every voice is Free */
        if (fr.length) { x.free.push(i); fr.forEach(function (v) { if (x.fv.indexOf(v) < 0) { x.fv.push(v); } }); }
        if (gone.length || fr.length) { bad++; }
      }
      x.dim = !x.inforce || bad === x.inforce; out[l.i] = x;
    });
    return out;
  }
  function zoneName(id, zs) { return (zs.filter(function (z) { return z.id === id; })[0] || {}).name || "a zone that is gone"; }
  /* a chip's words: "canon · v1 → v3 · ½ bar · +2", short "canon · v1 → v3", and its span "9–12 · Gasometer" */
  function relText(r, l, zs) {
    var v = function (x) { return "v" + (x + 1); }, rest = l.label.split(" · ").slice(1).join(" · ");
    var pair = r.type === "density" ? "" : v(r.a) + (r.type === "hocket" ? " + " : " → ") + v(r.b);
    if (r.type === "density") { rest = r.cap + " a bar · " + r.weight.join("/"); }
    var head = RHEAD[r.type], span = (l.from + 1) + (l.to > l.from ? "–" + (l.to + 1) : "");
    return { long: [head, pair, rest].filter(Boolean).join(" · "), short: [head, pair || r.cap + " a bar"].join(" · "), span: span + (r.zone ? " · " + zoneName(r.zone, zs) : "") };
  }
  function relRow(n, zs) {
    var r = el("div", "cd-row cd-relrow"); r.id = "cd-relrow"; r.appendChild(el("span", "cd-name", "relations"));
    var w = el("div", "cd-relw"), run = el("div", "cd-relrun"), ls = M.links(comp, n);
    ls.forEach(function (l) {
      var rel = comp.relations[l.i], t = relText(rel, l, zs), x = ri[l.i];
      var b = el("button", "cd-rchip" + (sel.kind === "rel" && sel.i === l.i ? " on" : "") + (x.dim ? " dim" : ""));
      b.type = "button"; b.dataset.i = l.i; b.dataset.fk = "rel:" + l.i; b.style.setProperty("--rc", relCol(l.i));
      b.appendChild(el("i")); b.appendChild(el("b", "cd-wl", t.long)); b.appendChild(el("b", "cd-ws", t.short)); b.appendChild(el("b", "cd-wt", RHEAD[rel.type])); b.appendChild(el("small", null, t.span));
      b.title = t.long + " · chords " + t.span + (x.dim ? " · silent here" : "");
      b.setAttribute("aria-label", "relation " + (l.i + 1) + ", " + t.long.replace(/→/g, "to") + ", chords " + t.span + (x.dim ? ", silent here" : ""));
      b.addEventListener("click", function () { pickRel(l.i); }); run.appendChild(b); });
    if (!ls.length) { run.appendChild(el("span", "cd-rhint", "none yet · the voices can follow, answer and share a line")); }
    w.appendChild(run);
    var ad = el("button", "cd-addrel", "+ relation"); ad.type = "button"; ad.id = "cd-addrel"; ad.dataset.fk = "addrel"; ad.setAttribute("aria-haspopup", "menu"); ad.setAttribute("aria-expanded", String(relMenu));
    var full = (comp.relations || []).length >= 12; ad.disabled = full; ad.title = full ? "A route holds 12 relations" : "Add a relation between the voices";
    ad.addEventListener("click", function () { relMenu = !relMenu; want = [relMenu ? "#cd-relmenu button" : "#cd-addrel"]; render(); }); w.appendChild(ad);
    if (relMenu) {
      var mn = el("div", "cd-relmenu"); mn.id = "cd-relmenu"; mn.setAttribute("role", "menu"); mn.setAttribute("aria-label", "Add a relation");
      RTYPES.forEach(function (x) { var b = el("button"); b.type = "button"; b.setAttribute("role", "menuitem"); b.dataset.t = x[0];
        b.appendChild(el("b", null, x[1])); b.appendChild(el("small", null, x[2])); b.addEventListener("click", function () { addRel(x[0]); });
        if (x[0] === "density" && (comp.relations || []).some(function (q) { return q.type === "density"; })) { b.disabled = true; b.title = "A route has one shared density already: only the first acts"; }
        mn.appendChild(b); });
      w.appendChild(mn);
    }
    r.appendChild(w); return r;
  }
  /* the links, over the lanes: a pair is a line under the leader's cells across its span, curving down (or up) to the follower's lane with
     an arrowhead; a hocket a dashed outline around both lanes; density only its chip. Drawn from the cells' own boxes, after they settle. */
  function drawLinks(main, n, zs) {
    var lanes = main.querySelector("#cd-lanes"); if (!lanes) { return; }
    var NS = "http://www.w3.org/2000/svg", box = lanes.getBoundingClientRect(), rows = [].slice.call(lanes.querySelectorAll(".cd-lane")), sv = document.createElementNS(NS, "svg");
    sv.id = "cd-rels"; sv.setAttribute("width", box.width); sv.setAttribute("height", box.height); sv.setAttribute("role", "group"); sv.setAttribute("aria-label", "Relations");
    var mk = function (tag, cls, attrs) { var e = document.createElementNS(NS, tag); if (cls) { e.setAttribute("class", cls); } Object.keys(attrs || {}).forEach(function (k) { e.setAttribute(k, attrs[k]); }); return e; };
    M.links(comp, n).forEach(function (l) {
      if (l.type === "density") { return; }
      var A = rows[l.a] && rows[l.a].querySelectorAll(".cd-cell"), B = rows[l.b] && rows[l.b].querySelectorAll(".cd-cell");
      if (!A || !B || !A[l.from] || !A[l.to] || !B[l.from]) { return; }
      var a0 = A[l.from].getBoundingClientRect(), a1 = A[l.to].getBoundingClientRect(), b0 = B[l.from].getBoundingClientRect(), b1 = B[l.to].getBoundingClientRect(), d;
      var x0 = a0.left - box.left + 3, x1 = Math.max(x0 + 6, a1.right - box.left - 8), t = relText(comp.relations[l.i], l, zs), x = ri[l.i];
      var g = mk("g", "cd-link" + (sel.kind === "rel" && sel.i === l.i ? " on" : "") + (x.dim ? " dim" : ""), { tabindex: "0", role: "button", "data-i": l.i, "data-fk": "link:" + l.i,
        "aria-label": "relation " + (l.i + 1) + ", " + t.long.replace(/→/g, "to") + ", chords " + t.span });
      g.style.setProperty("--rc", relCol(l.i));
      var tt = mk("title"); tt.textContent = t.long + " · chords " + t.span; g.appendChild(tt);
      if (l.type === "hocket") {
        var lo = Math.min(l.a, l.b), hi = Math.max(l.a, l.b), top = rows[lo].querySelectorAll(".cd-cell")[l.from].getBoundingClientRect().top, bot = rows[hi].querySelectorAll(".cd-cell")[l.from].getBoundingClientRect().bottom;
        d = "M" + x0 + "," + (top - box.top - 2) + " H" + (a1.right - box.left - 3) + " V" + (bot - box.top + 2) + " H" + x0 + " Z";
        g.appendChild(mk("path", "hit", { d: d })); g.appendChild(mk("path", "vis", { d: d, "stroke-dasharray": "4 3" }));
      } else {
        var dn = l.a < l.b, ya = (dn ? a0.bottom : a0.top) - box.top, yb = (dn ? b0.top : b0.bottom) - box.top, dir = dn ? 1 : -1;
        d = "M" + x0 + "," + ya + " H" + x1 + " C" + (x1 + 8) + "," + ya + " " + (x1 + 8) + "," + yb + " " + x1 + "," + yb;
        g.appendChild(mk("path", "hit", { d: d })); g.appendChild(mk("path", "vis", { d: d }));
        g.appendChild(mk("path", "arw", { d: "M" + x1 + "," + yb + " l-4," + (-6 * dir) + " h8 z" }));
      }
      g.addEventListener("click", function () { pickRel(l.i); }); sv.appendChild(g);
    });
    lanes.appendChild(sv);
  }
  function pickRel(i) {
    var r = (comp.relations || [])[i]; sel = { kind: "rel", i: i }; gen = false; relMenu = false;
    ctx.light(r ? (r.zone ? { zone: r.zone } : { chord: r.from }) : null); render();
  }
  function addRel(type) {
    relMenu = false;
    var hasLine = function (v) { var c = comp[VS[v]]; return c.role === "melody" || (c.role !== "off" && c.style !== "free"); };   /* the leader: the first voice with a line */
    var a = [0, 1, 2].filter(hasLine)[0]; if (a === undefined) { a = 0; }
    var i = M.addRelation(comp, type, a, [0, 1, 2].filter(function (v) { return v !== a; })[0]);
    if (i < 0) { render(); return; }
    sel = { kind: "rel", i: i }; gen = false; ctx.light({ chord: 0 }); want = ['.cd-rchip[data-i="' + i + '"]']; commit(true);
  }
  /* the one-click fix for a relation whose voice is Free: the voice's route style becomes arpeggio (and any Free style over it is dropped) - one undo step */
  function giveStyle(v) {
    var k = VS[v]; comp[k].style = "arpeggio";
    ["chord", "section", "zone"].forEach(function (L) { Object.keys(comp.over[L] || {}).forEach(function (id) { var o = comp.over[L][id][k]; if (o && o.style === "free") { delete o.style; } }); });
    commit(true);
  }
  /* a relation removed: focus on the chip now in its place, the one before, or + relation */
  function removeRel(k) {
    var was = sel.kind === "rel" ? sel.i : -1; M.removeRelation(comp, k);
    if (was === k) { sel = { kind: "route" }; ctx.light(null); } else if (was > k) { sel = { kind: "rel", i: was - 1 }; }
    want = ['.cd-rchip[data-i="' + k + '"]', '.cd-rchip[data-i="' + (k - 1) + '"]', "#cd-addrel"]; commit(true);
  }
  /* the relation's inspector: leader and follower, the type's own settings, its span and zone, what plays here */
  function relPanel(w, bs, n) {
    var j = sel.i, r = comp.relations[j], T = M.RELATIONS[r.type], zs = ctx.zones(), lo = Math.min(r.to, n - 1), x = ri[j] || { off: [], voices: [], free: [], fv: [], inforce: 1, dim: false };
    var RF = function (k) { return T.fields.filter(function (f) { return f.k === k; })[0]; }, F = function (f, o) { return Object.assign({}, f, o || {}); };
    var ih = el("div", "cd-ih"); ih.dataset.fk = "ih";
    var h = el("h3", null, T.label + (r.type === "density" ? "" : " · " + vn(r.a) + (r.type === "hocket" ? " + " : " → ") + vn(r.b))); h.id = "cd-insp-title"; ih.appendChild(h);
    var rm = el("button", "ghost cd-clear", "remove"); rm.type = "button"; rm.title = "Remove this relation (Ctrl+Z takes it back)"; rm.addEventListener("click", function () { removeRel(j); });
    ih.appendChild(rm); w.appendChild(ih);
    w.appendChild(el("p", "cd-isub", (r.from === 0 && lo === n - 1 ? "the whole route" : lo > r.from ? "chords " + (r.from + 1) + "–" + (lo + 1) : "chord " + (r.from + 1)) +
      (r.zone ? " · " + zoneName(r.zone, zs) : "") + " · " + RTYPES.filter(function (t) { return t[0] === r.type; })[0][2]));
    var set = function (k, conv) { return function (v) { if (M.setRelation(comp, j, k, conv ? conv(v) : v)) { commit(true); } else { render(); } }; }, num = function (v) { return +v; };
    var own = function (k, def, v) { return v !== def ? { src: "here", reset: function () { set(k)(def); } } : { src: "relation" }; }, HERE = { src: "here" };
    var wideRow = function (k, label, opts, v, fn) {
      var q = el("div", "cd-row2 wide mine"); q.dataset.k = k; q.dataset.fk = "row:" + k; q.appendChild(el("span", "cd-k", label));
      var c = el("span", "cd-ctl"); c.appendChild(seg(opts, v, fn, label)); q.appendChild(c); w.appendChild(q); };
    var vopts = [0, 1, 2].map(function (v) { return { id: v, label: vn(v) }; });
    var spanRows = function () {
      var cs = function (a, z) { var o = []; for (var i = a; i <= z; i++) { o.push({ id: i, label: "chord " + (i + 1) }); } return o; };
      w.appendChild(row({ k: "from", label: "from", kind: "menu", options: cs(0, lo) }, r.from, "", set("from", num), r.from ? own("from", 0, r.from) : HERE));
      w.appendChild(row({ k: "to", label: "to", kind: "menu", options: cs(r.from, n - 1) }, lo, "", set("to", num), r.to >= n - 1 ? { src: "relation" } : { src: "here", reset: function () { set("to")(63); } }));
      var zo = [{ id: "", label: "everywhere" }].concat(zs.map(function (z) { return { id: z.id, label: z.name }; }));
      if (r.zone && !zs.some(function (z) { return z.id === r.zone; })) { zo.push({ id: r.zone, label: zoneName(r.zone, zs) }); }
      w.appendChild(row({ k: "zone", label: "zone", kind: "menu", options: zo }, r.zone || "", "", set("zone"), r.zone ? { src: "here", reset: function () { set("zone")(""); } } : { src: "relation" }));
    };
    if (r.type !== "density") {
      w.appendChild(row({ k: "a", label: r.type === "hocket" ? "voice" : "leader", kind: "menu", options: vopts }, r.a, "", set("a", num), HERE));
      w.appendChild(row({ k: "b", label: r.type === "hocket" ? "with" : "follower", kind: "menu", options: vopts }, r.b, "", set("b", num), HERE));
    }
    if (r.type === "hocket") {
      wideRow("split", "split", [["alt", "alternate"], ["every", "every " + r.every], ["mask", "pattern"]], r.split, set("split"));
      if (r.split === "every") { w.appendChild(row(RF("every"), r.every, "", set("every", num), own("every", 2, r.every))); }
      if (r.split === "mask") {
        var mg = el("div", "cd-xmask"); mg.dataset.fk = "xmask"; mg.setAttribute("role", "group"); mg.setAttribute("aria-label", "Pattern"); mg.appendChild(el("span", "cd-gk", "mask"));
        for (var c = 0; c < 16; c++) { (function (c) { var on1 = r.mask.charAt(c) === "x", b = el("button", "cd-xm" + (c % 4 === 0 ? " beat" : "") + (on1 ? " on" : ""), on1 ? "x" : "·");
          b.type = "button"; b.dataset.i = c; b.setAttribute("aria-pressed", String(on1)); b.setAttribute("aria-label", "step " + (c + 1) + ", " + vn(on1 ? r.a : r.b));
          b.addEventListener("click", function () { set("mask")(r.mask.slice(0, c) + (on1 ? "." : "x") + r.mask.slice(c + 1)); }); mg.appendChild(b); })(c); }
        w.appendChild(mg); w.appendChild(el("p", "cd-isub wrap", "click the sixteenths: x for " + vn(r.a) + ", · for " + vn(r.b)));
      }
      spanRows();
    } else if (r.type === "motion") {
      wideRow("motion", "motion", [["contrary", "contrary"], ["par3", "3rds"], ["par6", "6ths"], ["free", "free"]], r.motion, set("motion"));
      w.appendChild(row(RF("strength"), r.strength, "", set("strength", num), own("strength", 1, r.strength)));
      spanRows();
      w.appendChild(el("p", "cd-isub wrap", "strength " + Math.round(r.strength * 100) + " %: " + (r.strength >= 1 ? "always" : "some of the time, the same every walk")));
    } else if (r.type === "density") {
      w.appendChild(row(F(RF("cap"), { fmt: "perbar" }), r.cap, "", set("cap", num), own("cap", 8, r.cap)));
      spanRows();
      w.appendChild(el("p", "cd-ieb", "weights"));
      [0, 1, 2].forEach(function (v) {
        var wset = function (val) { var a = r.weight.slice(); a[v] = +val; set("weight")(a); };
        w.appendChild(row({ k: "w" + v, label: vn(v), kind: "knob", min: 0, max: 4, step: 1, def: 1 }, r.weight[v], "", wset, r.weight[v] !== 1 ? { src: "here", reset: function () { wset(1); } } : { src: "relation" })); });
      var sum = r.weight[0] + r.weight[1] + r.weight[2];
      w.appendChild(el("p", "cd-isub", sum ? "a bar: " + [0, 1, 2].map(function (v) { return "v" + (v + 1) + " " + Math.round(r.cap * r.weight[v] / sum); }).join(" · ") : "every weight is 0: no voice plays"));
    } else {
      T.fields.forEach(function (f) {
        var g = f.k === "trans" ? F(f, { label: "move by", fmt: "sgn" }) : f.k === "delay" ? F(f, { fmt: "rbar" }) : f;
        w.appendChild(row(g, r[f.k], "", set(f.k, f.kind === "knob" ? num : null), own(f.k, f.def, r[f.k]))); });
      spanRows();
    }
    if (r.type === "canon" && ctx.chords[r.from]) {
      w.appendChild(el("p", "cd-ieb", "plays here"));
      var lead = comp[VS[r.a]].role === "melody" && motif().notes.length ? plays(r.from).join(" ") : vn(r.a) + "’s line";
      w.appendChild(el("p", "cd-isub wrap", "over " + ctx.chords[r.from].label + ": " + lead + " → " + vn(r.b) + ", " + barsWord(r.delay) + " later" + (r.trans ? ", " + fmt({ fmt: "sgn" }, r.trans) : "") + (r.transform !== "none" ? ", " + r.transform : "")));
    }
    if (!x.inforce) { w.appendChild(el("p", "cd-isub wrap", "its zone is not on this route, so this " + RHEAD[r.type] + " plays nowhere here.")); }
    else if (x.off.length) {
      var vs = x.voices.map(vn).join(" and "), vw = x.voices.length > 1 ? " are off " : " is off ";
      w.appendChild(el("p", "cd-isub wrap", vs + vw + (x.dim ? "here" : "on some of these chords") + ", so this " + RHEAD[r.type] + " is silent " + (x.dim ? "on these chords" : "there") + ". Turn " + (x.voices.length > 1 ? "them" : vn(x.voices[0])) + " on, or move the " + RHEAD[r.type] + "."));
    }
    if (x.inforce && x.fv.length) {
      var VERB = { canon: "follow", hetero: "follow", motion: "follow", unison: "follow", call: "answer", hocket: "share", density: "count" };
      w.appendChild(el("p", "cd-isub wrap", x.fv.map(vn).join(" and ") + (x.fv.length > 1 ? " are" : " is") + " Free, so there is nothing to " + VERB[r.type] + " — give " + (x.fv.length > 1 ? "them" : "it") + " a style"));
      x.fv.forEach(function (v) {
        var fx = el("button", "ghost cd-fixstyle", "give " + vn(v) + " a style"); fx.type = "button"; fx.dataset.v = v;
        fx.title = "Set " + vn(v) + "’s style to arpeggio (Ctrl+Z takes it back)"; fx.addEventListener("click", function () { giveStyle(v); }); w.appendChild(fx); });
    }
    var tools = el("div", "cd-itools"); tools.dataset.fk = "tools";
    var li = el("button", "cd-listen", "Listen here"); li.type = "button"; li.title = "Put the walker on this relation's chords";
    li.addEventListener("click", function () { listen((r.from + 0.5) / n); }); tools.appendChild(li);
    var sm = el("button", null, "Show on map"); sm.type = "button"; sm.title = "Bring the relation's stretch (or its zone) into view on the map";
    sm.addEventListener("click", function () { ctx.show(r.zone ? { zone: r.zone } : { chord: r.from }); }); tools.appendChild(sm);
    w.appendChild(tools);
  }
  document.addEventListener("click", function (e) { if (relMenu && ctx && !on(e.target, "#cd-relmenu, #cd-addrel")) { relMenu = false; render(); } });
  window.addEventListener("resize", function () { if (ctx) { clearTimeout(rzT); rzT = setTimeout(function () { if (ctx) { render(); } }, 120); } });
  /* a zone picked on the map while composing (the page's own selection path calls this) */
  function pickZone(id, name) {
    if (!ctx || !ctx.chords.length) { return false; }
    var z = (ctx.zones() || []).filter(function (x) { return x.id === id; })[0];
    pick({ kind: "zone", id: id, name: (z && z.name) || name || "zone" }); return true;
  }
  window.FsComposeDock = { open: open, close: close, pickZone: pickZone, isOpen: function () { return !!ctx; } };
})();
