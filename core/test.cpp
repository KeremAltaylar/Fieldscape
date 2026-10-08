// The core's self-check. Build and run on any platform:
//   c++ -std=c++17 -O2 core/core.cpp core/mix.cpp core/place.cpp core/sections.cpp core/webm.cpp core/devices/*.cpp core/test.cpp -o fs_test && ./fs_test
#include "fieldscape.h"
#include "harmony.hpp"
#include "devices/fft.hpp"
#include "samplers.hpp"
#include "eq.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

/* Every note the route engine plays over a 60 s walk, hashed (frequencies to 1 mHz), with fixed draws. */
#define ROUTE_NOTES_HASH 11448281553059379543ULL   /* today's engine, captured 2026-09-30 before the harmony core */
struct NoteLog { std::vector<double> f; std::vector<int> role; };
static double fixed_draw(void *p) { unsigned *s = (unsigned *)p; *s = *s * 1664525u + 1013904223u; return (*s >> 8) / 16777216.0; }
static void log_note(void *p, int role, double f, double, double, double) { ((NoteLog *)p)->f.push_back(f); ((NoteLog *)p)->role.push_back(role); }
static NoteLog walk_notes(const char *patch, int default_tuning) {
    NoteLog log; unsigned seed = 12345;
    fs_device *d = fs_create("piece");
    fs_prepare(d, 48000, 128);
    fs_piece_test_hooks(d, fixed_draw, &seed, log_note, &log);
    fs_piece_test_walk(d, 60);
    if (default_tuning >= 0) fs_piece_default_tuning(d, default_tuning);
    int r = fs_piece_add_route(d, patch);
    fs_piece_walk(d, r, 0, 0);
    for (int i = 0; i < 60 * 48000 / 128; i++) fs_process(d, 128);
    fs_destroy(d);
    return log;
}
static unsigned long long note_hash(const NoteLog &l) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < l.f.size(); i++) { h = (h ^ (unsigned long long)std::llround(l.f[i] * 1000)) * 1099511628211ULL; h = (h ^ (unsigned)l.role[i]) * 1099511628211ULL; }
    return h;
}

/* A walk that swaps between two routes every 7.3 s at odd places along them, hashed as walk_notes. */
static unsigned long long swap_walk_hash() {
    NoteLog log; unsigned seed = 777;
    fs_device *d = fs_create("piece");
    fs_prepare(d, 48000, 128);
    fs_piece_test_hooks(d, fixed_draw, &seed, log_note, &log);
    int a = fs_piece_add_route(d, "{}"), b = fs_piece_add_route(d, "{}");
    for (int i = 0; i < 90 * 48000 / 128; i++) {
        double sec = i * 128.0 / 48000;
        int r = ((int)(sec / 7.3)) % 2 ? b : a;
        fs_piece_walk(d, r, std::fmod(sec * 0.0173 + (r == b ? 0.41 : 0.0), 1.0), 0);
        fs_process(d, 128);
    }
    fs_destroy(d);
    return note_hash(log);
}

/* sample harmony 2a: render a Resonator straight, and measure what it made */
static std::vector<float> noise_src(double secs, float amp, uint32_t seed) {
    std::vector<float> v((size_t)(secs * 48000));
    for (auto &x : v) { seed = seed * 1664525u + 1013904223u; x = amp * (((int)(seed >> 8) - 8388608) / 8388608.0f); }
    return v;
}
static std::vector<float> res_render(int body, int excite, double focus, double colour, double tune, double f, double secs,
                                     const std::vector<float> &src, double att = 0.02, double rel = 0.6) {
    sampler::Resonator r; r.init(48000);
    r.body = body; r.excite = excite; r.focus = focus; r.colour = colour; r.tune = tune; r.att = att; r.rel = rel;
    const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
    r.attack(f, 0.0, 0.5); r.release(secs + 10);
    std::vector<float> L((size_t)(secs * 48000), 0.0f), R(L.size(), 0.0f);
    for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, (int)std::min<size_t>(128, L.size() - i), i / 48000.0);
    return L;
}
/* the strongest frequency within f x (1 +- span): a 65536-point Hann FFT of the last 1.37 s, log-parabola peak */
static double peak_near(const std::vector<float> &x, double sr, double f, double span) {
    const int N = 65536; FFT fft; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
    std::vector<float> b(4 * N); float *ar = b.data(), *ai = ar + N, *br = ai + N, *bi = br + N;
    size_t s0 = x.size() - N;
    for (int i = 0; i < N; i++) { ar[i] = (float)(x[s0 + i] * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N))); ai[i] = 0; }
    for (int p = 0; p < fft.passes; p++) { if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p)); }
    const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
    auto mag = [&](int k) { return std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]) + 1e-20; };
    int lo = std::max(2, (int)(f * (1 - span) * N / sr)), hi = std::min(N / 2 - 2, (int)(f * (1 + span) * N / sr) + 1), pk = lo;
    for (int k = lo; k <= hi; k++) if (mag(k) > mag(pk)) pk = k;
    double a = std::log(mag(pk - 1)), c = std::log(mag(pk)), d = std::log(mag(pk + 1)), den = a - 2 * c + d;
    return (pk + (den != 0 ? 0.5 * (a - d) / den : 0)) * sr / N;
}
/* the peak of a response to an impulse repeating every 65536 samples: the last period's exact (unwindowed) spectrum,
   scanned then narrowed - a Hann window and a 3-bin fit read a 73 Hz-wide Dry peak 1.7 cents off (the true peak, by a
   0.01 Hz scan, sat on the note) */
static double periodic_peak(const std::vector<float> &x, double sr, double f, double span) {
    const size_t N = 65536, s0 = x.size() - N;
    auto mag = [&](double fq) { double re = 0, im = 0; for (size_t i = 0; i < N; i++) { double ph = 2 * 3.141592653589793 * fq * i / sr; re += x[s0 + i] * std::cos(ph); im += x[s0 + i] * std::sin(ph); } return re * re + im * im; };
    double step = std::fmax(0.25, f * span / 40), bf = f * (1 - span), bm = -1;
    for (double q = f * (1 - span); q <= f * (1 + span); q += step) { double m = mag(q); if (m > bm) { bm = m; bf = q; } }
    double lo = bf - step, hi = bf + step;
    for (int i = 0; i < 40; i++) { double a = lo + (hi - lo) * 0.382, b = lo + (hi - lo) * 0.618; if (mag(a) > mag(b)) hi = b; else lo = a; }
    return 0.5 * (lo + hi);
}
/* a filter's tuning by steady tones: the input frequency near f that comes out loudest, the engine driven directly (no
   envelope, no level matching). For Spectral Dry: a click filtered frame by frame wraps inside each frame and leaves
   notches every bin (measured), so its impulse response says nothing about its tuning; a steady tone does */
static double tone_peak(int synth, int method, int mode, double focus, double colour, double f, double span) {
    auto level = [&](double q) {
        std::vector<float> sine(48000 * 2); for (size_t i = 0; i < sine.size(); i++) sine[i] = (float)(0.5 * std::sin(2 * 3.141592653589793 * q * i / 48000));
        const float *p[1] = { sine.data() };
        sampler::Resonator r; r.init(48000); r.synth = synth; r.method = method; r.mode = mode; r.focus = focus; r.colour = colour;
        r.set_source(1, (long long)sine.size(), p); r.attack(f, 0, 0.5);
        sampler::Voice &x = r.v[0]; double re = 0, im = 0;      /* the output's component at q itself (a lock-in): no edge ripple */
        for (int i = 0; i < 48000; i++) { const double in = r.src.at(x.pos++); const double y = r.resonate(x, in);
            if (i >= 48000 / 4) { const double ph = 2 * 3.141592653589793 * q * i / 48000; re += y * std::cos(ph); im += y * std::sin(ph); } }
        return re * re + im * im;
    };
    /* the top is broad and smooth (0.06 dB over +-4 Hz at 220 Hz, measured): a parabola through f and f +- span f, in dB -
       a step-by-step search wandered on rounding there */
    const double d = span * f, lo = std::log(level(f - d)), mid = std::log(level(f)), hi = std::log(level(f + d)), den = lo - 2 * mid + hi;
    return den < 0 ? f + 0.5 * (lo - hi) / den * d : f;
}
/* a pure line's frequency: the Hann-windowed DFT magnitude of the last 65536 samples maximised over f (1 +- span),
   a scan then a golden section (Pulsar and Freeze at Focus 1 are lines) */
static double line_peak(const std::vector<float> &x, double sr, double f, double span) {
    const size_t N = 65536, s0 = x.size() - N;
    auto mag = [&](double q) { double re = 0, im = 0; for (size_t i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N), ph = 2 * 3.141592653589793 * q * i / sr; re += x[s0 + i] * w * std::cos(ph); im += x[s0 + i] * w * std::sin(ph); } return re * re + im * im; };
    double step = std::fmax(0.1, f * span / 40), bf = f, bm = -1;
    for (double q = f * (1 - span); q <= f * (1 + span); q += step) { double m = mag(q); if (m > bm) { bm = m; bf = q; } }
    double lo = bf - step, hi = bf + step;
    for (int i = 0; i < 40; i++) { double a = lo + (hi - lo) * 0.382, b = lo + (hi - lo) * 0.618; if (mag(a) > mag(b)) hi = b; else lo = a; }
    return 0.5 * (lo + hi);
}
/* the Hann-windowed DFT magnitude at f over all of x (one window) */
static double peak_amp(const std::vector<float> &x, double sr, double f) {
    double re = 0, im = 0; const size_t N = x.size();
    for (size_t i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N), ph = 2 * 3.141592653589793 * f * i / sr; re += x[i] * w * std::cos(ph); im += x[i] * w * std::sin(ph); }
    return std::sqrt(re * re + im * im) / N + 1e-30;
}
static std::vector<float> part_render(int synth, int method, int mode, double focus, double colour, double tune, double f,
                                      double secs, const std::vector<float> &src, double off_at = 1e300) {
    sampler::Resonator r; r.init(48000);
    r.synth = synth; r.method = method; r.mode = mode; r.focus = focus; r.colour = colour; r.tune = tune; r.att = 0.02; r.rel = 0.03;
    const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
    r.attack(f, 0.0, 0.5); r.release(off_at);
    std::vector<float> L((size_t)(secs * 48000), 0.0f), R(L.size(), 0.0f);
    for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, (int)std::min<size_t>(128, L.size() - i), i / 48000.0);
    return L;
}
/* A bowed (noise-driven) resonance's centre: one spectrum of it wanders +-7 cents with the noise (measured
   2026-09-30), so power spectra of 65536-point Hann windows are averaged over the last `secs` seconds
   (half-overlapped), then the power-weighted mean frequency within 0.5% of the peak is taken. */
static double centre_near(const std::vector<float> &x, double sr, double f, double secs) {
    const int N = 65536; FFT fft; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
    std::vector<float> b(4 * N); float *ar = b.data(), *ai = ar + N, *br = ai + N, *bi = br + N;
    std::vector<double> pw(N / 2, 0.0);
    size_t from = x.size() - (size_t)(secs * sr);
    for (size_t s0 = from; s0 + N <= x.size(); s0 += N / 2) {
        for (int i = 0; i < N; i++) { ar[i] = (float)(x[s0 + i] * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N))); ai[i] = 0; }
        for (int p = 0; p < fft.passes; p++) { if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p)); }
        const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
        for (int k = 0; k < N / 2; k++) pw[k] += (double)re[k] * re[k] + (double)im[k] * im[k];
    }
    int lo = (int)(f * 0.96 * N / sr), hi = (int)(f * 1.04 * N / sr) + 1, pk = lo;
    for (int k = lo; k <= hi; k++) if (pw[k] > pw[pk]) pk = k;
    int a = (int)std::floor(pk * 0.995), z = (int)std::ceil(pk * 1.005); if (z - a < 2) { a = pk - 1; z = pk + 1; }
    double num = 0, den = 0; for (int k = a; k <= z; k++) { num += pw[k] * k; den += pw[k]; }
    return num / den * sr / N;
}
/* the decay time: RMS in 20 ms windows, a line through the part 6-26 dB under the peak, extended to -60 dB */
static double t60_of(const std::vector<float> &x) {
    const int W = 960; std::vector<double> db;
    for (size_t i = 0; i + W <= x.size(); i += W) { double e = 0; for (int k = 0; k < W; k++) e += (double)x[i + k] * x[i + k]; db.push_back(10 * std::log10(e / W + 1e-30)); }
    size_t pk = std::max_element(db.begin(), db.end()) - db.begin(); double top = db[pk], sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
    for (size_t i = pk; i < db.size(); i++) if (db[i] <= top - 6 && db[i] >= top - 26) { double t = i * 0.02; sx += t; sy += db[i]; sxx += t * t; sxy += t * db[i]; m++; }
    double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return -60 / slope;
}

/* The resampler as it was before its kernel table (2026-09-29): every tap's sinc and window computed
   per output sample. Kept here as the reference the table must match. */
static void ref_resample(const short *in, long long frames, double from_rate, short *out, double to_rate) {
    const int TAPS = 32; const double PI = 3.141592653589793;
    const double step = from_rate / to_rate, fc = 0.95 * std::min(1.0, to_rate / from_rate);
    const long long n_out = fs_resample_length(frames, from_rate, to_rate);
    auto window = [&](double x) { double t = (x + TAPS) / (2.0 * TAPS); if (t < 0 || t > 1) return 0.0;
        return 0.35875 - 0.48829 * std::cos(2 * PI * t) + 0.14128 * std::cos(4 * PI * t) - 0.01168 * std::cos(6 * PI * t); };
    for (long long j = 0; j < n_out; j++) {
        const double pos = j * step; const long long c = (long long)std::floor(pos); const double frac = pos - c;
        double acc = 0, norm = 0;
        for (int k = -TAPS + 1; k <= TAPS; k++) {
            const double x = k - frac, arg = PI * fc * x, h = fc * (x == 0 ? 1.0 : std::sin(arg) / arg) * window(x);
            norm += h; const long long idx = c + k; if (idx >= 0 && idx < frames) acc += h * in[idx];
        }
        double v = norm != 0 ? acc / norm : 0; v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; out[j] = (short)std::lround(v);
    }
}

