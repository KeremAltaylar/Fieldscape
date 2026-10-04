/* The Fieldscape core in an AudioWorklet. The main thread fetches core.wasm and passes its bytes
   in processorOptions: { wasm, device, params: [[index, value], ...] }. Params can also arrive
   later on the port as [index, value]. Same C interface as iOS and Android (core/fieldscape.h). */
class FieldscapeCore extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    const o = opts.processorOptions;
    let mem;
    const inst = new WebAssembly.Instance(new WebAssembly.Module(o.wasm), {
      /* views are re-created on every call, so memory growth needs no notice */
      env: new Proxy({}, { get: () => () => 0 }),
      wasi_snapshot_preview1: new Proxy({
        /* the core's timing stats; the audio thread has only Date.now(), so on the web these
           are millisecond-coarse - phone timing comes from the native hosts */
        clock_time_get: (id, prec, ptr) => { new BigInt64Array(mem.buffer, ptr, 1)[0] = BigInt(Date.now()) * 1000000n; return 0; }
      }, { get: (t, k) => t[k] || (() => 0) })
    });
    const x = this.x = inst.exports;
    mem = x.memory;
    if (x._initialize) { x._initialize(); }
    /* No TextEncoder in AudioWorkletGlobalScope; device ids are ASCII. */
    const name = Uint8Array.from(o.device + "\0", (ch) => ch.charCodeAt(0));
    const p = x.malloc(name.length);
    new Uint8Array(x.memory.buffer, p, name.length).set(name);
    this.dev = x.fs_create(p);
    x.free(p);
    x.fs_prepare(this.dev, sampleRate, 128);
    for (const [i, v] of o.params || []) { x.fs_set_param(this.dev, i, v); }
    this.stats = x.malloc(32);
    this.port.onmessage = (e) => {
      const m = e.data;
      if (Array.isArray(m)) { x.fs_set_param(this.dev, m[0], m[1]); }
      else if (m.type === "source") {
        try { this.setSource(m.channels); this.port.postMessage({ type: "source", frames: m.channels[0].length }); }
        catch (err) { this.port.postMessage({ type: "error", where: "source", message: String(err) }); }
      }
      else if (m.type === "note") { x.fs_bench_note(this.dev, m.hz, x.fs_bench_time(this.dev) + m.in, m.dur, m.vel); }
      else if (m.type === "stop") { x.fs_bench_stop(this.dev); }
      else if (m.type === "ring") {             /* a copy only; the page does the measuring */
        if (!this.ring) { this.ring = x.malloc(4 * 65536); }
        const n = x.fs_bench_ring(this.dev, this.ring), a = new Float32Array(n);
        a.set(new Float32Array(x.memory.buffer, this.ring, n));
        this.port.postMessage({ type: "ring", samples: a }, [a.buffer]);
      }
      else if (m.type === "stats") {
        x.fs_stats(this.dev, this.stats);
        const v = new DataView(x.memory.buffer, this.stats, 32);
        this.port.postMessage({ type: "stats", late: v.getInt32(0, true), underruns: v.getInt32(4, true),
          maxMs: v.getFloat32(8, true), frames: Number(v.getBigInt64(16, true)), wraps: v.getInt32(24, true) });
      }
    };
  }
  /* Copies the recording into wasm memory once; the device reads it from there (host-owned). */
  setSource(channels) {
    const x = this.x, n = channels[0].length, ptrs = x.malloc(4 * channels.length);
    if (this.src) { x.fs_set_source(this.dev, 0, 0, 0); this.src.forEach((p) => x.free(p)); x.free(this.srcPtrs); }
    this.src = channels.map((c) => { const p = x.malloc(4 * n); new Float32Array(x.memory.buffer, p, n).set(c); return p; });
    new Uint32Array(x.memory.buffer, ptrs, channels.length).set(this.src);
    this.srcPtrs = ptrs;
    x.fs_set_source(this.dev, channels.length, n, ptrs);
  }
  process(inputs, outputs) {
    const x = this.x, out = outputs[0], inp = inputs[0], n = out[0].length;
    for (let c = 0; c < 2; c++) {
      if (inp && inp[c]) { new Float32Array(x.memory.buffer, x.fs_in(this.dev, c), n).set(inp[c]); }
    }
    x.fs_process(this.dev, n);
    for (let c = 0; c < out.length; c++) {
      out[c].set(new Float32Array(x.memory.buffer, x.fs_out(this.dev, Math.min(c, 1)), n));
    }
    return true;
  }
}
registerProcessor("fieldscape-core", FieldscapeCore);

