/* The synths that play recordings (sample harmony, docs/superpowers/specs/2026-09-30-sample-harmony-design.md).
   2a: the Resonator (docs/superpowers/specs/2026-09-30-non-pitch-2a-resonator-design.md) - an unpitched
   recording feeds a tuned resonator per note, so wind or water plays the chord from its own material.
   A tone::Synth: notes at times on the caller's clock, rendered by adding into the buffers given. */
#pragma once
#include "synths.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace sampler {

enum Body { STRING = 0, TUBE = 1, BELL = 2 };
enum Excite { BOWED = 0, PLUCKED = 1 };
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

struct Voice {
    bool active = false, started = false, releasing = false, stealing = false;
    double f = 0, on_t = 0, off_t = 1e300; double vel = 0;
    bool has_next = false; double nf = 0, nt = 0, noff = 1e300, nvel = 0;   /* the note waiting for a steal's fade */
    double env = 0;
    long long pos = 0; int burst = 0, burst_len = 0;
    std::vector<float> line; unsigned w = 0; int N = 1;
    double c = 0, a = 0, g = 0, ap_x = 0, ap_y = 0, lp = 0;
    int modes = 0; double b0[4] = {}, a1[4] = {}, a2[4] = {}, y1[4] = {}, y2[4] = {}, wt[4] = {};
    double dc_x = 0, dc_y = 0, lx = 0, ly = 0, g60 = 0, rin = 0, rout = 0, agc = 1;
};

struct Resonator : tone::Synth {
    static const int VOICES = 6;
    static const unsigned MASK = (1u << 13) - 1;                  /* 8192-sample lines: down to ~6 Hz */
    Source src;
    int body = STRING, excite = BOWED;
    double focus = 0.5, colour = 0.5, tune = 1, att = 0.02, rel = 0.6, offset_s = 0;
    Voice v[VOICES]; int last = -1;

    static double t60(double focus) { return 0.2 * std::pow(50.0, std::fmin(1.0, std::fmax(0.0, focus))); }

