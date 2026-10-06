/* Sample harmony 3b/3c: a route's role setups (lab_route_roles) and their samples (recordings/lab/<route>/), shared by
   lab.html and the site's patch panel in lab mode (docs/superpowers/specs/2026-10-05-samples-3c-patch-screen-design.md). */
(function (root) {
  "use strict";
  var ROLES = ["voice", "sect", "v3"], ROLE_INDEX = { voice: 0, sect: 1, v3: 2 }, MAX_S = 30;
  var SAMPLER = [["s-retune", "Retune"], ["s-resonator", "Resonator"], ["s-harmonic", "Harmonic filter"], ["s-formant", "Formant"],
    ["s-pulsar", "Pulsar"], ["s-freeze", "Freeze"]];
  var NAMES = { voice: "Voice", sect: "Sections", v3: "Third voice" };
  function isSampler(s) { return typeof s === "string" && s.indexOf("s-") === 0; }
  function esc(t) { return String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/'/g, "&#39;").replace(/"/g, "&quot;"); }
  function sampleLabel(name, a, note) { return name + " · " + (a && a.f0 > 0 ? Math.round(a.f0) + " Hz" : "unpitched") + (note ? " · " + note : ""); }
  function colourName(syn, c, seconds) {
    if (syn === "s-retune") { return "Brightness"; }
    if (syn === "s-harmonic") { return "Overtones"; }
    if (syn === "s-formant") { return "Partial " + Math.round(1 + 15 * c); }
    if (syn === "s-pulsar") { return "Grain " + Math.round(100 * (0.05 + 0.95 * c)) + " %"; }
    if (syn === "s-freeze") { var s = Math.max(0, c * ((seconds || 0) - 2048 / 48000)); return "Moment " + Math.floor(s / 60) + ":" + ("0" + Math.floor(s % 60)).slice(-2); }
    return "Colour";
  }
  function wavBytes(chs, sr) {   /* ponytail: 16-bit; 24-bit if quiet samples ever sound grainy */
    var ch = chs.length, n = chs[0].length, v = new DataView(new ArrayBuffer(44 + n * ch * 2));
    var w = function (o, t) { for (var i = 0; i < t.length; i++) { v.setUint8(o + i, t.charCodeAt(i)); } };
    w(0, "RIFF"); v.setUint32(4, 36 + n * ch * 2, true); w(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, ch, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * ch * 2, true); v.setUint16(32, ch * 2, true); v.setUint16(34, 16, true); w(36, "data"); v.setUint32(40, n * ch * 2, true);
    for (var i = 0, o = 44; i < n; i++) { for (var c = 0; c < ch; c++, o += 2) { v.setInt16(o, Math.round(32767 * Math.max(-1, Math.min(1, chs[c][i]))), true); } }
    return new Uint8Array(v.buffer);
  }
  function wav(b) { var chs = []; for (var c = 0; c < b.numberOfChannels; c++) { chs.push(b.getChannelData(c)); } return new Blob([wavBytes(chs, b.sampleRate)], { type: "audio/wav" }); }
  function trimmed(ctx, b) {
    var n = Math.min(b.length, Math.round(MAX_S * b.sampleRate)), t = ctx.createBuffer(b.numberOfChannels, n, b.sampleRate);
    for (var c = 0; c < b.numberOfChannels; c++) { t.copyToChannel(b.getChannelData(c).subarray(0, n), c); }
    return t;
  }
  function silence(ctx) { return ctx.createBuffer(1, 128, ctx.sampleRate); }
  function overlay(patch, roles) {
    var p = JSON.parse(JSON.stringify(patch || {}));
    if (!roles) { return p; }
    ROLES.forEach(function (r) {
      var rr = roles[r]; if (!rr || !isSampler(rr.synth)) { return; }
      p[r] = p[r] || {}; p[r].synth = rr.synth; p[r].sampler = JSON.parse(JSON.stringify(rr.sampler || {}));
      delete p[r].harm; delete p[r].index;
    });
    return p;
  }
  /* 3c.1 F1: the sounding parts of a recording, in order. 50 ms blocks; a block sounds when its energy is at least the
     loudest block's -40 dB (the analyser's silence rule); a silent run under 0.25 s is kept (the breath in a call) */
  var BLOCK_S = 0.05, KEEP_GAP_S = 0.25, FADE_S = 0.005;
  function compactPlan(chs, sr) {
    var B = Math.max(1, Math.round(BLOCK_S * sr)), n = chs[0].length, nb = Math.ceil(n / B), e = new Float64Array(nb), top = 0;
    for (var b = 0; b < nb; b++) {
      var s = 0, z = Math.min(n, (b + 1) * B);
      for (var c = 0; c < chs.length; c++) { var d = chs[c]; for (var i = b * B; i < z; i++) { s += d[i] * d[i]; } }
      e[b] = s / ((z - b * B) * chs.length); if (e[b] > top) { top = e[b]; }
    }
    var kept = new Uint8Array(nb);
    for (b = 0; b < nb; b++) { kept[b] = top > 0 && e[b] >= top * 1e-4 ? 1 : 0; }
    if (top <= 0) { kept.fill(1); }
    var gap = Math.ceil(KEEP_GAP_S / BLOCK_S);          /* silent runs shorter than this between sounding blocks: kept */
    for (b = 0; b < nb; b++) {
      if (kept[b]) { continue; }
      var r = b; while (r < nb && !kept[r]) { r++; }
      if (b > 0 && r < nb && r - b < gap) { for (var k = b; k < r; k++) { kept[k] = 1; } }
      b = r;
    }
    var segs = [];
    for (b = 0; b < nb; b++) { if (kept[b] && (b === 0 || !kept[b - 1])) { var q = b; while (q < nb && kept[q]) { q++; } segs.push([b * B, Math.min(n, q * B)]); } }
    return { block: B, kept: kept, segments: segs };
  }
  function compactData(chs, sr, analysis) {
    var plan = compactPlan(chs, sr), n = 0, F = Math.max(1, Math.round(FADE_S * sr));
    plan.segments.forEach(function (sg) { n += sg[1] - sg[0]; });
    var whole = plan.segments.length === 1 && plan.segments[0][0] === 0 && plan.segments[0][1] === chs[0].length;
    var out = chs.map(function () { return new Float32Array(n); });
    var o = 0;
    plan.segments.forEach(function (sg, si) {
      var len = sg[1] - sg[0];
      for (var c = 0; c < chs.length; c++) {
        out[c].set(chs[c].subarray(sg[0], sg[1]), o);
        if (whole) { continue; }
        for (var i = 0; i < Math.min(F, len); i++) {
          var g = 0.5 - 0.5 * Math.cos(Math.PI * (i + 0.5) / F);       /* raised cosine: no step at a join */
          if (si > 0 || sg[0] > 0) { out[c][o + i] *= g; }
          if (si < plan.segments.length - 1 || sg[1] < chs[0].length) { out[c][o + len - 1 - i] *= g; }
        }
      }
      o += len;
    });
    var a = analysis ? JSON.parse(JSON.stringify(analysis)) : null;
    if (a && a.track && a.hop_s) {
      a.track = a.track.filter(function (fr, i) { var b = Math.floor(i * a.hop_s * sr / plan.block); return b < plan.kept.length && plan.kept[b]; });
    }
    if (a) { a.frames = n; }
    return { channels: out, analysis: a, kept: plan.kept, block: plan.block };
  }
  function prepare(ctx, b, analysis) {
    var chs = []; for (var c = 0; c < b.numberOfChannels; c++) { chs.push(b.getChannelData(c)); }
    var cd = compactData(chs, b.sampleRate, analysis), out = ctx.createBuffer(chs.length, Math.max(1, cd.channels[0].length), b.sampleRate);
    for (c = 0; c < chs.length; c++) { out.copyToChannel(cd.channels[c], c); }
    return { buf: out, analysis: cd.analysis, raw: b, kept: cd.kept, block: cd.block };
  }
  /* 3d: Position's place in the (compacted) recording, as the core reads it (samplers.hpp start_retune): from the pitch
     track's first confident frame, stopping 0.5 s (or half what is left) short of the end */
  function positionFrame(v, len, analysis, sr) {
    var from = 0, tr = analysis && analysis.track, hop = (analysis && analysis.hop_s) || 0.02;
    if (tr) { for (var i = 0; i < tr.length; i++) { if (tr[i][0] > 0 && tr[i][1] >= 0.8) { from = Math.floor(i * hop * sr); break; } } }
    from = Math.min(from, Math.max(0, len - 2));
    var room = Math.max(0, len - from - 2), tail = Math.min(0.5 * sr, 0.5 * room);
    return from + Math.min(1, Math.max(0, v)) * (room - tail);
  }
  function decodeSample(sb, ctx, smp) {
    return sb.storage.from("recordings").download(smp.path).then(function (d) {
      if (d.error || !d.data) { throw new Error((d.error && d.error.message) || "not found"); }
      return d.data.arrayBuffer();
    }).then(function (ab) { return ctx.decodeAudioData(ab); }).then(function (b) { return prepare(ctx, trimmed(ctx, b), smp.analysis || null); });
  }
  function fetchRoute(sb, ctx, id) {
    return sb.from("lab_route_roles").select("roles").eq("route_id", id).maybeSingle().then(function (q) {
      if (q.error) { throw new Error(q.error.message); }
      var roles = q.data ? q.data.roles || {} : null, buf = {}, ana = {};
      if (!roles) { return { roles: null, buf: buf, ana: ana }; }
      return Promise.all(ROLES.map(function (r) {
        var rr = roles[r]; if (!rr || !isSampler(rr.synth)) { return null; }
        if (!rr.sample || !rr.sample.path) { buf[r] = silence(ctx); ana[r] = null; return null; }
        return decodeSample(sb, ctx, rr.sample).then(function (p) { buf[r] = p.buf; ana[r] = p.analysis; },
          function () { buf[r] = silence(ctx); ana[r] = null; });
      })).then(function () { return { roles: roles, buf: buf, ana: ana }; });
    });
  }
  function sendRole(port, role, b, a) {
    var ch = []; for (var k = 0; k < b.numberOfChannels; k++) { ch.push(b.getChannelData(k).slice(0)); }
    port.postMessage({ type: "role", role: role, channels: ch });
    if (a) { port.postMessage({ type: "analysis", role: role, bytes: new TextEncoder().encode(JSON.stringify(a)) }); }
  }
  function session(o) {
    var s = { defaults: { voice: { gain: 0.45, harm: 1, index: 4 }, sect: { gain: 0.8, harm: 2.02, index: 7.5 }, v3: { gain: 0.55, harm: 1.5, index: 3 } },
      gen: 0, loading: false, failed: false, loadedFor: null };
    var st = {}, bufs = {}, anas = {}, raws = {}, kepts = {}, blocks = {}, recIds = { voice: 0, sect: 0, v3: 0 }, uploading = [];
    var keep = function (r, p) { bufs[r] = p.buf; anas[r] = p.analysis; raws[r] = p.raw; kepts[r] = p.kept; blocks[r] = p.block; };
    var changed = function (r) { if (o.onChange) { o.onChange(r); } };
    s.state = function (r) {
      if (!st[r]) { var d = s.defaults[r]; st[r] = { synth: "", gain: d.gain, harm: d.harm, index: d.index, body: 0, excite: 0, method: 0, mode: 0, focus: 0.5, colour: 0.5, tune: 1, sample: null, sampleNote: "" }; }
      return st[r];
    };
    s.buf = function (r) { return bufs[r] || null; };
    /* the waveform's: the sample as recorded (trimmed), which 50 ms blocks were kept, and the block length (3c.1) */
    s.raw = function (r) { return raws[r] || null; };
    s.kept = function (r) { return kepts[r] || null; };
    s.block = function (r) { return blocks[r] || 0; };
    s.ana = function (r) { return anas[r] || null; };
    s.recId = function (r) { return recIds[r]; };
    s.json = function () {
      var out = {};
      ROLES.forEach(function (r) { var x = s.state(r);
        out[r] = { synth: x.synth, gain: x.gain, harm: x.harm, index: x.index,
          sampler: { body: x.body, excite: x.excite, method: x.method, mode: x.mode, focus: x.focus, colour: x.colour, tune: x.tune },
          sample: x.sample && x.sample.path ? { path: x.sample.path, name: x.sample.name, analysis: x.sample.analysis } : null }; });
      return out;
    };
    /* each route choice is a generation: a load or upload from an earlier one never lands on the route now shown (3b I2/I5) */
    s.load = function (id) {
      st = {}; bufs = {}; anas = {}; raws = {}; kepts = {}; blocks = {}; var gen = ++s.gen; s.failed = false; s.loadedFor = null;
      if (!o.sb || !id) { s.loading = false; changed(null); return Promise.resolve(); }
      s.loading = true; changed(null);
      return o.sb.from("lab_route_roles").select("roles").eq("route_id", id).maybeSingle().then(function (q) {
        if (gen !== s.gen) { return; }
        s.loading = false;
        if (q.error) { s.failed = true; s.loadError = q.error.message; changed(null); return; }
        s.loadedFor = id;
        var roles = q.data ? q.data.roles || {} : {};
        ROLES.forEach(function (r) {
          var rr = roles[r]; if (!rr) { return; }
          var x = s.state(r); x.synth = rr.synth || "";
          ["gain", "harm", "index"].forEach(function (k) { if (rr[k] != null) { x[k] = rr[k]; } });
          if (rr.sampler) { Object.keys(rr.sampler).forEach(function (k) { x[k] = rr.sampler[k]; }); }
          x.sample = rr.sample || null; x.sampleNote = rr.sample ? "loading…" : "";
          if (rr.sample && rr.sample.path) {
            decodeSample(o.sb, o.ctx(), rr.sample).then(function (b) {
              if (gen !== s.gen) { return; } keep(r, b); recIds[r]++; x.sampleNote = ""; changed(r);
            }, function (e) {
              if (gen !== s.gen) { return; } bufs[r] = silence(o.ctx()); anas[r] = null; raws[r] = null; kepts[r] = null; recIds[r]++;
              x.sampleNote = "sample missing (" + (e && e.message || e) + ") - this role is silent"; changed(r);
            });
          }
        });
        changed(null);
      });
    };
    s.upload = function (r, file, id) {
      var x = s.state(r), gen = s.gen;
      return file.arrayBuffer().then(function (ab) { return o.ctx().decodeAudioData(ab); }).then(function (b) {
        var buf = trimmed(o.ctx(), b), n = buf.length, mono = new Float32Array(n);
        for (var c = 0; c < buf.numberOfChannels; c++) { var d = buf.getChannelData(c); for (var i = 0; i < n; i++) { mono[i] += d[i] / buf.numberOfChannels; } }
        return Promise.resolve(o.analyse(mono, buf.sampleRate)).then(function (a) { return { buf: buf, analysis: a }; });
      }).then(function (res) {
        if (gen !== s.gen) { return; }
        keep(r, prepare(o.ctx(), res.buf, res.analysis)); recIds[r]++;   /* the silence cut out; the upload stays as recorded */
        x.sample = { path: null, name: file.name, analysis: res.analysis };
        if (!o.sb || !id) { x.sampleNote = "not saved: log in as a setter on the site to save"; changed(r); return; }
        var path = "lab/" + id + "/" + r + "-" + Date.now() + ".wav";
        x.sampleNote = "uploading…"; changed(r);
        var up = o.sb.storage.from("recordings").upload(path, wav(res.buf), { upsert: false, contentType: "audio/wav" }).then(function (u) {
          if (u.error) { x.sampleNote = "upload failed: " + u.error.message; } else { x.sample.path = path; x.sampleNote = "uploaded"; }
          changed(r);
        });
        uploading.push(up);
        return up;
      }).catch(function (e) { x.sampleNote = "could not read " + file.name + ": " + (e && e.message || e); changed(r); });
    };
    s.save = function (id, userId) {
      if (!o.sb || !userId) { return Promise.resolve("Log in as a setter on the site to save."); }
      if (!id) { return Promise.resolve("Choose a route first."); }
      if (s.loading) { return Promise.resolve("Not saved: the route's saved roles are still loading - try again in a moment."); }
      if (s.failed) { return Promise.resolve("Not saved: the route's saved roles did not load, so saving could overwrite them - choose the route again."); }
      /* only the route these roles belong to: a save for another (a slow upload's, a timer's) never writes these into it (3c I1) */
      if (id !== s.loadedFor) { return Promise.resolve("Not saved: another route is open now"); }
      var gen = s.gen, waiting = uploading.slice(); uploading = [];
      return Promise.all(waiting).then(function () {
        if (gen !== s.gen || id !== s.loadedFor) { return "Not saved: the route changed while the sample uploaded"; }
        var bad = ROLES.filter(function (r) { var x = s.state(r); return x.sample && !x.sample.path; })[0];
        if (bad) { return "Not saved: the " + NAMES[bad] + " sample did not upload - choose it again"; }
        var roles = s.json();
        return o.sb.from("lab_route_roles").upsert({ route_id: id, roles: roles, updated_by: userId }).then(function (q) {
          if (q.error) { return "Not saved: " + q.error.message; }
          s.lastSaved = roles; return null;
        });
      }).catch(function (e) { return "Not saved: " + (e && e.message || e); });
    };
    return s;
  }
  var api = { ROLES: ROLES, ROLE_INDEX: ROLE_INDEX, MAX_S: MAX_S, SAMPLER: SAMPLER, NAMES: NAMES, isSampler: isSampler, esc: esc,
    sampleLabel: sampleLabel, colourName: colourName, wavBytes: wavBytes, wav: wav, trimmed: trimmed, overlay: overlay,
    fetchRoute: fetchRoute, sendRole: sendRole, session: session, decodeSample: decodeSample, silence: silence, compactPlan: compactPlan, compactData: compactData, prepare: prepare, positionFrame: positionFrame };
  root.FsRoles = api;
})(typeof globalThis !== "undefined" ? globalThis : self);