/* The whole walk (core/engine.cpp) in an AudioWorklet: the four stretch voices, the piece and the
   mix, driven by positions from the page. Everything is called on this audio thread, between
   renders. Messages in: { type: "features" | "places", bytes: Uint8Array (UTF-8 GeoJSON) },
   { type: "walk", lon, lat }, { type: "source", kind: "S" | "R", index, sub, id, channels, pcm:
   Int16Array interleaved }. Out: { type: "need", bytes } (lines "S slot id path" / "R handle slot
   id path") and { type: "state", bytes } (JSON) - raw UTF-8, since this scope has neither
   TextEncoder nor TextDecoder. */
class FieldscapeEngine extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    let mem;
    const inst = new WebAssembly.Instance(new WebAssembly.Module(opts.processorOptions.wasm), {
      env: new Proxy({}, { get: () => () => 0 }),
      wasi_snapshot_preview1: new Proxy({
        clock_time_get: (id, prec, ptr) => { new BigInt64Array(mem.buffer, ptr, 1)[0] = BigInt(Date.now()) * 1000000n; return 0; }
      }, { get: (t, k) => t[k] || (() => 0) })
    });
    const x = this.x = inst.exports;
    mem = x.memory;
    if (x._initialize) { x._initialize(); }
    this.e = x.fs_engine_create(sampleRate, 128);
    /* the screen's live reads (fs_engine_morphs / _chord / _route), ~30 times a second */
    this.liveEvery = Math.max(1, Math.round(sampleRate / 128 / 30)); this.liveN = 0;
    this.morphs = x.malloc(7 * 24 * 8); this.nums = x.malloc(24); this.label = x.malloc(32);
    this.port.onmessage = (ev) => {
      const m = ev.data;
      try {
        if (m.type === "features" || m.type === "places") {
          const p = this.bytes(m.bytes);
          (m.type === "features" ? x.fs_engine_features : x.fs_engine_places)(this.e, p);
          x.free(p);
        } else if (m.type === "walk") {
          const need = x.fs_engine_step(this.e, m.lon, m.lat);
          this.port.postMessage({ type: "need", bytes: this.cstr(need) });
          this.port.postMessage({ type: "state", bytes: this.cstr(x.fs_engine_state(this.e)) });
        } else if (m.type === "upsert") {          /* a setter's edit: one feature, new or changed */
          const p = this.bytes(m.bytes);
          x.fs_engine_upsert(this.e, p);
          x.free(p);
        } else if (m.type === "remove") {
          const id = this.bytes(Uint8Array.from(m.id + "", (ch) => ch.charCodeAt(0)));
          x.fs_engine_remove(this.e, id);
          x.free(id);
        } else if (m.type === "solo") {
          const id = this.bytes(Uint8Array.from(m.id + "", (ch) => ch.charCodeAt(0)));
          x.fs_engine_solo(this.e, id);
          x.free(id);
        } else if (m.type === "source") {
          const n = m.pcm.length, p = x.fs_alloc_i16(n);
          new Int16Array(x.memory.buffer, p, n).set(m.pcm);
          const id = this.bytes(Uint8Array.from(m.id + "", (ch) => ch.charCodeAt(0)));
          x.fs_engine_source(this.e, m.kind.charCodeAt(0), m.index, m.sub, id, m.channels, BigInt(n / m.channels), p);
          x.free(id);
        }
      } catch (err) { this.port.postMessage({ type: "error", message: String(err && err.stack || err) }); }
    };
  }
  /* nums: clock (f64) at 0, root at 8, shown at 12, chord count at 16 (i32) */
  live() {
    const x = this.x, p = this.nums;
    const n = x.fs_engine_morphs(this.e, this.morphs, 24, p, p + 8, p + 12);
    const step = x.fs_engine_chord(this.e, p + 16, this.label, 32);
    const v = new DataView(x.memory.buffer, p, 24);
    const m = new Float64Array(7 * Math.max(0, n));
    m.set(new Float64Array(x.memory.buffer, this.morphs, m.length));
    this.port.postMessage({ type: "live", n, clock: v.getFloat64(0, true), root: v.getInt32(8, true), shown: v.getInt32(12, true),
      route: x.fs_engine_route(this.e), chord: { step, count: v.getInt32(16, true), label: this.cstr(this.label) }, morphs: m }, [m.buffer]);
  }
  bytes(u8) {      /* a NUL-terminated copy in wasm memory; free it after the call */
    const p = this.x.malloc(u8.length + 1);
    const v = new Uint8Array(this.x.memory.buffer, p, u8.length + 1);
    v.set(u8); v[u8.length] = 0;
    return p;
  }
  cstr(p) {
    const b = new Uint8Array(this.x.memory.buffer);
    let e = p; while (b[e]) { e++; }
    return b.slice(p, e);
  }
  process(inputs, outputs) {
    const x = this.x, out = outputs[0], n = out[0].length;
    x.fs_engine_process(this.e, n);
    for (let c = 0; c < out.length; c++) {
      out[c].set(new Float32Array(x.memory.buffer, x.fs_engine_out(this.e, Math.min(c, 1)), n));
    }
    if (++this.liveN >= this.liveEvery) { this.liveN = 0; this.live(); }
    return true;
  }
}
registerProcessor("fieldscape-engine", FieldscapeEngine);