int main() {
    const float SR = 48000;
    const int B = 128;

    assert(fs_create("nope") == nullptr);

    /* passthrough is bit-exact */
    fs_device *p = fs_create("passthrough");
    fs_prepare(p, SR, B);
    for (int i = 0; i < B; i++) { fs_in(p, 0)[i] = std::sin(i * 0.1f); fs_in(p, 1)[i] = -0.5f; }
    fs_process(p, B);
    for (int i = 0; i < B; i++) { assert(fs_out(p, 0)[i] == fs_in(p, 0)[i]); assert(fs_out(p, 1)[i] == -0.5f); }
    fs_destroy(p);

    /* sine: 1 s at 440 Hz, level 0.2 -> RMS 0.2/sqrt(2), 880 zero crossings */
    fs_device *s = fs_create("sine");
    fs_prepare(s, SR, B);
    assert(fs_param_count(s) == 2);
    fs_set_param(s, 1, 5.0f);                        /* clamped to 1 */
    fs_set_param(s, 1, 0.2f);
    double sum = 0; int crossings = 0; float prev = 0;
    for (int b = 0; b < SR / B; b++) {
        fs_process(s, B);
        for (int i = 0; i < B; i++) {
            float v = fs_out(s, 0)[i];
            sum += v * v;
            if ((prev < 0) != (v < 0)) crossings++;
            prev = v;
        }
    }
    double rms = std::sqrt(sum / (SR / B * B));
    std::printf("sine rms %.4f (want %.4f), crossings %d (want ~880)\n", rms, 0.2 / std::sqrt(2.0), crossings);
    assert(std::fabs(rms - 0.2 / std::sqrt(2.0)) < 0.002);
    assert(crossings >= 876 && crossings <= 884);

    /* A-2: a level jump 0.2 -> 1.0 must ramp, not step. The largest sample-to-sample change stays
       near the sine's own slope (2*pi*440/48000 ~ 0.058 at full level). */
    fs_set_param(s, 1, 1.0f);
    float maxstep = 0; prev = fs_out(s, 0)[B - 1];
    for (int b = 0; b < 20; b++) {
        fs_process(s, B);
        for (int i = 0; i < B; i++) { float v = fs_out(s, 0)[i]; maxstep = std::fmax(maxstep, std::fabs(v - prev)); prev = v; }
    }
    std::printf("level jump: max step %.4f (limit 0.07)\n", maxstep);
    assert(maxstep < 0.07f);
    fs_destroy(s);

    /* mix: quiet material passes bit-exact, only delayed by the 5 ms lookahead */
    {
        fs_device *a = fs_create("sine"), *ref = fs_create("sine");
        fs_prepare(a, SR, B); fs_prepare(ref, SR, B);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 4);
        assert(fs_mix_add(m, a, SR, 1.0f) == 0);
        const int L = (int)(0.005f * SR);
        std::vector<float> want, got;
        for (int b = 0; b < 40; b++) {
            fs_mix_process(m, B); fs_process(ref, B);
            for (int i = 0; i < B; i++) { got.push_back(fs_mix_out(m, 0)[i]); want.push_back(fs_out(ref, 0)[i]); }
        }
        for (size_t i = L; i < got.size(); i++) assert(got[i] == want[i - L]);
        float gr; long long over; fs_mix_stats(m, &gr, &over);
        assert(gr == 0.0f && over == 0);
        fs_mix_destroy(m); fs_destroy(a); fs_destroy(ref);
    }

    /* mix: two full-level sines (peaks to 2.0) never pass -1 dBFS, and a slot's gain ramps */
    {
        fs_device *a = fs_create("sine"), *b2 = fs_create("sine");
        fs_prepare(a, SR, B); fs_prepare(b2, SR, B);
        fs_set_param(a, 1, 1.0f); fs_set_param(b2, 1, 1.0f); fs_set_param(b2, 0, 443.0f);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 4);
        fs_mix_add(m, a, SR, 1.0f);
        int s2 = fs_mix_add(m, b2, SR, 0.0f);
        fs_mix_set_gain(m, s2, 1.0f);                      /* fade the second in: must ramp */
        float peak = 0, maxstep = 0, prev = 0;
        for (int b = 0; b < 375 * 3; b++) {
            fs_mix_process(m, B);
            for (int i = 0; i < B; i++) {
                float v = fs_mix_out(m, 0)[i];
                peak = std::fmax(peak, std::fabs(v));
                maxstep = std::fmax(maxstep, std::fabs(v - prev)); prev = v;
            }
        }
        float gr; long long over; fs_mix_stats(m, &gr, &over);
        std::printf("mix: 2 sines peak %.4f (ceiling 0.8913), reduction %.1f dB, over %lld, max step %.4f\n", peak, gr, over, maxstep);
        assert(peak <= 0.8913f && over == 0 && gr < -5.0f);
        assert(maxstep < 0.13f);                           /* two sines' own slope ~0.116: no step from the fade or the limiter */
        fs_mix_destroy(m); fs_destroy(a); fs_destroy(b2);
    }

    /* mix: a burst from silence to 2.0 is caught before it arrives */
    {
        fs_device *p2 = fs_create("passthrough");
        fs_prepare(p2, SR, B);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 2);
        fs_mix_add(m, p2, SR, 1.0f);
        unsigned r = 1; float peak = 0;
        for (int b = 0; b < 200; b++) {
            for (int i = 0; i < B; i++) {
                r = r * 1664525u + 1013904223u;
                float v = b < 50 ? 0.0f : 2.0f * ((r >> 9) / 8388608.0f - 1.0f);
                fs_in(p2, 0)[i] = v; fs_in(p2, 1)[i] = -v;
            }
            fs_mix_process(m, B);
            for (int i = 0; i < B; i++) peak = std::fmax(peak, std::fabs(fs_mix_out(m, 0)[i]));
        }
        float gr; long long over; fs_mix_stats(m, &gr, &over);
        std::printf("mix: burst 0 -> 2.0 peak %.4f, reduction %.1f dB, over %lld\n", peak, gr, over);
        assert(peak <= 0.8913f && over == 0);
        fs_mix_destroy(m); fs_destroy(p2);
    }

    /* stretch: a 16-bit source plays bit-identically to the float source holding the same samples */
    {
        const int n = 48000 * 3;
        std::vector<short> q(n); std::vector<float> fq(n);
        unsigned r = 5;
        for (int i = 0; i < n; i++) { r = r * 1664525u + 1013904223u; q[i] = (short)((int)(r >> 16) - 32768); fq[i] = q[i] * (1.0f / 32768.0f); }
        const short *qs[1] = { q.data() }; const float *fs[1] = { fq.data() };
        fs_device *a = fs_create("stretch"), *b = fs_create("stretch");
        fs_prepare(a, SR, B); fs_prepare(b, SR, B);
        fs_set_source_i16(a, 1, n, qs); fs_set_source(b, 1, n, fs);
        fs_set_param(a, 0, 0.3f); fs_set_param(b, 0, 0.3f);
        long diff = 0; double energy = 0;
        for (int k = 0; k < 48000 * 4 / B; k++) {
            fs_process(a, B); fs_process(b, B);
            for (int c = 0; c < 2; c++) for (int i = 0; i < B; i++) { diff += fs_out(a, c)[i] != fs_out(b, c)[i]; energy += fs_out(a, c)[i] * fs_out(a, c)[i]; }
        }
        std::printf("stretch int16 vs float source: %ld differing samples (signal energy %.1f)\n", diff, energy);
        assert(diff == 0 && energy > 1);
        fs_destroy(a); fs_destroy(b);
    }

    /* mix: grit 1 settles on tanh(4 x crush(x, 3 bits)), the web's buildGrit at full amount */
    {
        fs_device *p2 = fs_create("passthrough");
        fs_prepare(p2, SR, B);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 1);
        fs_mix_add(m, p2, SR, 1.0f);
        fs_mix_set_grit(m, 0, 1.0f);
        double err = 0; long n = 0; const int L = (int)(0.005f * SR);   /* the limiter's delay */
        std::vector<float> hist;
        for (int b = 0; b < 400; b++) {
            for (int i = 0; i < B; i++) {
                float v = 0.1f * std::sin(0.01f * (b * B + i));
                fs_in(p2, 0)[i] = fs_in(p2, 1)[i] = v; hist.push_back(v);
            }
            fs_mix_process(m, B);
            if (b < 200) continue;            /* after the 0.2 s glide */
            for (int i = 0; i < B; i++) {
                float x = hist[b * B + i - L], step = 0.25f;               /* 3 bits */
                float want = std::tanh(4 * step * std::floor(x / step + 0.5f));
                err = std::fmax(err, (double)std::fabs(fs_mix_out(m, 0)[i] - want));   /* peak tanh(1) < the ceiling */
                n++;
            }
        }
        std::printf("mix: grit 1 vs tanh(4 crush3(x)): max err %.2g over %ld samples\n", err, n);
        assert(err < 1e-3);
        fs_mix_destroy(m); fs_destroy(p2);
    }

    {   /* the route card's patch: the default progression (16 chords, D dorian), key D, 72 bpm */
        fs_device *pc = fs_create("piece");
        assert(fs_piece_add_route(pc, "{}") == 0);
        char buf[4096];
        assert(fs_piece_route_info(pc, 0, buf, sizeof buf) > 0);
        std::string j = buf;
        int chords = 1; for (size_t k = 0; (k = j.find("],[", k)) != std::string::npos; k++) chords++;
        assert(chords == 16);
        assert(j.find("{\"tempo\":72,\"key\":\"D\",\"key2\":\"G\",\"sections\":7,\"voices\":4,\"prog\":[[") == 0);
        assert(j.find("],\"sectors\":[") != std::string::npos && j.back() == '}');
        assert(fs_piece_route_info(pc, 1, buf, sizeof buf) == 0);
        std::printf("route info: %s\n", j.c_str());
        fs_destroy(pc);
    }
    {   /* play: 1x as made, holds when paused, seeks without a step, stops at the end */
        const int n = 48000;
        std::vector<short> q(n); for (int i = 0; i < n; i++) q[i] = (short)(10000 * std::sin(i * 0.01));
        const short *qs[1] = { q.data() };
        fs_device *pl = fs_create("play");
        fs_prepare(pl, SR, B);
        fs_set_source_i16(pl, 1, n, qs);
        fs_stats_t st;
        fs_process(pl, B); fs_stats(pl, &st); assert(st.frames == 0);          /* not playing: holds */
        fs_set_param(pl, 0, 1);
        float maxstep = 0, prev = 0; double err = 0;
        for (int b = 0; b < 100; b++) {
            fs_process(pl, B);
            for (int i = 0; i < B; i++) {
                float v = fs_out(pl, 0)[i];
                maxstep = std::fmax(maxstep, std::fabs(v - prev)); prev = v;
                if (b > 10) { fs_stats(pl, &st); }
            }
            if (b == 40) fs_set_param(pl, 1, 0.5f);                            /* seek mid-way */
        }
        fs_stats(pl, &st);
        std::printf("play: position %lld, max step %.4f\n", st.frames, maxstep);
        assert(st.frames > n / 2 && st.frames < n / 2 + 60 * B);
        assert(maxstep < 0.02f);                                               /* the sine's own slope ~0.003; a hard jump would be ~0.6 */
        for (int b = 0; b < 400; b++) fs_process(pl, B);                       /* past the end */
        fs_stats(pl, &st); assert(st.frames == n);
        fs_process(pl, B); assert(fs_out(pl, 0)[B - 1] == 0.0f);
        fs_destroy(pl);
    }
    {   /* solo on a rhythm point: the route's synths fall silent, and come back when it is let go */
        fs_device *pc = fs_create("piece");
        fs_prepare(pc, SR, B);
        fs_piece_add_route(pc, "{}");
        int h = fs_piece_rhythm_add(pc, "{}", 0);
        assert(h >= 0);
        auto energy = [&](double secs) {
            double e = 0; int blocks = (int)(secs * SR / B);
            for (int b = 0; b < blocks; b++) { fs_piece_walk(pc, 0, 0.3, 0); fs_process(pc, B); for (int i = 0; i < B; i++) e += fs_out(pc, 0)[i] * fs_out(pc, 0)[i]; }
            return e / (blocks * B);
        };
        energy(6); double open = energy(2);
        fs_piece_solo(pc, h); energy(1); double soloed = energy(1);
        fs_piece_solo(pc, -1); energy(1); double back = energy(2);
        std::printf("piece solo: route %.2e, soloed %.2e, let go %.2e\n", open, soloed, back);
        assert(open > 1e-7 && soloed < open * 1e-6 && back > open * 0.05);
        fs_destroy(pc);
    }
    {   /* the morph cells: the playing route's morphs at the piece's clock, by the web's morphValue */
        fs_device *pc = fs_create("piece");
        fs_prepare(pc, SR, B);
        fs_piece_add_route(pc, "{}");
        fs_piece_add_route(pc, "{\"version\":12,\"prog\":[{\"r\":0,\"q\":\"m7\"}],\"morph\":{\"on\":true,\"cells\":false,\"list\":["
            "{\"id\":\"a\",\"on\":true,\"shape\":\"ramp\",\"period\":10,\"depth\":1,\"bias\":0,\"phase\":0.25,\"dest\":\"v3.cutoff\"},"
            "{\"id\":\"b\",\"on\":false,\"shape\":\"drift\",\"dest\":\"voice.cutoff\"}]}}");
        const double TAU = 6.283185307179586;
        double o[7 * 24], clock = 0; int root = -1, shown = -1;
        assert(fs_piece_morphs(pc, o, 24, &clock, &root, &shown) == -1);        /* nothing playing yet */
        for (int b = 0; b < (int)(3.0 * SR / B); b++) { fs_piece_walk(pc, 0, 0.3, 0); fs_process(pc, B); }
        int n = fs_piece_morphs(pc, o, 24, &clock, &root, &shown);
        double u = clock / 660, tide = 0.08 + 0.85 * (0.5 + (std::sin(TAU * u) + std::sin(TAU * u * 1.6180339887)) / 4);
        std::printf("morph cells: %d, clock %.2f s, root %d, m1 %.4f (web %.4f)\n", n, clock, root, o[4], tide);
        assert(n == 6 && shown == 1 && clock > 2.5 && root >= 0 && root < 12);
        assert(o[0] == 4 && o[1] == 0 && std::fabs(o[4] - tide) < 1e-9);       /* m1: tide on voice.cutoff */
        assert(o[7 * 3 + 1] == 1 && o[7 * 4 + 1] == 1);                        /* m4, m5 drive the second voice */
        for (int i = 0; i < n; i++) assert(o[7 * i + 4] >= 0 && o[7 * i + 4] <= 1);
        for (int b = 0; b < (int)(3.0 * SR / B); b++) { fs_piece_walk(pc, 1, 0.3, 0); fs_process(pc, B); }
        n = fs_piece_morphs(pc, o, 24, &clock, &root, &shown);
        double ramp = clock / 10 + 0.25; ramp -= std::floor(ramp);
        std::printf("morph cells, own patch: %d, shown %d, ramp %.4f (web %.4f)\n", n, shown, o[4], ramp);
        assert(n == 1 && shown == 0 && o[0] == 3 && o[1] == 2 && std::fabs(o[4] - ramp) < 1e-9);
        fs_destroy(pc);
    }
    {   /* the engine's live reads: morphs, chord and route of the piece it drives */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
            "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{}}}]}");
        double o[7 * 24], clock = 0; int root = -1, shown = -1, count = 0; char label[32];
        assert(fs_engine_route(e) == -1 && fs_engine_chord(e, &count, label, 32) == -1);
        for (int b = 0; b < (int)(4.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); }
        int n = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        int ch = fs_engine_chord(e, &count, label, 32);
        std::printf("engine live: route %d, chord %d of %d %s, %d morphs at %.2f s\n", fs_engine_route(e), ch, count, label, n, clock);
        assert(fs_engine_route(e) == 0 && ch >= 0 && count == 16 && label[0] && n == 6 && clock > 3);
        for (int b = 0; b < (int)(2.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.1, 41.1); fs_engine_process(e, B); }
        assert(fs_engine_route(e) == -1 && fs_engine_chord(e, &count, label, 32) == -1);
        assert(fs_engine_morphs(e, o, 24, &clock, &root, &shown) == -1);   /* 8 km away: nothing playing */
        fs_engine_destroy(e);
    }
    {   /* 3c: a route's sampler role, played by the whole engine, reads the role recording it is given; the state
           names the route by id */
        auto run = [&](bool give) {
            fs_engine *e = fs_engine_create(SR, B);
            fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
                "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{\"version\":17,"
                "\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"bed\":{\"on\":false},\"sect\":{\"on\":false},"
                "\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"s-freeze\"}}}}]}");
            std::vector<float> nz = noise_src(20, 0.5f, 7); const float *np[1] = { nz.data() };
            if (give) fs_engine_role_source(e, 0, 1, (long long)nz.size(), np);
            double en = 0; long n = 0;
            for (int b = 0; b < (int)(6.0 * SR / B); b++) {
                if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0);
                fs_engine_process(e, B);
                if (b > (int)(2.0 * SR / B)) { const float *l = fs_engine_out(e, 0); for (int k = 0; k < B; k++) { en += (double)l[k] * l[k]; n++; } }
            }
            std::string st = fs_engine_state(e);
            fs_engine_destroy(e);
            return std::make_pair(10 * std::log10(en / n + 1e-30), st);
        };
        const auto off = run(false), on = run(true);
        std::printf("3c engine role source: %.1f dB without, %.1f dB with | %s\n", off.first, on.first, on.second.c_str());
        assert(on.first > off.first + 20 && on.second.find("\"route_id\":\"r1\"") != std::string::npos);
    }
    {   /* 3c.1 F2: a sampler's note moved by octaves into a band where its recording has energy (pitch class kept); a note
           in a band that has it, and every note of a broadband recording, as played */
        auto played = [&](const std::vector<float> &src, double note) {
            sampler::Resonator r; r.synth = sampler::HARMONIC; r.init(SR);
            const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
            std::vector<float> sp; sampler::spectrum_of(src.data(), (long long)src.size(), SR, sp); r.spec = sp.empty() ? nullptr : sp.data(); r.spec_n = (int)sp.size(); r.spec_sr = SR;
            std::vector<float> L(B), R(B); r.attack(note, 0, 0.5); double f[4] = {}; r.render(L.data(), R.data(), B, 0);
            return r.sounding(f, 4) > 0 ? f[0] : 0.0;
        };
        std::vector<float> bird((size_t)(5 * SR)); for (size_t i = 0; i < bird.size(); i++) bird[i] = (float)(0.4 * std::sin(2 * 3.141592653589793 * 5000 * i / SR));
        std::vector<float> nz = noise_src(5, 0.5f, 3);
        /* on a 5 kHz recording: 156.25 Hz has no overtone (of 8) near 5 kHz; its octaves 625 (8th), 1250 (4th), 2500 (2nd)
           and 5000 (itself) do - the nearest, 625; 1250 keeps its own; on noise every note keeps its own */
        const double up = played(bird, 156.25), keep = played(bird, 1250), wide = played(nz, 110);
        std::printf("3c.1 fold: on a 5 kHz recording 156.25 Hz -> %.2f Hz, 1250 Hz -> %.2f Hz; on noise 110 Hz -> %.2f Hz\n", up, keep, wide);
        assert(std::fabs(up - 625) < 1e-6 && std::fabs(keep - 1250) < 1e-6 && std::fabs(wide - 110) < 1e-6);
    }
    {   /* 3c.1: Freeze's Moment on a call's soft edge freezes the loudest moment within +-85 ms, not the edge (a bird call
           fades in and out; a frozen edge was a whole silent note) */
        std::vector<float> src((size_t)(3 * SR));
        for (size_t i = 0; i < src.size(); i++) { const double t = i / SR; src[i] = (float)((t >= 1.0 && t < 1.3 ? 0.5 : 0.002) * std::sin(2 * 3.141592653589793 * 3000 * t)); }
        sampler::Resonator fz; fz.synth = sampler::FREEZE; fz.init(SR); const float *p[1] = { src.data() }; fz.set_source(1, (long long)src.size(), p);
        fz.colour = (0.94 * SR) / (double)(src.size() - 2048);    /* the Moment 60 ms before the call */
        std::vector<float> L(B), R(B); fz.attack(3000, 0, 0.5); fz.render(L.data(), R.data(), B, 0);
        double pw = 0; for (int i = 0; i < fz.nv; i++) if (fz.v[i].active) pw = std::max(pw, fz.v[i].fpow);
        std::printf("3c.1 freeze: the Moment 60 ms before a call holds power %.4f (the call's own 0.25)\n", pw);
        assert(pw > 0.1);
    }
    {   /* Kerem 2026-10-06: "harmonic filter on voice one doesn't change any attributes when I twist them" - a sampler's
           settings were read only when a note started, and the Voice holds its note for the whole chord. A settings change
           re-voices the sounding notes (a 50 ms crossfade into the same notes with the new settings) */
        auto bright = [](const std::vector<float> &x, size_t a, size_t z) {   /* the share of high frequencies: E[dx] / E[x] */
            double e = 0, d = 0; for (size_t i = a + 1; i < z; i++) { e += (double)x[i] * x[i]; const double q = x[i] - x[i - 1]; d += q * q; } return d / (e + 1e-30); };
        auto run = [&](bool change, int from = 0, int to = 1) {
            std::vector<float> nz = noise_src(20, 0.5f, 5); const float *np[1] = { nz.data() };
            unsigned seed = 77; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128); fs_piece_test_hooks(d, fixed_draw, &seed, nullptr, nullptr);
            fs_piece_role_source(d, 0, 1, (long long)nz.size(), np);
            const std::string pre = "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"morph\":{\"on\":false,\"list\":[]},\"bed\":{\"on\":false},\"sect\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"s-formant\",\"warp\":0,\"drive\":0,\"sampler\":{\"method\":0,\"colour\":";
            const int r = fs_piece_add_route(d, (pre + std::to_string(from) + "}}}").c_str()); fs_piece_walk(d, r, 0.3, 0);
            std::vector<float> o;
            for (int i = 0; i < (int)(6 * SR / 128); i++) {
                if (change && i == (int)(3 * SR / 128)) fs_piece_set_route(d, r, (pre + std::to_string(to) + "}}}").c_str());
                fs_process(d, 128); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + 128); }
            fs_destroy(d);
            return std::make_pair(bright(o, (size_t)(2 * SR), (size_t)(3 * SR)), bright(o, (size_t)(4 * SR), (size_t)(5 * SR)));
        };
        /* the change heard 1-2 s after it, against a note played with Colour 1 from its start: within 1 dB of it */
        const auto same = run(false), turned = run(true), bright1 = run(false, 1);
        const double got = 10 * std::log10(turned.second / same.second), full = 10 * std::log10(bright1.second / same.second);
        std::printf("3c.1 revoice: Formant Colour 0 -> 1 (the 1st partial to the 16th) on a held Voice note: %+.2f dB brighter (a note at Colour 1 from its start: %+.2f dB)\n", got, full);
        assert(full > 1.5 && std::fabs(got - full) < 1);
    }
    {   /* Kerem 2026-10-06: "Second voice should be louder in general for samplers" - each sampler on the Second voice alone
           against the same sampler on the Voice alone (the same recording, gain, no effects): the Second voice >= the Voice */
        auto level = [&](const char *role, const char *synth) {
            std::vector<float> nz = noise_src(20, 0.5f, 13); const float *np[1] = { nz.data() };
            unsigned seed = 21; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128); fs_piece_test_hooks(d, fixed_draw, &seed, nullptr, nullptr);
            fs_piece_role_source(d, std::string(role) == "voice" ? 0 : 1, 1, (long long)nz.size(), np);
            const std::string fx0 = "{\"delayWet\":0,\"revWet\":0}";
            const std::string pt = std::string("{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"morph\":{\"on\":false,\"list\":[]},\"bed\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"fx\":")
                + fx0 + ",\"fx2\":" + fx0 + ",\"voice\":{\"on\":" + (std::string(role) == "voice" ? "true" : "false") + ",\"gain\":0.8,\"warp\":0,\"drive\":0,\"synth\":\"" + synth + "\"},"
                + "\"sect\":{\"on\":" + (std::string(role) == "sect" ? "true" : "false") + ",\"gain\":0.8,\"warp\":0,\"drive\":0,\"synth\":\"" + synth + "\"}}";
            const int r = fs_piece_add_route(d, pt.c_str());
            double e = 0; long n = 0;
            for (int i = 0; i < (int)(30 * SR / 128); i++) {
                if (i % 94 == 0) { fs_piece_walk(d, r, 0.1 + 0.8 * i / (30 * SR / 128), 0); fs_piece_sector(d, (i / 940) % 4); }
                fs_process(d, 128); if (i > (int)(5 * SR / 128)) { const float *l = fs_out(d, 0); for (int k = 0; k < 128; k++) { e += (double)l[k] * l[k]; n++; } } }
            fs_destroy(d); return 10 * std::log10(e / n + 1e-30); };
        double worst = 1e9;
        for (const char *sy : { "s-resonator", "s-harmonic", "s-formant", "s-pulsar", "s-freeze" }) {
            const double v = level("voice", sy), sc = level("sect", sy);
            std::printf("3c.1 sect level: %-12s the Voice %.1f dB, the Second voice %.1f dB (%+.1f)\n", sy, v, sc, sc - v);
            worst = std::min(worst, sc - v);
        }
        assert(worst >= 0);
    }
    {   /* 3d: the pitch sampler's models (method 0 One-shot, 1 Looped, 2 Granular) and Position (Focus) */
        auto tone = [&](double secs, double f, double third, double from) {   /* f with a 3rd harmonic of `third` from `from` s */
            std::vector<float> x((size_t)(secs * SR));
            for (size_t i = 0; i < x.size(); i++) { const double t = i / SR; x[i] = (float)(0.3 * std::sin(2 * sampler::PI * f * t) + 0.15 * std::sin(2 * sampler::PI * 2 * f * t) + (t >= from ? third : 0) * std::sin(2 * sampler::PI * 3 * f * t)); }
            return x; };
        auto play = [&](const std::vector<float> &src, int model, double pos, double note, double secs) {
            sampler::Resonator r; r.init(SR); r.synth = sampler::RETUNE; r.nv = 6;
            const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
            const int nh = (int)(src.size() / SR / 0.02) + 1; std::vector<float> tf((size_t)nh, 220.0f), tc((size_t)nh, 0.95f);
            r.set_track(220, 0.02, tf.data(), tc.data(), nh);
            r.method = model; r.focus = pos; r.colour = 1; r.tune = 1;
            r.attack(note, 0, 0.5);
            std::vector<float> L((size_t)(secs * SR), 0.0f), R(L.size(), 0.0f);
            for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, (int)std::min<size_t>(128, L.size() - i), i / SR);
            return L; };
        auto win = [&](const std::vector<float> &x, double a, double z) { return std::vector<float>(x.begin() + (long)(a * SR), x.begin() + (long)(z * SR)); };
        auto rms = [&](const std::vector<float> &x, double a, double z) { double e = 0; size_t n = 0; for (size_t i = (size_t)(a * SR); i < (size_t)(z * SR) && i < x.size(); i++) { e += (double)x[i] * x[i]; n++; } return 10 * std::log10(e / std::max<size_t>(1, n) + 1e-30); };
        auto steps = [&](const std::vector<float> &x, double a, double z) { double m = 0; for (size_t i = (size_t)(a * SR) + 1; i < (size_t)(z * SR) && i < x.size(); i++) m = std::max(m, (double)std::fabs(x[i] - x[i - 1])); return m; };
        /* Position: the second half of a recording has a 3rd harmonic; at 0.75 the note reads it, at 0 it does not */
        const std::vector<float> halves = tone(2, 220, 0.3, 1.0);
        auto third = [&](const std::vector<float> &o) { const auto w = win(o, 0.05, 0.4); return 20 * std::log10(peak_amp(w, SR, 660) / peak_amp(w, SR, 220)); };
        const double h0 = third(play(halves, 0, 0.0, 220, 0.5)), h75 = third(play(halves, 0, 0.75, 220, 0.5));
        std::printf("3d position: the 3rd harmonic %.1f dB at Position 0, %.1f dB at 0.75\n", h0, h75);
        assert(h75 > h0 + 20);
        /* Looped: a 5 s note on a 1 s recording sounds to the end, on pitch, no join steps beyond the tone's own */
        const std::vector<float> one = tone(1, 220, 0, 9);
        const std::vector<float> lp = play(one, 1, 0.3, 220, 5), os = play(one, 0, 0.3, 220, 5);
        const double lp_late = rms(lp, 4, 5), lp_early = rms(lp, 0.2, 0.6), os_late = rms(os, 4, 5);
        const double cents = 1200 * std::log2(line_peak(win(lp, 3, 4.4), SR, 220, 0.02) / 220);
        std::printf("3d looped: 4-5 s %.1f dB (first 0.2-0.6 s %.1f; One-shot 4-5 s %.1f); %.2f cents; largest step %.4f (steady %.4f)\n",
            lp_late, lp_early, os_late, cents, steps(lp, 1, 5), steps(lp, 0.2, 0.6));
        assert(lp_late > lp_early - 6 && os_late < lp_early - 40 && std::fabs(cents) < 5 && steps(lp, 1, 5) <= 1.3 * steps(lp, 0.2, 0.6));
        /* a 0.3 s recording looped: still sounds, still no clicks (Review Focus 2) */
        const std::vector<float> tiny = tone(0.3, 220, 0, 9), tl = play(tiny, 1, 0.5, 220, 3);
        bool fin = true; for (float v : tl) fin = fin && std::isfinite(v);
        std::printf("3d looped short: 2-3 s %.1f dB, largest step %.4f (steady %.4f)\n", rms(tl, 2, 3), steps(tl, 0.5, 3), steps(tl, 0.05, 0.15));
        assert(fin && rms(tl, 2, 3) > rms(tl, 0.05, 0.15) - 6 && steps(tl, 0.5, 3) <= 1.3 * steps(tl, 0.05, 0.15));
        /* Granular: steady over 5 s (100 ms windows within 3 dB), on pitch, near One-shot's level (Review Focus 5) */
        const std::vector<float> gr = play(one, 2, 0.5, 220, 5);
        double lo = 1e9, hi = -1e9; for (double t = 0.5; t < 4.9; t += 0.1) { const double l = rms(gr, t, t + 0.1); lo = std::min(lo, l); hi = std::max(hi, l); }
        const double gc = 1200 * std::log2(line_peak(win(gr, 2, 3.4), SR, 220, 0.02) / 220);
        std::printf("3d granular: 100 ms windows %.1f .. %.1f dB; %.2f cents; level %.1f vs One-shot %.1f\n", lo, hi, gc, rms(gr, 1, 4), rms(os, 0.2, 0.6));
        assert(hi - lo < 3 && std::fabs(gc) < 10 && std::fabs(rms(gr, 1, 4) - rms(os, 0.2, 0.6)) < 3);
        /* final review 3d C1: a short recording that does not repeat cleanly (233 Hz and a slow ramp) looped - reads stay
           inside the recording: no step beyond the tone's own at the joins */
        auto rough = [&](double secs) { std::vector<float> x((size_t)(secs * SR)); for (size_t i = 0; i < x.size(); i++) { const double t = i / SR; x[i] = (float)(0.3 * std::sin(2 * sampler::PI * 233 * t) + 0.2 * t / secs); } return x; };
        for (double secs : { 0.3, 0.45, 0.52 }) {
            const std::vector<float> rr = rough(secs), lo = play(rr, 1, 0.5, 233, 3);
            std::vector<float> st = play(rough(2.0), 0, 0.0, 233, 0.5);
            std::printf("3d looped %.2f s (not periodic): largest step %.4f (steady %.4f), 2-3 s %.1f dB\n", secs, steps(lo, 0.3, 3), steps(st, 0.05, 0.45), rms(lo, 2, 3));
            assert(steps(lo, 0.3, 3) <= 1.3 * steps(st, 0.05, 0.45) && rms(lo, 2, 3) > -40);
        }
        /* final review 3d I1: Position 1 still sounds (at least 0.4 s of a one-shot note) */
        { const std::vector<float> ten = tone(10, 220, 0, 99), p1 = play(ten, 0, 1.0, 220, 1);
          std::printf("3d position 1: 0-0.4 s %.1f dB\n", rms(p1, 0.0, 0.4));
          assert(rms(p1, 0.0, 0.4) > -30); }
        /* final review 3d I2: Position 0 is the first clear frame, not the clearest (one 0.99 frame at 9.5 s among 0.85s) */
        { const std::vector<float> ten = tone(10, 220, 0, 99);
          sampler::Resonator r; r.init(SR); r.synth = sampler::RETUNE; r.nv = 6; const float *pp[1] = { ten.data() }; r.set_source(1, (long long)ten.size(), pp);
          std::vector<float> tf(500, 220.0f), tc(500, 0.85f); tc[475] = 0.99f; r.set_track(220, 0.02, tf.data(), tc.data(), 500);
          r.method = 0; r.focus = 0; r.colour = 1; r.tune = 1; r.attack(220, 0, 0.5);
          std::vector<float> L((size_t)(2 * SR), 0.0f), R(L.size(), 0.0f);
          for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, 128, i / SR);
          std::printf("3d position 0 with the clearest frame at 9.5 s: 1-2 s %.1f dB (a 10 s recording read from its start sounds the whole 2 s)\n", rms(L, 1, 2));
          assert(rms(L, 1, 2) > -30); }
        /* budget: 24 voices of each model */
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        for (int model : { 1, 2 }) {
            sampler::Resonator r; r.init(48000); r.synth = sampler::RETUNE; const float *p[1] = { one.data() }; r.set_source(1, (long long)one.size(), p);
            std::vector<float> tf(60, 220.0f), tc(60, 0.95f); r.set_track(220, 0.02, tf.data(), tc.data(), 60); r.method = model; r.focus = 0.4;
            for (int k = 0; k < sampler::Resonator::VOICES; k++) r.attack(110 * std::pow(2.0, k / 12.0), 0, 0.05);
            std::vector<double> ms; std::vector<float> b1(128), b2(128);
            for (int k = 0; k < (int)(4 * 48000 / 128); k++) { std::fill(b1.begin(), b1.end(), 0.0f); auto c0 = std::chrono::steady_clock::now(); r.render(b1.data(), b2.data(), 128, k * 128 / 48000.0);
                if (k > 100) ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count()); }
            std::sort(ms.begin(), ms.end());
            std::printf("3d budget: 24 %s voices, 99.9%% of blocks within %.3f ms (budget %.2f)\n", model == 1 ? "Looped" : "Granular", ms[(size_t)(ms.size() * 0.999)], 1.33 * slack);
            assert(ms[(size_t)(ms.size() * 0.999)] < 1.33 * slack);
        }
    }
    {   /* 4: Sample FM and Sample AM/ring - the pitch sampler as carrier, a cycle of the recording as modulator */
        std::vector<float> sine((size_t)(2 * SR)); for (size_t i = 0; i < sine.size(); i++) sine[i] = (float)(0.3 * std::sin(2 * sampler::PI * 220 * i / SR));
        auto play = [&](const std::vector<float> &src, int synth, double focus, double colour, bool track) {
            sampler::Resonator r; r.init(SR); r.synth = synth; r.nv = 6; r.method = 0;
            const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
            std::vector<float> tf(100, 220.0f), tc(100, 0.95f); if (track) r.set_track(220, 0.02, tf.data(), tc.data(), 100);
            r.focus = focus; r.colour = colour; r.tune = 1;
            r.attack(220, 0, 0.5);
            std::vector<float> L((size_t)(1.0 * SR), 0.0f), R(L.size(), 0.0f);
            for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, 128, i / SR);
            return L; };
        auto w = [&](const std::vector<float> &x) { return std::vector<float>(x.begin() + (long)(0.3 * SR), x.begin() + (long)(0.9 * SR)); };
        auto rel = [&](const std::vector<float> &x, double f, double ref) { return 20 * std::log10(peak_amp(w(x), SR, f) / ref + 1e-30); };
        /* FM at depth 0 is Retune at Position 0, sample for sample */
        const std::vector<float> rt = play(sine, sampler::RETUNE, 0, 1, true), fm0 = play(sine, sampler::SFM, 0.5, 0, true);
        double diff = 0; for (size_t i = 0; i < rt.size(); i++) diff = std::max(diff, (double)std::fabs(rt[i] - fm0[i]));
        const double c220 = peak_amp(w(rt), SR, 220);
        const std::vector<float> fm5 = play(sine, sampler::SFM, 0.5, 0.5, true), am5 = play(sine, sampler::SAM, 0.5, 0.5, true), am1 = play(sine, sampler::SAM, 0.5, 1, true);
        std::printf("4: FM depth 0 vs Retune max diff %.2g; FM depth .5 440 Hz %.1f dB (depth 0 %.1f); AM .5 440 %.1f dB; ring 220 %.1f dB\n",
            diff, rel(fm5, 440, c220), rel(fm0, 440, c220), rel(am5, 440, c220), rel(am1, 220, c220));
        assert(diff < 1e-5 && rel(fm5, 440, c220) > -25 && rel(fm0, 440, c220) < -60 && rel(am5, 440, c220) > -13 && rel(am5, 440, c220) < 0 && rel(am1, 220, c220) < -30);
        /* final review 4 C1: Sample AM carries no DC (a modulator cut from the carrier's own cycle correlates with it) */
        { double m = 0, e = 0; for (size_t i = (size_t)(0.3 * SR); i < am1.size(); i++) { m += am1[i]; e += (double)am1[i] * am1[i]; }
          const size_t n = am1.size() - (size_t)(0.3 * SR); const double dc = std::fabs(m / n) / std::sqrt(e / n + 1e-30);
          double m5 = 0, e5 = 0; for (size_t i = (size_t)(0.3 * SR); i < am5.size(); i++) { m5 += am5[i]; e5 += (double)am5[i] * am5[i]; }
          const double dc5 = std::fabs(m5 / n) / std::sqrt(e5 / n + 1e-30);
          std::printf("4: AM DC/RMS ring %.3f, depth 1/2 %.3f\n", dc, dc5);
          assert(dc < 0.05 && dc5 < 0.05); }
        /* final review 4 I2: Sample FM in Granular bends its grains (a sideband, as One-shot) */
        { auto play2 = [&](int model, double colour) {
              sampler::Resonator r; r.init(SR); r.synth = sampler::SFM; r.nv = 6; r.method = model;
              const float *p[1] = { sine.data() }; r.set_source(1, (long long)sine.size(), p);
              std::vector<float> tf(100, 220.0f), tc(100, 0.95f); r.set_track(220, 0.02, tf.data(), tc.data(), 100);
              r.focus = 0.5; r.colour = colour; r.tune = 1; r.attack(220, 0, 0.5);
              std::vector<float> L((size_t)SR, 0.0f), R(L.size(), 0.0f);
              for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, 128, i / SR);
              return L; };
          const std::vector<float> g0 = play2(2, 0), g5 = play2(2, 0.5);
          const double ref = peak_amp(w(g0), SR, 220);
          std::printf("4: Granular FM 440 Hz depth 1/2 %.1f dB (depth 0 %.1f)\n", rel(g5, 440, ref), rel(g0, 440, ref));
          assert(rel(g5, 440, ref) > -25); }
        /* final review 4 I3: Looped FM on a low recording (60 Hz, depth 1) keeps its reads inside - the loop starts past the bend */
        { std::vector<float> low((size_t)(2 * SR)); for (size_t i = 0; i < low.size(); i++) low[i] = (float)(0.3 * std::sin(2 * sampler::PI * 60 * i / SR));
          sampler::Resonator r; r.init(SR); r.synth = sampler::SFM; r.nv = 6; r.method = 1;
          const float *p[1] = { low.data() }; r.set_source(1, (long long)low.size(), p);
          std::vector<float> tf(100, 60.0f), tc(100, 0.95f); r.set_track(60, 0.02, tf.data(), tc.data(), 100);
          r.focus = 0.5; r.colour = 1; r.tune = 1; r.attack(60, 0, 0.5);
          std::vector<float> L(128), R(128); r.render(L.data(), R.data(), 128, 0);
          double lo = 1e18; for (int i = 0; i < r.nv; i++) if (r.v[i].active) lo = std::min(lo, r.v[i].rls - 2 * r.v[i].mdev - r.v[i].rxf);
          std::printf("4: Looped FM on 60 Hz: the loop's lowest read %.0f\n", lo);
          assert(lo >= 1); }
        /* final review 4 I6: a pitched recording with no clear frame (no track) gets the sine modulator, not its first noise */
        { std::vector<float> nn((size_t)(2 * SR)); unsigned rs = 3;
          for (size_t i = 0; i < nn.size(); i++) { rs = rs * 1664525u + 1013904223u; nn[i] = i < (size_t)(0.5 * SR) ? (float)(0.003 * ((rs >> 8) / 16777216.0 - 0.5)) : (float)(0.3 * std::sin(2 * sampler::PI * 220 * i / SR)); }
          sampler::Resonator r; r.init(SR); r.synth = sampler::SAM; const float *p[1] = { nn.data() }; r.set_source(1, (long long)nn.size(), p);
          r.set_track(220, 0.02, nullptr, nullptr, 0); r.focus = 0.5; r.colour = 0.5; r.attack(220, 0, 0.5);
          double c = 0, a2 = 0; for (int k = 0; k < 256; k++) { const double sn = std::sin(2 * sampler::PI * k / 256); c += r.mcyc[(size_t)k] * sn; a2 += (double)r.mcyc[(size_t)k] * r.mcyc[(size_t)k]; }
          const double corr = c / std::sqrt(a2 * 128 + 1e-30);
          std::printf("4: no clear frame - the modulator's correlation with a sine %.2f\n", corr);
          assert(corr > 0.95); }
        /* an unpitched recording: a sine modulator, finite */
        { const std::vector<float> nz = noise_src(2, 0.3f, 4), o = play(nz, sampler::SFM, 0.5, 0.8, false);
          bool fin = true; double e = 0; for (float v : o) { fin = fin && std::isfinite(v); e += (double)v * v; }
          std::printf("4: unpitched FM finite %d, level %.1f dB\n", (int)fin, 10 * std::log10(e / o.size() + 1e-30));
          assert(fin && e > 0); }
        /* budget: 24 voices of Sample FM (Looped) */
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        { sampler::Resonator r; r.init(48000); r.synth = sampler::SFM; const float *p[1] = { sine.data() }; r.set_source(1, (long long)sine.size(), p);
          std::vector<float> tf(100, 220.0f), tc(100, 0.95f); r.set_track(220, 0.02, tf.data(), tc.data(), 100); r.method = 1; r.focus = 0.6; r.colour = 0.7;
          for (int k = 0; k < sampler::Resonator::VOICES; k++) r.attack(110 * std::pow(2.0, k / 12.0), 0, 0.05);
          std::vector<double> ms; std::vector<float> b1(128), b2(128);
          for (int k = 0; k < (int)(3 * 48000 / 128); k++) { std::fill(b1.begin(), b1.end(), 0.0f); auto c0 = std::chrono::steady_clock::now(); r.render(b1.data(), b2.data(), 128, k * 128 / 48000.0);
              if (k > 100) ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count()); }
          std::sort(ms.begin(), ms.end());
          std::printf("4 budget: 24 Sample FM voices, 99.9%% of blocks within %.3f ms (budget %.2f)\n", ms[(size_t)(ms.size() * 0.999)], 1.33 * slack);
          assert(ms[(size_t)(ms.size() * 0.999)] < 1.33 * slack); }
    }
    {   /* 5: points follow the chord - a pitched point recording moved toward the nearest chord note by `follow` */
        const double dm[3] = { 293.66, 349.23, 440.0 };                     /* D minor */
        const double r1 = harmony::follow_rate(1.0, 330, dm, 3, 1), r0 = harmony::follow_rate(1.0, 330, dm, 3, 0), rh = harmony::follow_rate(1.0, 330, dm, 3, 0.5);
        std::printf("5 follow_rate: 330 Hz on D minor -> %.2f Hz at 1, %.2f at 0, %.2f at 1/2\n", 330 * r1, 330 * r0, 330 * rh);
        assert(std::fabs(330 * r1 - 349.23) < 0.01 && r0 == 1.0 && std::fabs(1200 * std::log2(rh) - 0.5 * 1200 * std::log2(349.23 / 330)) < 0.01);
        assert(harmony::follow_rate(1.5, 0, dm, 3, 1) == 1.5 && harmony::follow_rate(1.5, 330, dm, 0, 1) == 1.5);   /* unpitched, no chord */
        /* the stretch: a 330 Hz recording with its track, chord D minor (MIDI 62 65 69), follow 1 -> its peak near 349 Hz */
        auto stretch_peak = [&](double follow) {
            std::vector<float> rec((size_t)(6 * SR)); for (size_t i = 0; i < rec.size(); i++) rec[i] = (float)(0.3 * std::sin(2 * sampler::PI * 330 * i / SR));
            fs_device *d = fs_create("stretch"); fs_prepare(d, SR, B);
            const float *pp[1] = { rec.data() }; fs_set_source(d, 1, (int)rec.size(), pp);
            fs_set_param(d, 0, 0); fs_set_param(d, 1, 0.25f);                  /* x1, a 0.25 s window */
            fs_stretch_shape(d, "{\"glide\":0.5}");
            const float chord[6] = { 62, 65, 69, -1, -1, 62 };
            for (int k = 0; k < 6; k++) fs_set_param(d, 18 + k, chord[k]);
            std::vector<float> tf(300, 330.0f), tc(300, 0.95f); fs_stretch_track(d, tf.data(), tc.data(), 300, 0.02, (float)follow);
            std::vector<float> o;
            for (int b2 = 0; b2 < (int)(5 * SR / B); b2++) { fs_process(d, B); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + B); }
            fs_destroy(d);
            std::vector<float> w(o.end() - (long)(1.5 * SR), o.end());
            return std::make_pair(peak_amp(w, SR, 349.23), peak_amp(w, SR, 330)); };
        const auto f1 = stretch_peak(1), f0 = stretch_peak(0);
        /* final review 5: (I1) a new recording clears the old one's track; (I2) before any chord, no follow; (I4) with a
           transpose the followed pitch is the transposed one (330 Hz +7 -> 494 Hz -> A 440, not 523) */
        auto stretch2 = [&](double transpose, bool chord, bool second, double want, double other, double hz = 330) {
            std::vector<float> rec((size_t)(6 * SR)); for (size_t i = 0; i < rec.size(); i++) rec[i] = (float)(0.3 * std::sin(2 * sampler::PI * hz * i / SR));
            fs_device *d = fs_create("stretch"); fs_prepare(d, SR, B);
            const float *pp[1] = { rec.data() }; fs_set_source(d, 1, (int)rec.size(), pp);
            fs_set_param(d, 0, 0); fs_set_param(d, 1, 0.25f);
            const std::string shape = "{\"glide\":0.5,\"transpose\":" + std::to_string(transpose) + "}";
            fs_stretch_shape(d, shape.c_str());
            if (chord) { const float c6[6] = { 62, 65, 69, -1, -1, 62 }; for (int k = 0; k < 6; k++) fs_set_param(d, 18 + k, c6[k]); }
            std::vector<float> tf(300, (float)hz), tc(300, 0.95f); fs_stretch_track(d, tf.data(), tc.data(), 300, 0.02, 1.0f);
            std::vector<float> rec2;
            if (second) { rec2 = rec; const float *p2[1] = { rec2.data() }; fs_set_source(d, 1, (int)rec2.size(), p2); }   /* a new recording, no track yet */
            std::vector<float> o;
            for (int b2 = 0; b2 < (int)(5 * SR / B); b2++) { fs_process(d, B); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + B); }
            fs_destroy(d);
            std::vector<float> w(o.end() - (long)(1.5 * SR), o.end());
            return std::make_pair(peak_amp(w, SR, want), peak_amp(w, SR, other)); };
        /* no chord: an E-flat (311 Hz) - the stretch's default D minor 9 would pull it to D or E */
        const auto nochord = stretch2(0, false, false, 311.13, 329.63, 311.13), fresh = stretch2(0, true, true, 330, 349.23), tr = stretch2(7, true, false, 440, 523.25);
        std::printf("5 stretch: no chord 311 %.3g vs 330 %.3g; a new recording 330 %.3g vs 349 %.3g; transpose +7 440 %.3g vs 523 %.3g\n",
            nochord.first, nochord.second, fresh.first, fresh.second, tr.first, tr.second);
        assert(nochord.first > 10 * nochord.second && fresh.first > 10 * fresh.second && tr.first > 10 * tr.second);
        std::printf("5 stretch: follow 1 349 Hz %.3g vs 330 Hz %.3g; follow 0 349 %.3g vs 330 %.3g\n", f1.first, f1.second, f0.first, f0.second);
        assert(f1.first > 10 * f1.second && f0.second > 10 * f0.first);
        /* hits (an E-flat, 311 Hz - not in the route's D minor 9): a pitched hit's logged rate lands on a chord note the route plays (within 2 cents, any octave) at follow 1 */
        auto hits = [&](const char *json) {
            struct L2 { std::vector<double> chord, hit; } log;
            auto lg = [](void *p, int role, double f, double, double, double) { L2 *l = (L2 *)p; if (role <= 3) l->chord.push_back(f); else if (role == 10) l->hit.push_back(f); };
            unsigned seed = 5; fs_device *pc = fs_create("piece"); fs_prepare(pc, SR, B); fs_piece_test_hooks(pc, fixed_draw, &seed, lg, &log);
            fs_piece_add_route(pc, "{}");
            const int h = fs_piece_rhythm_add(pc, json, 0);
            const int n = (int)(0.4 * SR); short *pcm = (short *)std::malloc(sizeof(short) * n);
            for (int i = 0; i < n; i++) pcm[i] = (short)(8000 * std::sin(2 * sampler::PI * 311 * i / SR) * std::exp(-i / (0.1 * SR)));
            fs_piece_rhythm_source(pc, h, 0, 1, n, pcm);
            fs_piece_rhythm_track(pc, h, 0, "{\"f0\":311,\"hop_s\":0.02,\"track\":[[311,0.95,0],[311,0.95,0]]}");
            for (int b2 = 0; b2 < (int)(12 * SR / B); b2++) { fs_piece_walk(pc, 0, 0.3, 0); fs_process(pc, B); }
            fs_destroy(pc);
            double worst = 0; int n_hit = 0;
            for (double r : log.hit) { const double f = 311 * r; double best = 1e9;
                for (double c : log.chord) { const double k = std::round(std::log2(f / c)); best = std::min(best, std::fabs(1200 * std::log2(f / (c * std::pow(2.0, k))))); }
                worst = std::max(worst, best); n_hit++; }
            return std::make_pair(n_hit, worst); };
        const auto on = hits("{\"follow\":1}"), off = hits("{}");
        /* final review 5 I5: grains of a recording judged unpitched (f0 0) do not follow, even on confident frames */
        { struct G2 { std::vector<double> r; } gl;
          auto lg = [](void *p, int role, double f, double, double, double) { if (role == 20) ((G2 *)p)->r.push_back(f); };
          auto grains = [&](const char *trk) {
              gl.r.clear(); unsigned seed = 9; fs_device *pc = fs_create("piece"); fs_prepare(pc, SR, B); fs_piece_test_hooks(pc, fixed_draw, &seed, lg, &gl);
              fs_piece_add_route(pc, "{}");
              const int h = fs_piece_rhythm_add(pc, "{\"follow\":1}", 1);
              const int n = (int)(2 * SR); short *pcm = (short *)std::malloc(sizeof(short) * n);
              for (int i = 0; i < n; i++) pcm[i] = (short)(8000 * std::sin(2 * sampler::PI * 311 * i / SR));
              fs_piece_rhythm_source(pc, h, 0, 1, n, pcm);
              fs_piece_rhythm_track(pc, h, 0, trk);
              for (int b2 = 0; b2 < (int)(8 * SR / B); b2++) { fs_piece_walk(pc, 0, 0.3, 0); fs_process(pc, B); }
              fs_destroy(pc);
              return gl.r; };
          std::string tr = "["; for (int k = 0; k < 120; k++) tr += std::string(k ? "," : "") + "[311,0.95,0]"; tr += "]";
          const auto pitched = grains(("{\"f0\":311,\"hop_s\":0.02,\"track\":" + tr + "}").c_str()), unp = grains(("{\"f0\":0,\"hop_s\":0.02,\"track\":" + tr + "}").c_str());
          /* grains scatter their own pitch, so: against the same grains with no track at all */
          const auto none = grains("{}");
          auto moved = [&](const std::vector<double> &r) { int m = 0; for (size_t i = 0; i < r.size() && i < none.size(); i++) if (std::fabs(1200 * std::log2(r[i] / none[i])) > 1) m++; return m; };
          std::printf("5 grains: against no track, pitched %d of %zu grains moved; judged unpitched %d of %zu\n", moved(pitched), pitched.size(), moved(unp), unp.size());
          assert(pitched.size() > 3 && unp.size() == none.size() && moved(pitched) > 0 && moved(unp) == 0); }
        std::printf("5 hits: follow 1 %d hits, worst %.2f cents off a chord note; follow 0 %d hits, worst %.2f\n", on.first, on.second, off.first, off.second);
        assert(on.first > 3 && on.second < 2 && off.second > 5);
    }
    {   /* 3c.1 F4: each role's level and sounding notes, from the whole engine */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
            "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{\"version\":17,"
            "\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"bed\":{\"on\":false},\"sect\":{\"on\":false},"
            "\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"s-freeze\"}}}}]}");
        std::vector<float> nz = noise_src(20, 0.5f, 7); const float *np[1] = { nz.data() };
        fs_engine_role_source(e, 0, 1, (long long)nz.size(), np);
        for (int b = 0; b < (int)(5.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); }
        float out[3 * (2 + 6)] = {};
        const int w = fs_engine_roles(e, out, 6);
        std::printf("3c.1 roles: voice %.1f dB %d notes (%.1f Hz), sect %.1f dB, v3 %.1f dB\n", out[0], (int)out[1], out[2], out[8], out[16]);
        assert(w == 24 && out[0] > -60 && out[1] >= 1 && out[2] > 20 && out[8] <= -100 && out[16] <= -100);
        fs_engine_destroy(e);
        /* the level is what the role puts out, after its gain: muted, it reads silent (its notes still play) */
        fs_engine *m = fs_engine_create(SR, B);
        fs_engine_features(m, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
            "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{\"version\":17,"
            "\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"bed\":{\"on\":false},\"sect\":{\"on\":false},"
            "\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"s-freeze\",\"gain\":0}}}}]}");
        fs_engine_role_source(m, 0, 1, (long long)nz.size(), np);
        for (int b = 0; b < (int)(5.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(m, 29.001, 41.0); fs_engine_process(m, B); }
        fs_engine_roles(m, out, 6);
        std::printf("3c.1 roles: the voice muted %.1f dB, %d notes\n", out[0], (int)out[1]);
        assert(out[0] <= -90 && out[1] >= 1);
        fs_engine_destroy(m);
    }
    {   /* Listen (fs_engine_solo): one point alone at its full level from any distance */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.02,41.0]},"
            "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.03,41.0]},"
            "\"properties\":{\"id\":\"g1\",\"kind\":\"point\",\"name\":\"G\",\"has_audio\":true,\"audio_mode\":\"grains\",\"storage_path\":\"g1.webm\"}}]}");
        std::string need = fs_engine_step(e, 29.0, 41.0), st = fs_engine_state(e);
        assert(need.empty() && st.find("\"s1\"") == std::string::npos);            /* 1.7 km: out of reach */
        fs_engine_solo(e, "s1");
        need = fs_engine_step(e, 29.0, 41.0); st = fs_engine_state(e);
        std::printf("solo s1: %s | %s\n", need.c_str(), st.c_str());
        assert(need.find(" s1 s1.webm") != std::string::npos && st.find("\"id\":\"s1\",\"name\":\"S\",\"level\":1.0000") != std::string::npos);
        fs_engine_solo(e, "g1");
        need = fs_engine_step(e, 29.0, 41.0); st = fs_engine_state(e);
        std::printf("solo g1: %s | %s\n", need.c_str(), st.c_str());
        assert(need.find("R ") == 0 && need.find(" g1 g1.webm") != std::string::npos);
        assert(st.find("{\"id\":\"g1\",\"level\":1.0000") != std::string::npos && st.find("\"rows\":[]") != std::string::npos);
        fs_engine_solo(e, "");
        fs_engine_step(e, 29.0, 41.0); st = fs_engine_state(e);
        assert(st.find("\"rows\":[]") != std::string::npos && st.find("\"beats\":[]") != std::string::npos);
        fs_engine_destroy(e);
    }
    {   /* a setter's unpublished point: its recording is on this device, not yet on the server (no storage_path).
           It must still be asked for - it sat on "Preparing" forever (Kerem, 2026-09-27). */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.0001,41.0]},"
            "\"properties\":{\"id\":\"n1\",\"kind\":\"point\",\"name\":\"N\",\"has_audio\":true}}]}");
        std::string need = fs_engine_step(e, 29.0, 41.0);
        std::printf("unpublished: %s\n", need.c_str());
        assert(need.find(" n1") != std::string::npos);
        fs_engine_destroy(e);
    }
    {   /* a setter's draft rhythm and grains points: recordings on this device only (no storage_path).
           They were never asked for, so they stayed silent (Kerem, 2026-09-29). */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.0001,41.0]},"
            "\"properties\":{\"id\":\"d1\",\"kind\":\"point\",\"audio_mode\":\"hits\",\"hits\":{\"low\":{\"name\":\"k.wav\"},\"mid\":null}}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.0002,41.0]},"
            "\"properties\":{\"id\":\"g1\",\"kind\":\"point\",\"audio_mode\":\"grains\",\"has_audio\":true}}]}");
        std::string need = fs_engine_step(e, 29.0, 41.0);
        std::printf("draft beats: %s\n", need.c_str());
        assert(need.find(" 0 d1 ") != std::string::npos && need.find(" 1 d1 ") == std::string::npos);
        assert(need.find(" g1 ") != std::string::npos);
        fs_engine_destroy(e);
    }
    {   /* live edits (fs_engine_upsert / fs_engine_remove): a setter's change reaches the sound without a reload */
        fs_engine *e = fs_engine_create(SR, B);
        const char *route = "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
                            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":%s}}";
        char buf[1024];
        std::snprintf(buf, sizeof buf, route, "{}");
        fs_engine_upsert(e, buf);
        fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.02,41.0]},"
            "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\",\"sound\":{\"radius\":140}}}");
        double o[7 * 24], clock = 0; int root = 0, shown = 0;
        auto walk = [&](double secs) { std::string need; for (int b = 0; b < (int)(secs * SR / B); b++) { if (b % 40 == 0) need += fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); } return need; };
        std::string need = walk(3);
        int n0 = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        assert(n0 == 6 && need.find(" s1 ") == std::string::npos);                 /* default morphs; s1 1.7 km away */
        std::snprintf(buf, sizeof buf, route, "{\"version\":12,\"prog\":[{\"r\":0,\"q\":\"m7\"}],\"morph\":{\"on\":false,\"list\":[]}}");
        fs_engine_upsert(e, buf);                                                   /* the setter switches Morphs off */
        walk(0.5);
        int n1 = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.02,41.0]},"
            "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\",\"sound\":{\"radius\":3000}}}");
        need = walk(0.5);                                                           /* ...and gives s1 a 3 km reach */
        std::printf("live edits: morphs %d -> %d, s1 asked for after its radius grew: %s\n", n0, n1, need.find(" s1 s1.webm") != std::string::npos ? "yes" : "no");
        assert(n1 == 0 && need.find(" s1 s1.webm") != std::string::npos);
        fs_engine_remove(e, "s1"); walk(0.5);
        assert(std::string(fs_engine_state(e)).find("\"s1\"") == std::string::npos);  /* deleted: gone from what plays */
        fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.1,41.1],[29.102,41.1]]},"
            "\"properties\":{\"id\":\"r2\",\"kind\":\"route\",\"name\":\"R2\",\"patch\":{}}}");
        fs_engine_remove(e, "r1"); walk(0.5);
        assert(fs_engine_route(e) == -1);                                           /* r1 deleted: standing on its line plays nothing */
        for (int b = 0; b < (int)(2.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.101, 41.1); fs_engine_process(e, B); }
        assert(fs_engine_route(e) == 1);                                            /* r2, added live, plays (piece index 1) */
        fs_engine_destroy(e);
    }
    {   /* Fieldscape's own stretch shaping (docs/superpowers/specs/2026-09-27-own-stretch-shaping-design.md) */
        auto pidx = [](fs_device *d, const char *id) {
            for (int i = 0; i < fs_param_count(d); i++) if (!std::strcmp(fs_param_info(d, i)->id, id)) return i;
            return -1;
        };
        const double TAU = 6.283185307179586;
        /* Goertzel power at f over x */
        auto power = [&](const std::vector<float> &x, double f) {
            double w = TAU * f / SR, c = 2 * std::cos(w), s1 = 0, s2 = 0;
            for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
            return (s1 * s1 + s2 * s2 - c * s1 * s2) / (double)x.size();
        };
        auto rmsdb = [](const std::vector<float> &x) { double s = 0; for (float v : x) s += (double)v * v; return 10 * std::log10(s / x.size() + 1e-20); };
        /* play a device for `secs`, keep the left channel of the last `keep` seconds */
        auto run = [&](fs_device *d, double secs, double keep, std::vector<float> *all = nullptr) {
            std::vector<float> o; long total = (long)(secs * SR / B), from = total - (long)(keep * SR / B);
            for (long k = 0; k < total; k++) {
                fs_process(d, B);
                if (all) all->insert(all->end(), fs_out(d, 0), fs_out(d, 0) + B);
                if (k >= from) o.insert(o.end(), fs_out(d, 0), fs_out(d, 0) + B);
            }
            return o;
        };
        auto make = [&](const std::vector<float> &src) {
            fs_device *d = fs_create("stretch"); fs_prepare(d, SR, B);
            const float *s[1] = { src.data() }; fs_set_source(d, 1, (int)src.size(), s);
            return d;
        };
        const int n = 48000 * 3;

        /* 1: every new control at zero - the stretch exactly as it was (checksum captured before the stage existed) */
        {
            std::vector<float> l(n), r(n); unsigned s = 11;
            for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; l[i] = 0.3f * std::sin(0.0576f * i) + ((int)(s >> 16) - 32768) / 327680.0f; r[i] = l[(i * 7) % n]; }
            const float *src[2] = { l.data(), r.data() };
            uint64_t h = 1469598103934665603ULL;
            for (float stretch : { 0.0f, 0.4f }) {
                fs_device *d = fs_create("stretch"); fs_prepare(d, SR, B); fs_set_source(d, 2, n, src);
                fs_set_param(d, 0, stretch); fs_set_param(d, 3, stretch > 0 ? 0.3f : 0.0f);
                for (int k = 0; k < 48000 * 5 / B; k++) { fs_process(d, B); for (int c = 0; c < 2; c++) { const float *o = fs_out(d, c); for (int i = 0; i < B; i++) { uint32_t u; std::memcpy(&u, &o[i], 4); h = (h ^ u) * 1099511628211ULL; } } }
                fs_destroy(d);
            }
            std::printf("shape 1: all zero, checksum %llu (the stretch before the stage: 9279134860882250983)\n", (unsigned long long)h);
            assert(h == 9279134860882250983ULL);
        }
        std::vector<float> sine(n), noise(n);
        { unsigned s = 3; double b0 = 0, b1 = 0, b2 = 0;
          for (int i = 0; i < n; i++) {
              sine[i] = 0.3f * (float)std::sin(TAU * 440 * i / SR);
              s = s * 1664525u + 1013904223u; double w = ((int)(s >> 16) - 32768) / 32768.0;
              b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
              noise[i] = (float)(0.1 * (b0 + b1 + b2 + w * 0.1848));             /* pink (Kellet) */
          } }

        /* 2: transpose +12 moves 440 Hz to 880 Hz */
        {
            fs_device *d = make(sine);
            assert(pidx(d, "transpose") >= 0);
            fs_set_param(d, pidx(d, "transpose"), 12);
            std::vector<float> o = run(d, 4, 2);
            double p440 = power(o, 440), p880 = power(o, 880);
            std::printf("shape 2: transpose +12: 880 Hz %.2e vs 440 Hz %.2e\n", p880, p440);
            assert(p880 > 20 * p440);
            fs_destroy(d);
        }

        /* the chord's partials across the octaves, and the points half a semitone off them */
        auto on_off = [&](const std::vector<float> &o, std::initializer_list<int> pcs) {
            double on = 0, off = 0;
            for (int pc : pcs) for (int oct = 3; oct <= 6; oct++) {
                double f = 440 * std::pow(2.0, (pc + 12 * (oct + 1) - 69) / 12.0);
                on += power(o, f); off += power(o, f * std::pow(2.0, 0.5 / 12));
            }
            return on / (off + 1e-30);
        };
        auto set_chord = [&](fs_device *d, std::initializer_list<int> midi) {
            int i = 0; for (int m : midi) fs_set_param(d, pidx(d, (std::string("chord") + char('0' + i++)).c_str()), (float)m);
            for (; i < 5; i++) fs_set_param(d, pidx(d, (std::string("chord") + char('0' + i)).c_str()), -1);
            fs_set_param(d, pidx(d, "root"), (float)*midi.begin());
        };

        /* 3: tune pulls pink noise onto the chord (D F A) */
        double dry_ratio, wet_ratio, dry_db, wet_db;
        {
            fs_device *d = make(noise); std::vector<float> o = run(d, 4, 2);
            dry_ratio = on_off(o, { 2, 5, 9 }); dry_db = rmsdb(o); fs_destroy(d);
        }
        {
            fs_device *d = make(noise);
            assert(pidx(d, "tune") >= 0 && pidx(d, "chord0") >= 0);
            set_chord(d, { 62, 65, 69 });
            fs_set_param(d, pidx(d, "tune"), 1); fs_set_param(d, pidx(d, "focus"), 0.1f);
            fs_set_param(d, pidx(d, "partials"), 6);     /* the 11th overtone of D sits 51 cents above G: the measure's own off point */
            std::vector<float> o = run(d, 4, 2);
            double before = 0; for (size_t i = 1; i < o.size(); i++) before = std::fmax(before, std::fabs(o[i] - o[i - 1]));
            wet_ratio = on_off(o, { 2, 5, 9 }); wet_db = rmsdb(o);
            std::printf("shape 3: tune 1 on pink noise: on/off the chord %.1f (dry %.1f), level %.1f dB (dry %.1f)\n", wet_ratio, dry_ratio, wet_db, dry_db);
            assert(wet_ratio > 20 && dry_ratio < 3);
            assert(std::fabs(wet_db - dry_db) < 1.0);                                    /* 6: A-18, a timbre not a level */
            /* 4: a chord change glides, with no click */
            set_chord(d, { 67, 71, 74 }); fs_set_param(d, pidx(d, "glide"), 1);
            std::vector<float> all; std::vector<float> o2 = run(d, 4, 1.5, &all);
            double jump = 0; for (size_t i = 1; i < all.size(); i++) jump = std::fmax(jump, std::fabs(all[i] - all[i - 1]));
            double g_ratio = on_off(o2, { 7, 11, 2 });
            std::printf("shape 4: to G B D over a 1 s glide: on/off the new chord %.1f, largest step %.3f (the noise's own before: %.3f)\n", g_ratio, jump, before);
            assert(g_ratio > 20 && jump < 1.5 * before);                             /* no click beyond what the sound itself does */
            fs_destroy(d);
        }

        /* 5: layers add the chord's intervals above the recording (root D: F +3, A +7) */
        {
            fs_device *d = make(sine);
            set_chord(d, { 62, 65, 69 });
            fs_set_param(d, pidx(d, "layers"), 2); fs_set_param(d, pidx(d, "harmony"), 1);
            std::vector<float> o = run(d, 4, 2);
            double p0 = power(o, 440), p3 = power(o, 440 * std::pow(2.0, 3 / 12.0)), p7 = power(o, 440 * std::pow(2.0, 7 / 12.0)), px = power(o, 440 * std::pow(2.0, 5 / 12.0));
            std::printf("shape 5: layers 2: +3 st %.2e, +7 st %.2e, off (+5) %.2e, the recording %.2e\n", p3, p7, px, p0);
            assert(p3 > 20 * px && p7 > 20 * px);
            fs_destroy(d);
        }

        /* 6b: tune, layers and blur all at 1 keep the level */
        {
            fs_device *d = make(noise);
            set_chord(d, { 62, 65, 69 });
            for (const char *k : { "tune", "blur" }) fs_set_param(d, pidx(d, k), 1);
            fs_set_param(d, pidx(d, "layers"), 4); fs_set_param(d, pidx(d, "harmony"), 1);
            double db = rmsdb(run(d, 4, 2));
            std::printf("shape 6: tune + layers 4 + blur, level %.1f dB (dry %.1f)\n", db, dry_db);
            assert(std::fabs(db - dry_db) < 1.0);
            fs_destroy(d);
        }

        /* 7: a region plays only its part (300 Hz, then 900 Hz; region = the first half) */
        {
            std::vector<float> two(n);
            for (int i = 0; i < n; i++) two[i] = 0.3f * (float)std::sin(TAU * (i < n / 2 ? 300 : 900) * i / SR);
            fs_device *d = make(two);
            assert(pidx(d, "end") >= 0);
            fs_set_param(d, pidx(d, "start"), 0); fs_set_param(d, pidx(d, "end"), 0.5f);
            std::vector<float> o = run(d, 8, 5);
            double p3 = power(o, 300), p9 = power(o, 900);
            std::printf("shape 7: region 0-0.5: 300 Hz %.2e, 900 Hz %.2e\n", p3, p9);
            assert(p3 > 50 * p9);
            fs_destroy(d);
        }

        /* 8: drift changes the sound, not its level */
        {
            fs_device *a = make(noise), *b = make(noise);
            fs_set_param(b, pidx(b, "drift"), 1);
            std::vector<float> oa = run(a, 4, 2), ob = run(b, 4, 2);
            double diff = 0; for (size_t i = 0; i < oa.size(); i++) diff += std::fabs(oa[i] - ob[i]);
            std::printf("shape 8: drift 1: differs by %.1f, level %.1f dB vs %.1f\n", diff, rmsdb(ob), rmsdb(oa));
            assert(diff > 1 && std::fabs(rmsdb(ob) - rmsdb(oa)) < 1.0);
            fs_destroy(a); fs_destroy(b);
        }

        /* 9: everything on at the longest window, a chord change mid-way: still inside one 128-sample
           callback's budget (2.67 ms at 48 kHz; the stretch's own check holds 1.33 ms) */
        /* wall-clock: up to three runs, one must be clean - a busy desktop (a build next door) slowed one
           run of the -O1 test build past the budget on 2026-09-30; the bound itself is unchanged */
        bool clean = false;
        for (int attempt = 0; attempt < 3 && !clean; attempt++) {
            fs_device *d = make(noise);
            fs_set_param(d, 1, 2.0f);                                          /* window 2 s */
            set_chord(d, { 62, 65, 69, 72, 76 });
            for (const char *k : { "tune", "blur", "drift" }) fs_set_param(d, pidx(d, k), 1);
            fs_set_param(d, pidx(d, "layers"), 4); fs_set_param(d, pidx(d, "harmony"), 1);
            fs_set_param(d, pidx(d, "partials"), 24); fs_set_param(d, pidx(d, "transpose"), 5);
            std::vector<double> ms;
            for (int k = 0; k < (int)(12.0 * SR / B); k++) {
                if (k == (int)(6.0 * SR / B)) set_chord(d, { 67, 71, 74, 77 });
                auto t0 = std::chrono::steady_clock::now();
                fs_process(d, B);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            fs_stats_t st{}; fs_stats(d, &st);
            std::sort(ms.begin(), ms.end());
            const double p999 = ms[(size_t)(ms.size() * 0.999)], worst = ms.back();
            /* a frame finished late is an audible failure; one slow callback on a desktop is the OS */
            std::printf("shape 9: everything on, window 2 s, a chord change: late frames %d, 99.9%% of callbacks within %.3f ms (budget 1.33), worst %.3f ms (a callback is 2.67)\n", st.late_frames, p999, worst);
#ifdef FS_TEST_O1   /* core/tests/run.py's -O1 wasm build (wasm-opt blocked on Windows): 1.5x, native builds keep the full budget */
            const double slack = 1.5;
#else
            const double slack = 1;
#endif
            clean = st.late_frames == 0 && p999 < 1.33 * slack && worst < 2.67 * slack;
            fs_destroy(d);
        }
        assert(clean);
    }
    {   /* the engine hands a point its shaping and the route's chord: tune puts pink noise on C#m9 (not D: a stretch with no chord tunes to D, so D would pass without one) */
        auto pink = [](int n) {
            short *pcm = fs_alloc_i16(n);
            unsigned s = 9; double b0 = 0, b1 = 0, b2 = 0;
            for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; double w = ((int)(s >> 16) - 32768) / 32768.0;
                b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
                pcm[i] = (short)(3000 * (b0 + b1 + b2 + w * 0.1848)); }
            return pcm;
        };
        auto cm9 = [&](const std::vector<float> &o) {   /* power on C#m9's notes over power a quarter-tone off them */
            const double TAU = 6.283185307179586;
            auto power = [&](double f) { double w = TAU * f / SR, c = 2 * std::cos(w), s1 = 0, s2 = 0; for (float v : o) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; } return (s1 * s1 + s2 * s2 - c * s1 * s2) / o.size(); };
            double on = 0, off = 0;
            for (int pc : { 1, 4, 8, 11, 3 }) for (int oct = 3; oct <= 6; oct++) {
                double f = 440 * std::pow(2.0, (pc + 12 * (oct + 1) - 69) / 12.0);
                on += power(f); off += power(f * std::pow(2.0, 0.5 / 12));
            }
            return on / (off + 1e-30);
        };
        auto walk_ratio = [&](float tune, bool standing) {
            fs_engine *e = fs_engine_create(SR, B);
            fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
                "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{\"version\":12,\"key\":49,\"prog\":[{\"r\":0,\"q\":\"m9\"}],"
                "\"voice\":{\"on\":false},\"sect\":{\"on\":false},\"v3\":{\"on\":false},\"zones\":{\"on\":false},\"morph\":{\"on\":false,\"list\":[]}}}}");
            char pt[512];
            std::snprintf(pt, sizeof pt, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.001,41.0]},"
                "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\","
                "\"sound\":{\"radius\":200,\"shape\":{\"tune\":%g,\"focus\":0.1,\"partials\":6}}}}", tune);
            fs_engine_upsert(e, pt);
            const int n = 48000 * 4;
            short *pcm = pink(n);
            std::string need = fs_engine_step(e, 29.001, 41.0);
            size_t at = need.find("S "); assert(at != std::string::npos);
            fs_engine_source(e, 'S', need[at + 2] - '0', 0, "s1", 1, n, pcm);
            std::vector<float> o;
            for (int b = 0; b < (int)(10.0 * SR / B); b++) {
                /* standing: no more fixes, only the screen's live reads (30 a second on the web) */
                if (b % 40 == 0) { if (standing) fs_engine_chord(e, nullptr, nullptr, 0); else fs_engine_step(e, 29.001, 41.0); }
                fs_engine_process(e, B);
                if (b > (int)(7.0 * SR / B)) o.insert(o.end(), fs_engine_out(e, 0), fs_engine_out(e, 0) + B);
            }
            fs_engine_destroy(e);
            return cm9(o);
        };
        double dry = walk_ratio(0, false), wet = walk_ratio(1, false), still = walk_ratio(1, true);
        std::printf("engine shaping: a walk past a pink-noise point, on/off C#m9 - tune 0: %.1f, tune 1: %.1f, standing still: %.1f\n", dry, wet, still);
        assert(dry < 3 && wet > 10 && still > 0.9 * wet);   /* standing still hears the chord as walking does (without the live-read feed: 88 vs 229) */

        /* the apps' path (no engine): fs_stretch_shape + fs_stretch_chord from a piece, as ios/App.swift and jni.cpp call them */
        auto app_ratio = [&](const char *shape) {
            fs_device *piece = fs_create("piece"), *st = fs_create("stretch");
            fs_prepare(piece, SR, B); fs_prepare(st, SR, B);
            fs_piece_add_route(piece, "{\"version\":12,\"key\":49,\"prog\":[{\"r\":0,\"q\":\"m9\"}],\"voice\":{\"on\":false},\"sect\":{\"on\":false},"
                "\"v3\":{\"on\":false},\"zones\":{\"on\":false},\"morph\":{\"on\":false,\"list\":[]}}");
            fs_piece_walk(piece, 0, 0.5, 0);
            const int n = 48000 * 4;
            const short *ch[1] = { pink(n) };
            fs_set_source_i16(st, 1, n, ch);
            fs_stretch_shape(st, shape);
            std::vector<float> o;
            for (int b = 0; b < (int)(10.0 * SR / B); b++) {
                if (b % 40 == 0) fs_stretch_chord(st, piece);
                fs_process(piece, B); fs_process(st, B);
                if (b > (int)(7.0 * SR / B)) o.insert(o.end(), fs_out(st, 0), fs_out(st, 0) + B);
            }
            fs_destroy(st); fs_destroy(piece);
            return cm9(o);
        };
        double none = app_ratio(""), tuned = app_ratio("{\"tune\":1,\"focus\":0.1,\"partials\":6}");
        std::printf("app shaping: pink noise, on/off C#m9 - no shape: %.1f, tune 1: %.1f\n", none, tuned);
        assert(none < 3 && tuned > 10);
    }
    {   /* resampling: the kernel table matches the per-sample reference within 1 LSB, and is far faster.
           A 13.5-min 44.1 kHz MP3 took 64 s to resample on a Mac and minutes on a phone, and the app's
           one-at-a-time decode queue held every point behind it (Kerem, 2026-09-29: "no sound from points"). */
        const long long N = 44100 * 2;
        std::vector<short> in(N);
        uint32_t r = 7;
        for (long long i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; in[i] = (short)(9000 * std::sin(i * 0.031) + ((int)(r >> 17) - 16384) / 2); }
        const double RATES[4][2] = { { 44100, 48000 }, { 48000, 44100 }, { 22050, 48000 }, { 32000, 48000 } };
        for (const auto &rt : RATES) {
            const long long m = fs_resample_length(N, rt[0], rt[1]);
            std::vector<short> a(m), b(m);
            auto t0 = std::chrono::steady_clock::now();
            ref_resample(in.data(), N, rt[0], a.data(), rt[1]);
            auto t1 = std::chrono::steady_clock::now();
            fs_resample_i16(in.data(), N, rt[0], b.data(), rt[1]);
            auto t2 = std::chrono::steady_clock::now();
            int worst = 0;
            for (long long j = 0; j < m; j++) worst = std::max(worst, std::abs(a[j] - b[j]));
            const double ta = std::chrono::duration<double>(t1 - t0).count(), tb = std::chrono::duration<double>(t2 - t1).count();
            std::printf("resample %.0f -> %.0f: max diff %d LSB, %.1fx faster\n", rt[0], rt[1], worst, ta / std::max(tb, 1e-9));
            assert(worst <= 1);
            assert(ta / std::max(tb, 1e-9) > 8);
        }
    }
    {   /* the harmony core (sample harmony, spec D2/D8) */
        using namespace harmony;
        auto et = [](double m) { return 440 * std::pow(2.0, (m - 69) / 12); };   /* tone::mtof, today's tuning */
        /* equal temperament is today's mtof, bit for bit */
        for (int m = 20; m < 110; m++) assert(hz(EQUAL, m, 50, false) == et(m));
        /* just intonation: exact ratios of the root, the root itself equal-tempered, any octave */
        const int R = 50;                                     /* D3 */
        const double RR[12] = { 1, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3, 45.0/32, 3.0/2, 8.0/5, 5.0/3, 9.0/5, 15.0/8 };
        for (int iv = 0; iv < 12; iv++) {
            assert(std::fabs(hz(JUST, R + iv, R, false) / et(R) - RR[iv]) < 1e-12);
            assert(std::fabs(hz(JUST, R + iv + 12, R, false) / et(R) - 2 * RR[iv]) < 1e-12);
            assert(std::fabs(hz(JUST, R + iv - 24, R, false) / et(R) - RR[iv] / 4) < 1e-12);
        }
        assert(std::fabs(hz(JUST, R + 10, R, true) / et(R) - 7.0 / 4) < 1e-12);    /* dominant 7th */
        /* hysteresis: a pitch hovering across the midpoint of two targets never flips */
        double T[3] = { 220, 246.94, 277.18 };
        Follower f;
        assert(f.choose(221, T, 3) == 0);
        double mid = std::sqrt(220 * 246.94);
        int flips = 0, last = 0;
        for (int i = 0; i < 600; i++) {
            double wob = mid * std::pow(2.0, ((i * 7919 % 97) / 97.0 - 0.5) * 20 / 1200.0);   /* +-10 cents */
            int k = f.choose(wob, T, 3); if (k != last) flips++; last = k;
        }
        assert(flips == 0);
        /* ... while a sweep still reaches every target */
        Follower g; int seen = 0;
        for (double c = 0; c <= 1200; c += 5) { int k = g.choose(215 * std::pow(2.0, c / 1200), T, 3); seen |= 1 << k; }
        assert(seen == 7);
        /* glide: ends on the target exactly */
        Follower h; h.glide_s = 0.2; h.choose(220, T, 3); h.at = 200;
        double v = 0; for (int i = 0; i < 400; i++) v = h.step(0.01);
        assert(v == 220);
        std::printf("harmony: just ratios exact, 0 flips at a boundary, sweep reaches 3/3, glide lands\n");
    }
    {   /* the route engine through the harmony core: equal temperament is today, note for note */
        NoteLog today = walk_notes("{}", -1);
        std::printf("route notes, equal (default): %zu notes, hash %llu\n", today.f.size(), note_hash(today));
        assert(today.f.size() > 20);
        assert(note_hash(today) == ROUTE_NOTES_HASH);            /* captured from the engine before Task 2 */
        assert(note_hash(walk_notes("{\"tuning\":\"equal\"}", -1)) == ROUTE_NOTES_HASH);
        assert(note_hash(walk_notes("{}", 0)) == ROUTE_NOTES_HASH);
        /* just: the same notes (same draws, same drift), each moved from equal temperament by exactly one
           just ratio's correction - just_ratio(iv) / 2^(iv/12) - and the non-root notes really move */
        NoteLog just = walk_notes("{}", 1);
        assert(just.f.size() == today.f.size());
        double fix[13];
        for (int iv = 0; iv < 12; iv++) fix[iv] = harmony::just_ratio(iv, false) / std::pow(2.0, iv / 12.0);
        fix[12] = 7.0 / 4 / std::pow(2.0, 10 / 12.0);
        int moved = 0;
        for (size_t i = 0; i < just.f.size(); i++) {
            if (!(today.f[i] > 0)) continue;                      /* a noise zone has no pitch */
            double q = just.f[i] / today.f[i];
            bool one = false; for (double v : fix) if (std::fabs(q - v) < 1e-9) one = true;
            assert(one);
            if (std::fabs(q - 1) > 1e-9) moved++;
        }
        assert(moved > 0);
        assert(note_hash(walk_notes("{\"tuning\":\"just\"}", 0)) == note_hash(just));   /* the patch wins over the default */
        std::printf("route notes, just: every note one exact just correction off equal, %d moved; the patch overrides the default\n", moved);
    }
    {   /* the bench's progression: 16 chords of the default patch, exact just ratios, a safe buffer */
        char small[8] = "unused";
        int need = fs_harmony_progression("{}", 1, 0, small, sizeof small);
        assert(need > 100 && std::strcmp(small, "unused") == 0);           /* too small: nothing written */
        std::vector<char> buf(need + 1);
        assert(fs_harmony_progression("{}", 1, 0, buf.data(), (int)buf.size()) == need);
        std::string j = buf.data();
        int n = 0; for (size_t k = 0; (k = j.find("\"label\"", k)) != std::string::npos; k++) n++;
        assert(n == 16);
        assert(j.find("\"tuning\":\"just\"") != std::string::npos && j.find("\"scale\":[") != std::string::npos);
        assert(j.find("\"scale_hz\":[") != std::string::npos);
        /* the first chord (D m9, root 50): its fifth is exactly 3/2 of its root */
        size_t hz = j.find("\"hz\":[");
        double r = std::atof(j.c_str() + hz + 6);
        size_t c3 = hz + 6; for (int i = 0; i < 2; i++) c3 = j.find(',', c3) + 1;
        double fifth = std::atof(j.c_str() + c3);
        assert(std::fabs(fifth / r - 1.5) < 1e-6);
        std::printf("bench progression: 16 chords, just fifth %.4f / root %.4f\n", fifth, r);
    }
    {   /* the analyser: known pitches within 5 cents, noise unpitched, loop and cycle on the waveform */
        auto analyse = [](const std::vector<float> &x, double sr) {
            int need = fs_analyse(x.data(), (long long)x.size(), sr, nullptr, 0);
            std::vector<char> b(need + 1);
            fs_analyse(x.data(), (long long)x.size(), sr, b.data(), (int)b.size());
            return std::string(b.data());
        };
        auto num = [](const std::string &j, const char *k) { size_t p = j.find(std::string("\"") + k + "\":"); return p == std::string::npos ? -1.0 : std::atof(j.c_str() + p + std::strlen(k) + 3); };
        auto cents = [](double a, double b) { return 1200 * std::log2(a / b); };
        const double SR = 48000, PI = 3.141592653589793;
        std::vector<float> sine(SR * 2), saw(SR * 2), bell(SR * 2), noise(SR * 2);
        uint32_t r = 99;
        for (size_t i = 0; i < sine.size(); i++) {
            double t = i / SR;
            sine[i] = (float)(0.5 * std::sin(2 * PI * 220 * t));
            double s = 0; for (int k = 1; k <= 30; k++) s += std::sin(2 * PI * 110 * k * t) / k; saw[i] = (float)(0.3 * s);
            bell[i] = (float)((0.5 * std::sin(2 * PI * 330 * t) + 0.3 * std::sin(2 * PI * 660 * t) + 0.2 * std::sin(2 * PI * 990 * t) + 0.1 * std::sin(2 * PI * 330 * 2.76 * t)) * std::exp(-t * 0.8));
            r = r * 1664525u + 1013904223u; noise[i] = (float)(((int)(r >> 8) - 8388608) / 16777216.0);
        }
        std::string js = analyse(sine, SR), jw = analyse(saw, SR), jb = analyse(bell, SR), jn = analyse(noise, SR);
        std::printf("analyse sine %.2f, saw %.2f, bell %.2f, noise %s\n", num(js, "f0"), num(jw, "f0"), num(jb, "f0"), jn.find("\"unpitched\"") != std::string::npos ? "unpitched" : "PITCHED");
        assert(std::fabs(cents(num(js, "f0"), 220)) < 5 && js.find("\"pitched\"") != std::string::npos);
        assert(std::fabs(cents(num(jw, "f0"), 110)) < 5);          /* no octave error on a bright tone */
        assert(std::fabs(cents(num(jb, "f0"), 330)) < 5);
        assert(jn.find("\"verdict\":\"unpitched\"") != std::string::npos);
        /* the loop on the sine: a whole number of periods, ends on zero crossings */
        size_t lp = js.find("\"loop\":["); assert(lp != std::string::npos);
        long a = std::atol(js.c_str() + lp + 8), b = std::atol(js.c_str() + js.find(',', lp + 8) + 1);
        double periods = (b - a) * 220 / SR;
        assert(b > a && std::fabs(periods - std::round(periods)) < 0.02 && std::fabs(sine[a]) < 0.02 && std::fabs(sine[b]) < 0.02);
        /* the cycle: one period long */
        size_t cp = js.find("\"cycle\":["); assert(cp != std::string::npos);
        long c0 = std::atol(js.c_str() + cp + 9), c1 = std::atol(js.c_str() + js.find(',', cp + 9) + 1);
        assert(std::fabs((c1 - c0) - SR / 220) <= 1.5);
        /* 44.1 kHz, too short, silence: sane answers and no NaN */
        std::vector<float> s44(44100); for (size_t i = 0; i < s44.size(); i++) s44[i] = (float)(0.5 * std::sin(2 * PI * 440 * i / 44100.0));
        assert(std::fabs(cents(num(analyse(s44, 44100), "f0"), 440)) < 5);
        std::vector<float> tiny(500, 0.1f), quiet(SR, 0.0f);
        for (auto &j : { analyse(tiny, SR), analyse(quiet, SR) }) {
            assert(j.find("\"verdict\":\"unpitched\"") != std::string::npos);
            assert(j.find("nan") == std::string::npos && j.find("inf") == std::string::npos);
        }
        char small[4] = "abc";
        assert(fs_analyse(sine.data(), (long long)sine.size(), SR, small, sizeof small) > 4 && std::strcmp(small, "abc") == 0);
    }
    {   /* final review (2026-09-30): route swaps in equal temperament match the engine before the harmony
           core (hash from f0a81d7); the hysteresis margin is 30% of the gap past the midpoint (A-14);
           odd signals keep every per-frame pitch finite and near the search range */
        unsigned long long sw = swap_walk_hash();
        std::printf("swap walk: hash %llu\n", sw);
        assert(sw == 1554010510690710589ULL);
        double T2[2] = { 220, 440 * std::pow(2.0, -10 / 12.0) };            /* A3, B3: 200 cents apart */
        auto at = [](double c) { return 220 * std::pow(2.0, c / 1200); };
        harmony::Follower f; f.choose(221, T2, 2);
        assert(f.choose(at(100 + 0.2 * 200), T2, 2) == 0);                  /* 20% of the gap past the midpoint: held */
        assert(f.choose(at(100 + 0.35 * 200), T2, 2) == 1);                 /* 35%: moves */
        assert(f.choose(at(100 - 0.2 * 200), T2, 2) == 1);                  /* and back: held the other way */
        assert(f.choose(at(100 - 0.35 * 200), T2, 2) == 0);
        const double SR2 = 48000, PI2 = 3.141592653589793; uint32_t rr = 5;
        std::vector<float> clicks(SR2 * 2), chirp(SR2 * 2);
        for (size_t i = 0; i < clicks.size(); i++) {
            double t = i / SR2; clicks[i] = (i % 997 == 0) ? 0.9f : 0.0f;
            chirp[i] = (float)(0.5 * std::sin(2 * PI2 * (50 + 2500 * t) * t)); rr = rr * 1664525u + 1013904223u;
        }
        for (auto *x : { &clicks, &chirp }) {
            int need = fs_analyse(x->data(), (long long)x->size(), SR2, nullptr, 0);
            std::vector<char> b(need + 1); fs_analyse(x->data(), (long long)x->size(), SR2, b.data(), (int)b.size());
            std::string j = b.data(); size_t p = j.find("\"track\":[") + 9; double mx = 0;
            while ((p = j.find('[', p)) != std::string::npos) { mx = std::fmax(mx, std::atof(j.c_str() + p + 1)); p++; }
            assert(mx < 8800);                                              /* the search reaches 8 kHz (birds) */
        }
        std::printf("review fixes: swap walk unchanged, hysteresis holds to 30%% past the midpoint, per-frame pitch bounded\n");
    }
    {   /* birds (Kerem, 2026-09-30: a goldfinch read "unpitched"): calls at 3-6 kHz with silence between
           them are pitched, and the silence does not count against them */
        const double SR3 = 48000, PI3 = 3.141592653589793;
        auto grab = [](const std::vector<float> &x, double sr) {
            int need = fs_analyse(x.data(), (long long)x.size(), sr, nullptr, 0);
            std::vector<char> b(need + 1); fs_analyse(x.data(), (long long)x.size(), sr, b.data(), (int)b.size());
            return std::string(b.data());
        };
        auto f0of = [](const std::string &j) { return std::atof(j.c_str() + j.find("\"f0\":") + 5); };
        std::vector<float> whistle(SR3 * 2, 0.0f), glide(SR3 * 2, 0.0f);
        for (size_t i = 0; i < whistle.size(); i++) {
            double t = i / SR3, in = std::fmod(t, 0.25);                     /* an 80 ms call every 250 ms */
            if (in < 0.08) {
                double env = std::sin(PI3 * in / 0.08);
                whistle[i] = (float)(0.4 * env * std::sin(2 * PI3 * 4000 * t));
                double ph = 2 * PI3 * (3000 * in + 0.5 * (2000 / 0.08) * in * in);   /* 3 -> 5 kHz in each call */
                glide[i] = (float)(0.4 * env * std::sin(ph));
            }
        }
        std::string jw = grab(whistle, SR3), jg = grab(glide, SR3);
        double fw = f0of(jw), fg = f0of(jg);
        std::printf("birds: whistle %s %.1f Hz, glide %s %.1f Hz\n", jw.find("\"pitched\"") != std::string::npos ? "pitched" : "UNPITCHED", fw,
                    jg.find("\"pitched\"") != std::string::npos ? "pitched" : "UNPITCHED", fg);
        assert(jw.find("\"verdict\":\"pitched\"") != std::string::npos && std::fabs(1200 * std::log2(fw / 4000)) < 5);
        assert(jg.find("\"verdict\":\"pitched\"") != std::string::npos && fg > 3000 && fg < 5000);
        /* a loop is at least 100 ms, or there is none: the goldfinch's steadiest stretch was one frame and its
           loop one 13-sample cycle (a buzz). Short calls are played through, not looped. */
        for (const std::string *j : { &jw, &jg }) {
            size_t lp = j->find("\"loop\":");
            if (j->compare(lp + 7, 4, "null") == 0) continue;
            long la = std::atol(j->c_str() + lp + 8), lb = std::atol(j->c_str() + j->find(',', lp + 8) + 1);
            assert(lb - la >= (long)(0.1 * SR3));
        }

    }
    {   /* 2a: the Resonator's String - its notes land within 3 cents, Focus is the ring time, Tune 0 is the raw recording */
        const std::vector<float> wind = noise_src(12, 0.5f, 42);
        const double F[7] = { 55, 110, 220, 440, 880, 1760, 3520 };
        double worst = 0, worst_b = 0;
        for (double f : F) {                                             /* plucked: the loop's tuning, exactly */
            double got = peak_near(res_render(sampler::STRING, sampler::PLUCKED, 0.9, 0.5, 1, f, 3, wind), 48000, f, 0.04);
            worst = std::max(worst, std::fabs(1200 * std::log2(got / f)));
        }
        /* bowed: the same loop fed continuously; averaged, 110 Hz up (at 55 Hz the resonance is narrower than the
           measurement resolves - the plucked 55 Hz covers that loop) */
        for (double f : F) if (f >= 110) {
            double got = centre_near(res_render(sampler::STRING, sampler::BOWED, 0.9, 0.5, 1, f, 10, wind), 48000, f, 8);
            worst_b = std::max(worst_b, std::fabs(1200 * std::log2(got / f)));
        }
        std::printf("resonator string: worst pitch error plucked %.2f cents (55 Hz - 3.5 kHz), bowed %.2f cents (110 Hz - 3.5 kHz)\n", worst, worst_b);
        assert(worst < 3 && worst_b < 3);
        double t0 = t60_of(res_render(sampler::STRING, sampler::PLUCKED, 0, 1, 1, 220, 1.5, wind));
        double t1 = t60_of(res_render(sampler::STRING, sampler::PLUCKED, 1, 1, 1, 220, 6, wind));
        std::printf("resonator string: T60 at Focus 0 %.2f s, at Focus 1 %.2f s\n", t0, t1);
        assert(std::fabs(t0 / 0.2 - 1) < 0.2 && std::fabs(t1 / 10 - 1) < 0.2);
        std::vector<float> a = res_render(sampler::STRING, sampler::BOWED, 0.1, 0.2, 0, 220, 1, wind);
        std::vector<float> b = res_render(sampler::STRING, sampler::BOWED, 0.9, 0.9, 0, 440, 1, wind);
        assert(a == b);                                                  /* Tune 0: the resonator is out of the path */
    }
    {   /* 2a: Tube rings the odd harmonics, Bell its bar modes; both land on the note */
        const std::vector<float> wind = noise_src(12, 0.5f, 7);
        const double F[7] = { 55, 110, 220, 440, 880, 1760, 3520 };
        double worst = 0, worst_b = 0;
        for (int body : { (int)sampler::TUBE, (int)sampler::BELL }) for (double f : F) {
            double got = peak_near(res_render(body, sampler::PLUCKED, 0.9, 0.5, 1, f, 3, wind), 48000, f, 0.04);
            worst = std::max(worst, std::fabs(1200 * std::log2(got / f)));
            if (f >= 110) {
                double gb = centre_near(res_render(body, sampler::BOWED, 0.9, 0.5, 1, f, 10, wind), 48000, f, 8);
                worst_b = std::max(worst_b, std::fabs(1200 * std::log2(gb / f)));
            }
        }
        std::printf("resonator tube and bell: worst pitch error plucked %.2f cents, bowed %.2f cents\n", worst, worst_b);
        assert(worst < 3 && worst_b < 3);
        /* Tube: the odd harmonics stand >= 15 dB over the even */
        std::vector<float> tube = res_render(sampler::TUBE, sampler::BOWED, 0.8, 0.6, 1, 220, 10, wind);
        auto level_at = [&](double f) {
            const int N = 262144; double re = 0, im = 0; size_t s0 = tube.size() - N;
            for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N), ph = 2 * 3.141592653589793 * f * i / 48000; re += tube[s0 + i] * w * std::cos(ph); im += tube[s0 + i] * w * std::sin(ph); }
            return 10 * std::log10(re * re + im * im + 1e-30);
        };
        double odd = 0.5 * (level_at(centre_near(tube, 48000, 220, 8)) + level_at(centre_near(tube, 48000, 660, 8)));
        double even = 0.5 * (level_at(440) + level_at(880));
        std::printf("resonator tube: odd harmonics %.1f dB over even\n", odd - even);
        assert(odd - even >= 15);
        /* Bell: its modes at 2.76, 5.40, 8.93 x f within 1% */
        std::vector<float> bell = res_render(sampler::BELL, sampler::PLUCKED, 0.9, 1, 1, 220, 3, wind);
        for (int k = 1; k < 4; k++) {
            double want = 220 * sampler::BELL_RATIO[k], got = peak_near(bell, 48000, want, 0.05);
            std::printf("resonator bell: mode %d at %.1f Hz (want %.1f)\n", k, got, want);
            assert(std::fabs(got / want - 1) < 0.01);
        }
    }
    {   /* 2a: no clicks (attack, release, a steal, stop), no runaway, bowed level = the recording's, fast enough */
        const double SR = 48000;
        std::vector<float> dc((size_t)(SR * 10), 0.5f);               /* a flat recording: every step is the envelope's */
        sampler::Resonator r; r.init(SR); r.tune = 0; r.att = 0.005; r.rel = 0.03;
        const float *p[1] = { dc.data() }; r.set_source(1, (long long)dc.size(), p);
        /* every voice, then one more that steals; releases 40 ms apart so each step is one voice's; the stealer still
           sounds when Stop is pressed */
        const int NV = sampler::Resonator::VOICES;
        for (int k = 0; k <= NV; k++) { r.attack(110 * (k + 1), 0.01 * k, 0.5); r.release(k == NV ? 1.6 : 0.3 + 0.04 * k); }
        std::vector<float> L((size_t)(2 * SR), 0.0f), R(L.size(), 0.0f);
        for (size_t i = 0; i < L.size(); i += 128) { if (i == 72064) r.stop_all();   /* a block boundary: the loop steps 128 */ r.render(L.data() + i, R.data() + i, 128, i / SR); }
        double step = 0; for (size_t i = 1; i < L.size(); i++) step = std::max(step, (double)std::fabs(L[i] - L[i - 1]));
        const double bound = 0.5 * 0.5 * std::max(1.0 / (0.005 * SR), 1 - std::pow(1e-4, 1.0 / (0.03 * SR))) * 1.05;
        std::printf("resonator envelopes: largest step %.5f (bound %.5f)\n", step, bound);
        assert(step <= bound);
        double tail = 0; for (size_t i = 72064 + 480; i < L.size(); i++) tail = std::max(tail, (double)std::fabs(L[i]));
        assert(tail == 0);                                             /* stop_all: silent 10 ms later */
        /* no recording: silence, no crash */
        sampler::Resonator q; q.init(SR); q.attack(220, 0, 0.5);
        std::vector<float> z(4096, 0.0f), z2(4096, 0.0f); q.render(z.data(), z2.data(), 4096, 0);
        for (float s : z) assert(s == 0);
        /* extreme notes and a minute at Focus 1 on full-scale noise: finite and bounded */
        const std::vector<float> loud = noise_src(10, 1.0f, 3);
        for (double f : { 5.0, 12000.0, 55.0 }) {
            std::vector<float> o = res_render(sampler::STRING, sampler::BOWED, 1, 1, 1, f, f == 55.0 ? 60 : 2, loud);
            double mx = 0; for (float s : o) { assert(std::isfinite(s)); mx = std::max(mx, (double)std::fabs(s)); }
            assert(mx < 4);
        }
        /* bowed level follows the recording's within 1 dB, every body */
        const std::vector<float> wind = noise_src(10, 0.5f, 11);
        auto rms = [](const std::vector<float> &x) { double e = 0; size_t a = 2 * 48000, b = 4 * 48000; for (size_t i = a; i < b; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (b - a)); };
        for (int body = 0; body < 3; body++) {
            double wet = rms(res_render(body, sampler::BOWED, 0.5, 0.5, 1, 220, 4, wind)), dry = rms(res_render(body, sampler::BOWED, 0.5, 0.5, 0, 220, 4, wind));
            std::printf("resonator body %d: bowed %.2f dB vs the recording %.2f dB\n", body, wet, dry);
            assert(std::fabs(wet - dry) <= 1);
        }
        /* a setting changed while voices sound touches only the next note */
        sampler::Resonator s; s.init(SR); const float *wp[1] = { wind.data() }; s.set_source(1, (long long)wind.size(), wp);
        s.attack(220, 0, 0.5); std::vector<float> a1(4800, 0.0f), a2(4800, 0.0f); s.render(a1.data(), a2.data(), 4800, 0);
        double g = s.v[0].g; s.focus = 0.1; s.colour = 0.1; s.body = sampler::TUBE; s.render(a1.data(), a2.data(), 4800, 0.1);
        assert(s.v[0].g == g);
        /* every voice bowed at once inside one 128-sample block's budget */
        sampler::Resonator sp; sp.init(SR); sp.set_source(1, (long long)wind.size(), wp);
        for (int k = 0; k < sampler::Resonator::VOICES; k++) sp.attack(55 * (k + 1), 0, 0.05);
        std::vector<double> ms; std::vector<float> b1(128), b2(128);
        for (int k = 0; k < (int)(10 * SR / 128); k++) {
            std::fill(b1.begin(), b1.end(), 0.0f); std::fill(b2.begin(), b2.end(), 0.0f);
            auto c0 = std::chrono::steady_clock::now(); sp.render(b1.data(), b2.data(), 128, k * 128 / SR);
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
        }
        std::sort(ms.begin(), ms.end());
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        std::printf("resonator: %d bowed strings, 99.9%% of blocks within %.3f ms (budget %.2f)\n", sampler::Resonator::VOICES, ms[(size_t)(ms.size() * 0.999)], 1.33 * slack);
        assert(ms[(size_t)(ms.size() * 0.999)] < 1.33 * slack);
    }
    {   /* 2a: the bench device - notes at times, the clock, "pitch created", stop */
        fs_device *b = fs_create("bench");
        assert(b);
        fs_prepare(b, 48000, 128);
        const std::vector<float> wind = noise_src(4, 0.5f, 5);
        const float *wp[1] = { wind.data() };
        fs_set_source(b, 1, (int)wind.size(), wp);
        assert(std::isfinite(fs_bench_created(b, 220)));               /* before any sound */
        fs_bench_note(b, 220, 0.1, 5, 0.5);
        for (int i = 0; i < 48000 * 2 / 128; i++) fs_process(b, 128);
        assert(std::fabs(fs_bench_time(b) - 2.0) < 0.01);
        double made = fs_bench_created(b, 220);
        std::printf("bench: a bowed string on noise, pitch created %+.1f dB at 220 Hz\n", made);
        assert(made >= 10);
        fs_bench_stop(b);
        for (int i = 0; i < 8; i++) fs_process(b, 128);
        double mx = 0; for (int i = 0; i < 128; i++) mx = std::max(mx, (double)std::fabs(fs_out(b, 0)[i]));
        assert(mx == 0);
        fs_destroy(b);
    }
    {   /* final review (2026-09-30): no runaway anywhere - every body, Colour 0 / 0.5 / 1, Focus 1, 6 Hz to 12 kHz.
           Plucked: the broadband peak of the last second <= the first second's. Bowed: the loop itself stays
           bounded (its automatic gain would hide a growing loop at the output). */
        const std::vector<float> wind = noise_src(12, 0.5f, 19);
        int bad = 0;
        for (int body = 0; body < 3; body++) for (double col : { 0.0, 0.5, 1.0 }) for (double f : { 6.0, 15.0, 55.0, 440.0, 3520.0, 6000.0, 12000.0 }) {
            std::vector<float> o = res_render(body, sampler::PLUCKED, 1, col, 1, f, 8, wind);
            /* energy, not peak: in a near-lossless loop (sub-audio notes) the partials drift in phase and the
               waveform's peak wanders while the energy does not grow; a runaway grows in energy */
            double first = 0, lastp = 0;
            for (size_t i = 0; i < 48000; i++) first += (double)o[i] * o[i];
            for (size_t i = o.size() - 48000; i < o.size(); i++) { if (!std::isfinite(o[i])) { lastp = 1e300; break; } lastp += (double)o[i] * o[i]; }
            if (!(lastp <= first * 1.5)) { std::printf("  runaway: body %d colour %.1f %.0f Hz plucked: energy first %.3g last %.3g\n", body, col, f, first, lastp); bad++; }
            sampler::Resonator r; r.init(48000); r.body = body; r.excite = sampler::BOWED; r.focus = 1; r.colour = col;
            const float *p[1] = { wind.data() }; r.set_source(1, (long long)wind.size(), p);
            r.attack(f, 0, 0.5); std::vector<float> a(48000 * 8, 0.0f), b2(a.size(), 0.0f);
            for (size_t i = 0; i < a.size(); i += 128) r.render(a.data() + i, b2.data() + i, 128, i / 48000.0);
            double line = 0; for (float s : r.v[0].line) line = std::max(line, (double)std::fabs(s));
            for (int k = 0; k < 4; k++) line = std::max(line, std::fabs(r.v[0].y1[k]));
            if (!(line < 100)) { std::printf("  runaway: body %d colour %.1f %.0f Hz bowed: loop %.3g\n", body, col, f, line); bad++; }
        }
        std::printf("resonator stability sweep: %d runaways\n", bad);
        assert(bad == 0);
    }
    {   /* final review: Body / Excite changed mid-note leave the sounding note untouched (bit-identical); Tune moves
           without a zipper; a quiet passage is not boosted 60 dB */
        const std::vector<float> wind = noise_src(6, 0.5f, 23);
        const float *wp[1] = { wind.data() };
        auto run = [&](bool change) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), wp); r.focus = 0.8;
            r.attack(220, 0, 0.5); std::vector<float> a(48000 * 2, 0.0f), b(a.size(), 0.0f);
            for (size_t i = 0; i < a.size(); i += 128) {
                if (change && i == 48000) { r.body = sampler::BELL; r.excite = sampler::PLUCKED; }
                r.render(a.data() + i, b.data() + i, 128, i / 48000.0);
            }
            return a;
        };
        assert(run(false) == run(true));
        /* Tune from 0 to 1 in one step while a note holds a flat recording: the mix glides (no step over 0.01) */
        std::vector<float> dc(48000 * 2, 0.5f); const float *dp[1] = { dc.data() };
        sampler::Resonator t; t.init(48000); t.set_source(1, (long long)dc.size(), dp); t.tune = 0; t.att = 0.005;
        t.attack(220, 0, 0.5); std::vector<float> a(48000, 0.0f), b(a.size(), 0.0f);
        for (size_t i = 0; i < a.size(); i += 128) { if (i == 24064) t.tune = 1; t.render(a.data() + i, b.data() + i, 128, i / 48000.0); }
        double step = 0; for (size_t i = 24064; i < a.size(); i++) step = std::max(step, (double)std::fabs(a[i] - a[i - 1]));
        std::printf("resonator tune change: largest step %.5f\n", step);
        assert(step < 0.01);
        /* room tone at -80 dBFS for 2 s, then loud: a bowed Tube's output in the quiet stays under -50 dBFS */
        std::vector<float> room = noise_src(4, 0.5f, 29);
        for (size_t i = 0; i < 96000; i++) room[i] *= 0.0002f;              /* ~ -80 dBFS RMS */
        /* when the loud part returns, the gain the quiet part asked for must not swell it: the loudest 50 ms in
           the half second after the return stays within 3 dB of the settled level (seconds 3-4) */
        std::vector<float> o = res_render(sampler::TUBE, sampler::BOWED, 0.8, 0.5, 1, 220, 4, room);
        auto win_db = [&](size_t a, size_t n) { double e = 0; for (size_t i = a; i < a + n; i++) e += (double)o[i] * o[i]; return 10 * std::log10(e / n + 1e-30); };
        double swell = -300; for (size_t a = 96000; a + 2400 <= 96000 + 24000; a += 1200) swell = std::max(swell, win_db(a, 2400));
        double settled = win_db(144000, 48000);
        std::printf("resonator: bowed Tube, loud after -80 dBFS room tone: loudest 50 ms %.1f dB vs settled %.1f dB\n", swell, settled);
        assert(swell - settled <= 3);
    }
    {   /* final review: notes posted ahead all sound - a 7-note scale, a re-pressed chord, a 10-chord progression (a
           note queue in the bench; a steal never overwrites a waiting note) - checked at the output */
        const std::vector<float> wind = noise_src(30, 0.5f, 31);
        const float *wp[1] = { wind.data() };
        auto make = [&]() { fs_device *b = fs_create("bench"); fs_prepare(b, 48000, 128); fs_set_source(b, 1, (int)wind.size(), wp); fs_set_param(b, 2, 0.8f); return b; };
        auto render = [&](fs_device *b, double secs, std::vector<float> &out) {
            for (int i = 0; i < (int)(secs * 48000 / 128); i++) { fs_process(b, 128); const float *l = fs_out(b, 0); out.insert(out.end(), l, l + 128); }
        };
        auto pw = [](const std::vector<float> &x, double f, double a, double z) {       /* Goertzel power in [a, z) s */
            size_t i0 = (size_t)(a * 48000), i1 = std::min(x.size(), (size_t)(z * 48000)); double s1 = 0, s2 = 0, k = 2 * std::cos(2 * 3.141592653589793 * f / 48000);
            for (size_t i = i0; i < i1; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * (i - i0) / (i1 - i0)), s0 = x[i] * w + k * s1 - s2; s2 = s1; s1 = s0; }
            return s1 * s1 + s2 * s2 - k * s1 * s2;
        };
        /* 1: a 7-note scale posted at once, one note each 0.5 s: each is the loudest of the seven in its own turn */
        const double sc[7] = { 146.83, 164.81, 174.61, 196.00, 220.00, 246.94, 261.63 };
        fs_device *b = make(); std::vector<float> o;
        for (int k = 0; k < 7; k++) fs_bench_note(b, sc[k], 0.1 + 0.5 * k, 0.4, 0.4);
        render(b, 4, o);
        int heard = 0;
        for (int k = 0; k < 7; k++) { bool top = true; double me = pw(o, sc[k], 0.25 + 0.5 * k, 0.5 + 0.5 * k); for (int j = 0; j < 7; j++) if (j != k && pw(o, sc[j], 0.25 + 0.5 * k, 0.5 + 0.5 * k) >= me) top = false; heard += top; }
        fs_destroy(b);
        /* 2: a chord, then Stop and a new chord while the first still rings: all four new notes sound */
        const double c1[4] = { 146.83, 174.61, 220.00, 261.63 }, c2[4] = { 196.00, 246.94, 293.66, 349.23 };
        b = make(); o.clear();
        for (double h : c1) fs_bench_note(b, h, 0.05, 2, 0.3);
        render(b, 1, o);
        fs_bench_stop(b); for (double h : c2) fs_bench_note(b, h, fs_bench_time(b) + 0.02, 2, 0.3);
        render(b, 1.5, o);
        int second = 0; for (double h : c2) { double me = pw(o, h, 1.6, 2.4), off = pw(o, h * 1.06, 1.6, 2.4); second += me > 30 * off; }
        fs_destroy(b);
        /* 3: ten 3-note chords posted at once, one per second: each chord's root sounds in its second at least a quarter
           of its power alone (against itself: at Focus 0.8 a pitch can find almost nothing in the noise it is played on) */
        const double roots[10] = { 146.83, 196.00, 130.81, 174.61, 164.81, 220.00, 123.47, 185.00, 138.59, 207.65 };
        b = make(); o.clear();
        for (int k = 0; k < 10; k++) for (double r : { 1.0, 1.25, 1.5 }) fs_bench_note(b, roots[k] * r, 0.1 + k, 0.9, 0.2);
        render(b, 11, o);
        auto alone = [&](double h) { fs_device *a = make(); std::vector<float> x; fs_bench_note(a, h, 0.1, 0.9, 0.2); render(a, 1, x); fs_destroy(a); return pw(x, h, 0.4, 0.95); };
        int chords = 0; for (int k = 0; k < 10; k++) chords += pw(o, roots[k], 0.4 + k, 0.95 + k) > 0.25 * alone(roots[k]);
        fs_destroy(b);
        std::printf("bench queue: scale %d/7 notes heard, re-pressed chord %d/4, progression %d/10 chords\n", heard, second, chords);
        assert(heard == 7 && second == 4 && chords == 10);
        /* 4 (Kerem 2026-10-01: "smooth cloudy transitions when release is longer than the note"): 5-note chords every
           2.4 s with a 3 s release - the last chord's tail still sounds under the next (not cut by a steal), and the
           level never drops abruptly at a change */
        const double ch5[5] = { 1.0, 1.25, 1.5, 1.875, 2.25 }, r5[4] = { 146.83, 196.00, 130.81, 174.61 };
        b = make(); o.clear(); fs_set_param(b, 6, 3.0f);
        for (int k = 0; k < 4; k++) for (double r : ch5) fs_bench_note(b, r5[k] * r, 0.1 + 2.4 * k, 2.3, 0.15);
        render(b, 10, o);
        int tails = 0; double drop = 0;
        for (int k = 1; k < 4; k++) {
            const double at = 0.1 + 2.4 * k, own = pw(o, r5[k - 1], at - 1.0, at - 0.3), after = pw(o, r5[k - 1], at + 0.1, at + 0.6);
            tails += after > 0.02 * own;
            std::vector<double> db; for (double s = at - 0.1; s < at + 0.5; s += 0.02) { double e = 0; for (size_t i = (size_t)(s * 48000); i < (size_t)((s + 0.02) * 48000); i++) e += (double)o[i] * o[i]; db.push_back(10 * std::log10(e + 1e-30)); }
            for (size_t i = 1; i < db.size(); i++) drop = std::max(drop, db[i - 1] - db[i]);
        }
        fs_destroy(b);
        std::printf("bench release overlap: %d/3 tails under the next chord, largest 20 ms drop at a change %.1f dB\n", tails, drop);
        assert(tails == 3 && drop < 3);
    }
    {   /* Kerem 2026-10-01: the attack is an S-curve (raised cosine), not a straight line: a quarter in, it stands at
           ~0.15 of full (a line: 0.25) - long attacks fade in instead of arriving at the end */
        sampler::Resonator r; r.init(48000); r.att = 1;
        const std::vector<float> wind = noise_src(2, 0.5f, 5); const float *p[1] = { wind.data() }; r.set_source(1, (long long)wind.size(), p);
        r.attack(220, 0, 0.5); std::vector<float> a(12000, 0.0f), c(12000, 0.0f); r.render(a.data(), c.data(), 12000, 0);
        std::printf("resonator attack: a quarter in at %.3f of full\n", r.v[0].env);
        assert(r.v[0].env > 0.13 && r.v[0].env < 0.16);
    }
    {   /* 2b: the Harmonic filter and the Formant, every method in `methods` (spec "How we know it works") */
        const int methods[] = { sampler::BANK, sampler::SPECTRAL, sampler::COMB };
        const std::vector<float> wind = noise_src(16, 0.5f, 101);
        std::vector<float> clicks(65536, 0.0f); clicks[0] = 0.5f;          /* a repeating impulse: the response's exact peak */
        std::vector<float> gust(48000 * 8, 0.0f); for (size_t i = 0; i < 48000 * 3; i++) gust[i] = wind[i];   /* 3 s, then silence past every 6 s render (Spectral reads 43 ms ahead: a 6 s file looped into its noise) */
        auto rms = [](const std::vector<float> &x, double a, double z) { double e = 0; size_t i0 = (size_t)(a * 48000), i1 = (size_t)(z * 48000); for (size_t i = i0; i < i1; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (i1 - i0) + 1e-30); };
        for (int m : methods) {
            const double tol = m == sampler::COMB ? 3 : 1;
            for (int syn : { sampler::HARMONIC, sampler::FORMANT }) {
                /* pitch: the peak of the response to a repeating impulse (a noise band 73 Hz wide has no 1-cent pitch for
                   8 s of noise to pin down; its peak does). Ringing from 55 Hz, Dry from 220 Hz (closer partials blur) */
                double worst = 0; double wf = 0; int wmode = 0;
                for (int mode : { sampler::DRY, sampler::RINGING })
                    for (double f : { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 }) {
                        if (mode == sampler::DRY && f < 220) continue;
                        const double col = syn == sampler::FORMANT ? 0.0 : 0.5;
                        double got = m == sampler::SPECTRAL && mode == sampler::DRY ? tone_peak(syn, m, mode, 0.9, col, f, 0.02)
                                   : periodic_peak(part_render(syn, m, mode, 0.9, col, 1, f, 65536 * 4 / 48000.0, clicks), 48000, f, 0.03);
                        double e = std::fabs(1200 * std::log2(got / f)); if (e > worst) { worst = e; wf = f; wmode = mode; }
                    }
                std::printf("2b method %d synth %d: worst pitch error %.2f cents (%.0f Hz, mode %d)\n", m, syn, worst, wf, wmode);
                /* Spectral Dry: its band's top is flat to 0.06 dB over +-4 Hz at 220 Hz (measured), so 2 cents */
                assert(worst <= (m == sampler::SPECTRAL && wmode == sampler::DRY ? 2 : tol));
                /* Dry follows the recording: down 60 dB within 30 ms (Spectral: one frame, 43 ms) of silence */
                std::vector<float> d = part_render(syn, m, sampler::DRY, 1.0, 0.5, 1, 440, 5, gust);
                const double lim = m == sampler::SPECTRAL ? 0.043 : 0.030, drop = rms(d, 2.5, 3.0) - rms(d, 3.0 + lim, 3.0 + lim + 0.01);
                std::printf("2b method %d synth %d: Dry down %.1f dB %.0f ms after the recording stops\n", m, syn, drop, lim * 1000);
                assert(drop >= 60);
                /* Ringing sustains its Focus T60 within 20 % */
                std::vector<float> g = part_render(syn, m, sampler::RINGING, 0.5, 0.5, 1, 440, 6, gust);
                std::vector<float> tail(g.begin() + 3 * 48000, g.end());
                double t60 = t60_of(tail), want = sampler::Resonator::t60(0.5);
                std::printf("2b method %d synth %d: Ringing T60 %.2f s (want %.2f)\n", m, syn, t60, want);
                assert(std::fabs(t60 / want - 1) <= 0.2);
                /* level within 1 dB of the recording, settled; and no swell at the onset (the first 3 s <= settled + 3 dB) */
                for (int mode : { sampler::DRY, sampler::RINGING }) {
                    std::vector<float> o = part_render(syn, m, mode, 0.7, 0.5, 1, 220, 15, wind), dry = part_render(syn, m, mode, 0.7, 0.5, 0, 220, 15, wind);
                    double set = rms(o, 10, 14), ref = rms(dry, 10, 14), onset = -200, early = rms(o, 0.25, 2);
                    onset = rms(o, 0, 3);   /* as a whole: a 0.7 Hz-wide partial's level wanders +-2.5 dB second to second all through a note (measured), the gain only ~1 dB */
                    std::printf("2b method %d synth %d mode %d: level %.2f dB vs the recording %.2f, onset peak %.2f, 0.25-2 s %.2f\n", m, syn, mode, set, ref, onset, early);
                    /* final review #4: nor a fade-up - the first seconds within 2 dB under the settled level */
                    assert(std::fabs(set - ref) <= 1 && onset <= set + 3 && early >= set - 2);
                }
            }
            /* Harmonic filter: Colour 1 lifts partials 4-8 over the fundamental by >= 12 dB against Colour 0 */
            auto lift = [&](double col) {
                std::vector<float> o = part_render(sampler::HARMONIC, m, sampler::RINGING, 0.9, col, 1, 220, 10, wind);
                std::vector<float> t(o.end() - 65536, o.end());
                /* the loudest point within +-0.5 % of n f: Comb Ringing is a string loop, its upper partials a little off n f
                   and ~0.3 Hz wide at Focus 0.9 - read at n f exactly they vanished */
                auto band = [&](double f) { double mx = 0; for (double q = f * 0.995; q <= f * 1.005; q += f * 0.0005) mx = std::max(mx, peak_amp(t, 48000, q)); return 20 * std::log10(mx); };
                double hi = 0; for (int n = 4; n <= 8; n++) hi += band(220.0 * n) / 5;
                return hi - band(220);
            };
            double l0 = lift(0), l1 = lift(1);
            std::printf("2b method %d: Harmonic filter partials 4-8 vs fundamental %.1f dB at Colour 0, %.1f at 1\n", m, l0, l1);
            assert(l1 - l0 >= 12);
            /* Formant: among partials 2-16 the loudest is the one Colour names (+-1) */
            int ok = 0;
            for (int want : { 3, 6, 10, 14 }) {
                std::vector<float> o = part_render(sampler::FORMANT, m, sampler::RINGING, 0.9, (want - 1) / 15.0, 1, 110, 10, wind);
                std::vector<float> t(o.end() - 65536, o.end()); int best = 2;
                auto at = [&](double f) { double mx = 0; for (double q = f * 0.995; q <= f * 1.005; q += f * 0.0005) mx = std::max(mx, peak_amp(t, 48000, q)); return mx; };   /* as the lift's */
                double bm = 0; for (int n = 2; n <= 16; n++) { double a = at(110.0 * n); if (a > bm) { bm = a; best = n; } }
                ok += std::abs(best - want) <= 1;
            }
            std::printf("2b method %d: Formant peak on the named partial %d/4\n", m, ok);
            assert(ok == 4);
        }
    }
    {   /* 2b: a Synth / Method / Mode / Focus / Colour change while a note sounds touches only the next note (bit-identical) */
        const std::vector<float> wind = noise_src(4, 0.5f, 7); const float *wp[1] = { wind.data() };
        auto run = [&](bool change) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), wp);
            r.synth = sampler::FORMANT; r.method = sampler::BANK; r.mode = sampler::RINGING; r.focus = 0.8; r.colour = 0.4;
            r.attack(220, 0, 0.5); std::vector<float> a(48000 * 2, 0.0f), b(a.size(), 0.0f);
            for (size_t i = 0; i < a.size(); i += 128) {
                if (change && i == 48000) { r.synth = sampler::HARMONIC; r.method = sampler::COMB; r.mode = sampler::DRY; r.focus = 0.1; r.colour = 0.9; }
                r.render(a.data() + i, b.data() + i, 128, i / 48000.0);
            }
            return a;
        };
        assert(run(false) == run(true));
        /* stability: every partial synth x mode x Colour, extreme and ordinary notes, Focus 1, full-scale noise */
        const std::vector<float> loud = noise_src(2, 1.0f, 9);
        int bad = 0;
        /* final review #1: every Focus and the low register (a Bank nudge diverged at 80-165 Hz, default settings) */
        for (int m : { sampler::BANK, sampler::SPECTRAL, sampler::COMB }) for (int syn : { sampler::HARMONIC, sampler::FORMANT }) for (int mode : { sampler::DRY, sampler::RINGING })
            for (double foc : { 0.0, 0.5, 1.0 }) for (double col : { 0.0, 0.5, 1.0 }) for (double f : { 5.0, 41.0, 80.0, 130.8, 3500.0, 12000.0 }) {
                std::vector<float> o = part_render(syn, m, mode, foc, col, 1, f, 1, loud);
                double mx = 0; bool fin = true; for (float v : o) { fin = fin && std::isfinite(v); mx = std::max(mx, (double)std::fabs(v)); }
                if (!fin || mx >= 4) { std::printf("  2b runaway: method %d synth %d mode %d colour %.1f %.0f Hz: peak %.3g\n", m, syn, mode, col, f, mx); bad++; }
            }
        std::printf("2b stability sweep: %d runaways\n", bad);
        assert(bad == 0);
        /* every Bank band on a fine grid: poles inside the unit circle, centre within (0, sr / 2) */
        int badband = 0;
        for (int syn : { sampler::HARMONIC, sampler::FORMANT }) for (int mode : { sampler::DRY, sampler::RINGING })
            for (double foc = 0; foc <= 1.001; foc += 0.25) for (double col = 0; col <= 1.001; col += 0.25) for (double f = 30; f < 4000; f *= 1.07) {
                sampler::Resonator r; r.init(48000); r.synth = syn; r.method = sampler::BANK; r.mode = mode; r.focus = foc; r.colour = col;
                r.set_source(1, (long long)loud.size(), wp); r.attack(f, 0, 0.5);
                const sampler::Voice &x = r.v[0];
                for (int k = 0; k < x.np; k++) {
                    const double cw = -x.pa1[k] / (1 + x.pa2[k]);          /* cos of the centre */
                    if (!(x.pa2[k] >= 0 && x.pa2[k] < 1 && std::fabs(cw) < 1 && std::isfinite(x.agc))) { if (!badband) std::printf("  bad band: synth %d mode %d focus %.2f colour %.2f %.1f Hz partial %d\n", syn, mode, foc, col, f, k + 1); badband++; }
                }
            }
        std::printf("2b bank grid: %d bad bands\n", badband);
        assert(badband == 0);
        /* budget: 24 voices of each method inside one 128-sample block's budget */
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        for (int m : { sampler::BANK, sampler::SPECTRAL, sampler::COMB }) {
            sampler::Resonator sp; sp.init(48000); sp.set_source(1, (long long)wind.size(), wp);
            sp.synth = sampler::HARMONIC; sp.method = m; sp.mode = sampler::RINGING;
            for (int k = 0; k < sampler::Resonator::VOICES; k++) sp.attack(55 * (k + 1), 0, 0.05);
            std::vector<double> ms; std::vector<float> b1(128), b2(128);
            for (int k = 0; k < (int)(10 * 48000 / 128); k++) {
                std::fill(b1.begin(), b1.end(), 0.0f); std::fill(b2.begin(), b2.end(), 0.0f);
                auto c0 = std::chrono::steady_clock::now(); sp.render(b1.data(), b2.data(), 128, k * 128 / 48000.0);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
            }
            std::sort(ms.begin(), ms.end());
            std::printf("2b method %d: 24 voices, 99.9%% of blocks within %.3f ms (budget %.2f)\n", m, ms[(size_t)(ms.size() * 0.999)], 1.33 * slack);
            assert(ms[(size_t)(ms.size() * 0.999)] < 1.33 * slack);
            /* final review #2: low Dry chords starting - 8 notes at once, the starts timed with their block */
            sampler::Resonator st; st.init(48000); st.set_source(1, (long long)wind.size(), wp);
            st.synth = sampler::HARMONIC; st.method = m; st.mode = sampler::DRY; st.focus = 0.5;
            double worst = 0;
            for (int c = 0; c < 12; c++) {
                const double t = c * 0.5; std::vector<float> b1(128, 0.0f), b2(128, 0.0f);
                auto c0 = std::chrono::steady_clock::now();
                for (int k = 0; k < 8; k++) { st.attack(55 * std::pow(2.0, (k + c % 3) / 7.0), t, 0.1); st.release(t + 0.4); }
                st.render(b1.data(), b2.data(), 128, t);
                worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
                for (int i = 1; i < (int)(0.5 * 48000 / 128); i++) st.render(b1.data(), b2.data(), 128, t + i * 128 / 48000.0);
            }
            std::printf("2b method %d: a low 8-note Dry chord starting, worst block %.3f ms (budget %.2f)\n", m, worst, 1.33 * slack);
            assert(worst < 1.33 * slack);
            /* final review #3: 24 voices whose start times line their frames up (voice k starting k x 512 / 24 samples in - steals at
               arbitrary times can land like this) still spread their frames */
            sampler::Resonator sc; sc.init(48000); sc.set_source(1, (long long)wind.size(), wp);
            sc.synth = sampler::HARMONIC; sc.method = m; sc.mode = sampler::RINGING;
            for (int k = 0; k < sampler::Resonator::VOICES; k++) sc.attack(55 * (k + 1), (k * sampler::SPH / sampler::Resonator::VOICES) / 48000.0, 0.05);
            std::vector<double> ms2; std::vector<float> c1(128), c2(128);
            for (int k = 0; k < (int)(6 * 48000 / 128); k++) {
                std::fill(c1.begin(), c1.end(), 0.0f);
                auto c0 = std::chrono::steady_clock::now(); sc.render(c1.data(), c2.data(), 128, k * 128 / 48000.0);
                if (k * 128 > 48000 / 2) ms2.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
            }
            std::sort(ms2.begin(), ms2.end());
            std::printf("2b method %d: 24 voices started apart, 99.9%% of blocks within %.3f ms\n", m, ms2[(size_t)(ms2.size() * 0.999)]);
            assert(ms2[(size_t)(ms2.size() * 0.999)] < 1.33 * slack);
        }
    }
    {   /* 2c: Pulsar - grains of the recording fired f times a second (spec 2c "How we know it works") */
        const std::vector<float> wind = noise_src(16, 0.5f, 202); const float *wp[1] = { wind.data() };
        auto rms = [](const std::vector<float> &x, double a, double z) { double e = 0; size_t i0 = (size_t)(a * 48000), i1 = (size_t)(z * 48000); for (size_t i = i0; i < i1; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (i1 - i0) + 1e-30); };
        auto bandmax = [](const std::vector<float> &t, double f) { double mx = 0; for (double q = f * 0.995; q <= f * 1.005; q += f * 0.0005) mx = std::max(mx, peak_amp(t, 48000, q)); return 20 * std::log10(mx); };
        /* pitch: Focus 1 (one grain repeated) is a set of pure lines */
        double worst = 0, wf = 0;
        for (double f : { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 }) {
            std::vector<float> o = part_render(sampler::PULSAR, 0, 0, 1.0, 0.5, 1, f, 8, wind);
            double e = std::fabs(1200 * std::log2(line_peak(o, 48000, f, 0.02) / f)); if (e > worst) { worst = e; wf = f; }
        }
        std::printf("2c pulsar: worst pitch error %.3f cents (%.0f Hz)\n", worst, wf);
        assert(worst <= 1);
        /* colour: short grains (Colour 0) lift partials 4-8 against the fundamental >= 6 dB over long ones (Colour 1) */
        auto lift = [&](double col) { std::vector<float> o = part_render(sampler::PULSAR, 0, 0, 1.0, col, 1, 220, 6, wind); std::vector<float> t(o.end() - 65536, o.end());
            double hi = 0; for (int n = 4; n <= 8; n++) hi += bandmax(t, 220.0 * n) / 5; return hi - bandmax(t, 220); };
        const double l0 = lift(0), l1 = lift(1);
        std::printf("2c pulsar: partials 4-8 vs fundamental %.1f dB at Colour 0, %.1f at 1\n", l0, l1);
        assert(l0 - l1 >= 6);
        /* content: Focus 1 repeats one grain (power on the harmonics), Focus 0 refreshes it every grain (noise between) */
        auto on_lines = [&](double foc) {
            std::vector<float> o = part_render(sampler::PULSAR, 0, 0, foc, 0.5, 1, 220, 4, wind);
            const int N = 65536; FFT fft; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
            std::vector<float> b(4 * N); float *ar = b.data(), *ai = ar + N, *br = ai + N, *bi = br + N; size_t s0 = o.size() - N;
            for (int i = 0; i < N; i++) { ar[i] = (float)(o[s0 + i] * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N))); ai[i] = 0; }
            for (int q = 0; q < fft.passes; q++) { if (q % 2 == 0) fft.pass(q, ar, ai, br, bi, 0, fft.butterflies(q)); else fft.pass(q, br, bi, ar, ai, 0, fft.butterflies(q)); }
            const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
            double all = 0, lines = 0;
            for (int k = 1; k < N / 2; k++) { const double pw = (double)re[k] * re[k] + (double)im[k] * im[k]; all += pw;
                const double h = k * 48000.0 / N / 220.0; if (std::fabs(h - std::round(h)) * 220.0 * N / 48000.0 <= 2 && std::round(h) >= 1) lines += pw; }
            return lines / all;
        };
        const double c1 = on_lines(1.0), c0 = on_lines(0.0);
        std::printf("2c pulsar: power on the harmonics %.3f at Focus 1, %.3f at Focus 0\n", c1, c0);
        assert(c1 >= 0.9 && c0 < 0.6);
        /* level: within 1 dB of the recording, settled; no swell, no fade-up */
        {
            std::vector<float> o = part_render(sampler::PULSAR, 0, 0, 0.5, 0.5, 1, 220, 15, wind), dry = part_render(sampler::PULSAR, 0, 0, 0.5, 0.5, 0, 220, 15, wind);
            const double set = rms(o, 10, 14), ref = rms(dry, 10, 14), onset = rms(o, 0, 3), early = rms(o, 0.25, 2);
            std::printf("2c pulsar: level %.2f dB vs the recording %.2f, first 3 s %.2f, 0.25-2 s %.2f\n", set, ref, onset, early);
            /* the start within 1 dB: a lab chord lasts ~2.7 s, so a 3 s creep is heard as "low" (Kerem 2026-10-04) */
            assert(std::fabs(set - ref) <= 1 && onset <= set + 3 && early >= set - 1);
        }
        /* stability: every Focus x Colour, extreme notes, full-scale noise and a 0.3 s looped file */
        const std::vector<float> loud = noise_src(2, 1.0f, 9), shortf = noise_src(0.3, 1.0f, 4);
        int bad = 0;
        for (const std::vector<float> *sf : { &loud, &shortf }) for (double foc : { 0.0, 0.5, 1.0 }) for (double col : { 0.0, 0.5, 1.0 }) for (double f : { 5.0, 41.0, 80.0, 3500.0, 12000.0 }) {
            std::vector<float> o = part_render(sampler::PULSAR, 0, 0, foc, col, 1, f, 1, *sf);
            double mx = 0; bool fin = true; for (float v : o) { fin = fin && std::isfinite(v); mx = std::max(mx, (double)std::fabs(v)); }
            if (!fin || mx >= 4) { std::printf("  2c pulsar runaway: focus %.1f colour %.1f %.0f Hz peak %.3g\n", foc, col, f, mx); bad++; }
        }
        std::printf("2c pulsar stability: %d runaways\n", bad);
        assert(bad == 0);
        /* budget: 24 voices; 8-note chords starting; starts lined up */
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        auto timed = [&](sampler::Resonator &r, double from, double secs) { std::vector<double> ms; std::vector<float> b1(128), b2(128);
            for (int k = 0; k < (int)(secs * 48000 / 128); k++) { std::fill(b1.begin(), b1.end(), 0.0f); auto c0 = std::chrono::steady_clock::now(); r.render(b1.data(), b2.data(), 128, from + k * 128 / 48000.0);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count()); }
            std::sort(ms.begin(), ms.end()); return ms[(size_t)(ms.size() * 0.999)]; };
        sampler::Resonator a; a.init(48000); a.set_source(1, (long long)wind.size(), wp); a.synth = sampler::PULSAR;
        for (int k = 0; k < sampler::Resonator::VOICES; k++) a.attack(55 * (k + 1), 0, 0.05);
        const double t1 = timed(a, 0, 6);
        sampler::Resonator st; st.init(48000); st.set_source(1, (long long)wind.size(), wp); st.synth = sampler::PULSAR;
        double worstb = 0;
        for (int c = 0; c < 12; c++) { const double t = c * 0.5; std::vector<float> b1(128, 0.0f), b2(128, 0.0f); auto c0 = std::chrono::steady_clock::now();
            for (int k = 0; k < 8; k++) { st.attack(55 * std::pow(2.0, (k + c % 3) / 7.0), t, 0.1); st.release(t + 0.4); }
            st.render(b1.data(), b2.data(), 128, t); worstb = std::max(worstb, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
            for (int i = 1; i < (int)(0.5 * 48000 / 128); i++) st.render(b1.data(), b2.data(), 128, t + i * 128 / 48000.0); }
        std::printf("2c pulsar: 24 voices %.3f ms, an 8-note chord starting %.3f ms (budget %.2f)\n", t1, worstb, 1.33 * slack);
        assert(t1 < 1.33 * slack && worstb < 1.33 * slack);
    }
    {   /* 2c: Freeze - one moment's colour placed on the note's partials, held (spec 2c "How we know it works") */
        const std::vector<float> wind = noise_src(16, 0.5f, 303); const float *wp[1] = { wind.data() };
        auto rms = [](const std::vector<float> &x, double a, double z) { double e = 0; size_t i0 = (size_t)(a * 48000), i1 = (size_t)(z * 48000); for (size_t i = i0; i < i1; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (i1 - i0) + 1e-30); };
        auto bandmax = [](const std::vector<float> &t, double f) { double mx = 0; for (double q = f * 0.995; q <= f * 1.005; q += f * 0.0005) mx = std::max(mx, peak_amp(t, 48000, q)); return 20 * std::log10(mx); };
        /* pitch at Focus 1 (one clean line per partial) */
        double worst = 0, wf = 0;
        for (double f : { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 }) {
            std::vector<float> o = part_render(sampler::FREEZE, 0, 0, 1.0, 0.3, 1, f, 6, wind);
            double e = std::fabs(1200 * std::log2(line_peak(o, 48000, f, 0.02) / f)); if (e > worst) { worst = e; wf = f; }
        }
        std::printf("2c freeze: worst pitch error %.3f cents (%.0f Hz)\n", worst, wf);
        assert(worst <= 1);
        /* the moment: a dark first half, a bright second half - two moments, two colours */
        std::vector<float> dk((size_t)(48000 * 10)); { double lp = 0; for (size_t i = 0; i < dk.size(); i++) { if (i < dk.size() / 2) { lp = 0.95 * lp + 0.05 * wind[i] * 6; dk[i] = (float)lp; } else dk[i] = wind[i]; } }
        auto balance = [&](double col) { std::vector<float> o = part_render(sampler::FREEZE, 0, 0, 1.0, col, 1, 220, 5, dk); std::vector<float> t(o.end() - 65536, o.end());
            double hi = 0; for (int n = 4; n <= 8; n++) hi += bandmax(t, 220.0 * n) / 5; return hi - bandmax(t, 220); };
        const double bd = balance(0.1), bb = balance(0.9);
        std::printf("2c freeze: partials 4-8 vs fundamental %.1f dB at a dark moment, %.1f at a bright one\n", bd, bb);
        assert(bb - bd >= 6);
        /* holds: the recording falls silent under a held note - its level stays */
        std::vector<float> gust(48000 * 8, 0.0f); for (size_t i = 0; i < 48000 * 3; i++) gust[i] = wind[i];
        { std::vector<float> o = part_render(sampler::FREEZE, 0, 0, 0.7, 0.0, 1, 220, 6, gust);
          const double a = rms(o, 1, 2.5), b = rms(o, 4, 5.5);
          std::printf("2c freeze: %.2f dB while the recording sounds, %.2f after it falls silent\n", a, b);
          assert(std::fabs(a - b) <= 0.5); }
        /* purity: Focus 0 spreads a partial over >= 3x its Focus 1 width (-6 dB, averaged spectrum) */
        auto width = [&](double foc) {
            std::vector<float> o = part_render(sampler::FREEZE, 0, 0, foc, 0.3, 1, 220, 8, wind);
            const int N = 65536; FFT fft; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
            std::vector<float> b(4 * N); float *ar = b.data(), *ai = ar + N, *br = ai + N, *bi = br + N; std::vector<double> pw(N / 2, 0.0);
            for (size_t s0 = o.size() - (size_t)(4 * 48000); s0 + N <= o.size(); s0 += N / 2) {
                for (int i = 0; i < N; i++) { ar[i] = (float)(o[s0 + i] * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N))); ai[i] = 0; }
                for (int q = 0; q < fft.passes; q++) { if (q % 2 == 0) fft.pass(q, ar, ai, br, bi, 0, fft.butterflies(q)); else fft.pass(q, br, bi, ar, ai, 0, fft.butterflies(q)); }
                const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
                for (int k = 0; k < N / 2; k++) pw[k] += (double)re[k] * re[k] + (double)im[k] * im[k];
            }
            int pk = (int)(220.0 * 0.97 * N / 48000); for (int k = pk; k <= (int)(220.0 * 1.03 * N / 48000); k++) if (pw[k] > pw[pk]) pk = k;
            int l = pk, r = pk; while (l > 1 && pw[l - 1] > pw[pk] / 4) l--; while (r < N / 2 - 2 && pw[r + 1] > pw[pk] / 4) r++;
            return (r - l + 1) * 48000.0 / N;
        };
        const double w1 = width(1.0), w0 = width(0.0);
        std::printf("2c freeze: -6 dB width %.1f Hz at Focus 1, %.1f at Focus 0\n", w1, w0);
        assert(w0 >= 3 * w1);
        /* level: at the moment's own on a steady recording; no swell, no fade-up */
        { std::vector<float> o = part_render(sampler::FREEZE, 0, 0, 0.5, 0.3, 1, 220, 12, wind), dry = part_render(sampler::FREEZE, 0, 0, 0.5, 0.3, 0, 220, 12, wind);
          const double set = rms(o, 8, 11), ref = rms(dry, 8, 11), onset = rms(o, 0, 3), early = rms(o, 0.25, 2);
          std::printf("2c freeze: level %.2f dB vs the recording %.2f, first 3 s %.2f, 0.25-2 s %.2f\n", set, ref, onset, early);
          /* 3 dB over the moment (Kerem 2026-10-04: "Freeze can be more ... the outcome sound is low in volume"; the lab measured it
             3 dB under the Resonator on the same chord) */
          assert(std::fabs(set - ref - 3) <= 1 && onset <= set + 3 && early >= set - 2); }
        /* a silent moment stays silent */
        { std::vector<float> sil(48000 * 4, 0.0f); for (size_t i = 48000 * 2; i < sil.size(); i++) sil[i] = wind[i];
          std::vector<float> o = part_render(sampler::FREEZE, 0, 0, 0.5, 0.0, 1, 220, 3, sil);
          std::printf("2c freeze: a silent moment plays at %.1f dB\n", rms(o, 0.5, 2.5));
          assert(rms(o, 0.5, 2.5) < -100); }
        /* edges: recordings no longer than a frame, the moment at the very end */
        for (size_t len : { (size_t)1000, (size_t)2048 }) { std::vector<float> e(wind.begin(), wind.begin() + len); std::vector<float> o = part_render(sampler::FREEZE, 0, 0, 0.5, 1.0, 1, 220, 1, e);
          for (float v : o) assert(std::isfinite(v)); }
        /* stability */
        const std::vector<float> loud = noise_src(2, 1.0f, 9), shortf = noise_src(0.3, 1.0f, 4);
        int bad = 0;
        for (const std::vector<float> *sf : { &loud, &shortf }) for (double foc : { 0.0, 0.5, 1.0 }) for (double col : { 0.0, 0.5, 1.0 }) for (double f : { 5.0, 41.0, 80.0, 3500.0, 12000.0 }) {
            std::vector<float> o = part_render(sampler::FREEZE, 0, 0, foc, col, 1, f, 1, *sf);
            double mx = 0; bool fin = true; for (float v : o) { fin = fin && std::isfinite(v); mx = std::max(mx, (double)std::fabs(v)); }
            if (!fin || mx >= 4) { std::printf("  2c freeze runaway: focus %.1f colour %.1f %.0f Hz peak %.3g\n", foc, col, f, mx); bad++; }
        }
        std::printf("2c freeze stability: %d runaways\n", bad);
        assert(bad == 0);
        /* budget: 24 voices; 8 starts in one block; starts lined up */
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        auto timed = [&](sampler::Resonator &r, double from, double secs) { std::vector<double> ms; std::vector<float> b1(128), b2(128);
            for (int k = 0; k < (int)(secs * 48000 / 128); k++) { std::fill(b1.begin(), b1.end(), 0.0f); auto c0 = std::chrono::steady_clock::now(); r.render(b1.data(), b2.data(), 128, from + k * 128 / 48000.0);
                if (from + k * 128 / 48000.0 > 0.5) ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count()); }
            std::sort(ms.begin(), ms.end()); return ms[(size_t)(ms.size() * 0.999)]; };
        sampler::Resonator a; a.init(48000); a.set_source(1, (long long)wind.size(), wp); a.synth = sampler::FREEZE;
        for (int k = 0; k < sampler::Resonator::VOICES; k++) a.attack(55 * (k + 1), 0, 0.05);
        const double t1 = timed(a, 0, 6);
        sampler::Resonator lu; lu.init(48000); lu.set_source(1, (long long)wind.size(), wp); lu.synth = sampler::FREEZE;
        for (int k = 0; k < sampler::Resonator::VOICES; k++) lu.attack(55 * (k + 1), (k * sampler::SPH / sampler::Resonator::VOICES) / 48000.0, 0.05);
        const double t2 = timed(lu, 0, 6);
        sampler::Resonator st; st.init(48000); st.set_source(1, (long long)wind.size(), wp); st.synth = sampler::FREEZE;
        double worstb = 0;
        for (int c = 0; c < 12; c++) { const double t = c * 0.5; std::vector<float> b1(128, 0.0f), b2(128, 0.0f); auto c0 = std::chrono::steady_clock::now();
            for (int k = 0; k < 8; k++) { st.attack(55 * std::pow(2.0, (k + c % 3) / 7.0), t, 0.1); st.release(t + 0.4); }
            st.render(b1.data(), b2.data(), 128, t); worstb = std::max(worstb, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
            for (int i = 1; i < (int)(0.5 * 48000 / 128); i++) st.render(b1.data(), b2.data(), 128, t + i * 128 / 48000.0); }
        std::printf("2c freeze: 24 voices %.3f ms, lined up %.3f ms, an 8-note chord starting %.3f ms (budget %.2f)\n", t1, t2, worstb, 1.33 * slack);
        assert(t1 < 1.33 * slack && t2 < 1.33 * slack && worstb < 1.33 * slack);
    }
    {   /* 2c final review #1: a voice Freeze used leaves no state behind - a Spectral Ringing note after it starts as on a
           fresh voice (Freeze's phases were read as held levels: +24.5 dB, measured by the reviewer) */
        std::vector<float> quiet = noise_src(6, 0.01f, 41); const float *qp[1] = { quiet.data() };
        auto play = [&](bool after_freeze) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)quiet.size(), qp); r.rel = 0.03;
            std::vector<float> a(48000 * 2, 0.0f), b(a.size(), 0.0f);
            if (after_freeze) { r.synth = sampler::FREEZE; r.focus = 0.2; r.attack(220, 0, 0.5); r.release(0.5);
                for (size_t i = 0; i < a.size(); i += 128) r.render(a.data() + i, b.data() + i, 128, i / 48000.0); }
            r.synth = sampler::HARMONIC; r.method = sampler::SPECTRAL; r.mode = sampler::RINGING; r.focus = 0.5;
            std::fill(a.begin(), a.end(), 0.0f); r.attack(220, 2.0, 0.5);
            for (size_t i = 0; i < a.size(); i += 128) r.render(a.data() + i, b.data() + i, 128, 2.0 + i / 48000.0);
            double e = 0; for (size_t i = 0; i < 48000 * 3 / 10; i++) e += (double)a[i] * a[i]; return 10 * std::log10(e / (48000 * 3 / 10) + 1e-30);
        };
        const double fresh = play(false), reused = play(true);
        std::printf("2c review: Spectral Ringing's first 0.3 s %.1f dB on a fresh voice, %.1f after Freeze\n", fresh, reused);
        assert(std::fabs(reused - fresh) <= 1);
    }
    {   /* 2c final review #2: Freeze starts at the moment's level at every Focus - not 3-5 dB loud at the ends (the spec's
           1 dB, on noise and on a tonal moment) */
        const std::vector<float> wind = noise_src(10, 0.5f, 77);
        std::vector<float> tone(48000 * 10); for (size_t i = 0; i < tone.size(); i++) { double v = 0; for (int n = 1; n <= 12; n++) v += std::sin(2 * 3.141592653589793 * 196.0 * n * i / 48000) / n; tone[i] = (float)(0.2 * v); }
        auto rms = [](const std::vector<float> &x, double a, double z) { double e = 0; size_t i0 = (size_t)(a * 48000), i1 = (size_t)(z * 48000); for (size_t i = i0; i < i1; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (i1 - i0) + 1e-30); };
        double worst = 0; std::string wcase;
        for (const std::vector<float> *sf : { &wind, (const std::vector<float> *)&tone }) for (double foc : { 0.0, 0.5, 1.0 }) for (double f : { 110.0, 220.0, 880.0 }) {
            std::vector<float> o = part_render(sampler::FREEZE, 0, 0, foc, 0.3, 1, f, 6, *sf);
            const double d = rms(o, 0.05, 1) - rms(o, 2, 6);   /* a 1 s window: at Focus 0 a few random-turning bins wander +-1 dB over 0.25 s (tone, 110 Hz) */
            if (std::fabs(d) > std::fabs(worst)) { worst = d; wcase = (sf == &wind ? "noise" : "tone") + std::string(" focus ") + std::to_string(foc).substr(0, 4) + " " + std::to_string((int)f) + " Hz"; }
        }
        std::printf("2c review: Freeze's start against its settled level, worst %.2f dB (%s)\n", worst, wcase.c_str());
        assert(std::fabs(worst) <= 1);
    }
    {   /* 3a: the pitch sampler (RETUNE) - a recording read at (target / its own pitch at that moment), from the analysis */
        const double SR = 48000;
        auto sine_rec = [&](double secs, std::function<double(double)> fhz, double amp) {   /* a sine whose pitch follows fhz(t) */
            std::vector<float> x((size_t)(secs * SR)); double ph = 0; for (size_t i = 0; i < x.size(); i++) { ph += 2 * 3.141592653589793 * fhz(i / SR) / SR; x[i] = (float)(amp * std::sin(ph)); } return x; };
        auto track_of = [&](double secs, std::function<double(double)> fhz, std::function<double(double)> conf, std::vector<float> &tf, std::vector<float> &tc) {
            tf.clear(); tc.clear(); for (double t = 0.01; t < secs; t += 0.02) { tf.push_back((float)fhz(t)); tc.push_back((float)conf(t)); } };
        auto play = [&](const std::vector<float> &rec, double f0, const std::vector<float> &tf, const std::vector<float> &tc, double f, double secs, double colour = 1, int nv = 24) {
            sampler::Resonator r; r.init(SR); const float *p[1] = { rec.data() }; r.set_source(1, (long long)rec.size(), p);
            r.set_track(f0, 0.02, tf.data(), tc.data(), (int)tf.size()); r.synth = sampler::RETUNE; r.colour = colour; r.nv = nv; r.att = 0.02; r.rel = 0.03;
            r.attack(f, 0, 0.5); std::vector<float> L((size_t)(secs * SR), 0.0f), R(L.size(), 0.0f);
            for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, (int)std::min<size_t>(128, L.size() - i), i / SR);
            return L; };
        /* the frequency of a sine-like signal over [a, z) s: upward zero crossings, interpolated */
        auto freq = [&](const std::vector<float> &x, double a, double z) {
            size_t i0 = (size_t)(a * SR), i1 = (size_t)(z * SR); double first = -1, last = -1; int n = 0;
            for (size_t i = i0 + 1; i < i1; i++) if (x[i - 1] < 0 && x[i] >= 0) { double t = (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; n++; }
            return n > 1 ? (n - 1) * SR / (last - first) : 0.0; };
        std::vector<float> tf, tc;
        /* steady: a harmonic tone at 440 */
        std::vector<float> tone((size_t)(4 * SR)); for (size_t i = 0; i < tone.size(); i++) { double v = 0; for (int n = 1; n <= 8; n++) v += std::sin(2 * 3.141592653589793 * 440 * n * i / SR) / n; tone[i] = (float)(0.3 * v); }
        track_of(4, [](double) { return 440.0; }, [](double) { return 0.95; }, tf, tc);
        double worst = 0; for (double f : { 220.0, 330.0, 660.0, 880.0 }) { std::vector<float> o = play(tone, 440, tf, tc, f, 2.2); worst = std::max(worst, std::fabs(1200 * std::log2(line_peak(o, SR, f, 0.02) / f))); }
        std::printf("3a retune: steady recording, worst %.3f cents\n", worst);
        assert(worst <= 1);
        /* drifting +-30 cents: the note stays on its pitch */
        auto drift = [](double t) { return 440.0 * std::pow(2.0, 30.0 / 1200 * std::sin(2 * 3.141592653589793 * 0.5 * t)); };
        std::vector<float> dr = sine_rec(5, drift, 0.5); track_of(5, drift, [](double) { return 0.95; }, tf, tc);
        { std::vector<float> o = play(dr, 440, tf, tc, 330, 3.5); double w = 0; for (double t = 1; t < 3; t += 0.05) w = std::max(w, std::fabs(1200 * std::log2(freq(o, t, t + 0.05) / 330)));
          std::printf("3a retune: drifting recording, worst %.2f cents off the note\n", w); assert(w <= 2); }
        /* unconfident moments: the track says 600 Hz with confidence 0.3 for 0.5 s - the speed holds */
        std::vector<float> st = sine_rec(4, [](double) { return 440.0; }, 0.5);
        track_of(4, [](double t) { return t > 0.75 && t < 1.25 ? 600.0 : 440.0; }, [](double t) { return t > 0.75 && t < 1.25 ? 0.3 : 0.95; }, tf, tc);
        { std::vector<float> o = play(st, 440, tf, tc, 330, 2.5); double w = 0; for (double t = 0.3; t < 2.2; t += 0.05) w = std::max(w, std::fabs(1200 * std::log2(freq(o, t, t + 0.05) / 330)));
          std::printf("3a retune: through unconfident moments, worst %.2f cents\n", w); assert(w <= 2); }
        /* unpitched: no pitch, no track - it still plays */
        { std::vector<float> nz = noise_src(3, 0.5f, 5), e1, e2; std::vector<float> o = play(nz, 0, e1, e2, 330, 1.5); double e = 0; bool fin = true;
          for (float v : o) { e += (double)v * v; fin = fin && std::isfinite(v); } const double db = 10 * std::log10(e / o.size() + 1e-30);
          std::printf("3a retune: an unpitched recording plays at %.1f dB\n", db); assert(fin && db > -40); }
        /* a one-shot's end: silence, faded (no step bigger than the tone's own) */
        { std::vector<float> sh = sine_rec(0.3, [](double) { return 440.0; }, 0.5); track_of(0.3, [](double) { return 440.0; }, [](double) { return 0.95; }, tf, tc);
          std::vector<float> o = play(sh, 440, tf, tc, 330, 1.0); double own = 0, all = 0, tail = 0;
          for (size_t i = 1; i < o.size(); i++) { const double d = std::fabs(o[i] - o[i - 1]); all = std::max(all, d); if (i > 2400 && i < 9600) own = std::max(own, d); if (i > (size_t)(0.5 * SR)) tail = std::max(tail, (double)std::fabs(o[i])); }
          std::printf("3a retune: one-shot end: largest step %.4f (the tone's own %.4f), after it %.2g\n", all, own, tail);
          assert(all <= 1.2 * own && tail < 1e-6); }
        /* brightness: Colour 0 darkens partials 4-8 against the fundamental by >= 10 dB */
        auto bal = [&](double col) { std::vector<float> o = play(tone, 440, tf, tc, 330, 2.2, col); std::vector<float> t(o.end() - 65536, o.end());
            double hi = 0; for (int n = 4; n <= 8; n++) hi += 20 * std::log10(peak_amp(t, SR, 330.0 * n)) / 5; return hi - 20 * std::log10(peak_amp(t, SR, 330)); };
        track_of(4, [](double) { return 440.0; }, [](double) { return 0.95; }, tf, tc);
        const double b0 = bal(0), b1 = bal(1);
        std::printf("3a retune: partials 4-8 vs fundamental %.1f dB dark, %.1f bright\n", b0, b1);
        assert(b1 - b0 >= 10);
        /* six voices: eight notes, six sound */
        { sampler::Resonator r; r.init(SR); const float *p[1] = { tone.data() }; r.set_source(1, (long long)tone.size(), p);
          r.set_track(440, 0.02, tf.data(), tc.data(), (int)tf.size()); r.synth = sampler::RETUNE; r.nv = 6;
          for (int k = 0; k < 8; k++) r.attack(220 * std::pow(2.0, k / 12.0), 0, 0.3);
          std::vector<float> L(4800), R(4800); r.render(L.data(), R.data(), 4800, 0);
          int on = 0; for (int i = 0; i < sampler::Resonator::VOICES; i++) on += r.v[i].active;
          std::printf("3a retune: 8 notes on 6 voices -> %d voices in use\n", on); assert(on == 6); }
    }
    {   /* 3a: the route engine plays sampler synths per role (spec 3a "How we know it works") */
        const double SR = 48000;
        struct TNote { int role; double f, dur, t; };
        struct TLog { std::vector<TNote> n; };
        auto logger = [](void *p, int role, double f, double dur, double t, double) { ((TLog *)p)->n.push_back({ role, f, dur, t }); };
        auto rms_db = [](const std::vector<float> &x, size_t a, size_t z) { double e = 0; for (size_t i = a; i < z && i < x.size(); i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / std::max<size_t>(1, z - a) + 1e-30); };
        std::vector<float> nz = noise_src(20, 0.5f, 61); const float *nzp[1] = { nz.data() };
        std::vector<float> tone20((size_t)(20 * SR)); for (size_t i = 0; i < tone20.size(); i++) { double v = 0; for (int k = 1; k <= 6; k++) v += std::sin(2 * 3.141592653589793 * 440 * k * i / SR) / k; tone20[i] = (float)(0.3 * v); }
        const float *tnp[1] = { tone20.data() };
        std::string tjson = "{\"f0\":440,\"hop_s\":0.02,\"track\":["; for (int k = 0; k < 1000; k++) tjson += std::string(k ? "," : "") + "[440,0.95,0]"; tjson += "]}";
        auto run = [&](const char *patch, bool rec, double secs, TLog &log, std::vector<float> &out) {
            unsigned seed = 4242; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128);
            fs_piece_test_hooks(d, fixed_draw, &seed, logger, &log);
            fs_piece_default_tuning(d, 1);
            if (rec) { fs_piece_role_source(d, 2, 1, (long long)tone20.size(), tnp); fs_piece_role_analysis(d, 2, tjson.c_str()); }
            int r = fs_piece_add_route(d, patch); fs_piece_walk(d, r, 0.3, 0);
            for (int i = 0; i < (int)(secs * SR / 128); i++) { fs_process(d, 128); const float *l = fs_out(d, 0); out.insert(out.end(), l, l + 128); }
            fs_destroy(d); };
        const char *only_v3 = "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"},{\"r\":10,\"q\":\"maj9\"},{\"r\":3,\"q\":\"6/9\"}],\"bed\":{\"on\":false},\"voice\":{\"on\":false},\"sect\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"synth\":\"s-retune\",\"warp\":0,\"drive\":0,\"sampler\":{\"colour\":1}}}";
        /* the route with the Third voice off: other layers of a route still sound (-26 dB, measured) - the baseline */
        TLog l0; std::vector<float> base; run("{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"},{\"r\":10,\"q\":\"maj9\"},{\"r\":3,\"q\":\"6/9\"}],\"bed\":{\"on\":false},\"voice\":{\"on\":false},\"sect\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"on\":false}}", false, 40, l0, base);
        const double base_db = rms_db(base, 0, base.size());
        /* no recording: the Third voice's notes are played (logged) but silent; finite */
        { TLog log; std::vector<float> o; run(only_v3, false, 20, log, o);
          int n3 = 0; for (auto &e : log.n) n3 += e.role == 3; bool fin = true; for (float v : o) fin = fin && std::isfinite(v);
          std::printf("3a route: no recording - %d Third-voice notes, output %.2f dB (the route without it %.2f)\n", n3, rms_db(o, 0, o.size()), rms_db(base, 0, o.size()));
          assert(n3 > 0 && fin && std::fabs(rms_db(o, 0, o.size()) - rms_db(base, 0, o.size())) < 0.05); }
        /* a recording: the Third voice's own sound (the render minus the same walk without it - fixed draws, so the rest
           is identical) - each long note on the engine's Hz within 1 cent */
        { TLog log, l2; std::vector<float> o, q; run(only_v3, true, 40, log, o); run(only_v3, false, 40, l2, q);
          std::vector<float> v3(o.size()); for (size_t i = 0; i < o.size(); i++) v3[i] = o[i] - q[i];
          int checked = 0; double worst = 0;
          for (auto &e : log.n) if (e.role == 3 && e.dur >= 2.0 && e.t + 1.9 < v3.size() / SR && checked < 4) {
              const size_t z = (size_t)((e.t + 1.9) * SR); std::vector<float> w(v3.begin() + (long)(z - 65536), v3.begin() + (long)z);
              /* 3c.1 F2: the note moved by octaves into a band where the recording has energy - the engine's own rule */
              sampler::Resonator fr; std::vector<float> sp; sampler::spectrum_of(tone20.data(), (long long)tone20.size(), SR, sp); fr.spec = sp.data(); fr.spec_n = (int)sp.size(); fr.spec_sr = SR;
              const double ef = fr.fold(e.f);
              const double got = line_peak(w, SR, ef, 0.01);
              worst = std::max(worst, std::fabs(1200 * std::log2(got / ef))); checked++; }
          std::printf("3a route: the sampler Third voice alone at %.1f dB; %d notes checked, worst %.3f cents off the engine's Hz\n", rms_db(v3, 0, v3.size()), checked, worst);
          assert(checked >= 2 && worst <= 1 && rms_db(v3, 0, v3.size()) > -70); }
        /* consonance: a digital triangle root and a sampled fifth (the pitch sampler on a steady harmonic tone) - in Just the
           root's 3rd partial and the fifth's 2nd coincide (flat envelope); in Equal they beat */
        { std::vector<float> tone((size_t)(4 * SR)); for (size_t i = 0; i < tone.size(); i++) { double v = 0; for (int k = 1; k <= 6; k++) v += std::sin(2 * 3.141592653589793 * 440 * k * i / SR) / k; tone[i] = (float)(0.3 * v); }
          std::vector<float> tf(200, 440.0f), tc(200, 0.95f);
          auto fluct = [&](double fifth) {
              tone::SimpleSynth dg; dg.init(SR); sampler::Resonator sm; sm.init(SR); const float *p[1] = { tone.data() }; sm.set_source(1, (long long)tone.size(), p);
              sm.set_track(440, 0.02, tf.data(), tc.data(), 200); sm.synth = sampler::RETUNE; sm.nv = 6; sm.focus = 0;   /* 3d: Position 0, from the start */
              dg.attack(220, 0, 0.5); sm.attack(fifth, 0, 0.5);
              std::vector<float> L((size_t)(4 * SR), 0.0f), R(L.size(), 0.0f);
              for (size_t i = 0; i < L.size(); i += 128) { dg.render(L.data() + i, R.data() + i, 128, i / SR); sm.render(L.data() + i, R.data() + i, 128, i / SR); }
              double lo = 1e9, hi = -1e9; for (double t = 1; t < 3.5; t += 0.05) { std::vector<float> w(L.begin() + (long)(t * SR), L.begin() + (long)((t + 0.05) * SR)); const double a = 20 * std::log10(peak_amp(w, SR, 660)); lo = std::min(lo, a); hi = std::max(hi, a); }
              return hi - lo; };
          const double just = fluct(330), equal = fluct(220 * std::pow(2.0, 7 / 12.0));
          std::printf("3a consonance: the 660 Hz meeting partial fluctuates %.2f dB in Just, %.2f dB in Equal\n", just, equal);
          assert(just < 1 && equal > 3); }
        /* a role switched digital -> sampler -> digital mid-walk: no step beyond what either sound makes on its own */
        { const char *dig = "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"},{\"r\":10,\"q\":\"maj9\"},{\"r\":3,\"q\":\"6/9\"}],\"bed\":{\"on\":false},\"voice\":{\"on\":false},\"sect\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"synth\":\"am\"}}";
          auto walk = [&](int sw1, int sw2) {   /* switch to the sampler at block sw1, back at sw2 (-1: never) */
              std::vector<float> o; unsigned seed = 9; TLog log; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128);
              fs_piece_test_hooks(d, fixed_draw, &seed, logger, &log); fs_piece_role_source(d, 2, 1, (long long)tone20.size(), tnp); fs_piece_role_analysis(d, 2, tjson.c_str());
              int r = fs_piece_add_route(d, sw1 == 0 ? only_v3 : dig); fs_piece_walk(d, r, 0.3, 0);
              for (int i = 0; i < (int)(30 * SR / 128); i++) {
                  if (sw1 > 0 && i == sw1) fs_piece_set_route(d, r, only_v3);
                  if (sw2 > 0 && i == sw2) fs_piece_set_route(d, r, dig);
                  fs_process(d, 128); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + 128); }
              fs_destroy(d);
              double step = 0; bool fin = true; for (size_t i = 1; i < o.size(); i++) { step = std::max(step, (double)std::fabs(o[i] - o[i - 1])); fin = fin && std::isfinite(o[i]); }
              return fin ? step : 1e9; };
          const double sw = walk((int)(10 * SR / 128), (int)(20 * SR / 128)), dig_only = walk(-1, -1), smp_only = walk(0, -1);
          std::printf("3a route: switching the Third voice digital -> sampler -> digital, largest step %.4f (digital alone %.4f, sampler alone %.4f)\n", sw, dig_only, smp_only);
          assert(sw <= 1.1 * std::max(dig_only, smp_only)); }
        /* budget: every role a Freeze (the costliest) with recordings, chords starting */
        { const char *all = "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"},{\"r\":10,\"q\":\"maj9\"},{\"r\":3,\"q\":\"6/9\"}],\"bed\":{\"on\":false},\"zones\":{\"on\":false},\"voice\":{\"synth\":\"s-freeze\"},\"sect\":{\"synth\":\"s-freeze\"},\"v3\":{\"synth\":\"s-freeze\"}}";
          unsigned seed = 5; TLog log; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128); fs_piece_test_hooks(d, fixed_draw, &seed, logger, &log);
          for (int ro = 0; ro < 3; ro++) fs_piece_role_source(d, ro, 1, (long long)nz.size(), nzp);
          int r = fs_piece_add_route(d, all); fs_piece_walk(d, r, 0.3, 0);
          std::vector<double> ms;
          for (int i = 0; i < (int)(30 * SR / 128); i++) { auto c0 = std::chrono::steady_clock::now(); fs_process(d, 128); if (i > 400) ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count()); }
          fs_destroy(d); std::sort(ms.begin(), ms.end());
