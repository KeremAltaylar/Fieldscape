/* bench: the lab's audition bench on the engine (sample harmony 2a). One sampler synth, its recording and
   settings, notes at device times, and a measure of the pitch it created. */
#include "fieldscape.h"
#include "device.hpp"
#include "samplers.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

static const fs_param BENCH_PARAMS[] = {
    { "body", "Body", "", 0.0f, 2.0f, 0.0f },          /* String, Tube, Bell */
    { "excite", "Excite", "", 0.0f, 1.0f, 0.0f },      /* Bowed, Plucked */
    { "focus", "Focus", "", 0.0f, 1.0f, 0.5f },
    { "colour", "Colour", "", 0.0f, 1.0f, 0.5f },
    { "tune", "Tune", "", 0.0f, 1.0f, 1.0f },
    { "attack", "Attack", "s", 0.005f, 2.0f, 0.02f },
    { "release", "Release", "s", 0.03f, 5.0f, 0.6f },
    { "offset", "Position", "s", 0.0f, 600.0f, 0.0f },
};

struct Bench : Device {
    sampler::Resonator res;
    double sr = 48000, t = 0;
    std::vector<float> ring = std::vector<float>(65536, 0.0f); size_t w = 0;   /* 1.37 s: 0.7 Hz resolution */
    void prepare(float s, int) override { sr = s; res.init(s); t = 0; }
    const fs_param *params(int &n) override { n = 8; return BENCH_PARAMS; }
    void set_param(int i, float v) override {
        switch (i) {
            case 0: res.body = (int)std::lround(v); break;       case 1: res.excite = (int)std::lround(v); break;
            case 2: res.focus = v; break;                        case 3: res.colour = v; break;
            case 4: res.tune = v; break;                         case 5: res.att = v; break;
            case 6: res.rel = v; break;                          case 7: res.offset_s = v; break;
        }
    }
    void set_source(int ch, int n, const float *const *s) override { res.set_source(ch, n, s); }
    void set_source_i16(int ch, int n, const int16_t *const *s) override { res.set_source_i16(ch, n, s); }
    void process(int n) override {
        std::fill(out[0].begin(), out[0].begin() + n, 0.0f); std::fill(out[1].begin(), out[1].begin() + n, 0.0f);
        res.render(out[0].data(), out[1].data(), n, t);
        for (int i = 0; i < n; i++) { ring[w] = out[0][i]; w = (w + 1) % ring.size(); }
        t += n / sr;
    }
    void *cast(const char *id) override { return std::strcmp(id, "bench") == 0 ? this : nullptr; }
    /* Hann-windowed power at f over the last 65536 samples, in order */
    double power(double f) const {
        const size_t N = ring.size(); double s1 = 0, s2 = 0, k = 2 * std::cos(2 * sampler::PI * f / sr);
        for (size_t i = 0; i < N; i++) {
            double x = ring[(w + i) % N] * (0.5 - 0.5 * std::cos(2 * sampler::PI * i / N)), s0 = x + k * s1 - s2; s2 = s1; s1 = s0;
        }
        return s1 * s1 + s2 * s2 - k * s1 * s2;
    }
    double created(double f) const {
        const double off[6] = { 0.03, 0.05, 0.08, 0.11, 0.15, 0.2 };
        /* neighbours at least 3 resolution steps away, or a low note's own peak is compared with itself */
        const double step = 3 * sr / ring.size();
        std::vector<double> nb; for (double o : off) { double d = std::fmax(o * f, step); nb.push_back(power(f - d)); nb.push_back(power(f + d)); }
        std::nth_element(nb.begin(), nb.begin() + nb.size() / 2, nb.end());
        double p = power(f), m = nb[nb.size() / 2];
        return p > 1e-20 && m > 1e-30 ? 10 * std::log10(p / m) : 0;
    }
};
Device *make_bench() { return new Bench(); }

static Bench *B(fs_device *d) { return d ? (Bench *)fs_device_impl(d)->cast("bench") : nullptr; }
extern "C" {
void fs_bench_note(fs_device *d, double hz, double at_s, double dur_s, double vel) { Bench *b = B(d); if (b) { b->res.attack(hz, at_s, vel); b->res.release(at_s + dur_s); } }
void fs_bench_stop(fs_device *d) { Bench *b = B(d); if (b) b->res.stop_all(); }
double fs_bench_time(fs_device *d) { Bench *b = B(d); return b ? b->t : 0; }
double fs_bench_created(fs_device *d, double hz) { Bench *b = B(d); return b ? b->created(hz) : 0; }
}
