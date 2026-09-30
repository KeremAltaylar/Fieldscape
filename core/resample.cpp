/* Sample-rate conversion for a whole recording, once, at load: the host's job by the stretch spec
   (the device never resamples). iOS has AVAudioConverter; Android has no resampler in its SDK, so
   the core carries one: windowed sinc, 32 taps a side, a Blackman-Harris window, cutoff at the
   lower of the two Nyquists (x 0.95) so downsampling does not alias. 16-bit in, 16-bit out. */
#include "fieldscape.h"

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" long long fs_resample_length(long long frames, double from_rate, double to_rate) {
    return (long long)std::floor((double)frames * to_rate / from_rate);
}

extern "C" void fs_resample_i16(const short *in, long long frames, double from_rate, short *out, double to_rate) {
    const int TAPS = 32;
    const double PI = 3.141592653589793;
    const double step = from_rate / to_rate;                          /* input samples per output sample */
    const double fc = 0.95 * std::min(1.0, to_rate / from_rate);     /* cutoff, as a fraction of input Nyquist */
    const long long n_out = fs_resample_length(frames, from_rate, to_rate);
    auto window = [&](double x) {                                    /* Blackman-Harris over [-TAPS, TAPS] */
        double t = (x + TAPS) / (2.0 * TAPS);
        if (t < 0 || t > 1) return 0.0;
        return 0.35875 - 0.48829 * std::cos(2 * PI * t) + 0.14128 * std::cos(4 * PI * t) - 0.01168 * std::cos(6 * PI * t);
    };
    /* One tap's weight at `frac` (the output sample's offset past input sample c). */
    auto kernel = [&](double frac, double *h) {
        double norm = 0;
        for (int k = -TAPS + 1; k <= TAPS; k++) {
            const double x = k - frac, arg = PI * fc * x;
            h[k + TAPS - 1] = fc * (x == 0 ? 1.0 : std::sin(arg) / arg) * window(x);
            norm += h[k + TAPS - 1];
        }
        for (int k = 0; k < 2 * TAPS; k++) h[k] = norm != 0 ? h[k] / norm : 0;
    };
    /* Whole-number rates repeat their offsets every L outputs (44100 -> 48000: L = 160), so the kernels
       are computed once per offset, not once per sample: the per-sample sin/cos made a 10-minute
       recording take minutes on a phone (2026-09-29). Otherwise, or past 4096 offsets, per sample. */
    long long L = 0, M = 0;
    if (from_rate == std::floor(from_rate) && to_rate == std::floor(to_rate) && from_rate > 0 && to_rate > 0) {
        long long a = (long long)from_rate, b = (long long)to_rate;
        while (b) { long long t = a % b; a = b; b = t; }
        L = (long long)to_rate / a; M = (long long)from_rate / a;
        if (L > 4096) L = 0;
    }
    std::vector<double> table(L > 0 ? L * 2 * TAPS : 2 * TAPS);
    if (L > 0) for (long long p = 0; p < L; p++) kernel((double)p / L, &table[p * 2 * TAPS]);
    for (long long j = 0; j < n_out; j++) {
        long long c; const double *h;
        if (L > 0) { const long long q = j * M; c = q / L; h = &table[(q % L) * 2 * TAPS]; }
        else { const double pos = j * step; c = (long long)std::floor(pos); kernel(pos - c, table.data()); h = table.data(); }
        double acc = 0;
        for (int k = -TAPS + 1; k <= TAPS; k++) {
            const long long idx = c + k;
            if (idx >= 0 && idx < frames) acc += h[k + TAPS - 1] * in[idx];
        }
        double v = acc;
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        out[j] = (short)std::lround(v);
    }
}
