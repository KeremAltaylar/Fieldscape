/* Recording analysis for sample harmony (docs/superpowers/specs/2026-09-30-sample-harmony-design.md):
   run once when a recording is attached; every engine then tunes from what it measured. */
#include "fieldscape.h"
#include "devices/fft.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const double PI = 3.141592653589793;
struct Frame { double f0, conf, centroid; };

/* YIN (de Cheveigne & Kawahara 2002): the cumulative-mean-normalised difference, the first dip under
   the threshold, refined by parabolic interpolation. conf = 1 - d'(tau). */
Frame yin(const float *x, int W, double sr, double fmin, double fmax, std::vector<double> &d) {
    int tmax = std::min(W / 2, (int)(sr / fmin)), tmin = std::max(2, (int)(sr / fmax));
    d.assign(tmax + 1, 0);
    std::vector<double> raw(tmax + 2, 0);   /* the plain difference: YIN step 5 interpolates on it, not on d' */
    double run = 0;
    for (int tau = 1; tau <= tmax; tau++) {
        double s = 0;
        for (int i = 0; i < W - tmax; i++) { double e = x[i] - x[i + tau]; s += e * e; }
        raw[tau] = s; run += s; d[tau] = run > 0 ? s * tau / run : 1;
    }
    int best = -1;
    for (int tau = tmin; tau <= tmax; tau++) if (d[tau] < 0.15) { while (tau + 1 <= tmax && d[tau + 1] < d[tau]) tau++; best = tau; break; }
    if (best < 0) { best = tmin; for (int tau = tmin; tau <= tmax; tau++) if (d[tau] < d[best]) best = tau; }
    double t = best;
    if (best > 1 && best < tmax) { double a = raw[best - 1], b = raw[best], c = raw[best + 1], den = a - 2 * b + c; if (den != 0) t = best + std::max(-1.0, std::min(1.0, 0.5 * (a - c) / den)); }   /* a parabola never moves the dip past its neighbours */
    double conf = std::max(0.0, std::min(1.0, 1 - d[best]));
    return { t > 0 ? sr / t : 0, conf, 0 };
}

double centroid(const float *x, int n, double sr, FFT &fft, std::vector<float> &buf) {
    const int N = fft.n;
    float *ar = buf.data(), *ai = ar + N, *br = ai + N, *bi = br + N;
    for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * PI * i / N); ar[i] = i < n ? (float)(x[i] * w) : 0; ai[i] = 0; }
    for (int p = 0; p < fft.passes; p++) {
        if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p));
    }
    const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; k++) { double m = std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]); num += m * k * sr / N; den += m; }
    return den > 1e-9 ? num / den : 0;
}

/* The recording's pitch, refined from its spectrum: YIN finds the period, but a partial that is not a
   harmonic (a bell's 2.76x, a piano's stretched overtones) bends that period by several cents. Here
   each of up to 5 confident moments is transformed at 8192 points; the peak near each of the first 8
   harmonics (within 3%) is located by a parabola on log magnitude, and the fundamentals they imply are
   averaged, weighted by strength. Partials far from any harmonic are ignored. */
double refine(const float *x, long long n, double sr, double f0, const std::vector<long long> &centres) {
    const int N = 8192;
    if (n < N || !(f0 > 0)) return f0;
    FFT fft; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
    std::vector<float> buf(4 * N);
    float *ar = buf.data(), *ai = ar + N, *br = ai + N, *bi = br + N;
    double num = 0, den = 0;
    for (long long c : centres) {
        long long a = std::max(0LL, std::min(n - N, c - N / 2));
        for (int i = 0; i < N; i++) { ar[i] = (float)(x[a + i] * (0.5 - 0.5 * std::cos(2 * PI * i / N))); ai[i] = 0; }
        for (int p = 0; p < fft.passes; p++) {
            if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p));
        }
        const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
        auto mag = [&](int k) { return std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]); };
        for (int h = 1; h <= 8; h++) {
            double want = h * f0 * N / sr;
            int lo = std::max(1, (int)std::floor(want * 0.97)), hi = std::min(N / 2 - 2, (int)std::ceil(want * 1.03));
            if (lo >= hi) continue;
            int pk = lo; for (int k = lo; k <= hi; k++) if (mag(k) > mag(pk)) pk = k;
            if (pk == lo || pk == hi) continue;                       /* no peak inside: nothing harmonic here */
            double la = std::log(mag(pk - 1) + 1e-12), lb = std::log(mag(pk) + 1e-12), lc = std::log(mag(pk + 1) + 1e-12);
            double dd = la - 2 * lb + lc, off = dd != 0 ? 0.5 * (la - lc) / dd : 0;
            double w = mag(pk);
            num += w * (pk + off) * sr / N / h; den += w;
        }
    }
    return den > 0 ? num / den : f0;
}

