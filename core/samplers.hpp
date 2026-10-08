/* The synths that play recordings (sample harmony, docs/superpowers/specs/2026-09-30-sample-harmony-design.md).
   2a: the Resonator (docs/superpowers/specs/2026-09-30-non-pitch-2a-resonator-design.md) - an unpitched
   recording feeds a tuned resonator per note, so wind or water plays the chord from its own material.
   A tone::Synth: notes at times on the caller's clock, rendered by adding into the buffers given. */
#pragma once
#include "synths.hpp"
#include "devices/fft.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace sampler {

enum Body { STRING = 0, TUBE = 1, BELL = 2 };
enum Excite { BOWED = 0, PLUCKED = 1 };
/* 2b (docs/superpowers/specs/2026-10-02-non-pitch-2b-partials-design.md): which synth, drawn out how, in what time */
enum Synth { RESONATE = 0, HARMONIC = 1, FORMANT = 2, PULSAR = 3, FREEZE = 4, RETUNE = 5, SFM = 6, SAM = 7 };
/* the pitch sampler and the two built on it (4): the recording itself, retuned - its own dynamics, Tune bends its speed */
inline bool pitched_sampler(int s) { return s == RETUNE || s == SFM || s == SAM; }
enum Method { BANK = 0, SPECTRAL = 1, COMB = 2 };
enum Mode { DRY = 0, RINGING = 1 };
static const double BELL_RATIO[4] = { 1.0, 2.76, 5.40, 8.93 };   /* a bar's / bell's modes */
static const double BELL_DECAY[4] = { 1.0, 0.6, 0.35, 0.2 };     /* the higher modes ring shorter */
static const double PI = 3.141592653589793;

/* The recording, owned by the host, read as mono and looped. */
struct Source {
    const float *f[2] = { nullptr, nullptr };
    const int16_t *s[2] = { nullptr, nullptr };
    int nch = 0; long long frames = 0;
    float at(long long i) const {
        if (frames <= 0) return 0;
        i %= frames; if (i < 0) i += frames;
        if (f[0]) return nch > 1 ? 0.5f * (f[0][i] + f[1][i]) : f[0][i];
        if (s[0]) return (nch > 1 ? 0.5f * (s[0][i] + s[1][i]) : s[0][i]) * (1.0f / 32768.0f);
        return 0;
    }
};

/* Phase delay (samples) and gain at w of the loop's two filters: the tuning all-pass and the colour low-pass. */
inline double ap_delay(double c, double w) { std::complex<double> z = std::polar(1.0, -w); return -std::arg((c + z) / (1.0 + c * z)) / w; }
inline double lp_delay(double a, double w) { std::complex<double> z = std::polar(1.0, -w); return -std::arg((1.0 - a) / (1.0 - a * z)) / w; }
inline double lp_gain(double a, double w) { std::complex<double> z = std::polar(1.0, -w); return std::abs((1.0 - a) / (1.0 - a * z)); }
/* the loop's own DC blocker (1 - z^-1) / (1 - R z^-1): its phase (a small lead) and gain at w */
inline double dc_delay(double R, double w) { std::complex<double> z = std::polar(1.0, -w); return -std::arg((1.0 - z) / (1.0 - R * z)) / w; }
inline double dc_gain(double R, double w) { std::complex<double> z = std::polar(1.0, -w); return std::abs((1.0 - z) / (1.0 - R * z)); }
static const double LOOP_R = 0.9995;                              /* in-loop DC blocker: corner ~4 Hz */
/* the all-pass coefficient whose delay at w is `want` samples (its delay falls as c rises) */
inline double solve_ap(double want, double w) {
    double lo = -0.999, hi = 0.999;
    for (int i = 0; i < 60; i++) { double m = 0.5 * (lo + hi); if (ap_delay(m, w) > want) lo = m; else hi = m; }
    return 0.5 * (lo + hi);
}

static const int PARTIALS = 24;
static const int SPN = 2048, SPH = 512;                            /* Spectral: frame, hop (75 % overlap) */
/* partial n's weight (2b spec, Colour): the Harmonic filter's overtone balance n^-(2 - 2 Colour), or the Formant's peak
   on partial 1 + 15 Colour, a raised-cosine bump 1 + 3 (1 - Focus) partials wide each side over a -24 dB floor */
inline double partial_weight(int synth, int n, double colour, double focus) {
    colour = std::fmin(1.0, std::fmax(0.0, colour)); focus = std::fmin(1.0, std::fmax(0.0, focus));
    if (synth == HARMONIC) return std::pow((double)n, -(2 - 2 * colour));
    const double p = 1 + 15 * colour, h = 1 + 3 * (1 - focus), d = std::fabs(n - p);
    return 0.063 + (1 - 0.063) * (d < h ? 0.5 + 0.5 * std::cos(PI * d / h) : 0);
}
/* a partial's bandwidth in Hz: Dry never narrower than a 30 ms decay (60 dB in 2.2 / B s); Ringing from T60 */
inline double band_hz(int mode, double f, double focus, double T60) { return mode == RINGING ? 2.2 / T60 : std::fmax(73.0, 0.5 * f * (1 - focus)); }

/* A real transform of N points as one N/2-point complex transform plus an untangling step: half the work of a full
   complex FFT (24 Spectral voices ran 2.23 ms a block on full transforms, over budget). inverse() returns N x the
   signal, like a forward transform's inverse. Checked against the full transform to float precision. */
/* 3c.1 F2: a recording's average power spectrum (2048-point Hann frames, up to 48 spread over it), normalised to its
   strongest bin; empty for silence or a recording shorter than a frame. FOLD_DB: how far under the best octave a note's own
   octave may score and still be kept */
static const double FOLD_DB = 30;
struct RFFT;
inline bool spectrum_of(const float *x, long long n, double sr, std::vector<float> &out);
struct RFFT {
    int N = 0; FFT h; std::vector<float> zr, zi, br, bi, twr, twi;
    void init(int n) {
        N = n; h.reserve(N / 2); h.plan(N / 2); h.twiddles(0, N / 2);
        zr.assign(N / 2, 0); zi.assign(N / 2, 0); br.assign(N / 2, 0); bi.assign(N / 2, 0); twr.resize(N / 2 + 1); twi.resize(N / 2 + 1);
        for (int k = 0; k <= N / 2; k++) { twr[k] = (float)std::cos(2 * PI * k / N); twi[k] = (float)-std::sin(2 * PI * k / N); }
    }
    void run() {
        for (int p = 0; p < h.passes; p++) { if (p % 2 == 0) h.pass(p, zr.data(), zi.data(), br.data(), bi.data(), 0, h.butterflies(p)); else h.pass(p, br.data(), bi.data(), zr.data(), zi.data(), 0, h.butterflies(p)); }
        if (h.passes % 2) { std::copy(br.begin(), br.end(), zr.begin()); std::copy(bi.begin(), bi.end(), zi.begin()); }
    }
    void forward(const float *x, float *Xr, float *Xi) {         /* x: N real -> X[0..N/2] */
        const int M = N / 2;
        for (int n = 0; n < M; n++) { zr[n] = x[2 * n]; zi[n] = x[2 * n + 1]; }
        run();
        for (int k = 0; k <= M; k++) {
            const int a = k % M, b = (M - k) % M;
            const float er = 0.5f * (zr[a] + zr[b]), ei = 0.5f * (zi[a] - zi[b]), orr = 0.5f * (zi[a] + zi[b]), oi = -0.5f * (zr[a] - zr[b]);
            Xr[k] = er + twr[k] * orr - twi[k] * oi; Xi[k] = ei + twr[k] * oi + twi[k] * orr;
        }
    }
    void inverse(const float *Xr, const float *Xi, float *x) {   /* X[0..N/2], Hermitian -> N x the signal */
        const int M = N / 2;
        for (int k = 0; k < M; k++) {
            const float ar = Xr[k], ai = Xi[k], cr = Xr[M - k], ci = -Xi[M - k];
            const float er = 0.5f * (ar + cr), ei = 0.5f * (ai + ci), dr = 0.5f * (ar - cr), di = 0.5f * (ai - ci);
            const float orr = dr * twr[k] + di * twi[k], oi = di * twr[k] - dr * twi[k];
            zr[k] = er - oi; zi[k] = -(ei + orr);
        }
        run();
        for (int n = 0; n < M; n++) { x[2 * n] = 2 * zr[n]; x[2 * n + 1] = -2 * zi[n]; }
    }
};

