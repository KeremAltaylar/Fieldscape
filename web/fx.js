/* The route's master FX on the setter's patch screen (spec docs/superpowers/specs/2026-11-17-modulation-fx-design.md, B; Part 1
   Task 11), built to the mock Kerem approved (design/fx/master-mock.html, 2026-11-19):
     FsFx.mount(host, o)    the patch column: return, the three sends, the nine mixes in signal order, the load, Open
                            (o.compact, under five columns: the return, what is on, the load, Open)
     FsFx.editor(host, o)   the FX editor that Open swaps in for the patch body (a toggle, C-8)
     FsFx.scope(canvas, s)  one LFO's scope: { shape, phase, value, state } (state: { colour, half, last }, kept by the caller)
   o: { kind: "route", get() -> stored mfx, set(cleaned mfx), live() -> the engine's Float32Array(32) or null, tempo() -> bpm,
        sound() -> sound on, voices() -> a voice on, chord() -> { labels, i } or null, onOpen, onBack, onSound }
   Every edit is written through FsFxModel.clean. The live array (core/modfx.hpp Live): 0-3 LFO phase, 4-7 LFO value, 8 env,
   12 chord, 13-21 module levels, 22 in, 23 out, 24-31 each slot's destination now (0-1). Without it (sound off, or the walk
   is not on this route) the scopes run on a preview clock at the route's tempo and the meters wait. */
