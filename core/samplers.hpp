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
enum Synth { RESONATE = 0, HARMONIC = 1, FORMANT = 2, PULSAR = 3, FREEZE = 4 };
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
    double dc_x = 0, dc_y = 0, lx = 0, ly = 0, g60 = 0, rin = 0, rout = 0, agc = 1;
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
};

struct Resonator : tone::Synth {
    /* 24: three overlapping chords of up to 8 notes - a long release rings under the next chords instead of being
       stolen (Kerem 2026-10-01: "smooth cloudy transitions when release is longer than the note") */
    static const int VOICES = 24;
    static constexpr double STEAL_S = 0.05, STOP_S = 0.005;
    static const unsigned MASK = (1u << 13) - 1;                  /* 8192-sample lines: down to ~6 Hz */
    Source src;
    int body = STRING, excite = BOWED, synth = RESONATE, method = BANK, mode = DRY;
    double focus = 0.5, colour = 0.5, tune = 1, att = 0.02, rel = 0.6, offset_s = 0;
    Voice v[VOICES]; int last = -1;
    double tune_s = -1;                                           /* Tune as heard: glides to `tune` over ~10 ms (A-2) */
    std::vector<double> tune_buf;                                 /* per block; sized once to the largest block */
    RFFT rf; std::vector<float> xin, Xr, Xi, yout, win;           /* Spectral: one transform shared by the voices */

    static double t60(double focus) { return 0.2 * std::pow(50.0, std::fmin(1.0, std::fmax(0.0, focus))); }

    void init(double s) override {
        sr = s;
        for (auto &x : v) {
            x.line.assign(MASK + 1, 0.0f);
            x.ola.assign(SPN, 0.0f); x.hold.assign(SPN / 2 + 1, 0.0f); x.ph.assign(SPN / 2 + 1, 0.0f); x.mask.assign(SPN / 2 + 1, 0.0f); x.owner.assign(SPN / 2 + 1, 0);
        }
        rf.init(SPN); xin.assign(SPN, 0.0f); Xr.assign(SPN / 2 + 1, 0.0f); Xi.assign(SPN / 2 + 1, 0.0f); yout.assign(SPN, 0.0f);
        win.resize(SPN); for (int j = 0; j < SPN; j++) win[j] = (float)(0.5 - 0.5 * std::cos(2 * PI * j / SPN));
    }
    void set_source(int ch, long long n, const float *const *p) {
        src = Source(); if (!p || n <= 0) return;
        src.nch = std::min(ch, 2); for (int k = 0; k < src.nch; k++) src.f[k] = p[k]; src.frames = n;
    }
    void set_source_i16(int ch, long long n, const int16_t *const *p) {
        src = Source(); if (!p || n <= 0) return;
        src.nch = std::min(ch, 2); for (int k = 0; k < src.nch; k++) src.s[k] = p[k]; src.frames = n;
    }

    /* a voice made ready for a note: the loop tuned to f (String/Tube) or the modes set (Bell) */
    void start(Voice &x, double f, double t, double vel) {
        std::fill(x.line.begin(), x.line.end(), 0.0f);
        f = std::fmin(std::fmax(f, 6.0), 0.45 * sr);                 /* extreme octaves: clamped, never unstable */
        x.active = true; x.started = false; x.releasing = false; x.stealing = false; x.has_next = false;
        x.f = f; x.on_t = t; x.off_t = 1e300; x.vel = vel; x.env = 0; x.aph = 0; x.body = body; x.excite = excite; x.synth = synth; x.method = method; x.mode = mode;
        x.pos = (long long)(offset_s * sr); x.burst = 0; x.burst_len = (int)(0.025 * sr);
        x.w = 0; x.ap_x = x.ap_y = x.lp = 0; x.dc_x = x.dc_y = 0; x.lx = x.ly = 0; x.rin = x.rout = 0; x.agc = 1;
        const double w = 2 * PI * f / sr, T = t60(focus);
        /* Colour is set against the note (its cutoff 1.5x to 31x the fundamental, open at 1): dark stays dark in
           every register without swallowing the fundamental - a fixed filter killed high notes in milliseconds */
        const double col = std::fmin(1.0, std::fmax(0.0, colour));
        x.a = col >= 0.999 ? 0 : std::exp(-2 * PI * std::fmin(f * (1.5 + 30 * col * col), 0.45 * sr) / sr);
        if (synth == PULSAR) { start_pulsar(x, f); return; }
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
        } else start_loop(x, f, T, body == TUBE);
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

