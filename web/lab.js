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
  var synth = "retune", node = null, wasmBytes = null, engineReady = null, madeTimer = null, loudAt = 0;

  /* one output bus with a meter on it: what the bench plays, measured (fsLab.level) */
  var bus = null, meter = null, limReady = null;
  function audio() {
    if (!ctx) {
      ctx = new AudioContext();
      bus = ctx.createGain(); meter = ctx.createAnalyser(); meter.fftSize = 2048;
      /* the master: x2.5 (+8 dB; Kerem 2026-10-01 and 2026-10-04 "too low") and a 5 ms look-ahead limiter to -1 dBFS (core-worklet.js
         fs-limiter; Kerem 2026-10-02 heard clicks and clips where an oversampled curve rang past full scale). Until the
         module loads, a plain x2 stands in; the meter reads what leaves */
      var x2 = ctx.createGain(); x2.gain.value = 2.5;
      bus.connect(x2); x2.connect(meter); x2.connect(ctx.destination);
      limReady = ctx.audioWorklet.addModule("web/core-worklet.js?v=" + Date.now()).then(function () {
        var lim = new AudioWorkletNode(ctx, "fs-limiter", { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
        bus.connect(lim); lim.connect(meter); lim.connect(ctx.destination);
        bus.disconnect(x2); x2.disconnect();
      });
    }
    if (ctx.state === "suspended") { ctx.resume(); }
    return ctx;
  }
  /* pitch created (spec 2a): each note's level over the median of 12 neighbours 3-20% away (at least 3 bins, or a
     low note meets its own peak), in dB, on the bench's last 1.37 s - the same measure as the core's
     fs_bench_created. The worklet only copies its ring out; the measuring runs here, on the page (final review #5) */
  function goertzel(x, f) {                         /* x already windowed */
    var N = x.length, k = 2 * Math.cos(2 * Math.PI * f / ctx.sampleRate), s1 = 0, s2 = 0, s0, i;
    for (i = 0; i < N; i++) { s0 = x[i] + k * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
  }
  function createdDb(x, hz) {
    var N = x.length, step = 3 * ctx.sampleRate / N;
    for (var i = 0; i < N; i++) { x[i] *= 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N); }   /* Hann, once */
    return hz.map(function (f) {
      var nb = []; [0.03, 0.05, 0.08, 0.11, 0.15, 0.2].forEach(function (o) { var d = Math.max(o * f, step); nb.push(goertzel(x, f - d), goertzel(x, f + d)); });
      nb.sort(function (a, b) { return a - b; });
      var p = goertzel(x, f), m = nb[nb.length >> 1];
      return p > 1e-20 && m > 1e-30 ? 10 * Math.log10(p / m) : 0;
    });
  }
  function measureCreated() {
    var hz = sounding();
    if (!node || !hz.length) { return Promise.resolve(null); }
    return new Promise(function (res) {
      node.port.onmessage = function (e) { if (e.data && e.data.type === "ring") { res(createdDb(e.data.samples, hz)); } };
      node.port.postMessage({ type: "ring" });
    });
  }
  function sounding() { var now = ctx.currentTime; return last.filter(function (v) { return v.engine && now >= v.start && now < v.stopAt; }).map(function (v) { return v.hz; }); }
  /* the loudest frequency leaving the bench (tests measure the pitch that comes out, not the maths) */
  function peakHz() {
    if (!meter) { return 0; }
    var f = new Float32Array(meter.frequencyBinCount), best = 1; meter.getFloatFrequencyData(f);
    for (var i = 2; i < f.length; i++) { if (f[i] > f[best]) { best = i; } }
    return f[best] > -100 ? best * ctx.sampleRate / meter.fftSize : 0;
  }
  function levelDb() {
    if (!meter) { return -120; }
    var a = new Float32Array(meter.fftSize), e = 0; meter.getFloatTimeDomainData(a);
    for (var i = 0; i < a.length; i++) { e += a[i] * a[i]; }
    return e > 0 ? 10 * Math.log10(e / a.length) : -120;
  }
  function cstr(s) { var b = new TextEncoder().encode(s + "\0"), p = x.malloc(b.length); new Uint8Array(x.memory.buffer, p, b.length).set(b); return p; }
  function call(fn, args) {
    var need = fn.apply(null, args.concat([0, 0])), out = x.malloc(need + 1);
    fn.apply(null, args.concat([out, need + 1]));
    var s = new TextDecoder().decode(new Uint8Array(x.memory.buffer, out, need)); x.free(out);
    return JSON.parse(s);
  }

  var ready = fetch("web/core-lab.wasm", { cache: "no-cache" }).then(function (r) { return r.arrayBuffer(); }).then(function (b) {
    wasmBytes = b.slice(0);                                        /* the Resonator's worklet gets its own copy */
    return WebAssembly.instantiate(b, { env: new Proxy({}, { get: function () { return function () { return 0; }; } }),
      wasi_snapshot_preview1: new Proxy({}, { get: function () { return function () { return 0; }; } }) });
  }).then(function (r) {
    x = r.instance.exports; if (x._initialize) { x._initialize(); }
    return fetch(SUPA + "/rest/v1/public_features?select=id,properties&kind=eq.route", { headers: { apikey: ANON, Authorization: "Bearer " + ANON } });
  }).then(function (r) { return r.json(); }).then(function (rows) {
    routes = rows.map(function (r) { return { id: r.id, name: r.properties.name || "Route", patch: r.properties.patch || {} }; });
    $("#lab-route").innerHTML = routes.map(function (r) { return "<option value='" + r.id + "'>" + r.name.replace(/</g, "&lt;") + "</option>"; }).join("");
    setRoute(routes.length ? routes[0].id : null);
  }).catch(function (e) { $("#lab-why").textContent = "Could not load the engine or the routes: " + (e && e.message || e); throw e; });

  function setRoute(id) {
    route = routes.filter(function (r) { return r.id === id; })[0] || null;
    var p = cstr(JSON.stringify(route ? route.patch : {}));
    prog = call(x.fs_harmony_progression, [p, tuning, 0]); x.free(p);
    gate();
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
        (analysis.f0 > 0 ? "" : " — unpitched: Retune has no pitch to move; the Resonator plays it");
      /* where the Resonator starts reading: the loudest 100 ms (an unpitched file has no "clear moment") */
      var win = Math.round(0.1 * sr), bestE = -1; loudAt = 0;
      for (var s0 = 0; s0 + win <= n; s0 += Math.round(win / 2)) {
        var e = 0; for (var k = 0; k < win; k++) { e += mono[s0 + k] * mono[s0 + k]; }
        if (e > bestE) { bestE = e; loudAt = s0 / sr; }
      }
      sendSource(); sendParams();
      show(); gate(); colourLabel();
      return analysis;
    }).catch(function (e) { $("#lab-note").textContent = "Could not read that file: " + (e && e.message || e); throw e; });
  }

  function show() {
    var a = analysis, cv = $("#lab-track"), g = cv.getContext("2d");
    g.clearRect(0, 0, cv.width, cv.height);
    var pts = 0;
    a.track.forEach(function (f, i) {
      if (!(f[0] > 0)) { return; }
      var X = i / Math.max(1, a.track.length - 1) * cv.width, Y = cv.height - (Math.log2(f[0] / 50) / Math.log2(160)) * cv.height;
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
  function voice(hz, rate, at, dur, level) {
    var c = audio(), src = c.createBufferSource(), g = c.createGain(), A = Math.max(0.008, +$("#lab-attack").value), R = Math.max(0.03, +$("#lab-release").value);
    src.buffer = buffer; src.playbackRate.value = rate;
    if (analysis.loop) { src.loop = true; src.loopStart = analysis.loop[0] / analysis.rate; src.loopEnd = analysis.loop[1] / analysis.rate; }
    /* the attack is a raised cosine (S-curve), as in the core: it fades in, where an exponential from silence
       arrived all at the end (Kerem 2026-10-01: "smooth cloudy transitions"). Line segments, so Stop can cancel it */
    g.gain.setValueAtTime(0, at);
    for (var k = 1; k <= 16; k++) { g.gain.linearRampToValueAtTime(level * (0.5 - 0.5 * Math.cos(Math.PI * k / 16)), at + A * k / 16); }
    g.gain.setValueAtTime(level, at + A + dur); g.gain.exponentialRampToValueAtTime(0.0001, at + A + dur + R);
    var off = clearMoment();
    src.connect(g).connect(bus); src.start(at, off);
    var v = { src: src, g: g, hz: hz, base: hz / octaveFactor(), rate: rate, level: level, A: A, dur: dur, R: R, offset: off, start: at, stopAt: at + A + dur + R + 0.05 };
    src.stop(v.stopAt);
    playing.push(v); last.push(v);
  }
  /* Where a note starts in the recording (the spec's "position"): the most confident moment at its main
     pitch - a recording opening quietly made every short scale step play only that quiet (Kerem, 2026-09-30:
     "scale is too low in volume"). */
  function clearMoment() {
    var best = -1, bc = 0;
    analysis.track.forEach(function (f, i) {
      if (f[0] > 0 && f[1] > bc && Math.abs(1200 * Math.log2(f[0] / analysis.f0)) < 50) { best = i; bc = f[1]; }
    });
    return best < 0 ? 0 : Math.max(0, best * analysis.hop_s);
  }
  /* each voice's level: the voices that can sound together (a release overlapping the next chord's
     attack doubles them) never sum past 0.8 of full scale - clipping would be heard as the harshness a
     just/equal comparison is listening past (rulebook A-6) */
  function level(voices) { return Math.min(0.25, 0.8 / (Math.max(peak, 1e-3) * voices)); }
  function rateFor(hz) { var tune = +$("#lab-tune").value; return Math.pow(hz / analysis.f0, tune); }
  /* A chord or a scale moved by whole octaves as one block, centred on the recording's pitch (spec D2 "any
     octave"): a 4 kHz bird on chords around 150-300 Hz was played 28x slower and could not be heard, and
     folding each note on its own wrapped the scale and made every chord the same cluster (Kerem,
     2026-09-30: "I want to hear the different pitches of each step"). */
  function octaveFactor() { return Math.pow(2, +$("#lab-octave").value || 0); }
  /* Octave and Tune act on what is sounding and waiting, not only on the next press (Kerem, 2026-09-30:
     "it is there but it does not affect the sound"): each voice glides to its new rate (no jump, A-2). */
  function retune() {
    if (!ctx || !analysis || !(analysis.f0 > 0)) { return; }
    var now = ctx.currentTime;
    playing.forEach(function (v) {
      if (v.engine) { return; }                                     /* a ringing resonator is not retuned (a click); the next note is */
      v.hz = v.base * octaveFactor(); v.rate = rateFor(v.hz);
      v.src.playbackRate.setTargetAtTime(v.rate, now, 0.03);
    });
  }
  function block(hz) {
    var centre = Math.exp(hz.reduce(function (a, h) { return a + Math.log(h); }, 0) / hz.length);
    var k = Math.pow(2, Math.round(Math.log2(analysis.f0 / centre)) + (+$("#lab-octave").value || 0));   /* + the Octave control (Kerem, 2026-09-30) */
    return hz.map(function (h) { return h * k; });
  }

  function play(kind, opts) {
    opts = opts || {};
    if (!buffer || !analysis || !prog) { return { silent: true }; }
    if (!(analysis.f0 > 0) && synth === "retune") { return { silent: true }; }   /* the panel already says why */
    if (opts.tuning) { tuning = opts.tuning === "just" ? 1 : 0; setRoute(route ? route.id : null); }
    stop(); last = [];
    var c = audio(), t = c.currentTime + 0.05, step = opts.step != null ? opts.step : +$("#lab-step").value || 0;
    var chord = prog.chords[step], beat = 60 / prog.tempo, hz = [], rates = [];
    if (synth in ENGINE) { return playResonator(kind, c, t, chord, beat); }
    if (kind === "note") { hz = [chord.hz[0]]; }
    else if (kind === "chord") { hz = chord.hz.slice(); }
    else if (kind === "scale") { hz = chord.scale_hz.slice(); }
    if (hz.length) { hz = block(hz); }
    if (kind === "progression") {
      var most = Math.max.apply(null, prog.chords.map(function (ch) { return ch.hz.length; })), lv = level(2 * most);
      prog.chords.forEach(function (ch, i) {
        var name = noteName(ch.root).replace(/-?[0-9]+.*$/, "") + " " + ch.label;
        block(ch.hz).forEach(function (h) { voice(h, rateFor(h), t + i * beat * 4, beat * 4 - 0.1, lv); last[last.length - 1].chord = name; });
      });
      view();
      return { chords: prog.chords.length, gains: last.map(function () { return lv; }) };
    }
    var lvl = level(kind === "scale" ? 2 : hz.length), gains = [];
    hz.forEach(function (h, i) { var r = rateFor(h); rates.push(r); gains.push(lvl); voice(h, r, kind === "scale" ? t + i * beat : t, kind === "scale" ? beat * 0.9 : beat * 4, lvl); });
    view();
    return { rates: rates, hz: hz, gains: gains };
  }

  /* The Resonator runs in the engine (2a spec R4): the lab's wasm, device "bench", in an AudioWorklet. An
     unpitched recording has no pitch to centre on, so notes sound at the chord's written register (plus Octave). */
  var P_BODY = 0, P_EXCITE = 1, P_FOCUS = 2, P_COLOUR = 3, P_TUNE = 4, P_ATTACK = 5, P_RELEASE = 6, P_OFFSET = 7, P_SYNTH = 8, P_METHOD = 9, P_MODE = 10;
  var ENGINE = { resonator: 0, harmonic: 1, formant: 2, pulsar: 3, freeze: 4 };          /* the synths on the engine, by the bench's "synth" */
  /* Colour says what it does: the Harmonic filter's overtone balance, the Formant's peak partial (2b spec), Pulsar's
     grain as a share of the period, Freeze's moment in the recording (2c spec) */
  function colourLabel() {
    var c = +$("#lab-colour").value, sec = buffer ? Math.max(0, c * (buffer.duration - 2048 / buffer.sampleRate)) : 0;
    $("#lab-colour-label").textContent = synth === "harmonic" ? "Overtones" : synth === "formant" ? "Partial " + Math.round(1 + 15 * c)
      : synth === "pulsar" ? "Grain " + Math.round(100 * (0.05 + 0.95 * c)) + " %"
      : synth === "freeze" ? "Moment " + Math.floor(sec / 60) + ":" + ("0" + Math.floor(sec % 60)).slice(-2) : "Colour";
  }
  function ensureEngine() {
    if (engineReady) { return engineReady; }
    var c = audio();
    engineReady = ready.then(function () { return c.audioWorklet.addModule("web/core-worklet.js?v=" + Date.now()); }).then(function () {
      node = new AudioWorkletNode(c, "fieldscape-core", { numberOfInputs: 0, outputChannelCount: [2],
        processorOptions: { wasm: wasmBytes, device: "bench", params: [] } });
      node.connect(bus);
      sendParams(); sendSource(); gate();
    });
    return engineReady;
  }
  function sendParams() {
    if (!node) { return; }
    [[P_BODY, +$("#lab-body").value], [P_EXCITE, +$("#lab-excite").value], [P_FOCUS, +$("#lab-focus").value],
     [P_COLOUR, +$("#lab-colour").value], [P_TUNE, +$("#lab-tune").value], [P_ATTACK, Math.max(0.008, +$("#lab-attack").value)],
     [P_RELEASE, Math.max(0.03, +$("#lab-release").value)], [P_OFFSET, loudAt], [P_SYNTH, ENGINE[synth] || 0],
     [P_METHOD, +$("#lab-method").value], [P_MODE, +$("#lab-mode").value]].forEach(function (pv) { node.port.postMessage(pv); });
  }
  function sendSource() {
    if (!node || !buffer) { return; }
    var ch = []; for (var c = 0; c < buffer.numberOfChannels; c++) { ch.push(buffer.getChannelData(c).slice(0)); }
    node.port.postMessage({ type: "source", channels: ch });
  }
  function setSynth(s) {
    synth = s; $("#lab-synth").value = s;
    var eng = s in ENGINE; $("#lab-np").hidden = !eng; $("#lab-res").hidden = s !== "resonator"; $("#lab-part").hidden = !(s === "harmonic" || s === "formant");
    colourLabel(); if (eng) { ensureEngine().then(sendParams); }
    gate();
  }
  function playResonator(kind, c, t, chord, beat) {
    if (!node) { return { silent: true }; }
    var oct = octaveFactor(), A = Math.max(0.008, +$("#lab-attack").value), R = Math.max(0.03, +$("#lab-release").value), list = [];
    var groups = kind === "progression"
      ? prog.chords.map(function (ch, i) { return { hz: ch.hz, at: i * beat * 4, dur: beat * 4 - 0.1, name: noteName(ch.root).replace(/-?[0-9]+.*$/, "") + " " + ch.label }; })
      : [{ hz: kind === "note" ? [chord.hz[0]] : kind === "scale" ? chord.scale_hz : chord.hz, at: 0, dur: kind === "scale" ? beat * 0.9 : beat * 4, scale: kind === "scale" }];
    var most = Math.max.apply(null, groups.map(function (gr) { return gr.hz.length; }));
    var lvr = level(kind === "progression" ? 2 * most : kind === "scale" ? 2 : most);
    groups.forEach(function (gr) {
      gr.hz.forEach(function (h0, i) {
        var h = h0 * oct, at = t + gr.at + (gr.scale ? i * beat : 0);
        node.port.postMessage({ type: "note", hz: h, in: at - c.currentTime, dur: A + gr.dur, vel: lvr });
        var v = { engine: true, hz: h, base: h0, rate: 1, level: lvr, A: A, dur: gr.dur, R: R, offset: loudAt, start: at, stopAt: at + A + gr.dur + R, chord: gr.name };
        playing.push(v); last.push(v); list.push(h);
      });
    });
    clearInterval(madeTimer);
    madeTimer = setInterval(function () {
      measureCreated().then(function (db) {
        $("#lab-made").textContent = db ? "pitch created: +" + Math.round(db.reduce(function (a, d) { return a + d; }, 0) / db.length) + " dB" : "";
      });
    }, 300);
    view();
    return { rates: list.map(function () { return 1; }), hz: list, gains: list.map(function () { return lvr; }) };
  }

  /* The playing view (Kerem, 2026-09-30: "I can not differentiate ... since I don't see which envelope
     plays which"): every voice as its envelope - attack ramp, hold, release - at its pitch, named, with a
     playhead; the line under it names what is sounding now. */
  var viewRaf = 0;
  function view() {
    cancelAnimationFrame(viewRaf);
    var cv = $("#lab-roll"), g = cv.getContext("2d"), vs = last.slice();
    cv.dataset.voices = vs.length;
    if (!vs.length || !ctx) { g.clearRect(0, 0, cv.width, cv.height); $("#lab-now").textContent = ""; return; }
    var t0 = vs[0].start, t1 = vs.reduce(function (m, v) { return Math.max(m, v.stopAt); }, 0);
    var lo, hi;   /* re-fitted every frame: Octave and Tune move the voices while they play */
    var L = 70, W = cv.width - L - 8, H = cv.height;
    var X = function (t) { return L + (t - t0) / Math.max(0.1, t1 - t0) * W; }, Y = function (h) { return H - 12 - Math.log(h / lo) / Math.log(hi / lo) * (H - 40); };
    var bar = Math.max(6, Math.min(22, (H - 24) / 14));
    (function frame() {
      var now = ctx.currentTime;
      lo = Math.min.apply(null, vs.map(function (v) { return v.hz; })) / 1.12; hi = Math.max.apply(null, vs.map(function (v) { return v.hz; })) * 1.12;
      g.clearRect(0, 0, cv.width, cv.height);
      g.font = "20px system-ui, sans-serif"; g.textBaseline = "middle";
      /* the chord names over their blocks (a progression), and the note names down the side without overlap */
      var seen = {};
      vs.forEach(function (v) { if (v.chord && !seen[v.start]) { seen[v.start] = 1; g.fillStyle = "#9ca49d"; g.fillText(v.chord, X(v.start) + 2, 12); } });
      var names = {}, lastY = -99;
      vs.map(function (v) { return v.hz; }).sort(function (a, b) { return b - a; }).forEach(function (h) {
        var y = Y(h) - bar / 2; if (names[h.toFixed(3)] || y - lastY < 22) { return; }
        names[h.toFixed(3)] = 1; lastY = y; g.fillStyle = "#9ca49d"; g.fillText(noteName(69 + 12 * Math.log2(h / 440)).replace(/ .*$/, ""), 4, y);
      });
      vs.forEach(function (v) {
        var y = Y(v.hz), a = X(v.start), b = X(v.start + v.A), c = X(v.start + v.A + v.dur), d = X(v.start + v.A + v.dur + v.R);
        var on = now >= v.start && now < v.stopAt && playing.indexOf(v) >= 0;
        g.fillStyle = on ? "rgba(186,230,177,0.85)" : "rgba(186,230,177,0.25)";
        g.beginPath(); g.moveTo(a, y); g.lineTo(b, y - bar); g.lineTo(c, y - bar); g.lineTo(d, y); g.closePath(); g.fill();
      });
      if (now >= t0 && now <= t1) { g.fillStyle = "#e3e7e4"; g.fillRect(X(now), 0, 2, H); }
      var names = vs.filter(function (v) { return now >= v.start && now < v.stopAt && playing.indexOf(v) >= 0; })
        .map(function (v) { return noteName(69 + 12 * Math.log2(v.hz / 440)) + " (" + (v.engine ? "res" : v.rate.toFixed(2) + "x") + ")"; });
      $("#lab-now").textContent = names.length ? "Now: " + names.join(" · ") : (now < t0 ? "Starting…" : "");
      if (now <= t1 && playing.length) { viewRaf = requestAnimationFrame(frame); }
    })();
  }
  /* The play buttons only when there is something to play, and a line saying why not (Kerem,
     2026-09-30: "I can not click to note scale chord progression buttons" - they looked ready and did
     nothing before a file, or with a file that has no pitch). */
  function gate() {
    var why = !prog ? "Routes are still loading." : !analysis ? (synth in ENGINE ? "Load a recording first." : "Load a pitched recording first.") :
      synth in ENGINE && !node ? "Starting the engine…" :
      !(analysis.f0 > 0) && synth === "retune" ? "This recording is unpitched: Retune has no pitch to move. Choose Synth → Resonator, Harmonic filter or Formant to play it." : "";
    document.querySelectorAll("[data-play]").forEach(function (b) { b.disabled = !!why; });
    $("#lab-why").textContent = why;
  }
  var litTimer = null;
  function light(kind) {
    document.querySelectorAll("[data-play]").forEach(function (b) { b.setAttribute("aria-pressed", String(b.dataset.play === kind)); });
    clearTimeout(litTimer);
    if (kind && ctx) {
      var end = last.reduce(function (m, v) { return Math.max(m, v.stopAt); }, 0);
      litTimer = setTimeout(function () { light(null); }, Math.max(0, end - ctx.currentTime) * 1000);
    }
  }
  /* An iPhone mutes web audio with its silent switch unless the page is a playback app: say so
     (iOS 16.4+), and play a silent element inside the tap, as the site does on phones. */
  var unlocked = false;
  function unlock() {
    if (unlocked) { return; }
    unlocked = true;
    try { if (navigator.audioSession) { navigator.audioSession.type = "playback"; } } catch (e) { /* older iOS */ }
    var el = $("#lab-keep");
    if (el) { try { var p = el.play(); if (p && p.catch) { p.catch(function () {}); } } catch (e) { /* declined */ } }
  }
  function stop() {
    light(null);
    if (node) { node.port.postMessage({ type: "stop" }); }
    clearInterval(madeTimer); if ($("#lab-made")) { $("#lab-made").textContent = ""; }
    var c = ctx; if (!c) { return; }
    var now = c.currentTime;
    playing.forEach(function (v) {
      if (v.engine) { v.stopAt = Math.min(v.stopAt, now + 0.01); return; }
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
    var line = t + " — " + ($("#lab-note").textContent || "no file") + ", tune " + $("#lab-tune").value + ", octave " + $("#lab-octave").value + ", " + (tuning ? "just" : "equal") +
      ", synth " + synth + (synth === "resonator" ? " " + ["string", "tube", "bell"][+$("#lab-body").value] + " " + ["bowed", "plucked"][+$("#lab-excite").value] : "") +
      (synth === "harmonic" || synth === "formant" ? " " + ["bank", "spectral", "comb"][+$("#lab-method").value] + " " + ["dry", "ringing"][+$("#lab-mode").value] : "") +
      (synth in ENGINE ? " focus " + $("#lab-focus").value + " colour " + $("#lab-colour").value : "");
    try { localStorage.setItem("fs.lab.kept", JSON.stringify(kept().concat([line]))); } catch (e) { /* private window: not kept */ }
    $("#lab-verdict").value = ""; renderKept();
  });
  renderKept();

  /* the audio engine is made inside the tap itself (Safari keeps one made later, in a promise, silent) */
  $("#lab-file").addEventListener("change", function () { audio(); if (this.files[0]) { load(this.files[0]); } });
  var drop = $("#lab-drop");
  drop.addEventListener("dragover", function (e) { e.preventDefault(); });
  drop.addEventListener("drop", function (e) { e.preventDefault(); audio(); if (e.dataTransfer.files[0]) { load(e.dataTransfer.files[0]); } });
  $("#lab-route").addEventListener("change", function () { setRoute(this.value); });
  $("#lab-just").addEventListener("click", function () { tuning = 1; this.setAttribute("aria-pressed", "true"); $("#lab-equal").setAttribute("aria-pressed", "false"); setRoute(route && route.id); });
  $("#lab-equal").addEventListener("click", function () { tuning = 0; this.setAttribute("aria-pressed", "true"); $("#lab-just").setAttribute("aria-pressed", "false"); setRoute(route && route.id); });
  document.querySelectorAll("[data-play]").forEach(function (b) {
    b.addEventListener("click", function () { unlock(); audio(); var r = play(b.dataset.play); if (r && !r.silent) { light(b.dataset.play); } });
  });
  $("#lab-stop").addEventListener("click", stop);
  $("#lab-octave").addEventListener("change", retune);
  $("#lab-tune").addEventListener("input", retune);
  $("#lab-synth").addEventListener("change", function () { setSynth(this.value); });
  ["#lab-body", "#lab-excite", "#lab-method", "#lab-mode"].forEach(function (id) { $(id).addEventListener("change", sendParams); });
  $("#lab-colour").addEventListener("input", colourLabel);
  ["#lab-focus", "#lab-colour", "#lab-tune", "#lab-attack", "#lab-release"].forEach(function (id) { $(id).addEventListener("input", sendParams); });

  /* what the audio is doing, readable on any machine: the engine's state and the level leaving it */
  setInterval(function () {
    var el = $("#lab-audio"); if (!el) { return; }
    el.textContent = !ctx ? "Audio: not started" : "Audio: " + ctx.state + " · output " + Math.round(levelDb()) + " dB" +
      (ctx.state !== "running" ? " — tap a play button to start it" : "");
  }, 250);

  /* The Route panel (sample harmony 3a): the route above played by the real route engine (device "piece", a second node
     beside the bench), each role Digital or Sampler. A sampler role plays the loaded recording with the controls below
     (Body, Excite, Method, Mode, Focus, Colour, Tune). The walk is simulated: once along the route in the chosen minutes. */
  var DIGITAL = [["fm", "FM"], ["am", "AM"], ["duo", "Duo"], ["mono", "Mono"], ["simple", "Simple"], ["pluck", "Pluck"], ["metal", "Metal"],
    ["membrane", "Membrane"], ["wavetable", "Wavetable"], ["comb", "Comb"], ["formant", "Formant"]];
  var SAMPLER = [["s-retune", "Retune"], ["s-resonator", "Resonator"], ["s-harmonic", "Harmonic filter"], ["s-formant", "Formant"],
    ["s-pulsar", "Pulsar"], ["s-freeze", "Freeze"]];
  var ROLES = [["voice", 0], ["sect", 1], ["v3", 2]], pnode = null, pready = null, rtT = 0, rtTimer = null, rtChord = null;
  ROLES.forEach(function (r) {
    var sel = $("#lab-rt-" + r[0]); if (!sel) { return; }
    var html = "<option value=''>As the route</option><optgroup label='Digital'>" + DIGITAL.map(function (d) { return "<option value='" + d[0] + "'>" + d[1] + "</option>"; }).join("") +
      "</optgroup><optgroup label='Sampler'>" + SAMPLER.map(function (d) { return "<option value='" + d[0] + "'>" + d[1] + "</option>"; }).join("") + "</optgroup>";
    sel.innerHTML = html;
    sel.addEventListener("change", function () { if (rtTimer) { sendRolesAndPatch(false); } });
  });
  function utf8(str) { return new TextEncoder().encode(str); }
  function ensurePiece() {
    if (pready) { return pready; }
    pready = ensureEngine().then(function () {
      pnode = new AudioWorkletNode(audio(), "fieldscape-core", { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm: wasmBytes, device: "piece", params: [] } });
      pnode.connect(bus);
      pnode.port.onmessage = function (e) {
        var m = e.data; if (!m || m.type !== "chord") { return; }
        $("#lab-rt-now").textContent = m.i >= 0 ? "Chord " + (m.i + 1) + " of " + m.count + ": " + new TextDecoder().decode(m.label) + " · " + Math.round(rtT * 100) + "% along" : "";
      };
    });
    return pready;
  }
  /* the route's patch with the roles as chosen here */
  function rtPatch() {
    var p = JSON.parse(JSON.stringify((route && route.patch) || {}));
    var smp = { body: +$("#lab-body").value, excite: +$("#lab-excite").value, method: +$("#lab-method").value, mode: +$("#lab-mode").value,
      focus: +$("#lab-focus").value, colour: +$("#lab-colour").value, tune: +$("#lab-tune").value };
    ROLES.forEach(function (r) {
      var v = $("#lab-rt-" + r[0]).value; if (!v) { return; }
      p[r[0]] = p[r[0]] || {}; p[r[0]].synth = v; p[r[0]].on = true;
      if (v.indexOf("s-") === 0) { p[r[0]].sampler = smp; }
    });
    return p;
  }
  function sendRolesAndPatch(first) {
    var c = audio();
    ROLES.forEach(function (r) {
      var v = $("#lab-rt-" + r[0]).value;
      if (v.indexOf("s-") !== 0 || !buffer) { return; }
      var ch = []; for (var k = 0; k < buffer.numberOfChannels; k++) { ch.push(buffer.getChannelData(k).slice(0)); }
      pnode.port.postMessage({ type: "role", role: r[1], channels: ch });
      if (analysis) { pnode.port.postMessage({ type: "analysis", role: r[1], bytes: utf8(JSON.stringify(analysis)) }); }
    });
    pnode.port.postMessage({ type: first ? "route" : "patch", bytes: utf8(JSON.stringify(rtPatch())), tuning: tuning ? 1 : 0 });
    if (c.state === "suspended") { c.resume(); }
  }
  function rtPlay() {
    if (!route) { $("#lab-rt-now").textContent = "Routes are still loading."; return; }
    ensurePiece().then(function () {
      rtStop(true);
      sendRolesAndPatch(true); rtT = 0;
      pnode.port.postMessage({ type: "walk", t: 0 });
      rtTimer = setInterval(function () {
        rtT += 0.25 / (60 * +$("#lab-rt-pace").value); if (rtT > 1) { rtT -= 1; }
        pnode.port.postMessage({ type: "walk", t: rtT });
      }, 250);
      rtChord = setInterval(function () { pnode.port.postMessage({ type: "chord" }); }, 500);
    });
  }
  function rtStop(quiet) {
    clearInterval(rtTimer); clearInterval(rtChord); rtTimer = rtChord = null;
    if (pnode) { pnode.port.postMessage({ type: "walk", off: true }); }
    if (!quiet) { $("#lab-rt-now").textContent = ""; }
  }
  if ($("#lab-rt-play")) {
    $("#lab-rt-play").addEventListener("click", rtPlay);
    $("#lab-rt-stop").addEventListener("click", function () { rtStop(false); });
    $("#lab-rt-pace").addEventListener("input", function () { $("#lab-rt-pace-l").textContent = $("#lab-rt-pace").value + " min"; });
  }

  window.fsLab = { ready: ready, load: load, get analysis() { return analysis; }, get routes() { return routes; }, setRoute: setRoute, play: play, stop: stop,
    get peak() { return peak; }, level: levelDb,
    peakOut: function () { if (!meter) { return 0; } var a = new Float32Array(meter.fftSize), m = 0; meter.getFloatTimeDomainData(a); for (var i = 0; i < a.length; i++) { m = Math.max(m, Math.abs(a[i])); } return m; }, peakHz: peakHz, get bufferSeconds() { return buffer ? buffer.duration : 0; },
    now: function () { return ctx ? ctx.currentTime : 0; },
    voices: function () { return last.map(function (v) { return { start: v.start, stopAt: v.stopAt, offset: v.offset, hz: v.hz }; }); },
    setSynth: setSynth, get engineReady() { return engineReady || Promise.resolve(); },
    created: measureCreated,
    /* the engine's own timing: blocks over their time (underruns) and the slowest, in whole ms on the web */
    stats: function () {
      if (!node) { return Promise.resolve(null); }
      return new Promise(function (res) {
        var h = function (e) { if (e.data && e.data.type === "stats") { node.port.removeEventListener("message", h); res(e.data); } };
        node.port.addEventListener("message", h); node.port.start(); node.port.postMessage({ type: "stats" });
      });
    } };
})();