(function () {
  var M = window.FsFxModel, NS = "http://www.w3.org/2000/svg";
  var BY = {}; M.MODULES.forEach(function (m) { m.params.forEach(function (p) { BY[p.key] = p; }); });
  var DIVS = M.DIVISIONS.slice().sort(function (a, b) { return a.beats - b.beats; });
  var SRC = {}; M.SOURCES.forEach(function (s) { SRC[s.id] = s; });
  /* % of one core per module, all controls up (Task 7's bench, wasm -O1 in Node, 128-frame blocks): the load is their sum
     over the modules that are on - the engine has no per-module meter of its own yet */
  var COST = { shift: 0.37, swarm: 0.46, phase: 0.63, comb: 0.45, vowel: 0.34, cloud: 0.50, tape: 0.62, echo: 0.67, space: 1.20 };
  var BUDGET = 8, BAR_MAX = 12;
  var sel = "echo";   /* the module the editor shows, kept while the panel re-renders */
  var REDUCE = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

  function el(tag, cls, html) { var e = document.createElement(tag); if (cls) { e.className = cls; } if (html !== undefined) { e.innerHTML = html; } return e; }
  function clamp01(x) { return Math.max(0, Math.min(1, x)); }
  function arc(c, r, a0, a1) {   /* degrees, 0 = up, clockwise */
    var p = function (a) { var q = (a - 90) * Math.PI / 180; return [c + r * Math.cos(q), c + r * Math.sin(q)]; };
    var s = p(a0), e = p(a1); return "M" + s[0].toFixed(2) + " " + s[1].toFixed(2) + " A" + r + " " + r + " 0 " + (a1 - a0 > 180 ? 1 : 0) + " 1 " + e[0].toFixed(2) + " " + e[1].toFixed(2);
  }
  function smallKnob(x) {   /* web/knobs.js's 24 px knob */
    var a = -135 + 270 * x;
    return '<svg viewBox="0 0 36 36" aria-hidden="true"><path class="fx-kt" stroke-width="3.5" d="' + arc(18, 15, -135, 135) + '"/>' +
      (x > 0.002 ? '<path class="fx-kf" stroke-width="3.5" d="' + arc(18, 15, -135, a) + '"/>' : "") +
      '<line class="fx-kk" stroke-width="2" x1="18" y1="18" x2="18" y2="6" transform="rotate(' + a.toFixed(1) + ' 18 18)"/></svg>';
  }

  /* a parameter on its knob: its taper */
  function norm(p, v) {
    if (p.kind === "division") { var i = DIVS.findIndex(function (d) { return d.id === v; }); return i / (DIVS.length - 1); }
    if (p.taper === "exp") { return Math.log(v / p.min) / Math.log(p.max / p.min); }
    if (p.taper === "cubic") { return 0.5 + 0.5 * Math.cbrt(v / p.max); }
    return (v - p.min) / (p.max - p.min);
  }
  function denorm(p, x) {
    x = clamp01(x);
    if (p.kind === "division") { return DIVS[Math.round(x * (DIVS.length - 1))].id; }
    if (p.taper === "exp") { return p.min * Math.pow(p.max / p.min, x); }
    if (p.taper === "cubic") { return p.max * Math.pow(2 * x - 1, 3); }
    return p.min + x * (p.max - p.min);
  }
  var ENV = { attack: [1, 200, 10, "exp"], release: [20, 2000, 300, "exp"], sens: [0, 4, 1, "lin"] };
  function envNorm(k, v) { var e = ENV[k]; return e[3] === "exp" ? Math.log(v / e[0]) / Math.log(e[1] / e[0]) : (v - e[0]) / (e[1] - e[0]); }
  function envDenorm(k, x) { var e = ENV[k]; x = clamp01(x); return e[3] === "exp" ? Math.round(e[0] * Math.pow(e[1] / e[0], x)) : Math.round((e[0] + x * (e[1] - e[0])) * 10) / 10; }
  function envFmt(k, v) { return k === "sens" ? "×" + v.toFixed(1) : v + " ms"; }

  /* the stored mfx (clean, sparse) as a full working copy: defaults with the stored values over them; slots kept positional */
  function full(stored, kind) {
    var d = M.defaults(kind || "route"), c = M.clean(stored);
    Object.keys(c).forEach(function (k) {
      if (k === "lfo") { c.lfo.forEach(function (l, i) { Object.assign(d.lfo[i], l); }); }
      else if (k === "mods") { d.mods = c.mods.map(function (x) { return Object.assign({}, x); }); }
      else if (k === "send") { d.send = c.send.slice(); }
      else if (c[k] && typeof c[k] === "object") { Object.assign(d[k], c[k]); }
      else { d[k] = c[k]; }
    });
    while (d.mods.length < 8) { d.mods.push({}); }
    return d;
  }
  function mixOn(fx, id) { return M.get(fx, id + ".mix") > 0; }
  function loadOf(fx) {   /* a module costs while its mix is up, or a slot can raise it */
    return M.MODULES.reduce(function (a, m) {
      var on = mixOn(fx, m.id) || (fx.mods || []).some(function (x) { return x.src && x.dst === m.id + ".mix" && x.amt > 0; });
      return a + (on ? COST[m.id] : 0);
    }, 0);
  }
  function lvl01(x) { return Math.max(0, 1 + Math.log10(Math.max(x || 0, 1e-6)) / 3); }   /* -60..0 dBFS -> 0..1, as the core's env */

  /* drag (shift fine), double-click resets, arrows a step, Page Up/Down ten, Home/End the ends (C-12, as web/knobs.js) */
  function bindKnob(k, d) {
    var drag = null;
    function show() { var x = d.x(); k.setAttribute("aria-valuenow", x.toFixed(4)); k.setAttribute("aria-valuetext", d.text()); }
    k.tabIndex = 0; k.setAttribute("role", "slider"); k.setAttribute("aria-valuemin", "0"); k.setAttribute("aria-valuemax", "1");
    k.title = "Drag · shift for fine · double-click resets"; show();
    k.addEventListener("pointerdown", function (e) {
      e.preventDefault(); k.focus(); drag = { y: e.clientY, x: d.x(), id: e.pointerId }; k.classList.add("on");
      try { k.setPointerCapture(e.pointerId); } catch (er) { /* a pointer it does not know */ }
    });
    k.addEventListener("pointermove", function (e) {
      if (!drag || e.pointerId !== drag.id) { return; }
      d.set(clamp01(drag.x + (drag.y - e.clientY) / (e.shiftKey ? 1000 : 200))); show();
    });
    var end = function () { drag = null; k.classList.remove("on"); };
    k.addEventListener("pointerup", end); k.addEventListener("pointercancel", end);
    k.addEventListener("dblclick", function (e) { e.preventDefault(); d.reset(); show(); });
    k.addEventListener("keydown", function (e) {
      var x = d.x(), s = (e.shiftKey ? 0.002 : 0.01) * (d.mul || 1);
      var to = { ArrowUp: x + s, ArrowRight: x + s, ArrowDown: x - s, ArrowLeft: x - s, PageUp: x + 0.1, PageDown: x - 0.1, Home: 0, End: 1 }[e.key];
      if (to === undefined) { return; }
      e.preventDefault(); d.set(clamp01(to)); show();
    });
    return show;
  }

  /* ================= the patch column ================= */
  function mount(host, o) {
    var fx = full(o.get(), o.kind), rows = {};
    function row(key, label, lamp) {
      var r = el("label", "pprow"), v = M.get(fx, key), isMix = /\.mix$/.test(key);
      r.innerHTML = "<span>" + (lamp ? '<u class="fx-lmp"></u>' : "") + label + "</span>";
      var inp = el("input"); inp.type = "range"; inp.min = 0; inp.max = 1; inp.step = 0.01; inp.value = v; inp.dataset.k = "mfx." + key;
      inp.dataset.def = isMix ? 0 : M.get({}, key); r.appendChild(inp);
      var i = el("i"); r.appendChild(i);
      var show = function () {
        var x = M.get(fx, key); i.textContent = M.fmt(key, x);
        if (lamp) { r.classList.toggle("off", !x); r.querySelector(".fx-lmp").classList.toggle("lit", !!x); }
      };
      inp.addEventListener("input", function () { M.set(fx, key, +inp.value); show(); load(); o.set(M.clean(fx)); });
      show(); rows[key] = r; return r;
    }
    host.appendChild(row("ret", "return"));
    if (o.compact) {   /* under five columns: the return, what is on, the load - the sends and mixes are in the editor */
      host.appendChild(el("div", "fx-loadrow fx-sumrow", "<span>chain</span><em></em>"));
      host.lastChild.querySelector("em").textContent = M.summary(fx);
    } else {
      [["send1", "voice send"], ["send2", "second voice send"], ["send3", "third voice send"]].forEach(function (s) { host.appendChild(row(s[0], s[1])); });
      var chain = el("div", "fx-chain"); chain.setAttribute("aria-label", "The chain, in signal order");
      M.MODULES.forEach(function (m) { var r = row(m.id + ".mix", m.label, true); r.title = m.note; chain.appendChild(r); });
      host.appendChild(chain);
    }
    var lr = el("div", "fx-loadrow", '<span>load, est.</span><span class="fx-bar" title="estimated from the bench (not measured), of one core; the mark is the ' + BUDGET + ' % budget"><b></b><s style="left:' + (BUDGET / BAR_MAX * 100) + '%"></s></span><i></i>');
    host.appendChild(lr);
    function load() {
      var l = loadOf(fx); lr.querySelector("b").style.width = Math.min(100, l / BAR_MAX * 100) + "%"; lr.querySelector("i").textContent = "≈ " + l.toFixed(1) + " %";
      lr.classList.toggle("hot", l > BUDGET);
    }
    load();
    var b = el("button", "fx-open", "Open the FX editor"); b.type = "button"; if (o.openId) { b.id = o.openId; }
    b.addEventListener("click", function () { if (o.onOpen) { o.onOpen(); } });
    host.appendChild(b);
  }

  /* ================= drawing ================= */
  var C = {}, MONO = "";
  function colours() {
    var probe = el("i"); document.body.appendChild(probe);
    ["--sunk", "--ground", "--panel", "--raised", "--hairline", "--ink", "--dim", "--faint", "--accent", "--lamp",
      "--src-lfo1", "--src-lfo2", "--src-lfo3", "--src-lfo4", "--src-env", "--src-chord"].forEach(function (n) { probe.style.color = "var(" + n + ")"; C[n.slice(2)] = getComputedStyle(probe).color; });
    probe.remove();
    var css = getComputedStyle(document.documentElement);
    MONO = parseFloat(css.fontSize) * parseFloat(css.getPropertyValue("--t-xs")) + "px " + css.getPropertyValue("--font-mono");
  }
  function alpha(c, a) {   /* oklch(...) / rgb(...) -> the same colour at alpha a */
    if (/^rgb\(/.test(c)) { return c.replace(/^rgb\((.*)\)$/, "rgba($1, " + a + ")"); }
    return c.replace(/\)$/, " / " + a + ")");
  }
  function ctx2d(cv) {
    var r = cv.getBoundingClientRect(), d = window.devicePixelRatio || 1, w = Math.max(1, Math.round(r.width * d)), h = Math.max(1, Math.round(r.height * d));
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
    var g = cv.getContext("2d"); g.setTransform(d, 0, 0, d, 0, 0); g.clearRect(0, 0, r.width, r.height); g.W = r.width; g.H = r.height; return g;
  }
  function rnd(n) { var x = Math.sin(n * 12.9898 + 78.233) * 43758.5453; return x - Math.floor(x); }
  function shapeAt(shape, c) {   /* -1..1 at c cycles (the scope's picture; the engine's steps and drift are its own) */
    var f = c - Math.floor(c), n = Math.floor(c);
    switch (shape) {
      case "sine": return Math.sin(2 * Math.PI * f);
      case "tri": return f < 0.5 ? -1 + 4 * f : 3 - 4 * f;
      case "up": return -1 + 2 * f;
      case "down": return 1 - 2 * f;
      case "square": return f < 0.5 ? 1 : -1;
      case "steps": return rnd(n) * 2 - 1;
      default: var s = f * f * (3 - 2 * f); return (rnd(n) * (1 - s) + rnd(n + 1) * s) * 2 - 1;   /* drift */
    }
  }
  var SHAPE_PATH = { sine: "M1 5 C4 -2 6 -2 9 5 S14 12 17 5", tri: "M1 9 L5 1 L13 9 L17 1", up: "M1 9 L9 1 L9 9 L17 1", down: "M1 1 L9 9 L9 1 L17 9",
    square: "M1 9 L1 1 L9 1 L9 9 L17 9 L17 1", steps: "M1 7 L5 7 L5 2 L9 2 L9 9 L13 9 L13 4 L17 4", drift: "M1 6 C4 1 6 2 8 5 S13 10 17 3" };

  /* one LFO's scope: two cycles, the playhead at the phase (the half flips each time the phase wraps), the dot at the value */
  function scope(cv, s) {
    if (!MONO) { colours(); }
    var st = s.state || {}, g = ctx2d(cv), W = g.W, H = g.H, col = st.colour || C.accent, ph = s.phase - Math.floor(s.phase);
    if (st.last !== undefined && ph < st.last - 0.5) { st.half = st.half ? 0 : 1; }
    st.last = ph;
    var half = st.half || 0, y = function (v) { return H / 2 - v * (H / 2 - 5); }, x, v;
    g.strokeStyle = C.hairline; g.lineWidth = 1; g.beginPath(); g.moveTo(0, H / 2); g.lineTo(W, H / 2); g.stroke();
    g.setLineDash([2, 3]); g.beginPath(); g.moveTo(W / 2, 2); g.lineTo(W / 2, H - 2); g.stroke(); g.setLineDash([]);
    var path = function () { g.beginPath(); for (x = 0; x <= W; x++) { v = shapeAt(s.shape, 2 * x / W - 1e-9 * (x === W)); if (x) { g.lineTo(x, y(v)); } else { g.moveTo(x, y(v)); } } };
    path(); g.lineTo(W, H / 2); g.lineTo(0, H / 2); g.closePath(); g.fillStyle = alpha(col, 0.12); g.fill();
    path(); g.strokeStyle = col; g.lineWidth = 1.6; g.stroke();
    var px = (half + ph) / 2 * W;
    g.fillStyle = alpha(C.ink, 0.07); g.fillRect(0, 0, px, H);
    g.strokeStyle = C.ink; g.lineWidth = 1; g.beginPath(); g.moveTo(px, 0); g.lineTo(px, H); g.stroke();
    g.fillStyle = col; g.beginPath(); g.arc(px, y(s.value), 4, 0, 7); g.fill(); g.strokeStyle = C.sunk; g.lineWidth = 1.5; g.stroke();
    g.fillStyle = C.dim; g.font = MONO; g.textBaseline = "top"; g.textAlign = "right";
    g.fillText((s.value >= 0 ? "+" : "−") + Math.abs(s.value).toFixed(2), W - 3, 3);
    cv.dataset.phase = ph.toFixed(4);
  }

  /* ================= the editor ================= */
  function editor(host, o) {
    if (!MONO) { colours(); }
    var fx = full(o.get(), o.kind), raf = 0, L = null, t0 = performance.now(), lastDraw = 0, note = null;
    var lfoState = [0, 1, 2, 3].map(function (i) { return { colour: C["src-lfo" + (i + 1)] }; }), envHist = [], chordHist = [];
    var bpm = function () { return o.tempo && o.tempo() || 72; };
    var saved = JSON.stringify(M.clean(o.get())), lastSync = 0;
    function commit() { var c = M.clean(fx); saved = JSON.stringify(c); o.set(c); }
    function slotsOn(key) { var out = []; fx.mods.forEach(function (m, i) { if (m.src && m.dst === key) { out.push(i); } }); return out; }
    function now() { return (performance.now() - t0) / 1000; }
    function srcVal(src) {
      if (L) { return src === "env" ? L[8] : src === "chord" ? L[12] : L[4 + (+src[3] - 1)]; }
      if (src === "env" || src === "chord") { return 0; }
      var l = fx.lfo[+src[3] - 1]; return shapeAt(l.shape, now() / M.divSeconds(l.rate, bpm()) + (l.phase || 0));
    }
    function liveX(key) {   /* the destination now, 0-1: the engine's own when it plays this route, else the stored value + the slots */
      var s = slotsOn(key), x = norm(BY[key], M.get(fx, key));
      if (L && s.length) { return clamp01(L[24 + s[s.length - 1]]); }
      s.forEach(function (i) { x += fx.mods[i].amt * srcVal(fx.mods[i].src); });
      return clamp01(x);
    }

    host.textContent = "";
    var ed = el("div", "fx-ed"); host.appendChild(ed);
    var strip = el("div", "fx-strip"); ed.appendChild(strip);
    strip.appendChild(el("div", "fx-io", '<canvas data-io="in" aria-label="Into the bus"></canvas><span>in</span>'));
    var mods = el("div", "fx-mods"); mods.setAttribute("role", "tablist"); mods.setAttribute("aria-label", "Modules, in signal order"); strip.appendChild(mods);
    strip.appendChild(el("div", "fx-io", '<canvas data-io="out" aria-label="Out of the bus"></canvas><span>out</span>'));
    var ld = el("div", "fx-load"); strip.appendChild(ld);
    var nt = el("div", "fx-notice"); nt.hidden = true; ed.appendChild(nt);
    var card = el("section", "fx-card fx-left fx-r1"); ed.appendChild(card);
    var right = el("section", "fx-card fx-right fx-r1"); ed.appendChild(right);
    var mx = el("section", "fx-card fx-left fx-r2"); ed.appendChild(mx);
    var side = el("div", "fx-right fx-r2 fx-side"); ed.appendChild(side);

    /* the tablist's keys: left/right move to the next module (wrapping), Home/End to the ends - and select it */
    mods.addEventListener("keydown", function (e) {
      var ts = [].slice.call(mods.children), i = ts.indexOf(document.activeElement), k = { ArrowLeft: -1, ArrowRight: 1 }[e.key];
      var j = i < 0 ? -1 : e.key === "Home" ? 0 : e.key === "End" ? ts.length - 1 : k ? (i + k + ts.length) % ts.length : -1;
      if (j < 0) { return; }
      e.preventDefault(); var id = ts[j].dataset.m; ts[j].click();   /* the click can rebuild the editor: focus the tile found anew */
      var t = host.querySelector('.fx-tile[data-m="' + id + '"]'); if (t) { t.focus(); }
    });
    function tiles() {
      mods.textContent = "";
      M.MODULES.forEach(function (m) {
        var v = M.get(fx, m.id + ".mix"), srcs = [];
        fx.mods.forEach(function (x) { if (x.src && x.dst && x.dst.split(".")[0] === m.id && srcs.indexOf(x.src) < 0) { srcs.push(x.src); } });
        var t = el("button", "fx-tile" + (v ? "" : " off"), '<u class="fx-lmp' + (v ? " lit" : "") + '"></u><span class="fx-nm">' + m.label + '</span>' +
          '<span class="fx-mx"><span class="fx-bar"><b style="width:' + v * 100 + '%"></b></span><i>' + M.fmt(m.id + ".mix", v) + '</i></span>' +
          (srcs.length ? '<span class="fx-dots">' + srcs.map(function (s) { return '<u style="background:var(--src-' + s + ')" title="' + SRC[s].label + '"></u>'; }).join("") + '</span>' : ""));
        t.type = "button"; t.dataset.m = m.id; t.setAttribute("role", "tab"); t.setAttribute("aria-selected", String(m.id === sel)); t.title = m.note;
        t.addEventListener("click", function () { sel = m.id; [].forEach.call(mods.children, function (c) { c.setAttribute("aria-selected", String(c === t)); }); moduleCard(); });
        mods.appendChild(t);
      });
    }
    function loadBox() {
      var l = loadOf(fx), hot = l > BUDGET;
      ld.className = "fx-load" + (hot ? " hot" : "");
      ld.innerHTML = '<div class="fx-lt" title="estimated from the bench, not measured"><span>load</span><em>≈ ' + l.toFixed(1) + ' % est.</em></div>' +
        '<span class="fx-bar"><b style="width:' + Math.min(100, l / BAR_MAX * 100) + '%"></b><s style="left:' + (BUDGET / BAR_MAX * 100) + '%" title="the budget, ' + BUDGET + ' % of a core"></s></span>' +
        '<span class="fx-lb">' + (l === 0 ? "idle: nothing is on" : "of one core · budget " + BUDGET + " %") + '</span>';
    }

    /* the one message the editor has, if any: sound off, not on this route, no voice, no return, over budget */
    function stateNow() {
      if (!L) { return o.sound && o.sound() ? "away" : "off"; }
      if (o.voices && !o.voices()) { return "novoice"; }
      if (!fx.ret && M.MODULES.some(function (m) { return mixOn(fx, m.id); })) { return "noreturn"; }
      if (loadOf(fx) > BUDGET) { return "over"; }
      return "";
    }
    function setNotice(s) {
      if (s === note) { return; }
      note = s;
      var costly = M.MODULES.filter(function (m) { return mixOn(fx, m.id); }).sort(function (a, b) { return COST[b.id] - COST[a.id]; }).slice(0, 3).map(function (m) { return m.label; });
      var N = {
        off: ["", "Preview", "Sound is off. The scopes run on a preview clock at the route's tempo; the meters wait for sound.", "Turn the sound on", o.onSound],
        away: ["", "Preview", "The walk is not on this route. The scopes run on a preview clock at its tempo; the meters wait until you walk onto it.", null],
        novoice: ["", "No input", "No voice is on, so nothing reaches the bus. Turn a voice on in the patch.", "Back to the patch", o.onBack],
        noreturn: ["warn", "No return", "The return is at 0 %, so nothing of the bus is heard.", "Raise the return to 50 %", function () { M.set(fx, "ret", 0.5); commit(); note = null; }],
        over: ["warn", "Over budget", "≈ " + loadOf(fx).toFixed(1) + " % of a core (estimated), over the " + BUDGET + " % budget: a phone may drop out. " + costly.join(", ") + " cost the most.", null]
      }[s];
      ed.classList.toggle("has-note", !!N);
      nt.hidden = !N;
      if (!N) { nt.textContent = ""; return; }
      nt.className = "fx-notice" + (N[0] ? " " + N[0] : "");
      nt.innerHTML = "<b>" + N[1] + "</b><span>" + N[2] + "</span>";
      if (N[3]) { var b = el("button", "ghost", N[3]); b.type = "button"; b.addEventListener("click", function () { if (N[4]) { N[4](); } }); nt.appendChild(b); }
    }

    function editorKnob(key) {
      var p = BY[key], x = norm(p, M.get(fx, key)), a = -135 + 270 * x, c = 28, R0 = 18, h = "";
      h += '<path class="fx-kt" stroke-width="4" d="' + arc(c, R0, -135, 135) + '"/>';
      if (p.min < 0 && p.kind === "knob") { var az = -135 + 270 * norm(p, 0);   /* bipolar: the arc grows from the centre */
        if (Math.abs(a - az) > 0.5) { h += '<path class="fx-kf" stroke-width="4" d="' + arc(c, R0, Math.min(a, az), Math.max(a, az)) + '"/>'; } }
      else if (x > 0.002) { h += '<path class="fx-kf" stroke-width="4" d="' + arc(c, R0, -135, a) + '"/>'; }
      slotsOn(key).forEach(function (si, i) {   /* the modulation ring: how far this source can take it, a dot where it is now */
        var mm = fx.mods[si], r = 23.5 + i * 4, uni = mm.src === "env" || mm.src === "chord";
        var lo = clamp01(uni ? Math.min(x, x + mm.amt) : x - Math.abs(mm.amt)), hi = clamp01(uni ? Math.max(x, x + mm.amt) : x + Math.abs(mm.amt));
        h += '<path class="fx-ring" style="stroke:var(--src-' + mm.src + ')" d="' + arc(c, r, -135 + 270 * lo, -135 + 270 * hi + 0.01) + '"/>' +
          '<circle class="fx-ringdot" r="3.2" style="fill:var(--src-' + mm.src + ')" data-ring="' + key + '" data-s="' + si + '" data-r="' + r + '"/>';
      });
      h += '<line class="fx-kk" stroke-width="2.4" x1="' + c + '" y1="' + c + '" x2="' + c + '" y2="' + (c - R0 + 4) + '" transform="rotate(' + a.toFixed(1) + ' ' + c + ' ' + c + ')"/>';
      return '<svg viewBox="0 0 56 56" aria-hidden="true">' + h + '</svg>';
    }
    var shows = {};
    function moduleCard() {
      var m = M.MODULES.filter(function (x) { return x.id === sel; })[0], idx = M.MODULES.indexOf(m);
      shows = {};
      card.innerHTML = '<div class="fx-mhd"><h3>' + m.label + '</h3><p>' + m.note + '</p><span class="fx-pos">' + (idx + 1) + ' of 9 · ' + m.params.length + ' controls</span></div>';
      var ks = el("div", "fx-knobs"); card.appendChild(ks);
      ks.style.gridTemplateColumns = "repeat(" + Math.max(7, m.params.length) + ", minmax(0, 1fr))";
      m.params.forEach(function (p) {
        var kc = el("div", "fx-kc"); kc.dataset.key = p.key;
        if (p.kind === "switch") {
          var v = M.get(fx, p.key);
          kc.innerHTML = '<div class="fx-seg' + (p.key === "cloud.freeze" ? " freeze" : "") + '" role="group" aria-label="' + m.label + " " + p.label + '" data-k="' + p.key + '">' + p.options.map(function (op, i) {
            return '<button type="button" data-v="' + i + '" aria-pressed="' + (op.id === v) + '">' + op.label + '</button>'; }).join("") +
            '</div><span class="fx-lbl">' + p.label + '</span><span class="fx-val">' + M.fmt(p.key, v) + '</span><span class="fx-live"></span>';
          [].forEach.call(kc.querySelectorAll("button"), function (b, i) { b.addEventListener("click", function () { M.set(fx, p.key, p.options[i].id); commit(); moduleCard(); }); });
        } else {
          var mm = slotsOn(p.key);
          kc.innerHTML = '<span class="fx-ek" data-k="' + p.key + '" aria-label="' + m.label + " " + p.label + '">' + editorKnob(p.key) + '</span>' +
            '<span class="fx-lbl">' + p.label + '</span><span class="fx-val"></span><span class="fx-live"' + (mm.length ? ' style="color:var(--src-' + fx.mods[mm[0]].src + ')"' : "") + '></span>';
          var val = kc.querySelector(".fx-val");
          var text = function () { return M.fmt(p.key, M.get(fx, p.key), bpm()); };
          var upd = function () { kc.querySelector(".fx-ek").innerHTML = editorKnob(p.key); val.innerHTML = text().replace(" · ", " ·<wbr> "); if (/\.mix$/.test(p.key)) { tiles(); loadBox(); empty(); } };
          shows[p.key] = bindKnob(kc.querySelector(".fx-ek"), { mul: p.kind === "division" ? 6 : 1, text: text,
            x: function () { return norm(p, M.get(fx, p.key)); },
            set: function (x) { M.set(fx, p.key, denorm(p, x)); upd(); commit(); },
            reset: function () { var d = M.defaults(o.kind || "route"), k = p.key.split("."); M.set(fx, p.key, d[k[0]][k[1]]); upd(); commit(); } });
          upd();
        }
        ks.appendChild(kc);
      });
      var disp = el("div", "fx-disp", '<canvas data-disp="' + m.id + '"></canvas>'); card.appendChild(disp);
      disp.insertAdjacentHTML("beforeend", '<span class="fx-tag">' + ({ shift: "the partials, moved by Hz", swarm: "three taps a side", phase: "the notches, swept", comb: "five combs on the chord",
        vowel: "three formants", cloud: "the last 2 s, and its grains", tape: "pitch drift and the age filter", echo: "every repeat, transformed again", space: "the tail" })[m.id] + '</span>');
      if (m.id === "cloud" && M.get(fx, "cloud.freeze")) { disp.insertAdjacentHTML("beforeend", '<span class="fx-tag frz">Frozen · the grains keep reading these 2 s</span>'); }
      empty();
    }
    function empty() {   /* the display's empty states (C-5) */
      var disp = card.querySelector(".fx-disp"), old = disp && disp.querySelector(".fx-empty"), m = M.MODULES.filter(function (x) { return x.id === sel; })[0], h = "";
      if (!disp) { return; }
      if (M.MODULES.every(function (x) { return !mixOn(fx, x.id); })) { h = "<p>Nothing is on. Turn a module's mix up to hear it.</p><small>Every module starts dry; the dry voices are never touched.</small>"; }
      else if (!mixOn(fx, m.id)) { h = "<p>" + m.label + " is off.</p><small>Turn its mix up to hear it; its settings are kept meanwhile.</small>"; }
      else if (o.voices && !o.voices()) { h = "<p>Nothing reaches the bus.</p><small>The chain is ready; it sounds as soon as a voice is on.</small>"; }
      if (old) { old.remove(); }
      if (h) { disp.appendChild(el("div", "fx-empty", h)); }
    }

    function lfoCard() {
      right.innerHTML = '<div class="fx-ttl"><h4>Modulators</h4><small>on the route\'s clock</small><span class="fx-pv" hidden>preview clock</span></div>';
      var g = el("div", "fx-lfos"); right.appendChild(g);
      fx.lfo.forEach(function (l, i) {
        var id = "lfo" + (i + 1), targets = fx.mods.filter(function (m) { return m.src === id && BY[m.dst]; }).map(function (m) { var mod = M.MODULES.filter(function (x) { return x.id === m.dst.split(".")[0]; })[0]; return mod.label + " " + BY[m.dst].label; });
        var c = el("div", "fx-lfo"); c.dataset.i = i;
        c.innerHTML = '<div class="fx-lh"><u style="background:var(--src-' + id + ')"></u>LFO ' + (i + 1) + '<em>' + (targets.length ? "→ " + targets.join(", ") : "no target yet") + '</em></div>' +
          '<div class="fx-sc"><canvas data-lfo="' + i + '"></canvas></div>' +
          '<div class="fx-shapes" role="group" aria-label="LFO ' + (i + 1) + ' shape">' + M.LFO_SHAPES.map(function (s) { return '<button type="button" title="' + s.label + '" aria-label="' + s.label + '" aria-pressed="' + (s.id === l.shape) + '" data-s="' + s.id + '"><svg viewBox="0 0 18 10" aria-hidden="true"><path d="' + SHAPE_PATH[s.id] + '"/></svg></button>'; }).join("") + '</div>' +
          '<div class="fx-lrow"><select aria-label="LFO ' + (i + 1) + ' rate">' + DIVS.map(function (d) { return '<option value="' + d.id + '"' + (d.id === l.rate ? " selected" : "") + ">" + M.fmt("swarm.rate", d.id, bpm()) + "</option>"; }).join("") + '</select>' +
          '<span class="fx-pk" aria-label="LFO ' + (i + 1) + ' phase"></span><i></i></div>';
        [].forEach.call(c.querySelectorAll(".fx-shapes button"), function (b) { b.addEventListener("click", function () { l.shape = b.dataset.s; commit(); lfoCard(); }); });
        c.querySelector("select").addEventListener("change", function (e) { l.rate = e.target.value; commit(); });
        var pk = c.querySelector(".fx-pk"), ro = c.querySelector(".fx-lrow i");
        var upd = function () { pk.innerHTML = smallKnob(l.phase || 0); ro.textContent = Math.round((l.phase || 0) * 360) + "°"; };
        bindKnob(pk, { text: function () { return Math.round((l.phase || 0) * 360) + " degrees"; }, x: function () { return l.phase || 0; },
          set: function (x) { l.phase = Math.round(x * 100) / 100; upd(); commit(); }, reset: function () { l.phase = 0; upd(); commit(); } });
        upd();
        g.appendChild(c);
      });
    }

    function destOptions(cur) {
      return '<option value="">—</option>' + M.MODULES.map(function (m) { return '<optgroup label="' + m.label + '">' + m.params.filter(function (p) { return p.kind === "knob"; }).map(function (p) {
        return '<option value="' + p.key + '"' + (p.key === cur ? " selected" : "") + ">" + m.label + " · " + p.label + "</option>"; }).join("") + "</optgroup>"; }).join("");
    }
    function matrix() {
      var used = fx.mods.filter(function (m) { return m.src && m.dst; }).length;
      mx.innerHTML = '<div class="fx-ttl"><h4>Matrix</h4><small>' + (used ? "8 slots · amount is a share of the destination's range" : "No modulation yet: choose a source and a destination in a slot; its amount is how far the source may move it") + '</small></div>' +
        '<div class="fx-mxh"><span></span><span>source</span><span>destination</span><span>amount</span><span></span><span>now</span><span style="text-align:right">value</span></div>';
      fx.mods.forEach(function (m, i) {
        var on = !!(m.src && m.dst), r = el("div", "fx-mxr" + (m.src ? "" : " empty-slot")); r.dataset.slot = i;
        r.innerHTML = '<b>' + (i + 1) + '</b><select aria-label="Slot ' + (i + 1) + ' source"' + (m.src ? ' style="box-shadow:inset 3px 0 0 var(--src-' + m.src + ')"' : "") + '><option value="">—</option>' +
          M.SOURCES.map(function (s) { return '<option value="' + s.id + '"' + (s.id === m.src ? " selected" : "") + ">" + s.label + "</option>"; }).join("") + '</select>' +
          '<select aria-label="Slot ' + (i + 1) + ' destination">' + destOptions(m.dst) + '</select>' +
          (m.src ? '<span class="fx-bip" aria-label="Slot ' + (i + 1) + ' amount"><b></b><i></i></span><em class="fx-amt"></em>'
            : '<span class="fx-bip" aria-hidden="true" style="opacity:.35"></span><em class="fx-amt" style="color:var(--faint)">0 %</em>') +
          '<span class="fx-livebar"' + (on ? "" : ' style="opacity:.5"') + '>' + (on ? '<b data-lb="' + i + '" style="background:var(--src-' + m.src + ')"></b>' : "") + '</span>' +
          '<em class="fx-lv"' + (on ? ' data-lv="' + i + '"' : "") + '>' + (on ? "" : "empty") + '</em>';
        var sels = r.querySelectorAll("select");
        var change = function () {
          var src = sels[0].value, dst = sels[1].value;
          fx.mods[i] = src || dst ? { src: src, dst: dst, amt: m.amt || 0 } : {};   /* half a slot is kept here; clean stores it empty */
          commit(); matrix(); moduleCard(); tiles(); lfoCard();
        };
        sels[0].addEventListener("change", change); sels[1].addEventListener("change", change);
        if (m.src) {
          var bip = r.querySelector(".fx-bip"), amt = r.querySelector(".fx-amt");
          var upd = function () {
            bip.querySelector("b").style.left = Math.min(50, 50 + m.amt * 50) + "%"; bip.querySelector("b").style.width = Math.abs(m.amt) * 50 + "%";
            bip.querySelector("i").style.left = (50 + m.amt * 50) + "%";
            amt.textContent = (m.amt > 0 ? "+" : m.amt < 0 ? "−" : "") + Math.round(Math.abs(m.amt) * 100) + " %";
            if (m.dst) { var kc = card.querySelector('.fx-kc[data-key="' + m.dst + '"] .fx-ek'); if (kc) { kc.innerHTML = editorKnob(m.dst); } }
          };
          var put = function (a) { m.amt = Math.round(Math.max(-1, Math.min(1, a)) * 100) / 100; upd(); commit(); };
          var show = bindKnob(bip, { mul: 1, text: function () { return amt.textContent; }, x: function () { return (m.amt + 1) / 2; },
            set: function (x) { put(x * 2 - 1); }, reset: function () { put(0); } });
          var drag = false, fromX = function (e) { var b = bip.getBoundingClientRect(); put(((e.clientX - b.left) / b.width) * 2 - 1); show(); };
          bip.style.cursor = "ew-resize";
          bip.addEventListener("pointerdown", function (e) { drag = true; fromX(e); }, true);
          bip.addEventListener("pointermove", function (e) { if (drag) { e.stopImmediatePropagation(); fromX(e); } }, true);
          bip.addEventListener("pointerup", function () { drag = false; }, true);
          upd();
        }
        mx.appendChild(r);
      });
    }

    function sideCards() {
      var e = el("section", "fx-card");
      e.innerHTML = '<div class="fx-ttl"><h4 style="color:var(--src-env)">Envelope</h4><small>of the bus input</small></div><div class="fx-sc"><canvas data-envc></canvas></div><div class="fx-minirow"></div>';
      var row = e.querySelector(".fx-minirow");
      Object.keys(ENV).forEach(function (k) {
        var kc = el("div", "fx-kc", '<span class="fx-pk" data-env="' + k + '" aria-label="Envelope ' + k + '"></span><span class="fx-lbl">' + k + '</span><span class="fx-val"></span>');
        var pk = kc.querySelector(".fx-pk"), val = kc.querySelector(".fx-val");
        var upd = function () { pk.innerHTML = smallKnob(envNorm(k, fx.env[k])); val.textContent = envFmt(k, fx.env[k]); };
        bindKnob(pk, { text: function () { return envFmt(k, fx.env[k]); }, x: function () { return envNorm(k, fx.env[k]); },
          set: function (x) { fx.env[k] = envDenorm(k, x); upd(); commit(); }, reset: function () { fx.env[k] = ENV[k][2]; upd(); commit(); } });
        upd(); row.appendChild(kc);
      });
      var c = el("section", "fx-card");
      c.innerHTML = '<div class="fx-ttl"><h4 style="color:var(--src-chord)">Chord</h4><small>per chord</small></div><div class="fx-sc"><canvas data-chordc></canvas></div>' +
        '<div class="fx-chords"></div><div class="fx-chordval"><span>now</span><em></em></div>';
      side.textContent = ""; side.appendChild(e); side.appendChild(c);
    }

    function render() { tiles(); loadBox(); moduleCard(); lfoCard(); matrix(); sideCards(); note = null; setNotice(stateNow()); }
    render();

    /* ---------- the animation: the engine's live values, ~30 a second, drawn each frame ---------- */
    var logX = function (f, W) { return Math.log(f / 20) / Math.log(1000) * W; };
    function axis(g, W, H, ticks) {
      g.font = MONO; g.textBaseline = "bottom"; g.fillStyle = C.faint; g.strokeStyle = alpha(C.ink, 0.07); g.lineWidth = 1;
      ticks.forEach(function (tk) { g.beginPath(); g.moveTo(tk[0], 22); g.lineTo(tk[0], H - 16); g.stroke(); g.textAlign = tk[0] > W - 40 ? "right" : "left"; g.fillText(tk[1], tk[0] + (tk[0] > W - 40 ? -3 : 3), H - 2); });
    }
    function drawModule(cv, id, t) {
      var g = ctx2d(cv), W = g.W, H = g.H, top = 26, bot = H - 18, mid = (top + bot) / 2, ac = C.accent, lp = C.lamp, B = bpm();
      var Lv = function (k) { return denorm(BY[id + "." + k], liveX(id + "." + k)); }, on = mixOn(fx, id) && !!L;
      g.lineJoin = "round";
      if (id === "echo") {
        top += 16; mid = (top + bot) / 2;
        var T = M.divSeconds(M.get(fx, "echo.time"), B), span = T * 12.5, fb = Lv("fb"), sh = Lv("shift"), tone = Lv("tone"), wob = Lv("wobble"), spr = M.get(fx, "echo.spread");
        var ticks = []; for (var b = 0; b * 60 / B <= span; b += 4) { ticks.push([28 + (W - 40) * (b * 60 / B) / span, (b / 4) + (b === 4 ? " bar" : b ? " bars" : "")]); }
        axis(g, W, H, ticks);
        g.strokeStyle = C.hairline; g.beginPath(); g.moveTo(28, mid); g.lineTo(W - 12, mid); g.stroke();
        var ph = (t % (T * 8)) / T;
        for (var k = 0; k <= 12; k++) {
          var x = 28 + (W - 40) * k * T / span + (k ? Math.sin(t * 3 + k) * wob * 6 : 0); if (x > W - 12) { break; }
          var amp = Math.pow(Math.max(0.05, fb), k) * (k ? 0.92 : 1), hgt = amp * (bot - top) / 2 * 0.95, sd = k === 0 ? 0 : (k % 2 ? -1 : 1) * spr;
          var y0 = mid - hgt * (sd <= 0 ? 1 : 1 - spr * 0.6), y1 = mid + hgt * (sd >= 0 ? 1 : 1 - spr * 0.6), lit = Math.floor(ph) === k && on, w = 7 + k * 0.8;
          g.fillStyle = k === 0 ? alpha(C.ink, 0.75) : lit ? lp : alpha(ac, Math.max(0.25, 1 - k * 0.06 + tone * 0.2));
          g.beginPath(); if (g.roundRect) { g.roundRect(x - w / 2, y0, w, y1 - y0, 3); } else { g.rect(x - w / 2, y0, w, y1 - y0); } g.fill();
          if (k && k < 7 && (k % 2 || (W - 40) * T / span > 60)) { g.fillStyle = lit ? lp : C.dim; g.font = MONO; g.textAlign = "center"; g.textBaseline = "bottom"; g.fillText((sh * k >= 0 ? "+" : "−") + Math.abs(sh * k).toFixed(0) + " Hz", x, y0 - 4); }
        }
        g.fillStyle = C.dim; g.font = MONO; g.textAlign = "center"; g.textBaseline = "bottom"; g.fillText("in", 28, mid - (bot - top) / 2 * 0.95 - 4);
      } else if (id === "cloud") {
        var frz = M.get(fx, "cloud.freeze"), off = frz ? 0 : t, size = Lv("size") / 1000, dens = Lv("dens"), pitch = Lv("pitch"), sca = Lv("scatter"), rev = Lv("rev"), X0 = 16, XW = W - 32;
        axis(g, W, H, [[16, "−2 s"], [16 + (W - 32) / 2, "−1 s"], [W - 16, "now"]]);
        g.beginPath(); for (var x2 = 0; x2 <= XW; x2 += 2) { var s = (x2 / XW) * 2 - off, a = (0.25 + 0.75 * Math.abs(Math.sin(s * 5.3) * Math.sin(s * 1.7 + 1))) * (0.5 + 0.5 * rnd(Math.floor(s * 6))); g.rect(X0 + x2, mid - a * (bot - top) * 0.42, 1.2, a * (bot - top) * 0.84); }
        g.fillStyle = frz ? alpha(lp, 0.55) : alpha(C.faint, 0.55); g.fill();
        if (frz) { g.strokeStyle = lp; g.lineWidth = 1.2; g.strokeRect(X0 - 2, top - 4, XW + 4, bot - top + 8); }
        var n = Math.min(8, Math.round(dens * size * 2) + 2);
        for (var gi = 0; gi < n; gi++) {
          var seed = Math.floor(t * dens / n) * 31 + gi, age = (t * dens / n) % 1, back = (0.05 + sca * 1.7) * rnd(seed), gw = size * XW / 2, gx = X0 + XW * (1 - back / 2) - gw / 2;
          var hh = (bot - top) * 0.46 * Math.sin(Math.PI * Math.min(1, (age + gi / n) % 1));
          g.beginPath(); for (var u = 0; u <= 1.001; u += 0.05) { var yy = mid - hh * Math.sin(Math.PI * u); if (u) { g.lineTo(gx + u * gw, yy); } else { g.moveTo(gx, yy); } }
          g.strokeStyle = ac; g.lineWidth = 1.6; g.stroke(); g.fillStyle = alpha(ac, 0.14); g.fill();
          if (rnd(seed + 3) < rev) { g.fillStyle = C.dim; g.font = MONO; g.textAlign = "center"; g.textBaseline = "bottom"; g.fillText("◂", gx + gw / 2, mid - hh - 2); }
        }
        g.fillStyle = C.dim; g.font = MONO; g.textAlign = "right"; g.textBaseline = "top"; g.fillText(n + " grains · " + (pitch >= 0 ? "+" : "−") + Math.abs(pitch).toFixed(1) + " st", W - 8, top + 2);
      } else if (id === "space") {
        var dec = Lv("decay"), spn = Math.max(4, dec * 1.3);
        axis(g, W, H, [[16, "0 s"], [16 + (W - 32) / 2, (spn / 2).toFixed(spn < 10 ? 1 : 0) + " s"], [W - 16, spn.toFixed(spn < 10 ? 1 : 0) + " s"]]);
        g.beginPath(); g.moveTo(16, bot); for (var x3 = 0; x3 <= W - 32; x3 += 2) { var e3 = Math.pow(10, -3 * (x3 / (W - 32) * spn) / dec) * (0.7 + 0.3 * rnd(Math.floor(x3 * 1.3 + t * 30))); g.lineTo(16 + x3, bot - e3 * (bot - top)); }
        g.lineTo(W - 16, bot); g.closePath(); g.fillStyle = alpha(ac, 0.3); g.fill(); g.strokeStyle = ac; g.lineWidth = 1; g.stroke();
      } else {   /* a response over frequency, 20 Hz - 20 kHz */
        axis(g, W, H, [[logX(100, W), "100"], [logX(1000, W), "1k"], [logX(10000, W), "10k Hz"]]);
        var resp = function (f) {
          if (id === "phase") { var c0 = Lv("hz") * Math.pow(2, Lv("span") * 0.5 * Math.sin(2 * Math.PI * t / M.divSeconds(M.get(fx, "phase.rate"), B))), st = M.get(fx, "phase.stages"), d = 1;
            for (var j = 0; j < st / 2; j++) { var nf = c0 * Math.pow(2, (j - st / 4) * 0.7); d *= 1 - 0.85 * Math.exp(-Math.pow(Math.log2(f / nf) * 6, 2)); } return d; }
          if (id === "comb") { var hz = M.get(fx, "comb.tune") === "chord" ? 146.8 : Lv("hz"), q = 0.15; [1, 1.5, 2, 3, 4].forEach(function (r) { q += 0.8 * Math.exp(-Math.pow(Math.log2(f / (hz * r)) * 30 / Math.max(0.3, Math.log2(1 + Lv("decay"))), 2)); }); return Math.min(1, q) * (f > Lv("damp") ? Math.pow(Lv("damp") / f, 0.8) : 1); }
          if (id === "vowel") { var V = [[800, 1150, 2900], [400, 1600, 2700], [270, 2300, 3000], [450, 800, 2830], [325, 700, 2530]], ps = Lv("pos"), i0 = Math.min(3, Math.floor(ps)), fr = ps - i0, z = Lv("size"), out = 0.05;
            for (var j2 = 0; j2 < 3; j2++) { var fc = (V[i0][j2] * (1 - fr) + V[i0 + 1][j2] * fr) * z; out += (1 - j2 * 0.25) * Math.exp(-Math.pow(Math.log2(f / fc) * Lv("q") * 0.6, 2)); } return Math.min(1, out); }
          if (id === "tape") { return 0.75 / Math.sqrt(1 + Math.pow(f / Lv("tone"), 4)) * (1 + 0.05 * Math.sin(t * 2.5) * Lv("wow")); }
          if (id === "shift") { var hz2 = Lv("hz"), o2 = 0; [110, 220, 330, 440, 660, 880].forEach(function (p0) { o2 += Math.exp(-Math.pow((f - (p0 + hz2)) / (f * 0.02 + 4), 2)); }); return Math.min(1, o2); }
          return 0.55 + 0.4 * Lv("depth") * Math.cos(2 * Math.PI * f * Lv("delay") / 1000 * (1 + 0.2 * Math.sin(2 * Math.PI * t / M.divSeconds(M.get(fx, "swarm.rate"), B))));   /* swarm */
        };
        var line = function () { g.beginPath(); for (var x4 = 0; x4 <= W; x4 += 1.5) { var r4 = resp(20 * Math.pow(1000, x4 / W)); if (x4) { g.lineTo(x4, bot - r4 * (bot - top)); } else { g.moveTo(x4, bot - r4 * (bot - top)); } } };
        line(); g.lineTo(W, bot); g.lineTo(0, bot); g.closePath(); g.fillStyle = alpha(ac, 0.18); g.fill();
        line(); g.strokeStyle = ac; g.lineWidth = 1.6; g.stroke();
      }
    }
    function drawIO(cv, which) {
      var g = ctx2d(cv), W = g.W, H = g.H, v = L ? lvl01(L[which === "in" ? 22 : 23]) : 0;
      for (var i = 0; i < 24; i++) { var y = H - 3 - i * (H - 6) / 24; g.fillStyle = i / 24 < v ? (i > 19 ? C.lamp : C.accent) : alpha(C.ink, 0.08); g.fillRect(3, y - (H - 6) / 24 + 1.5, W - 6, (H - 6) / 24 - 1.5); }
    }
    function drawHist(cv, hist, span, t, fill, colr, idx) {
      var g = ctx2d(cv), W = g.W, H = g.H, X = function (s) { return W - (t - s) / span * W; };
      if (!hist.length) { return; }
      if (fill) { g.beginPath(); g.moveTo(X(hist[0][0]), H); hist.forEach(function (p) { g.lineTo(X(p[0]), H - p[1] * (H - 4)); }); g.lineTo(W, H); g.closePath(); g.fillStyle = alpha(C.faint, 0.25); g.fill(); }
      g.beginPath(); hist.forEach(function (p, j) { var y = H - 3 - p[idx] * (H - 6); if (j) { g.lineTo(X(p[0]), y); } else { g.moveTo(X(p[0]), y); } });
      g.strokeStyle = colr; g.lineWidth = 1.6; g.stroke();
    }
    function tick() {
      raf = 0;
      if (!host.isConnected || !host.offsetParent) { return; }   /* the panel closed or the patch is back: stop */
      raf = requestAnimationFrame(tick);
      var pn = performance.now();
      if (document.hidden || (REDUCE && pn - lastDraw < 250)) { return; }   /* C-6: a slow, steady update when asked for less motion */
      lastDraw = pn;
      if (pn - lastSync > 500) {   /* written elsewhere meanwhile (a reset, another tab's save): take it, never write over it */
        lastSync = pn;
        var st = JSON.stringify(M.clean(o.get()));
        if (st !== saved) { saved = st; fx = full(o.get(), o.kind); render(); }
      }
      L = o.live ? o.live() : null;
      var t = now(), q;
      setNotice(stateNow());
      var pv = right.querySelector(".fx-pv"); if (pv) { pv.hidden = !!L; }
      fx.lfo.forEach(function (l, i) {
        var cv = right.querySelector('canvas[data-lfo="' + i + '"]'); if (!cv) { return; }
        var c = t / M.divSeconds(l.rate, bpm()) + (l.phase || 0);
        scope(cv, { shape: l.shape, phase: L ? L[i] : c, value: L ? L[4 + i] : shapeAt(l.shape, c), state: lfoState[i] });
      });
      envHist.push([t, L ? lvl01(L[22]) : 0, L ? L[8] : 0]); while (envHist.length && envHist[0][0] < t - 4) { envHist.shift(); }
      chordHist.push([t, L ? L[12] : 0]); while (chordHist.length && chordHist[0][0] < t - 12) { chordHist.shift(); }
      if ((q = side.querySelector("canvas[data-envc]"))) { drawHist(q, envHist, 4, t, true, C["src-env"], 2); }
      if ((q = side.querySelector("canvas[data-chordc]"))) { drawHist(q, chordHist, 12, t, false, C["src-chord"], 1); }
      var ch = o.chord ? o.chord() : null, cs = side.querySelector(".fx-chords");
      if (cs) { cs.innerHTML = ch && ch.i >= 0 ? [-2, -1, 0, 1].map(function (d) { var n = ch.labels.length, k = ch.i + d; return k < 0 || k >= n ? "" : "<span" + (d === 0 ? ' class="now"' : "") + ">" + ch.labels[k] + "</span>"; }).filter(Boolean).join("<span>·</span>") : "<span>the chord waits for the walk</span>"; }
      if ((q = side.querySelector(".fx-chordval em"))) { q.textContent = L ? L[12].toFixed(2) : "—"; }
      [].forEach.call(strip.querySelectorAll("canvas[data-io]"), function (cv) { drawIO(cv, cv.dataset.io); });
      if ((q = card.querySelector("canvas[data-disp]"))) { drawModule(q, q.dataset.disp, t); }
      [].forEach.call(card.querySelectorAll(".fx-ringdot"), function (d) {   /* each ring's dot: where its source has the knob now */
        var key = d.dataset.ring, m = fx.mods[+d.dataset.s]; if (!m || !m.src) { return; }
        var x = clamp01(norm(BY[key], M.get(fx, key)) + m.amt * srcVal(m.src)), a = (-135 + 270 * x - 90) * Math.PI / 180, r = +d.dataset.r;
        d.setAttribute("cx", (28 + r * Math.cos(a)).toFixed(2)); d.setAttribute("cy", (28 + r * Math.sin(a)).toFixed(2));
      });
      [].forEach.call(card.querySelectorAll(".fx-kc"), function (kc) {
        var key = kc.dataset.key, lv = kc.querySelector(".fx-live");
        if (lv && BY[key].kind === "knob") { lv.textContent = slotsOn(key).length ? "→ " + M.fmt(key, denorm(BY[key], liveX(key)), bpm()) : ""; }
      });
      fx.mods.forEach(function (m, i) {
        if (!m.src || !m.dst) { return; }
        var v = Math.max(-1, Math.min(1, m.amt * srcVal(m.src))), b = mx.querySelector('[data-lb="' + i + '"]'), tx = mx.querySelector('[data-lv="' + i + '"]');
        if (b) { b.style.left = Math.min(50, 50 + v * 50) + "%"; b.style.width = Math.abs(v) * 50 + "%"; }
        if (tx) { tx.textContent = M.fmt(m.dst, denorm(BY[m.dst], L ? clamp01(L[24 + i]) : liveX(m.dst)), bpm()); }
      });
    }
    raf = requestAnimationFrame(tick);
    return { destroy: function () { if (raf) { cancelAnimationFrame(raf); } raf = 0; host.textContent = ""; }, render: render };
  }

  window.FsFx = { mount: mount, editor: editor, scope: scope, load: function (stored) { return loadOf(full(stored)); } };
})();