    /* Pulsar (2c): a grain of the recording every period - fractional, so the repetition rate is the note exactly -
       Hann-shaped, d of a period long (Colour); its slice refreshed every R(Focus) = 1 ms ... 10 s, never at Focus 1 */
    void start_pulsar(Voice &x, double f) {
        const double d = 0.05 + 0.95 * std::fmin(1.0, std::fmax(0.0, colour));
        x.pP = sr / f; x.pD = std::fmax(4.0, d * x.pP); x.sk = 0; x.pk = -1;
        x.pr = x.pos; x.plast = 0;
        x.prefresh = focus >= 0.999 ? -1 : (long long)std::llround(0.001 * std::pow(10.0, 4 * std::fmax(0.0, focus)) * sr);
        x.agc = std::fmin(1000.0, 1 / std::sqrt(0.375 * std::fmin(1.0, x.pD / x.pP)));   /* a Hann grain of duty d: 0.375 d of the power */
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
            std::fill(x.ola.begin(), x.ola.end(), 0.0f); std::fill(x.hold.begin(), x.hold.end(), 0.0f);
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
        for (int i = 0; i < VOICES; i++) if (!v[i].active) { q = i; break; }
        if (q >= 0) { start(v[q], f, t, vel); last = q; return; }
        /* the quietest gives way (A-5): first a voice already fading out, then the quietest sounding one - never one
           holding a waiting note or about to start its own, which would be lost (final review: a scale lost its
           first note, a re-pressed chord half its notes - a not-yet-started voice reads as silent) */
        auto rank = [](const Voice &x) { return x.stealing ? -1.0 : !x.started ? 2.0 : x.env; };
        for (int i = 0; i < VOICES; i++) if (!v[i].has_next && (q < 0 || rank(v[i]) < rank(v[q]))) q = i;
        if (q < 0) { q = 0; for (int i = 1; i < VOICES; i++) if (v[i].env < v[q].env) q = i; }
        Voice &x = v[q];
        if (!x.stealing) { x.steal_at = t - STEAL_S; x.fade = 1.0 / (STEAL_S * sr); }   /* one already fading keeps fading */
        x.stealing = true; x.has_next = true; x.nf = f; x.nt = t; x.nvel = vel; x.noff = 1e300; last = q;
    }
    void release(double t) override {
        if (last < 0) return;
        Voice &x = v[last];
        if (x.has_next) x.noff = t; else x.off_t = t;
    }
    /* the two timbre slots the morphs drive (spec D10): Focus and Colour, for the next note */
    void timbre(int which, double val, double, double) override { if (which == 0) focus = val; else colour = val; }
    void stop_all() { for (auto &x : v) if (x.active) { x.stealing = true; x.has_next = false; x.steal_at = 0; x.fade = 1.0 / (STOP_S * sr); } }

    double resonate(Voice &x, double in) {
        if (x.synth == PULSAR) return pulsar(x);
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
                double wet = resonate(x, fed ? exc * std::sqrt(std::fmax(0.0, 1 - x.g60 * x.g60)) : exc);
                if (part || x.excite == BOWED) {                      /* the bowed level follows the recording's */
                    const double k = part ? ks : ka;                 /* partial synths: 3 s, so gusts keep their shape */
                    x.rin += (exc * exc - x.rin) * k; x.rout += (wet * wet - x.rout) * k;
                    /* only while the recording sounds: silence is never boosted (up to 1000x - a bowed Bell or Tube keeps
                       a small share of broadband energy). The loop is linear, so the gain asked for is the same at any
                       input level: a quiet passage does not wind it up (measured: no swell when loud returns). */
                    if (x.rin > 1e-10) { double tgt = x.rout > 1e-14 ? std::fmin(1000.0, std::sqrt(x.rin / x.rout)) : 1; if (!part || x.rout > 1e-14) x.agc += (tgt - x.agc) * k; }
                    wet *= x.agc;
                }
                const double o = x.env * x.vel * vol * ((1 - tu[(size_t)i]) * exc + tu[(size_t)i] * wet);
                L[i] += (float)o; R[i] += (float)o;
            }
        }
    }
};

}  // namespace sampler