#ifdef FS_TEST_O1
          const double slack = 1.5;
#else
          const double slack = 1;
#endif
          std::printf("3a route: every role a Freeze, 99.9%% of blocks within %.3f ms, worst %.3f (budget %.2f)\n", ms[(size_t)(ms.size() * 0.999)], ms.back(), 1.33 * slack);
          assert(ms[(size_t)(ms.size() * 0.999)] < 1.33 * slack); }
    }
    {   /* 3a final review: a sampler Voice role releases its held bass (C1: old bass notes piled up to six), and a switch
           away from a sampler releases every voice (I1: the others froze at full level and were cut 2.5 s later) */
        const double SR = 48000;
        std::vector<float> tone20((size_t)(20 * SR)); for (size_t i = 0; i < tone20.size(); i++) { double v = 0; for (int k = 1; k <= 6; k++) v += std::sin(2 * 3.141592653589793 * 440 * k * i / SR) / k; tone20[i] = (float)(0.3 * v); }
        const float *tnp[1] = { tone20.data() };
        std::string tjson = "{\"f0\":440,\"hop_s\":0.02,\"track\":["; for (int k = 0; k < 1000; k++) tjson += std::string(k ? "," : "") + "[440,0.95,0]"; tjson += "]}";
        struct TNote { int role; double f, dur, t; };
        struct TLog { std::vector<TNote> n; };
        auto logger = [](void *p, int role, double f, double dur, double t, double) { ((TLog *)p)->n.push_back({ role, f, dur, t }); };
        const std::string pre = "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"},{\"r\":10,\"q\":\"maj9\"},{\"r\":3,\"q\":\"6/9\"}],\"bed\":{\"on\":false},\"sect\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"on\":false},";
        const std::string smp = pre + "\"voice\":{\"synth\":\"s-retune\",\"warp\":0,\"drive\":0,\"top\":0,\"sampler\":{\"colour\":1}}}";
        const std::string dig = pre + "\"voice\":{\"synth\":\"fm\",\"warp\":0,\"drive\":0,\"top\":0}}";
        /* C1: after several chord changes, every earlier bass note (not among the notes of the last 4 s) is >= 40 dB under the
           bass note now sounding */
        { TLog log; std::vector<float> o; unsigned seed = 3; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128);
          fs_piece_test_hooks(d, fixed_draw, &seed, logger, &log); fs_piece_role_source(d, 0, 1, (long long)tone20.size(), tnp); fs_piece_role_analysis(d, 0, tjson.c_str());
          int r = fs_piece_add_route(d, smp.c_str()); fs_piece_walk(d, r, 0.3, 0);
          for (int i = 0; i < (int)(120 * SR / 128); i++) { if (i % 375 == 0) fs_piece_walk(d, r, 0.3 + 0.6 * i / (120 * SR / 128), 0); fs_process(d, 128); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + 128); }
          fs_destroy(d);
          int nb = 0; for (auto &e : log.n) nb += e.role == 0; std::printf("3a review C1: %d bass notes in 120 s walking\n", nb);
          const double tend = o.size() / SR; std::vector<float> w(o.end() - 65536, o.end());
          /* 3c.1 F2: each note sounds moved into a band where the recording has energy - looked for there (the engine's rule) */
          sampler::Resonator fr; std::vector<float> sp; sampler::spectrum_of(tone20.data(), (long long)tone20.size(), SR, sp); fr.spec = sp.data(); fr.spec_n = (int)sp.size(); fr.spec_sr = SR;
          auto fold = [&](double f) { return fr.fold(f); };
          double now_f = 0; for (auto &e : log.n) if (e.role == 0 && e.t < tend - 1.5) now_f = fold(e.f);
          std::vector<double> recent; for (auto &e : log.n) if (e.t > tend - 6) recent.push_back(fold(e.f));
          double worst = -200; int old = 0;
          for (auto &e : log.n) if (e.role == 0 && e.t < tend - 6) {
              const double ef = fold(e.f);
              bool near = std::fabs(1200 * std::log2(now_f / ef)) < 30; for (double q : recent) near = near || std::fabs(1200 * std::log2(q / ef)) < 30;
              if (near) continue; old++;
              worst = std::max(worst, 20 * std::log10(peak_amp(w, SR, ef) / peak_amp(w, SR, now_f))); }
          std::printf("3a review C1: %d earlier bass notes, the loudest %.1f dB against the bass now\n", old, worst);
          assert(old >= 1 && worst <= -40); }   /* 3c.1: folded, the walk's 3 bass notes meet on fewer distinct pitches */
        /* I1: a long Third-voice sampler note (its release past the switch + 2.5 s) when the role switches to fm: it fades
           with its release - 1.2-2.4 s later >= 40 dB under where it was (not frozen at full level until the 2.5 s cut) */
        { const std::string v3s = pre.substr(0, pre.find("\"v3\":{\"on\":false},")) + pre.substr(pre.find("\"v3\":{\"on\":false},") + std::string("\"v3\":{\"on\":false},").size());
          const std::string smp3 = v3s + "\"fx\":{\"delayWet\":0,\"revWet\":0},\"fx2\":{\"delayWet\":0,\"revWet\":0},\"fx3\":{\"delayWet\":0,\"revWet\":0},\"voice\":{\"on\":false},\"v3\":{\"synth\":\"s-retune\",\"warp\":0,\"drive\":0,\"sampler\":{\"colour\":1}}}";
          const std::string dig3 = v3s + "\"fx\":{\"delayWet\":0,\"revWet\":0},\"fx2\":{\"delayWet\":0,\"revWet\":0},\"fx3\":{\"delayWet\":0,\"revWet\":0},\"voice\":{\"on\":false},\"v3\":{\"synth\":\"fm\",\"warp\":0,\"drive\":0}}";
          auto walk = [&](double ts, TLog &log, std::vector<float> &o, bool rec = true) {
              unsigned seed = 3; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128);
              fs_piece_test_hooks(d, fixed_draw, &seed, logger, &log);
              if (rec) { fs_piece_role_source(d, 2, 1, (long long)tone20.size(), tnp); fs_piece_role_analysis(d, 2, tjson.c_str()); }
              int r = fs_piece_add_route(d, smp3.c_str()); fs_piece_walk(d, r, 0.3, 0);
              const int sw = ts > 0 ? (int)(ts * SR / 128) : -1;
              for (int i = 0; i < (int)(30 * SR / 128); i++) { if (i == sw) fs_piece_set_route(d, r, dig3.c_str()); fs_process(d, 128); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + 128); }
              fs_destroy(d); };
          TLog l0; std::vector<float> o0; walk(-1, l0, o0);
          double ts = -1, nf = 0; for (auto &e : l0.n) if (e.role == 3 && e.dur > 2.75 && e.t > 4 && e.t < 20) { ts = e.t + 0.1; nf = e.f; break; }
          assert(ts > 0);
          /* the sampler alone: the same walk and switch with its role silent (no recording) - fixed draws, so the rest is identical */
          TLog log, lq; std::vector<float> o, q; walk(ts, log, o); walk(ts, lq, q, false);
          std::vector<float> smp(o.size()); for (size_t i = 0; i < o.size(); i++) smp[i] = o[i] - q[i];
          auto rmsw = [&](double a0, double z) { double e2 = 0; for (size_t i = (size_t)(a0 * SR); i < (size_t)(z * SR); i++) e2 += (double)smp[i] * smp[i]; return 10 * std::log10(e2 / ((z - a0) * SR) + 1e-30); };
          const double drop = rmsw(ts + 1.2, ts + 2.4) - rmsw(ts - 0.7, ts - 0.05);
          std::printf("3a review I1: switch 0.1 s into a %.0f Hz sampler note; the sampler 1.2-2.4 s later is %.1f dB against before\n", nf, drop);
          assert(drop <= -40); }
        /* I1 at the synth: three long notes sounding when the role is switched away - release() (the last note only) leaves
           two at full level until the engine's 2.5 s cut; release_all() releases all three */
        { auto after = [&](bool all) {
              sampler::Resonator r; r.init(SR); r.set_source(1, (long long)tone20.size(), tnp); std::vector<float> tf(1000, 440.0f), tc(1000, 0.95f);
              r.set_track(440, 0.02, tf.data(), tc.data(), 1000); r.synth = sampler::RETUNE; r.nv = 6;
              for (double f : { 220.0, 277.18, 329.63 }) { r.attack(f, 0, 0.3); r.release(10); }
              std::vector<float> L((size_t)(3 * SR), 0.0f), R(L.size(), 0.0f);
              for (size_t i = 0; i < L.size(); i += 128) { if (i == (size_t)(1 * SR / 128) * 128) { if (all) r.release_all(1.0); else r.release(1.0); } r.render(L.data() + i, R.data() + i, 128, i / SR); }
              double e0 = 0, e1 = 0; for (size_t i = (size_t)(0.5 * SR); i < (size_t)(0.9 * SR); i++) e0 += (double)L[i] * L[i]; for (size_t i = (size_t)(2.2 * SR); i < (size_t)(2.6 * SR); i++) e1 += (double)L[i] * L[i];
              return 10 * std::log10((e1 + 1e-30) / (e0 + 1e-30)); };
          const double one = after(false), all = after(true);
          std::printf("3a review I1: three long notes 1.2-1.6 s after the switch: %.1f dB with release(), %.1f dB with release_all()\n", one, all);
          assert(one > -20 && all <= -60); }
    }
    {   /* 3a: each sampler synth sits its set amount over a digital synth (fm), within 1 dB (the sampler trims) */
        const double SR = 48000;
        std::vector<float> nz = noise_src(20, 0.5f, 61); const float *nzp[1] = { nz.data() };
        std::vector<float> tone20((size_t)(20 * SR)); for (size_t i = 0; i < tone20.size(); i++) { double v = 0; for (int k = 1; k <= 6; k++) v += std::sin(2 * 3.141592653589793 * 440 * k * i / SR) / k; tone20[i] = (float)(0.3 * v); }
        const float *tnp[1] = { tone20.data() };
        std::string tjson = "{\"f0\":440,\"hop_s\":0.02,\"track\":["; for (int k = 0; k < 1000; k++) tjson += std::string(k ? "," : "") + "[440,0.95,0]"; tjson += "]}";
        auto level = [&](const char *synth) {
            std::string patch = std::string("{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"},{\"r\":10,\"q\":\"maj9\"},{\"r\":3,\"q\":\"6/9\"}],")
                + "\"bed\":{\"on\":false},\"sect\":{\"on\":false},\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"" + synth + "\"}}";
            unsigned seed = 31; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128); fs_piece_test_hooks(d, fixed_draw, &seed, nullptr, nullptr);
            if (std::string(synth) == "s-retune" || std::string(synth) == "s-fm" || std::string(synth) == "s-am") { fs_piece_role_source(d, 0, 1, (long long)tone20.size(), tnp); fs_piece_role_analysis(d, 0, tjson.c_str()); }
            else fs_piece_role_source(d, 0, 1, (long long)nz.size(), nzp);
            int r = fs_piece_add_route(d, patch.c_str()); fs_piece_walk(d, r, 0.3, 0);
            double e = 0; long n = 0;
            for (int i = 0; i < (int)(30 * SR / 128); i++) { fs_process(d, 128); if (i > (int)(5 * SR / 128)) { const float *l = fs_out(d, 0); for (int k = 0; k < 128; k++) { e += (double)l[k] * l[k]; n++; } } }
            fs_destroy(d); return 10 * std::log10(e / n + 1e-30); };
        /* Kerem 2026-10-05: the samplers raised over fm, "x2 or more" (measured offsets, each at least ~6 dB) */
        const double ref = level("fm"), OVER[8] = { 8, 6, 6.5, 6.5, 6, 6, 11.3, 8 }; double worst = 0; int o = 0;
        for (const char *sy : { "s-retune", "s-resonator", "s-harmonic", "s-formant", "s-pulsar", "s-freeze", "s-fm", "s-am" }) {
            const double l = level(sy); std::printf("3a level: %-12s %+.2f dB against fm (%.1f), %+.1f wanted\n", sy, l - ref, ref, OVER[o]); worst = std::max(worst, std::fabs(l - ref - OVER[o++])); }
        assert(worst <= 1);
    }
    {   /* 2c guard: the Harmonic filter and the Formant render exactly as in 2b - a hash of every method x mode */
        const std::vector<float> wind = noise_src(3, 0.5f, 77); const float *p[1] = { wind.data() };
        uint64_t h = 1469598103934665603ull;
        for (int syn : { sampler::HARMONIC, sampler::FORMANT }) for (int m = 0; m < 3; m++) for (int mode = 0; mode < 2; mode++) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), p);
            r.synth = syn; r.method = m; r.mode = mode; r.focus = 0.7; r.colour = 0.4;
            r.attack(196, 0, 0.5); r.release(1.2);
            std::vector<float> a(48000 * 2, 0.0f), b(a.size(), 0.0f);
            for (size_t i = 0; i < a.size(); i += 128) r.render(a.data() + i, b.data() + i, 128, i / 48000.0);
            for (float s : a) { uint32_t u; std::memcpy(&u, &s, 4); h = (h ^ u) * 1099511628211ull; }
        }
        std::printf("2b hash %016llx\n", (unsigned long long)h);
        assert(h == 0x0d78bb74e4b92934ull);   /* the em++ -O1 test build */
    }
    {   /* 2b guard: the Resonator renders exactly as in 2a (P5) - a hash of every body x excite */
        const std::vector<float> wind = noise_src(3, 0.5f, 77); const float *p[1] = { wind.data() };
        uint64_t h = 1469598103934665603ull;
        for (int body = 0; body < 3; body++) for (int ex = 0; ex < 2; ex++) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), p);
            r.body = body; r.excite = ex; r.focus = 0.7; r.colour = 0.4;
            r.attack(196, 0, 0.5); r.release(1.2);
            std::vector<float> a(48000 * 2, 0.0f), b(a.size(), 0.0f);
            for (size_t i = 0; i < a.size(); i += 128) r.render(a.data() + i, b.data() + i, 128, i / 48000.0);
            for (float s : a) { uint32_t u; std::memcpy(&u, &s, 4); h = (h ^ u) * 1099511628211ull; }
        }
        std::printf("resonator 2a hash %016llx\n", (unsigned long long)h);
        assert(h == 0x41baaacdc5874316ull);   /* the em++ -O1 test build (no FMA in wasm); 2026-10-07: Plucked made up to level */
    }
    {   /* 2026-10-07 (Kerem: "Bell/Plucked ... very low volume, can't hear it"): every Resonator body x excite sounds within
           6 dB of String Bowed - RMS over a 2 s note on wind, at the default Focus/Colour and at Kerem's likely settings */
        const std::vector<float> wind = noise_src(3, 0.5f, 77);
        std::printf("6 resonator levels (dB vs String Bowed):");
        double worst = 0;
        for (double fo : { 0.5, 0.9 }) for (double f : { 110.0, 196.0, 440.0 }) {
            double ref = 0;
            for (int body = 0; body < 3; body++) for (int ex = 0; ex < 2; ex++) {
                const std::vector<float> o = res_render(body, ex, fo, 0.5, 1, f, 2, wind);
                /* the note's first 300 ms: a pluck decays, so it is judged where it speaks */
                double e = 0; const size_t w = (size_t)(0.3 * 48000); for (size_t i = 0; i < w; i++) e += (double)o[i] * o[i];
                const double db = 10 * std::log10(e / w + 1e-30);
                if (body == 0 && ex == 0) ref = db;
                const double d = db - ref; worst = std::fmin(worst, d);
                if (f == 196.0) std::printf(" f%.1f %s/%s %+.1f", fo, body == 0 ? "String" : body == 1 ? "Tube" : "Bell", ex ? "Plucked" : "Bowed", d);
            }
        }
        std::printf("; worst %+.1f\n", worst);
        assert(worst > -6);
    }
    {   /* 6 (Kerem 2026-10-07: "octave control for every sampler synth"): Octave moves the note by whole octaves */
        const std::vector<float> wind = noise_src(4, 0.5f, 77); const float *p[1] = { wind.data() };
        auto at = [&](int oct) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), p);
            r.body = sampler::STRING; r.excite = sampler::BOWED; r.focus = 0.9; r.octave = oct;
            r.attack(220, 0, 0.5); r.release(10);
            std::vector<float> L((size_t)(3 * 48000), 0.0f), R(L.size(), 0.0f);
            for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, 128, i / 48000.0);
            return L; };
        /* the old fundamental gone an octave up, a new one an octave down (a string at 220 Hz has 440 in it already) */
        const auto z = at(0), up = at(1), dn = at(-1);
        const double z220 = peak_amp(z, 48000, 220), z110 = peak_amp(z, 48000, 110), u220 = peak_amp(up, 48000, 220), u440 = peak_amp(up, 48000, 440), d110 = peak_amp(dn, 48000, 110);
        std::printf("6 octave: at 0 220 Hz %.3g, 110 Hz %.3g; +1 220 Hz %.3g (440 %.3g); -1 110 Hz %.3g\n", z220, z110, u220, u440, d110);
        assert(u220 < 0.1 * z220 && u440 > 0.3 * z220 && d110 > 10 * z110);
    }
    {   /* 2026-10-08 (Kerem: "Bell plucked resonator still silent"): every pluck struck the recording's first 25 ms - his
           Titmouse song starts in silence, so every note was -108 dB. A recording like his as the engine has it (the silence cut
           to gaps of 0.25 s at most): 1 s of silence, then 0.35 s calls 0.25 s apart. Notes every 0.83 s for 20 s: Plucked within 10 dB of Bowed, for every body */
        std::vector<float> bird((size_t)(20 * 48000), 0.0f);
        for (size_t i = 48000; i < bird.size(); i++) { const double t = (i - 48000) / 48000.0, ph = std::fmod(t, 0.6);
            if (ph < 0.35) bird[i] = (float)(0.4 * std::sin(sampler::PI * ph / 0.35) * std::sin(2 * sampler::PI * 3000 * t)); }
        const float *p[1] = { bird.data() };
        auto level = [&](int body, int ex) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)bird.size(), p); r.body = body; r.excite = ex; r.focus = 0.5; r.colour = 1;
            std::vector<float> L(48000 * 20 / 128 * 128), R(L.size()); double nt = 0;
            for (size_t i = 0; i < L.size(); i += 128) { const double t = i / 48000.0;
                if (t >= nt) { r.attack(375, t, 0.6); r.release(t + 1.5); nt += 0.83; }   /* 375 Hz: its 8th overtone at 3 kHz, the calls' */
                r.render(L.data() + i, R.data() + i, 128, t); }
            double s = 0; for (float v : L) s += (double)v * v; return 10 * std::log10(s / L.size() + 1e-30); };
        std::printf("6 plucked on a sparse recording (dB vs Bowed):");
        double worst = 0;
        for (int body = 0; body < 3; body++) { const double d = level(body, sampler::PLUCKED) - level(body, sampler::BOWED); worst = std::fmin(worst, d);
            std::printf(" %s %+.1f", body == 0 ? "String" : body == 1 ? "Tube" : "Bell", d); }
        std::printf("\n");
        assert(worst > -10);
    }
    {   /* 6c (Kerem 2026-10-08: "I want digital voices too"): the live row names a digital voice's sounding notes, as a sampler's -
           every note named is one the voice played, and some are named while it sounds */
        NoteLog log; unsigned seed = 4242; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128);
        fs_piece_test_hooks(d, fixed_draw, &seed, log_note, &log);
        const int r = fs_piece_add_route(d, "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"}],\"tempo\":72}"); fs_piece_walk(d, r, 0.3, 0);
        int named[3] = {}, wrong = 0; float out[3 * 8];
        for (int i = 0; i < 20 * 48000 / 128; i++) {
            fs_process(d, 128);
            if (i % 100) continue;
            fs_piece_roles(d, out, 6);
            for (int q = 0; q < 3; q++) for (int k = 0; k < (int)out[q * 8 + 1]; k++) {
                named[q]++; const double f = out[q * 8 + 2 + k]; bool played = false;
                for (double g : log.f) if (std::fabs(g - f) < 0.01) { played = true; break; }
                if (!played) wrong++;
            }
        }
        fs_destroy(d);
        std::printf("6c digital notes named: voice %d, second %d, third %d; named but never played %d\n", named[0], named[1], named[2], wrong);
        assert(named[0] > 0 && named[1] > 0 && wrong == 0);
    }
    {   /* final review 6 C1: a re-voice (every lab control on the role) keeps a held note's octave - it moved up one each time */
        const std::vector<float> wind = noise_src(5, 0.5f, 77); const float *p[1] = { wind.data() };
        auto held = [&](int revoices) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), p);
            r.body = sampler::STRING; r.excite = sampler::BOWED; r.focus = 0.9; r.octave = 1;
            r.attack(220, 0, 0.5);
            std::vector<float> L((size_t)(4 * 48000), 0.0f), R(L.size(), 0.0f);
            for (size_t i = 0; i < L.size(); i += 128) { if (revoices >= 1 && i == 48000 / 128 * 128) r.revoice(i / 48000.0); if (revoices >= 2 && i == 96000 / 128 * 128) r.revoice(i / 48000.0);
                r.render(L.data() + i, R.data() + i, 128, i / 48000.0); }
            return peak_amp(L, 48000, 440); };
        const double ref = held(0), two = held(2);
        std::printf("6 octave and re-voice: 440 Hz %.3g held, %.3g after two re-voices\n", ref, two);
        assert(two > 0.5 * ref);
    }
    {   /* final review 6 I3: Octave on the pitch sampler is whole octaves whatever Tune is (Tune 0.5 made +1 an augmented 4th) */
        std::vector<float> sine((size_t)(4 * 48000)); for (size_t i = 0; i < sine.size(); i++) sine[i] = (float)(0.3 * std::sin(2 * sampler::PI * 220 * i / 48000));
        auto play = [&](int oct) {
            sampler::Resonator r; r.init(48000); r.synth = sampler::RETUNE; r.method = 0; const float *p[1] = { sine.data() }; r.set_source(1, (long long)sine.size(), p);
            std::vector<float> tf(200, 220.0f), tc(200, 0.95f); r.set_track(220, 0.02, tf.data(), tc.data(), 200);
            r.tune = 0.5; r.focus = 0; r.octave = oct; r.attack(440, 0, 0.5);
            std::vector<float> L((size_t)(1.5 * 48000 / 128) * 128, 0.0f), R(L.size(), 0.0f);   /* whole blocks */
            for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, 128, i / 48000.0);
            return L; };
        const auto z = play(0), up = play(1);
        const double base = 220 * std::sqrt(2.0);   /* Tune 0.5: halfway (in pitch) from the recording's 220 Hz to the note's 440 */
        const double z0 = peak_amp(z, 48000, base), u2 = peak_amp(up, 48000, 2 * base), u440 = peak_amp(up, 48000, 440);
        std::printf("6 retune octave at tune 0.5: octave 0 %.0f Hz %.3g; octave +1 %.0f Hz %.3g (440 Hz %.3g)\n", base, z0, 2 * base, u2, u440);
        assert(z0 > 0.01 && u2 > 5 * u440);
    }
    {   /* final review 6 I2: a Bell Plucked note's make-up is cheap at its start (it ran 150 ms of modes: ~110 us a note) */
        const std::vector<float> wind = noise_src(3, 0.5f, 77); const float *p[1] = { wind.data() };
        auto cost = [&](int ex) {
            std::vector<double> us;   /* fresh voices only: past 24 an attack steals, and a steal's start waits for render */
            for (int rep = 0; rep < 12; rep++) {
                sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), p); r.body = sampler::BELL; r.excite = ex;
                for (int k = 0; k < sampler::Resonator::VOICES; k++) { auto c0 = std::chrono::steady_clock::now(); r.attack(110 * std::pow(2.0, k / 12.0), 0, 0.5);
                    us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count()); } }
            std::sort(us.begin(), us.end()); return us[us.size() / 2]; };
        const double pl = cost(sampler::PLUCKED), bo = cost(sampler::BOWED);
        std::printf("6 bell plucked note start: median %.1f us (bowed %.1f us)\n", pl, bo);
        assert(pl < 40);   /* ~26 us (the burst through four modes, the strike search); the old 108 us is what this catches */
    }
    {   /* 6: the five-band EQ - a +6 dB bell at 1 kHz where it should be and nowhere else; flat is the input exactly; a jump
           of +12 dB never steps (the coefficients glide) */
        auto gain_at = [](double hz, double g1k) {
            Eq5 e; e.init(48000); e.set(2, 1000, g1k, 1);
            std::vector<float> L(48000), R(48000);
            for (int i = 0; i < 48000; i++) L[i] = R[i] = (float)(0.25 * std::sin(2 * 3.141592653589793 * hz * i / 48000));
            std::vector<float> in = L;
            for (int i = 0; i < 48000; i += 32) e.process(L.data() + i, R.data() + i, 32);
            double a = 0, b = 0; for (int i = 24000; i < 48000; i++) { a += (double)L[i] * L[i]; b += (double)in[i] * in[i]; }
            return 10 * std::log10(a / b); };
        const double g1 = gain_at(1000, 6), g100 = gain_at(100, 6), g10k = gain_at(10000, 6);
        Eq5 flat; flat.init(48000);
        std::vector<float> fl(4800), fr(4800); for (int i = 0; i < 4800; i++) fl[i] = fr[i] = (float)std::sin(i * 0.37);
        std::vector<float> fin = fl; for (int i = 0; i < 4800; i += 32) flat.process(fl.data() + i, fr.data() + i, 32);
        Eq5 j; j.init(48000); double step = 0, dstep = 0; float prev = 0, dprev = 0;
        for (int b = 0; b < 48000 / 32; b++) {
            if (b == 600) j.set(0, 200, 12, 0.7);
            float L[32], R[32], D[32];
            for (int i = 0; i < 32; i++) { const int n = b * 32 + i; L[i] = R[i] = D[i] = (float)(0.2 * std::sin(2 * 3.141592653589793 * 80 * n / 48000)); }
            j.process(L, R, 32);
            for (int i = 0; i < 32; i++) { step = std::fmax(step, std::fabs(L[i] - prev)); prev = L[i]; dstep = std::fmax(dstep, std::fabs(D[i] - dprev)); dprev = D[i]; }
        }
        std::printf("6 eq: bell +6 at 1 kHz reads %+.2f dB, at 100 Hz %+.2f, at 10 kHz %+.2f; flat identical %d; a +12 dB jump's largest step %.2fx the dry's\n",
            g1, g100, g10k, (int)(fl == fin), step / dstep);
        assert(std::fabs(g1 - 6) < 0.3 && std::fabs(g100) < 0.5 && std::fabs(g10k) < 0.5 && fl == fin && step < 4.2 * dstep);
    }
    {   /* 6: a role's EQ on a route - a -12 dB low shelf at 16 kHz on every role takes the route ~12 dB down (all of it goes through
           the role EQs); a flat one changes nothing */
        auto render = [](const char *patch) {
            unsigned seed = 4242; fs_device *d = fs_create("piece"); fs_prepare(d, 48000, 128);
            fs_piece_test_hooks(d, fixed_draw, &seed, nullptr, nullptr);
            const int r = fs_piece_add_route(d, patch); fs_piece_walk(d, r, 0.3, 0);
            std::vector<float> o;
            for (int i = 0; i < 12 * 48000 / 128; i++) { fs_process(d, 128); const float *l = fs_out(d, 0); o.insert(o.end(), l, l + 128); }
            fs_destroy(d); return o; };
        auto hi = [](const std::vector<float> &o) { double e = 0; for (float x : o) e += (double)x * x; return e; };
        const std::string shelf = "[[16000,-12,0.7],[250,0,1],[1000,0,1],[4000,0,1],[10000,0,0.7]]", zero = "[[80,0,0.7],[250,0,1],[1000,0,1],[4000,0,1],[10000,0,0.7]]";
        const std::string V = "{\"version\":17,\"prog\":[{\"r\":0,\"q\":\"m9\"}],";
        const auto none = render((V + "\"tempo\":72}").c_str()), cut = render((V + "\"voice\":{\"eq\":" + shelf + "},\"sect\":{\"eq\":" + shelf + "},\"v3\":{\"eq\":" + shelf + "}}").c_str()),
                   flat = render((V + "\"voice\":{\"eq\":" + zero + "},\"sect\":{\"eq\":" + zero + "},\"v3\":{\"eq\":" + zero + "}}").c_str());
        const double drop = 10 * std::log10(hi(cut) / hi(none));
        std::printf("6 route eq: %+.1f dB with a -12 dB low shelf at 16 kHz on every role (energy %.3g); flat EQ identical %d\n", drop, hi(none), (int)(flat == none));
        assert(drop < -11 && flat == none);
    }
    std::printf("core ok\n");
    return 0;
}