    void init(double s) override { sr = s; for (auto &x : v) x.line.assign(MASK + 1, 0.0f); }
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
        x.f = f; x.on_t = t; x.off_t = 1e300; x.vel = vel; x.env = 0;
        x.pos = (long long)(offset_s * sr); x.burst = 0; x.burst_len = (int)(0.025 * sr);
        x.w = 0; x.ap_x = x.ap_y = x.lp = 0; x.dc_x = x.dc_y = 0; x.lx = x.ly = 0; x.rin = x.rout = 0; x.agc = 1;
        const double w = 2 * PI * f / sr, T = t60(focus);
        /* Colour is set against the note (its cutoff 1.5x to 31x the fundamental, open at 1): dark stays dark in
           every register without swallowing the fundamental - a fixed filter killed high notes in milliseconds */
        const double col = std::fmin(1.0, std::fmax(0.0, colour));
        x.a = col >= 0.999 ? 0 : std::exp(-2 * PI * std::fmin(f * (1.5 + 30 * col * col), 0.45 * sr) / sr);
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
        } else {
            const double P = sr / f / (body == TUBE ? 2 : 1);        /* Tube: half a period, feedback inverted */
            const double lpd = lp_delay(x.a, w) + dc_delay(LOOP_R, w);
            int N = (int)std::floor(P - lpd - 0.2); N = std::max(1, std::min(N, (int)MASK - 2));
            x.N = N; x.c = solve_ap(P - N - lpd, w);
            x.g60 = std::pow(10.0, -3.0 * (P / sr) / T);              /* one pass of the loop, in T60 terms */
            /* the filters' loss made good at f, so the fundamental rings exactly T60; the DC blocker in the loop
               keeps 0 Hz (where the gain would exceed 1) from growing, and above f the colour filter only cuts */
            x.g = x.g60 / (lp_gain(x.a, w) * dc_gain(LOOP_R, w));
        }
    }

    void attack(double f, double t, double vel) override {
        int q = -1;
        for (int i = 0; i < VOICES; i++) if (!v[i].active) { q = i; break; }
        if (q >= 0) { start(v[q], f, t, vel); last = q; return; }
        q = 0; for (int i = 1; i < VOICES; i++) if (v[i].env < v[q].env) q = i;   /* the quietest gives way (A-5) */
        Voice &x = v[q]; x.stealing = true; x.has_next = true; x.nf = f; x.nt = t; x.nvel = vel; x.noff = 1e300; last = q;
    }
    void release(double t) override {
        if (last < 0) return;
        Voice &x = v[last];
        if (x.has_next) x.noff = t; else x.off_t = t;
    }
    /* the two timbre slots the morphs drive (spec D10): Focus and Colour, for the next note */
    void timbre(int which, double val, double, double) override { if (which == 0) focus = val; else colour = val; }
    void stop_all() { for (auto &x : v) if (x.active) { x.stealing = true; x.has_next = false; } }

    double resonate(Voice &x, double in) {
        if (body == BELL) {
            double y = 0;
            for (int k = 0; k < x.modes; k++) {
                double o = x.b0[k] * in - x.a1[k] * x.y1[k] - x.a2[k] * x.y2[k];
                if (std::fabs(o) < 1e-20) o = 0;
                x.y2[k] = x.y1[k]; x.y1[k] = o; y += x.wt[k] * o;
            }
            return y;
        }
        double read = x.line[(x.w - (unsigned)x.N) & MASK];
        double ap = x.c * read + x.ap_x - x.c * x.ap_y; x.ap_x = read; x.ap_y = std::fabs(ap) < 1e-20 ? 0 : ap;
        x.lp = (1 - x.a) * x.ap_y + x.a * x.lp; if (std::fabs(x.lp) < 1e-20) x.lp = 0;
        double hp = x.lp - x.lx + LOOP_R * x.ly; x.lx = x.lp; x.ly = std::fabs(hp) < 1e-20 ? 0 : hp;
        double y = in + (body == TUBE ? -1 : 1) * x.g * x.ly;
        x.line[x.w & MASK] = (float)y; x.w++;
        double o = y - x.dc_x + 0.995 * x.dc_y; x.dc_x = y; x.dc_y = std::fabs(o) < 1e-20 ? 0 : o;   /* DC blocker */
        return x.dc_y;
    }

    void render(float *L, float *R, int n, double t0) override {
        const double up = 1.0 / (std::fmax(0.005, att) * sr), fade = 1.0 / (0.005 * sr);
        const double kr = std::pow(1e-4, 1.0 / (std::fmax(0.03, rel) * sr)), ka = 1 - std::exp(-1.0 / (0.3 * sr));
        for (auto &x : v) {
            if (!x.active) continue;
            for (int i = 0; i < n; i++) {
                const double t = t0 + i / sr;
                if (!x.started) { if (t < x.on_t) continue; x.started = true; }
                if (!x.releasing && t >= x.off_t) x.releasing = true;
                if (x.stealing) {
                    x.env -= fade;
                    if (x.env <= 0) {
                        x.env = 0;
                        if (!x.has_next) { x.active = false; break; }
                        double nf = x.nf, nt = std::fmax(x.nt, t), nv = x.nvel, no = x.noff;
                        start(x, nf, nt, nv); x.off_t = no; continue;
                    }
                } else if (x.releasing) { x.env *= kr; if (x.env < 1e-4) { x.active = false; break; } }
                else if (x.env < 1) x.env = std::fmin(1.0, x.env + up);
                const double in = src.at(x.pos++);
                double exc = in;
                if (excite == PLUCKED) { exc = x.burst < x.burst_len ? in * 0.5 * (1 - std::cos(2 * PI * x.burst / x.burst_len)) : 0; x.burst++; }
                double wet = resonate(x, excite == BOWED && body != BELL ? exc * (1 - x.g60) : exc);
                if (excite == BOWED) {                                /* the bowed level follows the recording's */
                    x.rin += (exc * exc - x.rin) * ka; x.rout += (wet * wet - x.rout) * ka;
                    double tgt = x.rout > 1e-12 ? std::fmin(50.0, std::sqrt(x.rin / x.rout)) : 1;
                    x.agc += (tgt - x.agc) * ka; wet *= x.agc;
                }
                const double o = x.env * x.vel * vol * ((1 - tune) * exc + tune * wet);
                L[i] += (float)o; R[i] += (float)o;
            }
        }
    }
};

}  // namespace sampler