struct Voice {
    bool active = false, started = false, releasing = false, stealing = false;
    double f = 0, on_t = 0, off_t = 1e300; double vel = 0;
    bool has_next = false; double nf = 0, nt = 0, noff = 1e300, nvel = 0;   /* the note waiting for a steal's fade */
    double steal_at = 0;                                          /* the fade starts 50 ms before that note, not at once */
    double fade = 0;                                              /* per sample while stealing: 50 ms for a steal, 5 ms for Stop */
    double aph = 0;                                               /* attack progress 0..1, shaped as a raised cosine */
    double env = 0;
    long long pos = 0; int burst = 0, burst_len = 0;
    std::vector<float> line; unsigned w = 0; int N = 1;
    double c = 0, a = 0, g = 0, ap_x = 0, ap_y = 0, lp = 0;
    int modes = 0; double b0[4] = {}, a1[4] = {}, a2[4] = {}, y1[4] = {}, y2[4] = {}, wt[4] = {};
    double dc_x = 0, dc_y = 0, lx = 0, ly = 0, g60 = 0, rin = 0, rout = 0, agc = 1, pkg = 1, req = 0, soc = 1;   /* req: the note as asked (before fold and Octave: a re-voice starts from it); soc: Retune's Octave; pkg: a Plucked note's make-up gain */
    int body = 0, excite = 0, synth = 0, method = 0, mode = 0;    /* the note's own: a later change is the next note's */
    int np = 0; double pb0[PARTIALS] = {}, pa1[PARTIALS] = {}, pa2[PARTIALS] = {}, py1[PARTIALS] = {}, py2[PARTIALS] = {}, pw[PARTIALS] = {};
    double px1 = 0, px2 = 0;                                      /* the bank's shared input history */
    /* Spectral: the overlap-add ring, each bin's held level and phase, its weight and its partial; samples since the
       note began, this voice's place in the hop, the hold's decay per frame */
    std::vector<float> ola, hold, ph, mask; std::vector<int> owner; long long sk = 0; int soff = 0; double sd = 1, sf = 0;
    double rotr[PARTIALS + 1] = {}, roti[PARTIALS + 1] = {};      /* Ringing: each partial's turn per hop, times the decay */
    double fb0 = 0, fa1 = 0, fa2 = 0, fx1 = 0, fx2 = 0, fy1 = 0, fy2 = 0;   /* Comb's Formant peak */
    /* Pulsar (2c): the period and grain length in samples; the slice's start, when it was last refreshed, the grain
       now sounding, the refresh interval (-1: never) */
    double pP = 1, pD = 1; long long pr = 0, plast = 0, pk = -1, prefresh = -1;
    double vfocus = 0.5, fpow = 0; uint32_t rng = 1;             /* Freeze (2c): the note's Focus, the moment's power, jitter */
    /* the pitch sampler (3a): read place and speed, the recording's pitch there, its brightness filter, the end */
    double rpos = 0, rspd = 1, rf0 = 261.63, rlp = 0, rlpa = 0; long long rend = 0;
    /* 3d: Looped - the loop [rls, rls + rL) and its crossfade; Granular - four grains (source place, age), a new one every hop */
    double mph = 0, mstep = 0, mdep = 0, mdev = 0, mlast = 0;     /* 4: the modulator's phase, its step, depth, FM deviation, the last offset */
    double rls = 0, rL = 0, rxf = 1; double gpos[4] = {}; long long gage[4] = {}; bool gon[4] = {}; long long gt = 0; double gc = 0;
};

inline bool spectrum_of(const float *x, long long n, double sr, std::vector<float> &out) {
    (void)sr; out.clear();
    const int N = 2048; if (!x || n < N) return false;
    RFFT rf; rf.init(N); std::vector<float> w(N), Xr(N / 2 + 1), Xi(N / 2 + 1); std::vector<double> e(N / 2 + 1, 0.0);
    const int F = (int)std::min<long long>(48, n / N);
    for (int fr = 0; fr < F; fr++) {
        const long long a = (n - N) * fr / std::max(1, F - 1);
        double mean = 0; for (int j = 0; j < N; j++) mean += x[a + j]; mean /= N;          /* no DC in the low bins */
        for (int j = 0; j < N; j++) w[j] = (float)((x[a + j] - mean) * (0.5 - 0.5 * std::cos(2 * PI * j / N)));
        rf.forward(w.data(), Xr.data(), Xi.data());
        for (int b = 0; b <= N / 2; b++) e[b] += (double)Xr[b] * Xr[b] + (double)Xi[b] * Xi[b];
    }
    double top = 0; for (double v : e) top = std::max(top, v);
    if (top <= 0) return false;
    out.resize(e.size()); for (size_t b = 0; b < e.size(); b++) out[b] = (float)(e[b] / top);
    return true;
}

struct Resonator : tone::Synth {
    /* 24: three overlapping chords of up to 8 notes - a long release rings under the next chords instead of being
       stolen (Kerem 2026-10-01: "smooth cloudy transitions when release is longer than the note") */
    static const int VOICES = 24;
    static constexpr double STEAL_S = 0.05, STOP_S = 0.005;
    static const unsigned MASK = (1u << 13) - 1;                  /* 8192-sample lines: down to ~6 Hz */
    Source src;
    int body = STRING, excite = BOWED, synth = RESONATE, method = BANK, mode = DRY;
    double focus = 0.5, colour = 0.5, tune = 1, att = 0.02, rel = 0.6, offset_s = 0; int octave = 0;   /* 6: whole octaves on every note */
    Voice v[VOICES]; int last = -1;
    double tune_s = -1;                                           /* Tune as heard: glides to `tune` over ~10 ms (A-2) */
    std::vector<double> tune_buf;                                 /* per block; sized once to the largest block */
    RFFT rf; std::vector<float> xin, Xr, Xi, yout, win;           /* Spectral: one transform shared by the voices */
    std::vector<double> freeze_pw;                                /* Freeze: the moment's averaged power spectrum */
    /* the pitch sampler (3a): voices in use (a route role: 6), the recording's pitch (analysis f0) and its track - f0 and
       confidence every thop s - and its clearest moment (the most confident frame within 50 cents of f0) */
    /* 3c.1 F2: the recording's power spectrum (spectrum_of; not owned). A note keeps its octave when its overtones meet the
       recording's energy within FOLD_DB of the best octave; otherwise it moves to the nearest octave that does (pitch class kept) */
    const float *spec = nullptr; int spec_n = 0; double spec_sr = 48000;
    int nv = VOICES; double tf0 = 0, thop = 0.02, kglide = 0; long long tclear = 0, tfirst = 0; bool tclear_found = false; std::vector<float> tf, tc;
    const float *tfp = nullptr, *tcp = nullptr; int tn = 0;      /* the track as read (a copy's, or a host's buffers) */
    long long freeze_at = -1; const void *freeze_src = nullptr; double freeze_tp = 0;   /* ...and which moment it is */
    long long seek_from = -1, seek_got = 0; const void *seek_src = nullptr;          /* 3c.1: the loudest-nearby search, once per Moment */

    static double t60(double focus) { return 0.2 * std::pow(50.0, std::fmin(1.0, std::fmax(0.0, focus))); }