long zero_up(const float *x, long long n, long at, long reach) {   /* the nearest rising zero crossing */
    for (long d = 0; d <= reach; d++) for (long s : { at + d, at - d }) if (s > 0 && s < n && x[s - 1] <= 0 && x[s] > 0) return s;
    return -1;
}

void num(std::string &s, double v, const char *fmt) { char b[40]; std::snprintf(b, sizeof b, fmt, std::isfinite(v) ? v : 0.0); s += b; }

}  // namespace

extern "C" int fs_analyse(const float *x, long long n, double sr, char *out, int size) {
    /* YIN's cost grows with the square of the window, so the pitch search runs on a copy averaged down
       to ~16 kHz (a 30 s file in well under a second in the browser); brightness and the loop and cycle
       cuts use the recording itself. W and H are in the recording's own frames. */
    const int K = std::max(1, (int)(sr / 16000));
    const double sd = sr / K;
    const int Wd = (int)std::lround(sd * 0.04), Hd = (int)std::lround(sd * 0.02), W = Wd * K, H = Hd * K;
    std::vector<float> dx((size_t)(n / K));
    for (size_t i = 0; i < dx.size(); i++) { double acc = 0; for (int k = 0; k < K; k++) acc += x[i * K + k]; dx[i] = (float)(acc / K); }
    std::vector<Frame> tr;
    FFT fft; int N = 2048; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
    std::vector<float> buf(4 * N); std::vector<double> d;
    double peak = 0; for (long long i = 0; i < n; i++) peak = std::max(peak, (double)std::fabs(x[i]));
    /* Two searches per frame, the more confident one kept: 50 Hz - 2 kHz on the 16 kHz copy, and
       1 - 8 kHz on the recording itself over 10 ms (birds sing at 2-8 kHz; Kerem's goldfinch read
       "unpitched" with the low search alone, 2026-09-30). The high one is cheap: its lags are short. */
    const int Wh = (int)std::lround(sr * 0.01);
    const double fhi = std::min(8000.0, sr / 4);
    std::vector<double> d2, en;
    for (long long at = 0; Wd > 0 && at + Wd <= (long long)dx.size() && peak > 1e-4; at += Hd) {
        Frame f = yin(dx.data() + at, Wd, sd, 50, 2000, d);
        if (fhi > 1000 && at * K + Wh <= n) {
            Frame g = yin(x + at * K, Wh, sr, 1000, fhi, d2);
            if (g.conf > f.conf) f = g;
        }
        double e = 0; for (int i = 0; i < Wd; i++) e += (double)dx[at + i] * dx[at + i];
        en.push_back(e / Wd);
        f.centroid = centroid(x + at * K, std::min(W, N), sr, fft, buf);
        tr.push_back(f);
    }
    /* Silence is not a pitch, and does not vote: a frame 40 dB under the loudest (or absolutely quiet)
       has no pitch and is left out of the verdict and the mean confidence - the calls of a bird are
       separated by more silence than sound. */
    double emax = 0; for (double e : en) emax = std::max(emax, e);
    std::vector<char> sounding(tr.size(), 1);
    for (size_t i = 0; i < tr.size(); i++) if (en[i] < std::max(1e-7, emax * 1e-4)) { sounding[i] = 0; tr[i].f0 = 0; tr[i].conf = 0; }
    /* octave errors: a confident frame an octave off the median of its confident neighbours is folded */
    std::vector<double> good; for (auto &f : tr) if (f.conf >= 0.8 && f.f0 > 0) good.push_back(f.f0);
    double f0 = 0;
    if (!good.empty()) { std::vector<double> g = good; std::nth_element(g.begin(), g.begin() + g.size() / 2, g.end()); f0 = g[g.size() / 2]; }
    for (auto &f : tr) if (f0 > 0 && f.f0 > 0) { double c = 1200 * std::log2(f.f0 / f0); if (std::fabs(c - 1200) < 60) f.f0 /= 2; else if (std::fabs(c + 1200) < 60) f.f0 *= 2; }
    size_t heard = 0; for (char c : sounding) heard += c;
    bool pitched = heard > 0 && good.size() * 2 >= heard;
    if (pitched && f0 > 0) {                                           /* up to 5 confident moments, spread evenly */
        std::vector<long long> cs;
        std::vector<int> ok; for (int i = 0; i < (int)tr.size(); i++) if (tr[i].conf >= 0.8 && tr[i].f0 > 0) ok.push_back(i);
        for (int k = 0; k < 5 && !ok.empty(); k++) cs.push_back((long long)ok[ok.size() * (2 * k + 1) / 10] * H + W / 2);
        f0 = refine(x, n, sr, f0, cs);
    }
    double conf = 0, cen = 0;
    for (size_t i = 0; i < tr.size(); i++) if (sounding[i]) { conf += tr[i].conf; cen += tr[i].centroid; }
    if (heard) { conf /= heard; cen /= heard; }
    /* the steadiest confident run (within 20 cents of f0): the loop lives there; its most confident frame gives the cycle */
    long loop_a = -1, loop_b = -1, cyc_a = -1, cyc_b = -1;
    if (pitched && f0 > 0) {
        int bs = -1, bl = 0, s = -1;
        for (int i = 0; i <= (int)tr.size(); i++) {
            bool ok = i < (int)tr.size() && tr[i].conf >= 0.8 && std::fabs(1200 * std::log2(std::max(tr[i].f0, 1e-9) / f0)) < 20;
            if (ok && s < 0) s = i;
            if (!ok && s >= 0) { if (i - s > bl) { bl = i - s; bs = s; } s = -1; }
        }
        if (bs >= 0) {
            double period = sr / f0;
            long a = zero_up(x, n, (long)bs * H + W / 2, (long)period);
            long want = (long)((long long)(bl - 1) * H);
            long k = std::max(1L, (long)std::floor(want / period));
            long b = a >= 0 ? zero_up(x, n, a + (long)std::lround(k * period), (long)(period / 2)) : -1;
            /* a loop shorter than 100 ms is a buzz, not the sound (a goldfinch gave one 13-sample cycle): none */
            if (a >= 0 && b > a && b - a >= (long)(0.1 * sr)) { loop_a = a; loop_b = b; }
            int bestf = bs; for (int i = bs; i < bs + bl; i++) if (tr[i].conf > tr[bestf].conf) bestf = i;
            long c0 = zero_up(x, n, (long)bestf * H + W / 2, (long)period);
            long c1 = c0 >= 0 ? zero_up(x, n, c0 + (long)std::lround(period), (long)(period / 4)) : -1;
            if (c0 >= 0 && c1 > c0) { cyc_a = c0; cyc_b = c1; }
        }
    }
    std::string s = "{\"rate\":"; num(s, sr, "%.0f"); s += ",\"frames\":" + std::to_string(n) + ",\"f0\":"; num(s, pitched ? f0 : 0, "%.2f");
    s += ",\"confidence\":"; num(s, conf, "%.3f"); s += ",\"centroid_hz\":"; num(s, cen, "%.0f");
    s += std::string(",\"verdict\":\"") + (pitched ? "pitched" : "unpitched") + "\",\"range\":";
    if (pitched && f0 > 0) { int m = (int)std::lround(69 + 12 * std::log2(f0 / 440)); s += "[" + std::to_string(m - 6) + "," + std::to_string(m + 6) + "]"; } else s += "null";
    s += ",\"loop\":" + (loop_a >= 0 ? "[" + std::to_string(loop_a) + "," + std::to_string(loop_b) + "]" : std::string("null"));
    s += ",\"cycle\":" + (cyc_a >= 0 ? "[" + std::to_string(cyc_a) + "," + std::to_string(cyc_b) + "]" : std::string("null"));
    s += ",\"hop_s\":0.02,\"track\":[";
    for (size_t i = 0; i < tr.size(); i++) { s += i ? ",[" : "["; num(s, tr[i].f0, "%.2f"); s += ","; num(s, tr[i].conf, "%.3f"); s += ","; num(s, tr[i].centroid, "%.0f"); s += "]"; }
    s += "]}";
    int need = (int)s.size();
    if (out && size > need) std::memcpy(out, s.c_str(), need + 1);
    return need;
}