/* The lab's master (sample harmony 2b): x2.5 (2c listening: x2 still "too low"), then a 5 ms look-ahead limiter to -1 dBFS. Kerem heard "clicks and clips"
   on dense Dry progressions: the oversampled curve before this rang past full scale (output peaks 1.14). Here the gain
   falls over the 5 ms before a peak arrives, so nothing passes -1 dBFS and nothing is shaped; quiet passages are
   exactly x2.5. Lab only - the walk's FieldscapeEngine never uses it. */
class FsLimiter extends AudioWorkletProcessor {
  constructor() {
    super();
    this.L = Math.round(sampleRate * 0.005); this.buf = [new Float32Array(this.L), new Float32Array(this.L)];
    this.peaks = new Float32Array(this.L); this.w = 0; this.g = 1;
    this.ka = 1 - Math.exp(-1 / (0.0008 * sampleRate)); this.kr = 1 - Math.exp(-1 / (0.15 * sampleRate));
  }
  process(inputs, outputs) {
    const inp = inputs[0] || [], out = outputs[0], n = out[0].length, C = 0.891, L = this.L;
    const src = (c, i) => 2.5 * (((inp[c] || inp[0]) || [])[i] || 0);   /* x2.5 (Kerem 2026-10-04: "too low") */
    for (let i = 0; i < n; i++) {
      let p = 0; for (let c = 0; c < out.length; c++) p = Math.max(p, Math.abs(src(c, i)));
      this.peaks[this.w] = p;
      let m = 0; for (let k = 0; k < L; k++) if (this.peaks[k] > m) m = this.peaks[k];   /* ponytail: an O(L) scan per sample (~0.05 ms a block); a monotonic deque if it ever costs */
      const tgt = m > C ? C / m : 1;
      this.g += (tgt - this.g) * (tgt < this.g ? this.ka : this.kr);
      for (let c = 0; c < out.length; c++) {
        const x = src(c, i), d = this.buf[c][this.w]; this.buf[c][this.w] = x;
        const y = d * this.g; out[c][i] = y > C ? C : y < -C ? -C : y;
      }
      this.w = (this.w + 1) % L;
    }
    return true;
  }
}
registerProcessor("fs-limiter", FsLimiter);