    std::vector<float> ghann;                                      /* 3d: one 80 ms Hann grain, looked up (24 voices x 4 grains of cos were over budget) */
    void init(double s) override {
        sr = s; kglide = 1 - std::exp(-1.0 / (0.01 * sr));
        { const int G = (int)(0.08 * sr); ghann.resize((size_t)G); for (int i = 0; i < G; i++) ghann[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * PI * i / G)); }
        for (auto &x : v) {
            x.line.assign(MASK + 1, 0.0f);
            x.ola.assign(SPN, 0.0f); x.hold.assign(SPN / 2 + 1, 0.0f); x.ph.assign(SPN / 2 + 1, 0.0f); x.mask.assign(SPN / 2 + 1, 0.0f); x.owner.assign(SPN / 2 + 1, 0);
        }
        rf.init(SPN); freeze_pw.assign(SPN / 2 + 1, 0.0); xin.assign(SPN, 0.0f); Xr.assign(SPN / 2 + 1, 0.0f); Xi.assign(SPN / 2 + 1, 0.0f); yout.assign(SPN, 0.0f);
        win.resize(SPN); for (int j = 0; j < SPN; j++) win[j] = (float)(0.5 - 0.5 * std::cos(2 * PI * j / SPN));
    }
    void set_track(double f0, double hop_s, const float *f0s, const float *confs, int n) {
        n = f0s && confs ? std::max(0, n) : 0;
        tf.assign(f0s, f0s + n); tc.assign(confs, confs + n);
        set_track_view(f0, hop_s, tf.data(), tc.data(), n);
    }
    /* no copy, no allocation: the host keeps the buffers alive (the route engine, on its audio thread) */
    /* how much of the recording's energy a note at g can resonate: its first 8 overtones, each the strongest bin within a
       quarter tone of n*g, weighted 1/n */
    double fold_score(double g) const {
        const double bin = spec_sr / 2048; double sc = 0;
        for (int n = 1; n <= 8; n++) {
            const double c = n * g; if (c >= 0.45 * spec_sr) break;
            const int lo = std::max(1, (int)std::floor(c * 0.9715 / bin)), hi = std::min(spec_n - 1, (int)std::ceil(c * 1.0293 / bin));
            float m = 0; for (int b = lo; b <= hi; b++) m = std::max(m, spec[b]);
            sc += m / n;
        }
        return sc;
    }
    double fold(double f) const {
        if (!spec || spec_n < 2 || !(f > 0)) return f;
        double best = 0; for (int k = -6; k <= 6; k++) { const double g = std::ldexp(f, k); if (g >= 20 && g < 0.45 * spec_sr) best = std::max(best, fold_score(g)); }
        if (best <= 0) return f;
        const double floor_ = best * std::pow(10.0, -FOLD_DB / 10);
        if (fold_score(f) >= floor_) return f;
        for (int s = 1; s <= 6; s++) for (int d : { s, -s }) { const double g = std::ldexp(f, d); if (g >= 20 && g < 0.45 * spec_sr && fold_score(g) >= floor_) return g; }
        return f;
    }
    /* 3c.1 F4: the notes sounding now (active, not releasing), up to max */
    int sounding(double *f, int max) const { int k = 0; for (int i = 0; i < nv && k < max; i++) if (v[i].active && !v[i].releasing) f[k++] = v[i].f; return k; }
    void set_track_view(double f0, double hop_s, const float *f0s, const float *confs, int n) {
        tf0 = f0 > 0 ? f0 : 0; thop = hop_s > 0 ? hop_s : 0.02; tfp = f0s; tcp = confs; tn = f0s && confs ? std::max(0, n) : 0;
        int best = -1; float bc = 0;
        for (int i = 0; i < tn; i++) if (tfp[i] > 0 && tcp[i] > bc && tf0 > 0 && std::fabs(1200 * std::log2(tfp[i] / tf0)) < 50) { best = i; bc = tcp[i]; }
        tclear = best < 0 ? 0 : (long long)(best * thop * sr); tclear_found = best >= 0;
        /* 3d: Position's origin - the first confident frame (the clearest could sit at the end; final review 3d I2) */
        tfirst = 0; for (int i = 0; i < tn; i++) if (tfp[i] > 0 && tcp[i] >= 0.8f) { tfirst = (long long)(i * thop * sr); break; }
    }
    void set_source(int ch, long long n, const float *const *p) {
        src = Source(); freeze_at = -1; seek_from = -1;   /* a new recording: no frozen moment carried over */ if (!p || n <= 0) return;
        src.nch = std::min(ch, 2); for (int k = 0; k < src.nch; k++) src.f[k] = p[k]; src.frames = n;
    }
    void set_source_i16(int ch, long long n, const int16_t *const *p) {
        src = Source(); freeze_at = -1; seek_from = -1;   /* a new recording: no frozen moment carried over */ if (!p || n <= 0) return;
        src.nch = std::min(ch, 2); for (int k = 0; k < src.nch; k++) src.s[k] = p[k]; src.frames = n;
    }

    /* a voice made ready for a note: the loop tuned to f (String/Tube) or the modes set (Bell) */
    void start(Voice &x, double f, double t, double vel) {
        x.req = f;
        f = fold(f) * std::ldexp(1.0, std::max(-2, std::min(2, octave)));   /* 6: Octave after the fold: from the octave the recording sounds in */
        std::fill(x.line.begin(), x.line.end(), 0.0f);
        f = std::fmin(std::fmax(f, 6.0), 0.45 * sr);                 /* extreme octaves: clamped, never unstable */
        x.active = true; x.started = false; x.releasing = false; x.stealing = false; x.has_next = false;
        x.f = f; x.on_t = t; x.off_t = 1e300; x.vel = vel; x.env = 0; x.aph = 0; x.body = body; x.excite = excite; x.synth = synth; x.method = method; x.mode = mode; x.vfocus = focus;
        x.pos = (long long)(offset_s * sr); x.burst = 0; x.burst_len = (int)(0.025 * sr);
        if (synth == RESONATE && excite == PLUCKED && offset_s <= 0) x.pos = pluck_place(t, x.burst_len);
        x.w = 0; x.ap_x = x.ap_y = x.lp = 0; x.dc_x = x.dc_y = 0; x.lx = x.ly = 0; x.rin = x.rout = 0; x.agc = 1; x.pkg = 1;
        const double w = 2 * PI * f / sr, T = t60(focus);
        /* Colour is set against the note (its cutoff 1.5x to 31x the fundamental, open at 1): dark stays dark in
           every register without swallowing the fundamental - a fixed filter killed high notes in milliseconds */
        const double col = std::fmin(1.0, std::fmax(0.0, colour));
        x.a = col >= 0.999 ? 0 : std::exp(-2 * PI * std::fmin(f * (1.5 + 30 * col * col), 0.45 * sr) / sr);
        if (synth == SFM || synth == SAM) { start_fmam(x, f); return; }
        if (synth == RETUNE) { start_retune(x, f); return; }
        if (synth == PULSAR) { start_pulsar(x, f); return; }
        if (synth == FREEZE) { start_freeze(x, f); return; }
        if (synth != RESONATE) { start_partials(x, f, T); return; }
        if (body == BELL) {
            x.modes = 0;
            const double cw = 0.15 + 0.85 * colour;                  /* Colour: how strongly the upper modes speak */
            for (int k = 0; k < 4; k++) {
                double fk = f * BELL_RATIO[k]; if (fk > 0.45 * sr) break;
                double wk = 2 * PI * fk / sr, r = std::pow(10.0, -3.0 / (T * BELL_DECAY[k] * sr));
                x.a1[k] = -2 * r * std::cos(wk); x.a2[k] = r * r;
                x.b0[k] = (1 - r) * std::abs(1.0 - r * std::polar(1.0, -2 * wk));   /* about unity at the peak */
                x.y1[k] = x.y2[k] = 0; x.wt[k] = std::pow(cw, k); x.modes++;
            }
            if (excite == PLUCKED) pluck_gain(x);
        } else { start_loop(x, f, T, body == TUBE); if (excite == PLUCKED) x.pkg = 2.5; }   /* ponytail: String/Tube Plucked a fixed +8 dB (measured 7-11 dB under Bowed over 300 ms); per-note like the Bell if it varies more */
    }
    /* Where a pluck strikes (Kerem 2026-10-08: "Bell plucked still silent"): every pluck struck the recording's first 25 ms,
       silence on his Titmouse song, and the same strike every note. Now the recording is read along with the note's time, as a
       bowed note reads it, and the strike is its loudest 25 ms in the next 0.3 s (the engine's recordings have no gap longer than
       0.25 s) - 5 ms blocks, every 4th sample, ~3900 reads */
    long long pluck_place(double t, int W) const {
        const long long F = src.frames; if (F <= 0) return 0;
        const long long a = ((long long)(std::fmax(0.0, t) * sr)) % F;
        const int S = std::max(1, (int)(0.005 * sr)), nb = (int)(0.3 * sr) / S + W / S, per = std::max(1, W / S);
        double e[128] = { 0 }; const int NB = std::min(nb, 128);
        if (a + (long long)NB * S < F) {          /* no wrap (nearly always): straight reads, every 4th sample */
            if (src.f[0]) { const float *q = src.f[0] + a; for (int b = 0; b < NB; b++, q += S) { float acc = 0; for (int i = 0; i < S; i += 4) acc += q[i] * q[i]; e[b] = acc; } }
            else if (src.s[0]) { const int16_t *q = src.s[0] + a; for (int b = 0; b < NB; b++, q += S) { float acc = 0; for (int i = 0; i < S; i += 4) acc += (float)q[i] * q[i]; e[b] = acc; } }
        } else {
            long long j = a;
            for (int b = 0; b < NB; b++) for (int i = 0; i < S; i++, j = j + 1 >= F ? 0 : j + 1) if (!(i & 3)) {
                const float v = src.f[0] ? src.f[0][j] : src.s[0] ? (float)src.s[0][j] : 0.0f; e[b] += (double)v * v; }
        }
        int best = 0; double bs = -1;
        for (int b = 0; b + per <= NB; b++) { double sum = 0; for (int k = 0; k < per; k++) sum += e[b + k]; if (sum > bs) { bs = sum; best = b; } }
        return (a + (long long)best * S) % F;
    }
    /* A Bell's modes have unity gain only at their peaks: a 25 ms burst of a broadband recording leaves them 48-59 dB under
       Bowed (Kerem 2026-10-07: "can't hear it"). The burst is known at the note's start, so it is run through the modes
       here (150 ms, ~0.1 ms of CPU) and the note made up to the burst's own level. A silent burst is never boosted. */
    void pluck_gain(Voice &x) {
        /* only the burst is run (the window's cosine by rotation, not a cos a sample); each mode's ringing over the rest of
           the 150 ms is a decaying sinusoid, its energy in closed form (final review 6 I2: ~110 us a note before) */
        double y1[4] = { 0 }, y2[4] = { 0 }, ein = 0, eout = 0; const int n = (int)(0.15 * sr), L = x.burst_len;
        const double dc = std::cos(2 * PI / L), ds = std::sin(2 * PI / L); double hc = 1, hs = 0;
        /* the burst read in place, one wrap check a sample (Source::at's modulo a sample was most of the cost) */
        const long long F = src.frames; long long j = F > 0 ? ((x.pos % F) + F) % F : 0;
        const int m = x.modes; double B0[4], A1[4], A2[4], WT[4]; for (int k = 0; k < m; k++) { B0[k] = x.b0[k]; A1[k] = x.a1[k]; A2[k] = x.a2[k]; WT[k] = x.wt[k]; }
        for (int i = 0; i < L; i++) {
            const float sv = F <= 0 ? 0.0f : src.f[0] ? (src.nch > 1 ? 0.5f * (src.f[0][j] + src.f[1][j]) : src.f[0][j])
                           : src.s[0] ? (src.nch > 1 ? 0.5f * (src.s[0][j] + src.s[1][j]) : src.s[0][j]) * (1.0f / 32768.0f) : 0.0f;
            if (F > 0 && ++j >= F) j = 0;
            const double in = sv * 0.5 * (1 - hc); { const double t = hc * dc - hs * ds; hs = hs * dc + hc * ds; hc = t; }
            double y = 0;
            for (int k = 0; k < m; k++) { const double o = B0[k] * in - A1[k] * y1[k] - A2[k] * y2[k]; y2[k] = y1[k]; y1[k] = o; y += WT[k] * o; }
            ein += in * in; eout += y * y;
        }
        const int M = n - L;
        for (int k = 0; k < x.modes && M > 0; k++) {
            const double r = std::sqrt(x.a2[k]), cw = -x.a1[k] / (2 * r), sw = std::sqrt(std::fmax(1e-12, 1 - cw * cw));
            const double q = (r * y2[k] - y1[k] * cw) / sw, A2 = y1[k] * y1[k] + q * q, r2 = r * r;
            eout += x.wt[k] * x.wt[k] * 0.5 * A2 * r2 * (1 - std::pow(r2, M)) / std::fmax(1e-12, 1 - r2);
        }
        if (ein > 1e-10 * L && eout > 0) x.pkg = std::fmin(1000.0, std::fmax(1.0, std::sqrt(ein / x.burst_len / (eout / n))));
    }

    /* the String / Tube loop tuned to f, ringing for T (2a; Comb Ringing in 2b plays the String) */
    void start_loop(Voice &x, double f, double T, bool tube) {
        const double w = 2 * PI * f / sr;
        const double P = sr / f / (tube ? 2 : 1);        /* Tube: half a period, feedback inverted */
        x.g60 = std::pow(10.0, -3.0 * (P / sr) / T);              /* one pass of the loop, in T60 terms */
        /* The feedback never reaches 1 at any frequency (final review 2026-09-30: an uncapped g ran away through
           the DC blocker's own low resonance, dark Colour or high notes). So g <= GMAX, and where Colour would
           lose more at f than GMAX can make good, Colour's cutoff rises until it does not: the fundamental still
           rings its T60. Below ~15 Hz (the DC blocker's own loss) T60 simply comes out shorter. */
        const double GMAX = 0.99995, dcg = dc_gain(LOOP_R, w), need = x.g60 / (GMAX * dcg);
        if (need >= 1) x.a = 0;
        else if (lp_gain(x.a, w) < need) {                         /* lp_gain falls as a rises */
            double lo = 0, hi = x.a;
            for (int i = 0; i < 50; i++) { double m = 0.5 * (lo + hi); if (lp_gain(m, w) >= need) lo = m; else hi = m; }
            x.a = lo;
        }
        x.g = std::fmin(GMAX, x.g60 / (lp_gain(x.a, w) * dcg));
        const double lpd = lp_delay(x.a, w) + dc_delay(LOOP_R, w);
        int N = (int)std::floor(P - lpd - 0.2); N = std::max(1, std::min(N, (int)MASK - 2));
        x.N = N; x.c = solve_ap(P - N - lpd, w);
    }

    /* The pitch sampler (3a): the recording read at (f / its own pitch there)^Tune - the pitch track's f0 at the read place,
       between frames linearly, where confident (>= 0.8), else the last confident; the speed glides over 10 ms. From the
       clearest moment (or Position); a one-shot fades out over its last 5 ms. Brightness (Colour): a low-pass following
       the note. No level matching - the recording's own dynamics. */
    void start_retune(Voice &x, double f) {
        /* 3d Position (Focus): 0 ... 1 from the pitch track's first clear frame to the end (the bench's offset wins) */
        /* ... and it stops 0.5 s (or half what is left) short of the end, so Position 1 still plays a note (3d I1) */
        const double len = (double)src.frames, from = std::fmin((double)tfirst, std::fmax(0.0, len - 2));
        const double room = std::fmax(0.0, len - from - 2), tail = std::fmin(0.5 * sr, 0.5 * room);
        const double at = offset_s > 0 ? offset_s * sr : from + std::fmin(1.0, std::fmax(0.0, focus)) * (room - tail);
        x.rpos = at; x.rend = src.frames;
        x.rf0 = tf0 > 0 ? tf0 : 261.63;
        /* Octave outside Tune's power: whole octaves whatever Tune is (final review 6 I3) */
        x.soc = std::ldexp(1.0, std::max(-2, std::min(2, octave))); x.sf = f / x.soc; x.rspd = std::pow(x.sf / x.rf0, tune) * x.soc;
        const double b = std::fmin(1.0, std::fmax(0.0, colour));
        x.rlpa = b >= 0.999 ? 0 : std::exp(-2 * PI * std::fmin(f * (1.5 + 30 * b * b), 0.45 * sr) / sr); x.rlp = 0; x.agc = 1;
        if (x.method == 1) {        /* Looped: about 0.5 s of whole periods at Position, inside the recording */
            /* whole periods that fit, with the crossfade before the loop, inside the recording (a read past the end
               wrapped to the start: clicks on loops under ~0.5 s; final review 3d C1) */
            const double f0 = track_f0(at), P = sr / (f0 > 0 ? f0 : 261.63), avail = len - 5;
            double L = std::fmax(1.0, std::round(0.5 * sr / P)) * P;
            while (L > P && L + std::fmin(0.03 * sr, 0.5 * L) > avail) L -= P;
            if (L + std::fmin(0.03 * sr, 0.5 * L) > avail) L = std::fmax(8.0, avail / 1.5);   /* under ~1.5 periods: what exists */
            x.rxf = std::fmin(0.03 * sr, 0.5 * L);
            const double s0 = std::fmin(std::fmax(at, x.rxf + 2), len - 3 - L);
            x.rls = s0; x.rL = L; x.rpos = s0;
        }
        if (x.method == 2) {        /* Granular: grains around Position */
            x.gc = at; x.gt = 0; for (int g = 0; g < 4; g++) x.gon[g] = false; x.rpos = at;
        }
    }
    /* the recording's pitch at a place (its track where confident, else its f0) */
    /* 4: Sample FM / AM - the pitch sampler at Position 0, brightness open, under a modulator: one cycle of the recording
       (from the first rising zero crossing after its clearest pitched frame), else a sine. Ratio 0.25 * 16^Focus, depth Colour */
    std::vector<float> mcyc; const void *mcyc_src = nullptr; double mcyc_f0 = -2; long long mcyc_at = -2;
    void build_modulator() {
        const void *sid = src.f[0] ? (const void *)src.f[0] : (const void *)src.s[0];
        const long long key = tclear_found ? tclear : -1;
        if (!mcyc.empty() && sid == mcyc_src && tf0 == mcyc_f0 && key == mcyc_at) return;
        mcyc_src = sid; mcyc_f0 = tf0; mcyc_at = key; mcyc.assign(256, 0.0f);
        /* a cycle only from a clear pitched frame (else the first samples - noise - were cut; final review 4 I6) */
        const double P = tf0 > 0 && tclear_found ? sr / tf0 : 0;
        long long z = -1;
        if (P > 2 && src.frames > (long long)(3 * P) + 4) {
            const long long a = std::max<long long>(1, std::min<long long>(tclear, src.frames - (long long)(3 * P) - 3));
            for (long long i = a; i < a + (long long)(2 * P); i++) if (src.at(i - 1) < 0 && src.at(i) >= 0) { z = i; break; }
        }
        if (z < 0) { for (int k = 0; k < 256; k++) mcyc[(size_t)k] = (float)std::sin(2 * PI * k / 256); return; }
        double mean = 0, pk = 0;
        for (int k = 0; k < 256; k++) { mcyc[(size_t)k] = (float)read(z + P * k / 256); mean += mcyc[(size_t)k]; }
        mean /= 256; for (auto &v : mcyc) { v = (float)(v - mean); pk = std::max(pk, (double)std::fabs(v)); }
        if (pk < 1e-9) { for (int k = 0; k < 256; k++) mcyc[(size_t)k] = (float)std::sin(2 * PI * k / 256); return; }
        for (auto &v : mcyc) v = (float)(v / pk);
    }
    void start_fmam(Voice &x, double f) {
        const double fo = focus, co = colour;
        focus = 0; colour = 1; start_retune(x, f); focus = fo; colour = co;
        build_modulator();
        const double ratio = 0.25 * std::pow(16.0, std::fmin(1.0, std::fmax(0.0, fo)));
        x.mdep = std::fmin(1.0, std::fmax(0.0, co)); x.mph = 0; x.mstep = 256.0 * x.sf * x.soc * ratio / sr; x.mlast = 0;
        const double P = sr / (tf0 > 0 ? tf0 : 261.63);
        x.mdev = x.synth == SFM ? x.mdep * 2 * P : 0;
        if (x.mdev > 0 && x.method == 0 && x.rpos < x.mdev + 2) x.rpos = x.mdev + 2;   /* the bend stays inside the recording */
        if (x.mdev > 0 && x.method == 1) {      /* Looped: the loop and its crossfade, bent both ways, inside (60 Hz read at -3198) */
            const double len = (double)src.frames, lo = 2 * x.mdev + x.rxf + 2, hi = len - 3 - x.rL - 2 * x.mdev;
            if (x.rls < lo && lo <= hi) { x.rls = lo; x.rpos = lo; }
            else if (x.rls < lo || x.rls > hi) x.mdev = std::fmax(0.0, std::fmin((x.rls - x.rxf - 2) / 2, (len - 3 - x.rL - x.rls) / 2));
        }
    }
    double fmam(Voice &x) {
        const int i0 = (int)x.mph; const double u = x.mph - i0;
        const double m = mcyc[(size_t)(i0 & 255)] * (1 - u) + mcyc[(size_t)((i0 + 1) & 255)] * u;
        x.mph += x.mstep; if (x.mph >= 256) x.mph -= 256 * std::floor(x.mph / 256);
        if (x.synth == SFM) {
            const double d = x.mdev * m, dd = d - x.mlast; x.rpos += dd; x.mlast = d;
            if (x.method == 2) for (int g = 0; g < 4; g++) if (x.gon[g]) x.gpos[g] += dd;   /* Granular: the grains themselves bend */
            return retune(x);
        }
        /* the product of the carrier and its own cycle has a DC term (0.8 of RMS at ring, measured): the voice's DC blocker */
        const double y = retune(x) * ((1 - x.mdep) + x.mdep * m), o = y - x.dc_x + 0.995 * x.dc_y;
        x.dc_x = y; x.dc_y = std::fabs(o) < 1e-20 ? 0 : o;
        return x.dc_y;
    }
    double track_f0(double pos) const {
        const int fr = (int)std::floor(pos / sr / thop);
        if (fr >= 0 && fr < tn && tcp[fr] >= 0.8f && tfp[fr] > 0) return tfp[fr];
        return tf0;
    }
    double read(double pos) const {
        const long long i = (long long)std::floor(pos); const double u = pos - (double)i;
        double y0, y1, y2, y3;
        if (src.f[0] && src.nch == 1 && i >= 1 && i + 2 < src.frames) { const float *d = src.f[0] + i; y0 = d[-1]; y1 = d[0]; y2 = d[1]; y3 = d[2]; }   /* inside: direct (24 Granular voices sat at the budget's edge) */
        else { y0 = src.at(i - 1); y1 = src.at(i); y2 = src.at(i + 1); y3 = src.at(i + 2); }
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * u + c2) * u + c1) * u + y1;
    }
    double retune(Voice &x) {
        if (x.method == 1 || x.method == 2) {
            const double ft = x.rpos / sr / thop - 0.5; const int fr = (int)std::floor(ft);
            if (fr >= 0 && fr < tn && tcp[fr] >= 0.8f && tfp[fr] > 0) x.rf0 = tfp[fr];
            x.rspd += (std::pow(x.sf / x.rf0, tune) * x.soc - x.rspd) * kglide;
            double y = 0;
            if (x.method == 1) {    /* the loop, its last rxf crossfaded into the same place one loop earlier */
                if (x.rpos >= x.rls + x.rL) x.rpos -= x.rL;
                y = read(x.rpos);
                const double into = x.rpos - (x.rls + x.rL - x.rxf);
                if (into > 0) { const double w = into / x.rxf; y = y * (1 - w) + read(x.rpos - x.rL) * w; }
                x.rpos += x.rspd;
            } else {                /* four Hann grains of 80 ms, one every 20 ms, at Position +-30 ms; the read place stays */
                const long long G = (long long)(0.08 * sr), H = G / 4;
                if (x.gt % H == 0) {
                    const int g = (int)((x.gt / H) % 4);
                    x.rng = x.rng * 1664525u + 1013904223u;
                    /* the jitter in whole periods of the recording there: grains of a pitched sound stay in phase (a steady
                       level - random phases beat by 6 dB, measured) */
                    const double P = sr / (x.rf0 > 0 ? x.rf0 : 261.63);
                    const double jit = std::round(((x.rng >> 8) / 16777216.0 * 2 - 1) * 0.03 * sr / P) * P;
                    x.gpos[g] = std::fmin(std::fmax(1.0, x.rpos + jit), std::fmax(1.0, (double)src.frames - 3 - G * x.rspd)); x.gage[g] = 0; x.gon[g] = true;
                }
                /* the read place runs at the note's speed and steps back whole periods to stay at Position: every grain
                   starts on it, so a pitched sound's grains are in phase (each 20 ms later, 20 ms of it further on) */
                { const double P = sr / (x.rf0 > 0 ? x.rf0 : 261.63);
                  x.rpos += x.rspd; if (x.rpos > x.gc + 0.03 * sr) x.rpos -= std::fmax(1.0, std::round(0.03 * sr / P)) * P; }
                for (int g = 0; g < 4; g++) if (x.gon[g]) {
                    y += ghann[(size_t)x.gage[g]] * read(x.gpos[g]);
                    x.gpos[g] += x.rspd; if (++x.gage[g] >= G) x.gon[g] = false;
                }
                y *= 0.5; x.gt++;   /* Hann at 75 % overlap sums to 2 */
            }
            x.rlp = (1 - x.rlpa) * y + x.rlpa * x.rlp; if (std::fabs(x.rlp) < 1e-20) x.rlp = 0;
            return x.rlp;
        }
        const double left = (double)x.rend - 2 - x.rpos;
        if (left <= 0) return 0;
        const double ft = x.rpos / sr / thop - 0.5; const int fr = (int)std::floor(ft);   /* frame centres at (k + 0.5) thop */
        if (fr >= 0 && fr + 1 < tn && tcp[fr] >= 0.8f && tcp[fr + 1] >= 0.8f && tfp[fr] > 0 && tfp[fr + 1] > 0) x.rf0 = tfp[fr] + (tfp[fr + 1] - tfp[fr]) * (ft - fr);
        else if (fr >= 0 && fr < tn && tcp[fr] >= 0.8f && tfp[fr] > 0) x.rf0 = tfp[fr];
        x.rspd += (std::pow(x.sf / x.rf0, tune) * x.soc - x.rspd) * kglide;
        const long long i = (long long)x.rpos; const double u = x.rpos - (double)i;
        const double y0 = src.at(i - 1), y1 = src.at(i), y2 = src.at(i + 1), y3 = src.at(i + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        double y = ((c3 * u + c2) * u + c1) * u + y1;
        const double fade = 0.005 * sr * x.rspd; if (left < fade) y *= left / fade;   /* the end, faded: no click */
        x.rpos += x.rspd;
        x.rlp = (1 - x.rlpa) * y + x.rlpa * x.rlp; if (std::fabs(x.rlp) < 1e-20) x.rlp = 0;
        return x.rlp;
    }

    /* Pulsar (2c): a grain of the recording every period - fractional, so the repetition rate is the note exactly -
       Hann-shaped, d of a period long (Colour); its slice refreshed every R(Focus) = 1 ms ... 10 s, never at Focus 1 */
    void start_pulsar(Voice &x, double f) {
        const double d = 0.05 + 0.95 * std::fmin(1.0, std::fmax(0.0, colour));
        x.pP = sr / f; x.pD = std::fmax(4.0, d * x.pP); x.sk = 0; x.pk = -1;
        x.pr = x.pos; x.plast = 0;
        x.prefresh = focus >= 0.999 ? -1 : (long long)std::llround(0.001 * std::pow(10.0, 4 * std::fmax(0.0, focus)) * sr);
        /* a Hann grain of duty d passes 0.375 d of the power, and reading between samples keeps 2/3 of white noise's
           (all of a dark recording's): 5/6, between - the start within ~0.8 dB either way (it crept up 1.2 dB over 3 s, and
           a lab chord lasts ~2.7 s: Kerem heard it as low, 2026-10-04) */
        x.agc = std::fmin(1000.0, 1 / std::sqrt(0.375 * std::fmin(1.0, x.pD / x.pP) * 5.0 / 6.0));
    }
    double pulsar(Voice &x) {
        const double n = (double)x.sk++;
        const long long k = (long long)std::floor(n / x.pP);
        if (k != x.pk) {                                           /* a new grain: its slice refreshed when due */
            x.pk = k;
            if (x.prefresh >= 0 && (long long)n - x.plast >= x.prefresh) { x.pr = x.pos - 1; x.plast = (long long)n; }
        }
        const double tau = n - k * x.pP;
        if (tau >= x.pD) return 0;
        /* read between samples: a grain starts at a fractional time, and whole-sample reads shifted each repeat by up to
           half a sample - on noise, a quarter of the power fell between the harmonics (measured) */
        const long long i = (long long)tau; const double fr = tau - (double)i;
        const double v = src.at(x.pr + i) * (1 - fr) + src.at(x.pr + i + 1) * fr;
        return v * (0.5 - 0.5 * std::cos(2 * PI * tau / x.pD));
    }

    /* Freeze (2c): one moment's spectrum (4 frames averaged), its level read at each partial and held for ever. Each kept
       bin turns at its partial's own frequency every hop (in tune) and, below Focus 1, by a random amount as well, so a wide
       band breathes as air around the line */
    void start_freeze(Voice &x, double f) {
        std::fill(x.ola.begin(), x.ola.end(), 0.0f); std::fill(x.ph.begin(), x.ph.end(), 0.0f);
        std::fill(x.mask.begin(), x.mask.end(), 0.0f); std::fill(x.owner.begin(), x.owner.end(), 0);
        const long long len = std::max<long long>(src.frames, 1), at0 = (long long)(std::fmin(1.0, std::fmax(0.0, colour)) * (double)std::max<long long>(0, len - SPN));
        /* 3c.1: the loudest moment within +-85 ms (8 hops) of the Moment - a bird call fades in and out, and a frozen soft
           edge was a whole near-silent note */
        const void *sid0 = src.f[0] ? (const void *)src.f[0] : (const void *)src.s[0];
        if (at0 != seek_from || sid0 != seek_src) {      /* a chord's notes share the Moment: searched once (the 2c budget) */
            long long best = at0; double be = -1;
            for (int k = -8; k <= 8; k++) {
                const long long a = at0 + (long long)k * SPH; if (a < 0 || a > std::max<long long>(0, len - SPN)) continue;
                double e = 0; for (int j = 0; j < 3 * SPH + SPN; j += 4) { const float v = src.at(a + j); e += (double)v * v; }
                if (e > be * 1.0001) { be = e; best = a; }
            }
            seek_from = at0; seek_src = sid0; seek_got = best;
        }
        const long long at = seek_got;
        /* a chord's notes freeze the same moment: it is analysed once (an 8-note chord start cost 2.99 ms, measured) */
        const void *sid = src.f[0] ? (const void *)src.f[0] : (const void *)src.s[0];
        if (at != freeze_at || sid != freeze_src) {
            std::fill(freeze_pw.begin(), freeze_pw.end(), 0.0); double tp = 0;
            for (int fr = 0; fr < 4; fr++) {
                for (int j = 0; j < SPN; j++) { const float smp = src.at(at + fr * SPH + j); xin[j] = smp * win[j]; tp += (double)smp * smp; }
                rf.forward(xin.data(), Xr.data(), Xi.data());
                for (int k = 0; k <= SPN / 2; k++) freeze_pw[k] += (double)Xr[k] * Xr[k] + (double)Xi[k] * Xi[k];
            }
            freeze_at = at; freeze_src = sid; freeze_tp = tp;
        }
        /* the level it keeps: the moment's own power, +3 dB (Kerem 2026-10-04: "Freeze can be more since the outcome sound
           is low in volume" - on the same chord it played 3 dB under the Resonator, measured in the lab) */
        x.fpow = 2 * freeze_tp / (4.0 * SPN);
        const double bin = sr / SPN, fo = std::fmin(1.0, std::fmax(0.0, focus)), h = 1 + 3 * (1 - fo);
        int npart = 0;
        for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++, npart++) {
            const double c = n * f / bin;
            for (int k = std::max(1, (int)std::ceil(c - h)); k <= std::min(SPN / 2 - 1, (int)std::floor(c + h)); k++) {
                const double m = (0.5 + 0.5 * std::cos(PI * (k - c) / h)) * std::sqrt(freeze_pw[k] / 4);
                if (m > x.mask[k]) { x.mask[k] = (float)m; x.owner[k] = n; }
            }
        }
        /* the output's expected power, so the level starts at the moment's (final review 2c #2: a flat blend was 3-5 dB
           loud at Focus 0 and 1). A partial's bins sound as one line, each weighted by the Hann window's spectrum at its
           distance from n f (overlap-add of turning bins); with the random turn of up to a per hop, successive frames
           keep (sin a / a)^2 of that coherence, the rest adds as independent bins (Hann^2 overlap 1.5) */
        auto D = [](double d) { return std::fabs(d) < 1e-9 ? 1.0 : std::sin(PI * d) / (PI * d); };
        double coh = 0, inc = 0, amp[PARTIALS + 1] = {};
        for (int k = 1; k < SPN / 2; k++) if (x.owner[k] > 0) {
            const double d = k - x.owner[k] * f / bin; amp[x.owner[k]] += x.mask[k] * (0.5 * D(d) + 0.25 * (D(d - 1) + D(d + 1)));
            inc += (double)x.mask[k] * x.mask[k];
        }
        for (int n = 1; n <= npart; n++) coh += amp[n] * amp[n];
        const double ja = (1 - fo) * PI, beta = ja < 1e-9 ? 1 : std::pow(std::sin(ja) / ja, 2);
        const double pcoh = coh * std::pow(2.0 / (1.5 * SPH), 2) / 2, pinc = inc * std::pow(2.0 / SPN, 2) / 3;
        /* and a measured correction over Focus (the model reads 0.3-2 dB high at Focus 0.25-1; the midpoint of noise and of
           a harmonic tone, measured at 110 / 220 / 880 Hz) */
        static const double CORR_DB[5] = { 0.1, -0.33, -1.1, -1.67, -1.2 };
        const double cf = fo * 4; const int ci = std::min(3, (int)cf);
        const double est = (beta * pcoh + (1 - beta) * pinc) * std::pow(10.0, (CORR_DB[ci] + (CORR_DB[ci + 1] - CORR_DB[ci]) * (cf - ci)) / 10);
        x.agc = x.fpow > 1e-20 ? (est > 1e-30 ? std::fmin(1000.0, std::sqrt(x.fpow / est)) : 1) : 0;
        x.rin = x.fpow; x.rout = est;                                  /* the matching starts settled, not from zero (a 4.9 dB swell) */
        const long long s0 = (long long)std::ceil(x.on_t * sr - 1e-9);
        x.sf = f; x.sk = 0; x.soff = (int)((((&x - v) * SPH / VOICES - s0) % SPH + SPH) % SPH); x.rng = 0x9e3779b9u ^ (uint32_t)(&x - v);
    }
    void freeze_frame(Voice &x) {
        const double jit = (1 - std::fmin(1.0, std::fmax(0.0, x.vfocus))) * PI;
        for (int k = 0; k <= SPN / 2; k++) {
            double re = 0, im = 0;
            if (x.mask[k] > 0) {
                x.rng ^= x.rng << 13; x.rng ^= x.rng >> 17; x.rng ^= x.rng << 5;
                const double turn = 2 * PI * x.owner[k] * x.sf * SPH / sr + jit * ((x.rng >> 8) / 8388608.0 - 1);
                x.ph[k] = (float)std::fmod(x.ph[k] + turn, 2 * PI);
                re = x.mask[k] * std::cos((double)x.ph[k]); im = x.mask[k] * std::sin((double)x.ph[k]);
            }
            Xr[k] = (float)re; Xi[k] = (float)im;
        }
        rf.inverse(Xr.data(), Xi.data(), yout.data());
        for (int j = 0; j < SPN; j++) x.ola[(size_t)((x.sk + j) & (SPN - 1))] += yout[j] / SPN * win[j] / 1.5f;
    }

    /* Harmonic filter / Formant (2b): the engines' state, and the level expected for a white input, so the slow level
       matching starts close (no swell while it settles) */
    void start_partials(Voice &x, double f, double T) {
        const double B = band_hz(x.mode, f, focus, T);
        double G = 0; x.np = 0;
        if (x.method == BANK) {
            /* a true band-pass (zeros at DC and Nyquist, peak 0 dB, 3 dB width B): an all-pole one as wide as a Dry band
               left a floor under the fundamental only 4 dB down at 100 Hz (220 Hz note, measured) */
            x.px1 = x.px2 = 0;
            double fc[PARTIALS];
            auto coef = [&](int k) {
                const double wc = 2 * PI * fc[k] / sr, al = std::sin(wc) * B / (2 * fc[k]);
                x.pb0[k] = al / (1 + al); x.pa1[k] = -2 * std::cos(wc) / (1 + al); x.pa2[k] = (1 - al) / (1 + al);
            };
            for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++, x.np++) {
                const int k = x.np; fc[k] = n * f; coef(k);
                x.py1[k] = x.py2[k] = 0; x.pw[k] = partial_weight(x.synth, n, colour, focus);
                G += x.pw[k] * x.pw[k] * PI * B / sr;              /* a peak-1 band of width B passes pi B / sr of white power */
            }
            /* Neighbouring bands' skirts add a quarter-cycle out of phase and pull the summed peak off the partial (a
               220 Hz Dry fundamental peaked 24 cents sharp, measured): each band's centre is nudged until the sum peaks
               on n f - a parabola on the summed response, near bands only. Bounded (final review #1: where the sum is
               flat the step was unbounded, centres flew past Nyquist and the bands blew up): each step <= B / 4, the
               whole nudge <= B / 2, bands under 5 % of the loudest left alone. Cheap at a note start (review #2):
               five passes, neighbours within 10 B + 2 f; narrow Ringing bands (B < f / 50) pull too little to need it. */
            double wmax = 0; for (int k = 0; k < x.np; k++) wmax = std::fmax(wmax, x.pw[k]);
            auto lmag = [&](double fq) {                                /* complex arithmetic by hand: std::complex division is a
                                                                           slow library call in wasm (review #2) */
                const double w = 2 * PI * fq / sr, c1 = std::cos(w), s1 = -std::sin(w), c2 = c1 * c1 - s1 * s1, s2 = 2 * c1 * s1;
                const double nr = 1 - c2, ni = -s2; double hr = 0, hi = 0;
                for (int k = 0; k < x.np; k++) if (std::fabs(fc[k] - fq) < 10 * B + 2 * f) {
                    const double dr = 1 + x.pa1[k] * c1 + x.pa2[k] * c2, di = x.pa1[k] * s1 + x.pa2[k] * s2, g = x.pw[k] * x.pb0[k] / (dr * dr + di * di);
                    hr += g * (nr * dr + ni * di); hi += g * (ni * dr - nr * di);
                }
                return std::log(hr * hr + hi * hi + 1e-300);
            };
            if (B > f / 50)
                for (int it = 0; it < 5; it++)
                    for (int k = 0; k < x.np; k++) {
                        if (x.pw[k] < 0.05 * wmax) continue;
                        const double fn = (k + 1) * f, d = std::fmax(0.02, 0.01 * B), lo = lmag(fn - d), mid = lmag(fn), hi = lmag(fn + d), den = lo - 2 * mid + hi;
                        if (!(den < 0)) continue;
                        const double step = std::fmax(-0.25 * B, std::fmin(0.25 * B, -0.5 * (lo - hi) / den * d));
                        fc[k] = std::fmin(0.49 * sr, std::fmax(0.5 * fn, std::fmax(fn - 0.5 * B, std::fmin(fn + 0.5 * B, fc[k] + step))));
                        coef(k);
                    }
            /* bands that overlap (B > f / 4: low Dry notes) add up: two bands' shared white-noise power falls as
               1 / (1 + (df / B)^2) (Lorentzians) - a closed form, not the integration review #2 found ~1 ms per note */
            if (B > 0.25 * f) {
                G = 0;
                for (int i = 0; i < x.np; i++) for (int j = 0; j < x.np; j++) { const double r = (fc[i] - fc[j]) / B; G += x.pw[i] * x.pw[j] / (1 + r * r); }
                G *= PI * B / sr;
            }
        }
        if (x.method == SPECTRAL) {
            /* each partial keeps the bins within a raised-cosine band around n f: Dry at least 3 bins each side (its
               centre then sits on n f within a hair), Ringing 2 (its pitch comes from the phase, below) */
            /* ph too: Ringing reads it as the held bins' imaginary half, and a Freeze note before left phases there (a
               +25 dB burst, final review 2c #1) */
            std::fill(x.ola.begin(), x.ola.end(), 0.0f); std::fill(x.hold.begin(), x.hold.end(), 0.0f); std::fill(x.ph.begin(), x.ph.end(), 0.0f);
            std::fill(x.mask.begin(), x.mask.end(), 0.0f); std::fill(x.owner.begin(), x.owner.end(), 0);
            const double bin = sr / SPN, half = x.mode == RINGING ? 2.0 : std::fmax(3.0, 0.5 * B / bin);
            int npart = 0; double cs[PARTIALS + 1], wts[PARTIALS + 1];
            for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++) { cs[n] = n * f / bin; wts[n] = partial_weight(x.synth, n, colour, focus); npart = n; }
            auto build = [&]() {
                std::fill(x.mask.begin(), x.mask.end(), 0.0f); std::fill(x.owner.begin(), x.owner.end(), 0);
                for (int n = 1; n <= npart; n++)
                    for (int k = std::max(1, (int)std::ceil(cs[n] - half)); k <= std::min(SPN / 2 - 1, (int)std::floor(cs[n] + half)); k++) {
                        const double m = wts[n] * (0.5 + 0.5 * std::cos(PI * (k - cs[n]) / half));
                        if (m > x.mask[k]) { x.mask[k] = (float)m; x.owner[k] = n; }
                    }
            };
            build();
            for (int k = 0; k <= SPN / 2; k++) G += x.mask[k] * x.mask[k] / (SPN / 2);
            /* frames on the global clock, voice k at k / 24 of the hop: keyed to the note's own start, voices whose starts lined
               up all transformed in one block (final review #3: 4.36 ms). Reading ahead makes any phase valid. */
            const long long s0 = (long long)std::ceil(x.on_t * sr - 1e-9);
            x.sf = f; x.sk = 0; x.soff = (int)((((&x - v) * SPH / VOICES - s0) % SPH + SPH) % SPH); x.sd = std::pow(10.0, -3.0 * SPH / (sr * T));
            for (int n = 1; n <= npart; n++) { const double th = 2 * PI * n * f * SPH / sr; x.rotr[n] = x.sd * std::cos(th); x.roti[n] = x.sd * std::sin(th); }
        }
        if (x.method == COMB) {
            std::fill(x.line.begin(), x.line.end(), 0.0f);
            /* the Formant's Colour places its peak, not a tilt: its comb runs open, a flat base as in the Bank and the
               Spectral (a dark comb under it pulled a 220 Hz note 12.9 cents flat, measured) */
            if (x.synth == FORMANT) x.a = 0;
            /* the white-noise power gain, as it is (final review #4: assumed 2 and 1, a Colour-filtered comb started up to
               6 dB quiet and faded up for seconds) */
            if (x.mode == RINGING) {                                    /* the 2a String; input scaled as bowed */
                x.body = STRING; start_loop(x, f, T, false);
                G = 0; const int K = 256;                               /* mean over frequency of 1 / (1 - |loop gain|^2) */
                for (int k = 0; k < K; k++) { const double w = PI * (k + 0.5) / K, lg = x.g * lp_gain(x.a, w) * dc_gain(LOOP_R, w); G += 1 / std::fmax(1e-9, 1 - lg * lg); }
                G *= (1 - x.g60 * x.g60) / K;
            } else {                                                    /* feed-forward: the recording plus itself one period later */
                const double w = 2 * PI * f / sr, P = sr / f, lpd = lp_delay(x.a, w);
                int N = (int)std::floor(P - lpd - 0.2); N = std::max(1, std::min(N, (int)MASK - 2));
                x.N = N; x.c = solve_ap(P - N - lpd, w); G = 1 + (1 - x.a) / (1 + x.a);   /* + a one-pole low-pass's noise gain */
            }
            if (x.synth == FORMANT) {                                   /* one peak, +18 dB, at the named partial */
                const double fp = std::fmin((1 + 15 * colour) * f, 0.44 * sr), wp = 2 * PI * fp / sr;
                const double Bp = std::fmax(x.mode == DRY ? 73.0 : 20.0, (1 + 3 * (1 - focus)) * 0.5 * f), al = std::sin(wp) * Bp / (2 * fp);
                x.fb0 = al / (1 + al); x.fa1 = -2 * std::cos(wp) / (1 + al); x.fa2 = (1 - al) / (1 + al); x.fx1 = x.fx2 = x.fy1 = x.fy2 = 0;
                G *= 1 + 6.9 * 6.9 * PI * Bp / sr;                    /* the peak's own share of the power */
            }
        }
        x.agc = G > 0 ? std::fmin(1000.0, 1 / std::sqrt(G)) : 1;
    }
    double comb(Voice &x, double in) {
        double y;
        if (x.mode == RINGING) y = loop(x, in);
        else {
            double read = x.line[(x.w - (unsigned)x.N) & MASK];
            double ap = x.c * read + x.ap_x - x.c * x.ap_y; x.ap_x = read; x.ap_y = std::fabs(ap) < 1e-20 ? 0 : ap;
            x.lp = (1 - x.a) * x.ap_y + x.a * x.lp; if (std::fabs(x.lp) < 1e-20) x.lp = 0;
            x.line[x.w & MASK] = (float)in; x.w++;
            y = in + x.lp;
        }
        if (x.synth == FORMANT) {                                       /* a true band-pass at the peak, added at +18 dB */
            double o = x.fb0 * (y - x.fx2) - x.fa1 * x.fy1 - x.fa2 * x.fy2; if (std::fabs(o) < 1e-20) o = 0;
            x.fx2 = x.fx1; x.fx1 = y; x.fy2 = x.fy1; x.fy1 = o; y += 6.9 * o;
        }
        return y;
    }
    /* one output sample; a frame is analysed when this voice's hop comes round (voices staggered across the hop, so 24
       never all transform in one block). The recording is a file, so the frame reads ahead of the note: no delay. */
    double spectral(Voice &x) {
        if (((x.sk + x.soff) & (SPH - 1)) == 0) spectral_frame(x);
        float &o = x.ola[(size_t)(x.sk & (SPN - 1))]; const double y = o; o = 0; x.sk++;
        return y;
    }
    void spectral_frame(Voice &x) {
        if (x.synth == FREEZE) { freeze_frame(x); return; }
        const long long base = x.pos - 1;                              /* this sample's place in the recording */
        for (int j = 0; j < SPN; j++) xin[j] = src.at(base + j) * win[j];
        rf.forward(xin.data(), Xr.data(), Xi.data());
        for (int k = 0; k <= SPN / 2; k++) {
            double re = 0, im = 0;
            if (x.mask[k] > 0) {
                const double m = x.mask[k];
                if (x.mode == DRY) { re = Xr[k] * m; im = Xi[k] * m; }
                else {
                    /* a spectral sustain, sounding at the partial's own frequency: the held bin (hold, ph as re, im) turns by
                       its partial's step each hop and decays, unless the recording is louder there now - no trigonometry
                       per bin (atan2 / cos / sin per bin put 24 voices over budget: 2.49 ms of 2.0, measured) */
                    const int n = x.owner[k]; const double tr = x.hold[k] * x.rotr[n] - x.ph[k] * x.roti[n], ti = x.hold[k] * x.roti[n] + x.ph[k] * x.rotr[n];
                    const double nr = Xr[k] * m, ni = Xi[k] * m;
                    if (nr * nr + ni * ni >= tr * tr + ti * ti) { re = nr; im = ni; } else { re = tr; im = ti; }
                    x.hold[k] = (float)re; x.ph[k] = (float)im;
                }
            }
            Xr[k] = (float)re; Xi[k] = (float)im;
        }
        rf.inverse(Xr.data(), Xi.data(), yout.data());
        const long long at = x.sk;
        for (int j = 0; j < SPN; j++) x.ola[(size_t)((at + j) & (SPN - 1))] += yout[j] / SPN * win[j] / 1.5f;
    }
    double bank(Voice &x, double in) {
        double y = 0; const double dx = in - x.px2; x.px2 = x.px1; x.px1 = in;
        for (int k = 0; k < x.np; k++) {
            double o = x.pb0[k] * dx - x.pa1[k] * x.py1[k] - x.pa2[k] * x.py2[k];
            if (std::fabs(o) < 1e-20) o = 0;
            x.py2[k] = x.py1[k]; x.py1[k] = o; y += x.pw[k] * o;
        }
        return y;
    }

    void attack(double f, double t, double vel) override {
        int q = -1;
        for (int i = 0; i < nv; i++) if (!v[i].active) { q = i; break; }
        if (q >= 0) { start(v[q], f, t, vel); last = q; return; }
        /* the quietest gives way (A-5): first a voice already fading out, then the quietest sounding one - never one
           holding a waiting note or about to start its own, which would be lost (final review: a scale lost its
           first note, a re-pressed chord half its notes - a not-yet-started voice reads as silent) */
        auto rank = [](const Voice &x) { return x.stealing ? -1.0 : !x.started ? 2.0 : x.env; };
        for (int i = 0; i < nv; i++) if (!v[i].has_next && (q < 0 || rank(v[i]) < rank(v[q]))) q = i;
        if (q < 0) { q = 0; for (int i = 1; i < nv; i++) if (v[i].env < v[q].env) q = i; }
        Voice &x = v[q];
        if (!x.stealing) { x.steal_at = t - STEAL_S; x.fade = 1.0 / (STEAL_S * sr); }   /* one already fading keeps fading */
        x.stealing = true; x.has_next = true; x.nf = f; x.nt = t; x.nvel = vel; x.noff = 1e300; last = q;
    }
    /* every voice to its release at t (a role switched away: none frozen until the 2.5 s cut - final review 3a I1) */
    void release_all(double t) { for (auto &x : v) if (x.active) { if (x.has_next) x.noff = std::fmin(x.noff, t); else x.off_t = std::fmin(x.off_t, t); } }
    void release(double t) override {
        if (last < 0) return;
        Voice &x = v[last];
        if (x.has_next) x.noff = t; else x.off_t = t;
    }
    /* the two timbre slots the morphs drive (spec D10): Focus and Colour, for the next note */
    void timbre(int which, double val, double, double) override { if (which == 0) focus = val; else colour = val; }
    /* a setting changed (Kerem 2026-10-06: twisting the Harmonic filter changed nothing - settings were read at a note's
       start, and a held note never restarts): every sounding note fades out over 50 ms while the same note starts again with
       the settings now set (a free voice; else the steal path restarts it after its fade). Its planned release is kept */
    void revoice(double t) {
        double fs[VOICES], vs[VOICES], ts[VOICES], offs[VOICES]; int n = 0;
        for (int i = 0; i < nv; i++) {
            Voice &x = v[i];
            if (!x.active || x.releasing || x.stealing) continue;
            fs[n] = x.req; vs[n] = x.vel;   /* as asked: x.f already has the fold and Octave in it (final review 6 C1) */ ts[n] = std::fmax(t, x.on_t); offs[n] = x.off_t; n++;
            x.stealing = true; x.has_next = false; x.steal_at = t; x.fade = 1.0 / (STEAL_S * sr);
        }
        for (int k = 0; k < n; k++) { attack(fs[k], ts[k], vs[k]); if (offs[k] < 1e299) release(offs[k]); }
    }
    void stop_all() { for (auto &x : v) if (x.active) { x.stealing = true; x.has_next = false; x.steal_at = 0; x.fade = 1.0 / (STOP_S * sr); } }

    double resonate(Voice &x, double in) {
        if (x.synth == SFM || x.synth == SAM) return fmam(x);
        if (x.synth == RETUNE) return retune(x);
        if (x.synth == PULSAR) return pulsar(x);
        if (x.synth == FREEZE) return spectral(x);
        if (x.synth != RESONATE) return x.method == SPECTRAL ? spectral(x) : x.method == COMB ? comb(x, in) : bank(x, in);
        if (x.body == BELL) {
            double y = 0;
            for (int k = 0; k < x.modes; k++) {
                double o = x.b0[k] * in - x.a1[k] * x.y1[k] - x.a2[k] * x.y2[k];
                if (std::fabs(o) < 1e-20) o = 0;
                x.y2[k] = x.y1[k]; x.y1[k] = o; y += x.wt[k] * o;
            }
            return y;
        }
        return loop(x, in);
    }
    double loop(Voice &x, double in) {
        double read = x.line[(x.w - (unsigned)x.N) & MASK];
        double ap = x.c * read + x.ap_x - x.c * x.ap_y; x.ap_x = read; x.ap_y = std::fabs(ap) < 1e-20 ? 0 : ap;
        x.lp = (1 - x.a) * x.ap_y + x.a * x.lp; if (std::fabs(x.lp) < 1e-20) x.lp = 0;
        double hp = x.lp - x.lx + LOOP_R * x.ly; x.lx = x.lp; x.ly = std::fabs(hp) < 1e-20 ? 0 : hp;
        double y = in + (x.body == TUBE ? -1 : 1) * x.g * x.ly;
        x.line[x.w & MASK] = (float)y; x.w++;
        double o = y - x.dc_x + 0.995 * x.dc_y; x.dc_x = y; x.dc_y = std::fabs(o) < 1e-20 ? 0 : o;   /* DC blocker */
        return x.dc_y;
    }

    void render(float *L, float *R, int n, double t0) override {
        /* the attack is a raised cosine (S-curve): it fades in rather than arriving at the end. Its steepest point is
           pi/2 x a straight line's, so the shortest attack is 8 ms - no steeper than the old 5 ms line (A-2) */
        const double up = 1.0 / (std::fmax(0.008, att) * sr);
        const double kr = std::pow(1e-4, 1.0 / (std::fmax(0.03, rel) * sr)), ka = 1 - std::exp(-1.0 / (0.3 * sr));
        const double kt = 1 - std::exp(-1.0 / (0.01 * sr)), ks = 1 - std::exp(-1.0 / (3.0 * sr));
        if (tune_s < 0) tune_s = tune;
        std::vector<double> &tu = tune_buf; tu.resize((size_t)n);
        for (int i = 0; i < n; i++) { tune_s += (tune - tune_s) * kt; if (std::fabs(tune - tune_s) < 1e-9) tune_s = tune; tu[(size_t)i] = tune_s; }
        for (auto &x : v) {
            if (!x.active) continue;
            for (int i = 0; i < n; i++) {
                const double t = t0 + i / sr;
                if (!x.started) { if (t < x.on_t) continue; x.started = true; }
                if (!x.releasing && t >= x.off_t) x.releasing = true;
                if (x.stealing && t >= x.steal_at) {
                    x.env -= x.fade;
                    if (x.env <= 0) {
                        x.env = 0;
                        if (!x.has_next) { x.active = false; break; }
                        double nf = x.nf, nt = std::fmax(x.nt, t), nv = x.nvel, no = x.noff;
                        start(x, nf, nt, nv); x.off_t = no; continue;
                    }
                } else if (x.releasing) { x.env *= kr; if (x.env < 1e-4) { if (x.has_next) { x.env = 0; x.steal_at = 0; continue; } x.active = false; break; } }
                else if (x.aph < 1) { x.aph = std::fmin(1.0, x.aph + up); x.env = 0.5 - 0.5 * std::cos(PI * x.aph); }
                const double in = src.at(x.pos++);
                const bool part = x.synth != RESONATE;
                double exc = in;
                if (!part && x.excite == PLUCKED) { exc = x.burst < x.burst_len ? in * 0.5 * (1 - std::cos(2 * PI * x.burst / x.burst_len)) : 0; x.burst++; }
                /* bowed noise through a feedback loop gains 1 / (1 - g^2) in power: fed through sqrt(1 - g^2), the
                   loop's level starts near the recording's and the automatic gain below only fine-tunes it */
                const bool fed = part ? x.method == COMB && x.mode == RINGING : x.excite == BOWED && x.body != BELL;   /* a feedback loop fed continuously */
                double wet = resonate(x, fed ? exc * std::sqrt(std::fmax(0.0, 1 - x.g60 * x.g60)) : exc) * x.pkg;
                if ((part || x.excite == BOWED) && !pitched_sampler(x.synth)) {   /* the bowed level follows the recording's (not Retune's: its own dynamics) */
                    /* partial synths: 3 s, so gusts keep their shape. Freeze matches a fixed power, the moment's - the
                       recording moving on underneath must not move a frozen note - at the 0.3 s rate */
                    const bool frz = x.synth == FREEZE;
                    const double k = part && !frz ? ks : ka;
                    if (frz) x.rin = x.fpow; else x.rin += (exc * exc - x.rin) * k;
                    x.rout += (wet * wet - x.rout) * k;
                    /* only while the recording sounds: silence is never boosted (up to 1000x - a bowed Bell or Tube keeps
                       a small share of broadband energy). The loop is linear, so the gain asked for is the same at any
                       input level: a quiet passage does not wind it up (measured: no swell when loud returns). */
                    if (x.rin > 1e-10) { double tgt = x.rout > 1e-14 ? std::fmin(1000.0, std::sqrt(x.rin / x.rout)) : 1; if (!part || x.rout > 1e-14) x.agc += (tgt - x.agc) * k; }
                    wet *= x.agc;
                }
                const double mix = pitched_sampler(x.synth) ? 1 : tu[(size_t)i];   /* Retune's Tune bends its speed, not a dry blend */
                const double o = x.env * x.vel * vol * ((1 - mix) * exc + mix * wet);
                L[i] += (float)o; R[i] += (float)o;
            }
        }
    }
};

}  // namespace sampler
