/* The audition bench (sample harmony, sub-project 1; spec D12): drop a recording, see what the analyser
   measured, and hear it retuned to a route's chords in just intonation or equal temperament. In this
   sub-project the sound is the recording itself at playbackRate = target / measured f0 (sub-project 2
   moves playback into the core's synths). */
(function () {
  "use strict";
  var SUPA = "https://ujdygmcpqsbyeysggypc.supabase.co";
  var ANON = /* the site's public anon key (index.html FA_CONFIG.anonKey) */ "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVqZHlnbWNwcXNieWV5c2dneXBjIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODkwNDE4NDIsImV4cCI6MjEwNDYxNzg0Mn0.SJlrNKQftKxdM0G6f18e6PsRCvdhIH8fco5tW3CktwM";
  var MAX_S = 30;                                   /* analysed length; a longer file is cut (spec Review Focus) */
  var $ = function (s) { return document.querySelector(s); };
  var ctx = null, x = null, buffer = null, peak = 1, analysis = null, routes = [], route = null, prog = null, tuning = 1, playing = [], last = [];

  function audio() { if (!ctx) { ctx = new AudioContext(); } if (ctx.state === "suspended") { ctx.resume(); } return ctx; }
  function cstr(s) { var b = new TextEncoder().encode(s + "\0"), p = x.malloc(b.length); new Uint8Array(x.memory.buffer, p, b.length).set(b); return p; }
  function call(fn, args) {
    var need = fn.apply(null, args.concat([0, 0])), out = x.malloc(need + 1);
    fn.apply(null, args.concat([out, need + 1]));
    var s = new TextDecoder().decode(new Uint8Array(x.memory.buffer, out, need)); x.free(out);
    return JSON.parse(s);
  }

  var ready = fetch("web/core-lab.wasm").then(function (r) { return r.arrayBuffer(); }).then(function (b) {
    return WebAssembly.instantiate(b, { env: new Proxy({}, { get: function () { return function () { return 0; }; } }),
      wasi_snapshot_preview1: new Proxy({}, { get: function () { return function () { return 0; }; } }) });
  }).then(function (r) {
    x = r.instance.exports; if (x._initialize) { x._initialize(); }
    return fetch(SUPA + "/rest/v1/public_features?select=id,properties&kind=eq.route", { headers: { apikey: ANON, Authorization: "Bearer " + ANON } });
  }).then(function (r) { return r.json(); }).then(function (rows) {
    routes = rows.map(function (r) { return { id: r.id, name: r.properties.name || "Route", patch: r.properties.patch || {} }; });
    $("#lab-route").innerHTML = routes.map(function (r) { return "<option value='" + r.id + "'>" + r.name.replace(/</g, "&lt;") + "</option>"; }).join("");
    setRoute(routes.length ? routes[0].id : null);
  });

  function setRoute(id) {
    route = routes.filter(function (r) { return r.id === id; })[0] || null;
    var p = cstr(JSON.stringify(route ? route.patch : {}));
    prog = call(x.fs_harmony_progression, [p, tuning, 0]); x.free(p);
    $("#lab-step").innerHTML = prog.chords.map(function (c, i) { return "<option value='" + i + "'>" + (i + 1) + " · " + noteName(c.root).replace(/-?[0-9]+.*$/, "") + " " + c.label + "</option>"; }).join("");
  }

  function load(file) {
    return ready.then(function () { return file.arrayBuffer(); }).then(function (ab) { return audio().decodeAudioData(ab); }).then(function (b) {
      var sr = b.sampleRate, n = Math.min(b.length, Math.round(MAX_S * sr)), mono = new Float32Array(n);
      /* only what was analysed is kept: a 10-minute file would otherwise stay whole in memory (a phone) */
      buffer = audio().createBuffer(b.numberOfChannels, n, sr); peak = 0;
      for (var c = 0; c < b.numberOfChannels; c++) {
        var d = b.getChannelData(c).subarray(0, n); buffer.copyToChannel(d, c);
        for (var i = 0; i < n; i++) { mono[i] += d[i] / b.numberOfChannels; peak = Math.max(peak, Math.abs(d[i])); }
      }
      var p = x.malloc(n * 4); new Float32Array(x.memory.buffer, p, n).set(mono);
      analysis = call(x.fs_analyse, [p, BigInt(n), sr]); x.free(p);
      $("#lab-note").textContent = file.name + (b.length > n ? " — only the first 30 s analysed" : "") +
        (analysis.f0 > 0 ? "" : " — unpitched: it has no pitch to retune; the pitch-making synths come next (sub-project 2)");
      show();
      return analysis;
    }).catch(function (e) { $("#lab-note").textContent = "Could not read that file: " + (e && e.message || e); throw e; });
  }

  function show() {
    var a = analysis, cv = $("#lab-track"), g = cv.getContext("2d");
    g.clearRect(0, 0, cv.width, cv.height);
    var pts = 0;
    a.track.forEach(function (f, i) {
      if (!(f[0] > 0)) { return; }
      var X = i / Math.max(1, a.track.length - 1) * cv.width, Y = cv.height - (Math.log2(f[0] / 50) / Math.log2(40)) * cv.height;
      g.fillStyle = "rgba(186,230,177," + (0.15 + 0.85 * f[1]) + ")"; g.fillRect(X, Y - 2, 3, 4); pts++;
    });
    cv.dataset.points = pts;
    var note = a.f0 > 0 ? noteName(69 + 12 * Math.log2(a.f0 / 440)) : "—";
    $("#lab-facts").innerHTML = "<dt>Verdict</dt><dd>" + a.verdict + "</dd><dt>Pitch</dt><dd>" + (a.f0 > 0 ? a.f0.toFixed(2) + " Hz · " + note : "none") +
      "</dd><dt>Confidence</dt><dd>" + a.confidence.toFixed(2) + "</dd><dt>Brightness</dt><dd>" + a.centroid_hz + " Hz</dd>" +
      "<dt>Loop</dt><dd>" + (a.loop ? (a.loop[0] / a.rate).toFixed(3) + "–" + (a.loop[1] / a.rate).toFixed(3) + " s" : "none") +
      "</dd><dt>Cycle</dt><dd>" + (a.cycle ? (a.cycle[1] - a.cycle[0]) + " samples" : "none") + "</dd>";
  }
  function noteName(m) { var N = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], r = Math.round(m), c = Math.round((m - r) * 100); return N[(r % 12 + 12) % 12] + (Math.floor(r / 12) - 1) + (c ? (c > 0 ? " +" : " ") + c + "¢" : ""); }

  /* one retuned voice: attack and release ramps (A-2, A-3), exponential to silence */
  function voice(rate, at, dur, level) {
    var c = audio(), src = c.createBufferSource(), g = c.createGain(), A = Math.max(0.005, +$("#lab-attack").value), R = Math.max(0.03, +$("#lab-release").value);
    src.buffer = buffer; src.playbackRate.value = rate;
    if (analysis.loop) { src.loop = true; src.loopStart = analysis.loop[0] / analysis.rate; src.loopEnd = analysis.loop[1] / analysis.rate; }
    g.gain.setValueAtTime(0.0001, at); g.gain.exponentialRampToValueAtTime(level, at + A);
    g.gain.setValueAtTime(level, at + A + dur); g.gain.exponentialRampToValueAtTime(0.0001, at + A + dur + R);
    src.connect(g).connect(c.destination); src.start(at);
    var v = { src: src, g: g, start: at, stopAt: at + A + dur + R + 0.05 };
    src.stop(v.stopAt);
    playing.push(v); last.push(v);
  }
  /* each voice's level: the voices that can sound together (a release overlapping the next chord's
     attack doubles them) never sum past 0.8 of full scale - clipping would be heard as the harshness a
     just/equal comparison is listening past (rulebook A-6) */
  function level(voices) { return Math.min(0.25, 0.8 / (Math.max(peak, 1e-3) * voices)); }
  function rateFor(hz) { var tune = +$("#lab-tune").value; return Math.pow(hz / analysis.f0, tune); }

  function play(kind, opts) {
    opts = opts || {};
    if (!buffer || !analysis || !prog) { return { silent: true }; }
    if (!(analysis.f0 > 0)) { return { silent: true }; }                 /* the panel already says why */
    if (opts.tuning) { tuning = opts.tuning === "just" ? 1 : 0; setRoute(route ? route.id : null); }
    stop(); last = [];
    var c = audio(), t = c.currentTime + 0.05, step = opts.step != null ? opts.step : +$("#lab-step").value || 0;
    var chord = prog.chords[step], beat = 60 / prog.tempo, hz = [], rates = [];
    if (kind === "note") { hz = [chord.hz[0]]; }
    else if (kind === "chord") { hz = chord.hz.slice(); }
    else if (kind === "scale") { hz = chord.scale_hz.slice(); }
    if (kind === "progression") {
      var most = Math.max.apply(null, prog.chords.map(function (ch) { return ch.hz.length; })), lv = level(2 * most);
      prog.chords.forEach(function (ch, i) { ch.hz.forEach(function (h) { voice(rateFor(h), t + i * beat * 4, beat * 4 - 0.1, lv); }); });
      return { chords: prog.chords.length, gains: last.map(function () { return lv; }) };
    }
    var lvl = level(kind === "scale" ? 2 : hz.length), gains = [];
    hz.forEach(function (h, i) { var r = rateFor(h); rates.push(r); gains.push(lvl); voice(r, kind === "scale" ? t + i * beat : t, kind === "scale" ? beat * 0.9 : beat * 4, lvl); });
    return { rates: rates, hz: hz, gains: gains };
  }
  function stop() {
    var c = ctx; if (!c) { return; }
    var now = c.currentTime;
    playing.forEach(function (v) {
      /* not started yet: it never starts (a stop before its start is silence, not a click) */
      if (v.start > now) { v.src.stop(now); v.stopAt = now; return; }
      v.g.gain.cancelScheduledValues(now); v.g.gain.setValueAtTime(Math.max(0.0001, v.g.gain.value), now);
      v.g.gain.exponentialRampToValueAtTime(0.0001, now + 0.05); v.stopAt = now + 0.06; v.src.stop(v.stopAt);
    });
    playing = [];
  }

  /* the listener's kept verdicts: this browser only (research notes; spec D12 "keep") */
  function kept() { try { return JSON.parse(localStorage.getItem("fs.lab.kept")) || []; } catch (e) { return []; } }
  function renderKept() { $("#lab-kept").innerHTML = kept().map(function (k) { return "<li>" + k.replace(/</g, "&lt;") + "</li>"; }).join(""); }
  $("#lab-keep").addEventListener("click", function () {
    var t = $("#lab-verdict").value.trim(); if (!t) { return; }
    var line = t + " — " + ($("#lab-note").textContent || "no file") + ", tune " + $("#lab-tune").value + ", " + (tuning ? "just" : "equal");
    try { localStorage.setItem("fs.lab.kept", JSON.stringify(kept().concat([line]))); } catch (e) { /* private window: not kept */ }
    $("#lab-verdict").value = ""; renderKept();
  });
  renderKept();

  $("#lab-file").addEventListener("change", function () { if (this.files[0]) { load(this.files[0]); } });
  var drop = $("#lab-drop");
  drop.addEventListener("dragover", function (e) { e.preventDefault(); });
  drop.addEventListener("drop", function (e) { e.preventDefault(); if (e.dataTransfer.files[0]) { load(e.dataTransfer.files[0]); } });
  $("#lab-route").addEventListener("change", function () { setRoute(this.value); });
  $("#lab-just").addEventListener("click", function () { tuning = 1; this.setAttribute("aria-pressed", "true"); $("#lab-equal").setAttribute("aria-pressed", "false"); setRoute(route && route.id); });
  $("#lab-equal").addEventListener("click", function () { tuning = 0; this.setAttribute("aria-pressed", "true"); $("#lab-just").setAttribute("aria-pressed", "false"); setRoute(route && route.id); });
  document.querySelectorAll("[data-play]").forEach(function (b) { b.addEventListener("click", function () { play(b.dataset.play); }); });
  $("#lab-stop").addEventListener("click", stop);

  window.fsLab = { ready: ready, load: load, get analysis() { return analysis; }, get routes() { return routes; }, setRoute: setRoute, play: play, stop: stop,
    get peak() { return peak; }, get bufferSeconds() { return buffer ? buffer.duration : 0; },
    now: function () { return ctx ? ctx.currentTime : 0; },
    voices: function () { return last.map(function (v) { return { start: v.start, stopAt: v.stopAt }; }); } };
})();
