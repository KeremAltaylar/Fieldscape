/* stretch: the Paulstretch mechanism (SPEC.md) as a real-time device.

   Each frame: read N samples at the read position, window, FFT, keep magnitudes, draw random
   phases, inverse FFT, window again, overlap-add at hop H = N/2. Both channels go through one
   complex FFT (left in the real part, right in the imaginary part) and are separated in the
   spectrum, so a stereo frame costs one forward and one inverse transform.

   Real time: a Voice plays one hop while it computes the next. The next frame's work is split into
   weighted stages and spread over the first half of the current hop, so no single callback carries
   a whole frame (see NOTES.md). A window or shape change starts a second Voice at the new size and
   crossfades to it; nothing allocates after prepare. */
#include "../harmony.hpp"
#include "../device.hpp"
#include "fft.hpp"
#include "../json.hpp"
#include <cstdint>
#include <cstring>

static const float TWO_PI = 6.2831853f;

/* PCG32 (O'Neill): one per voice, seeded from the device's, so output is repeatable per seed. */
struct Pcg32 {
    uint64_t state = 0, inc = 1;
    void seed(uint64_t s, uint64_t seq) { state = 0; inc = (seq << 1) | 1; next(); state += s; next(); }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t x = (uint32_t)(((old >> 18) ^ old) >> 27), rot = (uint32_t)(old >> 59);
        return (x >> rot) | (x << ((32 - rot) & 31));
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }   /* [0, 1) */
};

/* Smallest even N >= n0 whose only prime factors are 2, 3, 5 (SPEC 1). */
static int window_size(int n0) {
    for (int n = n0 + (n0 & 1);; n += 2) {
        int m = n;
        while (m % 2 == 0) m /= 2;
        while (m % 3 == 0) m /= 3;
        while (m % 5 == 0) m /= 5;
        if (m == 1) return n;
    }
}

/* A recording: float, or 16-bit (half the memory; read as s / 32768, exactly the float a 16-bit
   file decodes to). */
struct Source { const float *ch[2] = { nullptr, nullptr }; const int16_t *s16[2] = { nullptr, nullptr }; int len = 0; };

/* What the device hands a voice at each frame. The second line is Fieldscape's own shaping
   (docs/superpowers/specs/2026-09-27-own-stretch-shaping-design.md): on the magnitudes only, after
   the stretch has made them and before the random phases - so with all of it at zero (`shaping`
   false, drift 0, the whole recording) the stretch is exactly what it was, bit for bit. */
struct Controls {
    double log_s; bool freeze; float onset, width;
    bool shaping; double transpose; float tune, focus, partials, layers, harmony, glide, drift, blur, start, end;
    float chord[5], root;
    float follow; const float *tf, *tc; int tn; double thop;   /* 5: the recording's pitch track, and how far it follows the chord */
};

enum Stage { S_WIN, S_TWID, S_GAIN, S_READ, S_FWD, S_MAG, S_ONSET, S_BINC, S_COMB, S_SH_IN, S_SH_T, S_SH_L, S_SH_NORM, S_PHASE, S_INV, S_OUT, S_FINISH };
static const int CENTS = 1200;        /* the tune comb: one octave at 1-cent steps */
static const int BANDS = 32;

struct Voice {
    int N = 0, H = 0, shape = 0;
    float scale = 0;                 /* normalisation gain / N (the 1/N of the inverse FFT folded in) */
    FFT fft;
    Pcg32 rng;
    const Source *src = nullptr;
    double sr = 48000;
    std::vector<float> win, hc, ar, ai, br, bi, mag[2], prev[2], tail[2], hop[2][2];

    double pos = 0, log_s = 0;       /* read position of the next frame; smoothed log stretch */
    double fpos = 0, folog = 0;      /* 5: the frame's place in the recording (its centre); the follow, in cents, glided */
    int cur = 0, play = 0;           /* hop[cur] is playing, at index play */
    long long frames = 0;
    int wraps = 0, late = 0;
    bool onset_on = false, get_next = true, have_mag = false, reading = false;
    double tau = 0, credit = 0;
    float bands[BANDS], old_bands[BANDS];
    float width = 0, theta = 1;
    bool freeze = false;
    /* shaping */
    Controls cc{};
    std::vector<float> shp[2], sm[2], pre[2], tt[2], binc, table;
    double e0[2] = { 0, 0 }, e1[2] = { 0, 0 }, sh_T = 1, ratio[4]; int nr = 0; float sh_g[2] = { 1, 1 };
    double log2k[25];
    double gnote[5] = { -1, -1, -1, -1, -1 }, groot = -1;   /* the chord as it glides (MIDI) */
    bool have_sm = false;
    long long comb_key = -1, table_key = -1;
    double drift_off = 0, drift_v = 0;
    int region0 = 0, region1 = 0;
    bool binc_ready = false;          /* each bin's pitch in cents, built as a stage (48 000 log2 at a 2 s window) */

    struct Step { int stage, pass, count, weight; };
    Step plan[160];
    int nsteps = 0, si = 0, item = 0;
    long long total = 0, done = 0;
    bool setup = false;

    void reserve(int nmax, double sample_rate) {
        sr = sample_rate;
        fft.reserve(nmax);
        for (auto *v : { &win, &ar, &ai, &br, &bi }) v->assign(nmax, 0.0f);
        hc.assign(nmax / 2, 0.0f);
        binc.assign(nmax / 2 + 1, 0.0f);
        for (int k = 1; k <= 24; k++) log2k[k] = 1200.0 * std::log2((double)k);
        table.assign(CENTS, 0.0f);
        for (int c = 0; c < 2; c++) {
            shp[c].assign(nmax / 2 + 1, 0.0f); sm[c].assign(nmax / 2 + 1, 0.0f);
            pre[c].assign(nmax / 2 + 1, 0.0f); tt[c].assign(nmax / 2 + 1, 0.0f);
            mag[c].assign(nmax / 2 + 1, 0.0f); prev[c].assign(nmax / 2 + 1, 0.0f);
            tail[c].assign(nmax / 2, 0.0f); hop[0][c].assign(nmax / 2, 0.0f); hop[1][c].assign(nmax / 2, 0.0f);
        }
    }

    /* Begin at size n, reading from p. The first hop is silence while the first frame is built. */
    void start(int n, int shp, double p, uint64_t seed, const Controls &c) {
        N = n; H = n / 2; shape = shp; pos = p; log_s = c.log_s;
        fft.plan(N);
        rng.seed(seed, 54);
        for (int k = 0; k < 2; k++) {
            std::fill(tail[k].begin(), tail[k].end(), 0.0f);
            std::fill(hop[0][k].begin(), hop[0][k].end(), 0.0f);
        }
        cur = 0; play = 0; frames = 0; wraps = 0;
        cc = c; set_region(src ? src->len : 0);
        onset_on = false; have_mag = false; get_next = true; tau = 0; credit = 0;
        std::memset(bands, 0, sizeof bands);
        have_sm = false; table_key = -1; drift_off = 0; drift_v = 0; binc_ready = false;
        setup = true;
        begin_job(c);
    }

    void add(int stage, int pass, int count, int weight) {
        plan[nsteps++] = { stage, pass, count, weight };
        total += (long long)count * weight;
    }

    /* Plan the next frame. Weights are rough per-item costs so the spread is even in time. */
    void begin_job(const Controls &c) {
        nsteps = si = item = 0; total = done = 0;
        if (setup) { add(S_WIN, 0, N, 8); add(S_TWID, 0, N, 8); add(S_GAIN, 0, 1, 2 * N); setup = false; }
        log_s += (c.log_s - log_s) * (1.0 - std::exp(-H / (0.1 * sr)));   /* 100 ms per-frame smoothing */
        freeze = c.freeze; width = c.width; theta = 1.0f - c.onset;
        cc = c;
        glide_chord();
        bool want = c.onset > 0;
        if (want && !onset_on) { tau = 0; credit = 0; get_next = true; }
        onset_on = want;
        reading = !onset_on || get_next;
        if (reading) {
            add(S_READ, 0, N, 2);
            for (int p = 0; p < fft.passes; p++) add(S_FWD, p, fft.butterflies(p), 3 * fft.radix[p]);
            add(S_MAG, 0, N / 2 + 1, 6);
            std::memcpy(old_bands, bands, sizeof bands);
            std::memset(bands, 0, sizeof bands);
            if (onset_on) add(S_ONSET, 0, 1, 64);
        }
        if (c.shaping) {
            if (c.tune > 0 && !binc_ready) add(S_BINC, 0, N / 2 + 1, 60);
            if (c.tune > 0 && comb_key != table_key) add(S_COMB, 0, CENTS, 40 + 25 * (int)std::lround(c.partials < 1 ? 1 : c.partials > 24 ? 24 : c.partials));
            /* split per bin like the FFT, so no callback carries a whole spectrum (measured: 7.8 ms in one) */
            const int M = N / 2 + 1, L = (int)std::lround(c.layers < 0 ? 0 : c.layers > 4 ? 4 : c.layers);
            /* weights per bin, both channels (measured against the FFT's 3 x radix per butterfly) */
            add(S_SH_IN, 0, M, 10);
            add(S_SH_T, 0, M, 16);
            add(S_SH_L, 0, M, 30 + 24 * L);
            add(S_SH_NORM, 0, M, 5);
        }
        add(S_PHASE, 0, N / 2 + 1, 20);
        for (int p = 0; p < fft.passes; p++) add(S_INV, p, fft.butterflies(p), 3 * fft.radix[p]);
        add(S_OUT, 0, H, 6);
        add(S_FINISH, 0, 1, 1);
    }

    /* The chord's pitches move toward the target, in semitones, once a frame: `glide` is the time to
       arrive (within 1 %, so to the cent), not a time constant. A voice with none yet takes it at once. */
    void glide_chord() {
        const double rate = 1.0 - std::exp(-5.0 * H / ((cc.glide > 0.05 ? cc.glide : 0.05) * sr));
        for (int i = 0; i < 5; i++) {
            const double t = cc.chord[i];
            if (t < 0) { gnote[i] = -1; continue; }
            gnote[i] = gnote[i] < 0 ? t : gnote[i] + (t - gnote[i]) * rate;
        }
        groot = cc.root < 0 ? (gnote[0] >= 0 ? gnote[0] : 62) : (groot < 0 ? cc.root : groot + (cc.root - groot) * rate);
        /* what the comb depends on, to 1 cent: rebuilt only when it would change */
        long long k = (long long)std::lround(cc.focus * 1000) * 31 + (long long)std::lround(cc.partials);
        for (double g : gnote) k = k * 131071 + (g < 0 ? 7 : std::lround(g * 100));
        comb_key = k;
    }
    /* A magnitude spectrum read at bin j / r (linear between bins): moved in pitch by r. */
    static float at(const float *m, double x, int M) {
        if (x < 0 || x >= M - 1) return 0;
        int i = (int)x; float f = (float)(x - i);
        return m[i] + (m[i + 1] - m[i]) * f;
    }

    /* Result buffers after the forward or inverse passes. */
    float *res_r() { return fft.passes % 2 ? br.data() : ar.data(); }
    float *res_i() { return fft.passes % 2 ? bi.data() : ai.data(); }

    void run(int stage, int pass, int a, int b) {
        switch (stage) {
        case S_WIN:
            for (int k = a; k < b; k++) {
                if (shape == 0) {
                    double t = -1.0 + 2.0 * k / (N - 1);
                    win[k] = (float)std::pow(1.0 - t * t, 1.25);
                } else {
                    win[k] = (float)(0.5 - 0.5 * std::cos(6.283185307179586 * k / (N - 1)));
                }
                if (k < H) {
                    const double A = (1.0 + std::sqrt(0.5)) / 2.0;
                    hc[k] = shape == 0 ? 1.0f : (float)(2.0 * (A - (1.0 - A) * std::cos(6.283185307179586 * k / H)) / A);
                }
            }
            break;
        case S_TWID: fft.twiddles(a, b); break;
        case S_GAIN: {
            /* Random phases spread a frame's energy sum(b^2) evenly over N samples; windowing again
               and overlap-adding two independent frames gives mean output power
               sigma^2 * (sum w^2 / N) * (1/H) sum_m (w[m]^2 + w[m+H]^2) h[m]^2. Undo that. */
            double sw = 0, so = 0;
            for (int k = 0; k < N; k++) sw += (double)win[k] * win[k];
            for (int m = 0; m < H; m++)
                so += ((double)win[m] * win[m] + (double)win[m + H] * win[m + H]) * hc[m] * hc[m];
            scale = (float)(1.0 / std::sqrt(sw / N * so / H) / N);
            break;
        }
        case S_READ: {
            const int len = src ? src->len : 0;
            if (!len) {
                std::fill(ar.begin() + a, ar.begin() + b, 0.0f);
                std::fill(ai.begin() + a, ai.begin() + b, 0.0f);
                break;
            }
            /* the region [region0, region1): the whole recording unless the setter chose a part (the
               recording can arrive after the voice started, so it is measured at every read) */
            set_region(len);
            const long long r0 = region0, rl = region1 - region0;
            long long idx = r0 + ((((long long)(pos + drift_off) - r0 + a) % rl) + rl) % rl;
            if (a == 0) fpos = (double)(r0 + ((idx - r0 + N / 2) % rl));
            const long long r1 = region1;
            if (src->s16[0]) {
                const int16_t *x0 = src->s16[0], *x1 = src->s16[1];
                for (int k = a; k < b; k++) {
                    ar[k] = (x0[idx] * (1.0f / 32768.0f)) * win[k];
                    ai[k] = (x1[idx] * (1.0f / 32768.0f)) * win[k];
                    if (++idx == r1) idx = r0;
                }
                break;
            }
            const float *x0 = src->ch[0], *x1 = src->ch[1];
            for (int k = a; k < b; k++) {
                ar[k] = x0[idx] * win[k];
                ai[k] = x1[idx] * win[k];
                if (++idx == r1) idx = r0;
            }
            break;
        }
        case S_FWD:
        case S_INV:
            if (pass % 2 == 0) fft.pass(pass, ar.data(), ai.data(), br.data(), bi.data(), a, b);
            else fft.pass(pass, br.data(), bi.data(), ar.data(), ai.data(), a, b);
            break;
        case S_MAG: {
            /* Z = FFT(left + i right): left_j = (Z_j + conj Z_{N-j}) / 2, right_j = (Z_j - conj Z_{N-j}) / 2i */
            const float *zr = res_r(), *zi = res_i();
            const int div = (N / 2 + 1) / BANDS;
            for (int j = a; j < b; j++) {
                const int k = j ? N - j : 0;
                float m0 = 0.5f * std::sqrt((zr[j] + zr[k]) * (zr[j] + zr[k]) + (zi[j] - zi[k]) * (zi[j] - zi[k]));
                float m1 = 0.5f * std::sqrt((zr[j] - zr[k]) * (zr[j] - zr[k]) + (zi[j] + zi[k]) * (zi[j] + zi[k]));
                prev[0][j] = have_mag ? mag[0][j] : m0; prev[1][j] = have_mag ? mag[1][j] : m1;
                mag[0][j] = m0; mag[1][j] = m1;
                if (div && j < div * BANDS) bands[j / div] += 0.5f * (m0 + m1) / div;
            }
            break;
        }
        case S_ONSET: {
            /* SPEC onset measure: m = clamp(2 mean(B_t - B_t-1) / (mean|B_t-1| + 0.001), 0, 1) */
            const float *old = have_mag ? old_bands : bands;
            float d = 0, o = 0;
            for (int i = 0; i < BANDS; i++) { d += bands[i] - old[i]; o += std::fabs(old[i]); }
            float m = 2.0f * (d / BANDS) / (o / BANDS + 0.001f);
            m = m < 0 ? 0 : m > 1 ? 1 : m;
            if (m > theta) { tau = 1; credit += 1; }
            break;
        }
        case S_BINC:
            /* each bin's pitch within the octave in cents (0 = C), for the comb; spread like the FFT */
            for (int j = a; j < b; j++) {
                const double f = (double)j * sr / N;
                binc[j] = f < 40 ? -1.0f : (float)std::fmod(1200.0 * std::log2(f / 440.0) + 900.0 + 1200.0 * 64, 1200.0);
            }
            if (b == N / 2 + 1) binc_ready = true;
            break;
        case S_COMB: {
            /* The tune comb over one octave: a peak at each chord note and at its overtones (folded into
               the octave, 1/sqrt(k) each), as wide as focus says - pure at 0, breathy at 1. */
            const double w = 3.0 + std::pow(cc.focus, 1.5) * 80.0;
            const int P = (int)std::lround(cc.partials < 1 ? 1 : cc.partials > 24 ? 24 : cc.partials);
            for (int c = a; c < b; c++) {
                double v = 0;
                for (int i = 0; i < 5; i++) {
                    if (gnote[i] < 0) continue;
                    const double pc = std::fmod(gnote[i] * 100.0, 1200.0);
                    for (int k = 1; k <= P; k++) {
                        double d = std::fabs(std::fmod(c - pc - log2k[k] + 1200.0 * 64, 1200.0));
                        if (d > 600) d = 1200 - d;
                        if (d < 4 * w) v += std::exp(-0.5 * (d / w) * (d / w)) / std::sqrt((double)k);
                    }
                }
                table[c] = (float)(v > 1 ? 1 : v);
            }
            if (b == CENTS) table_key = comb_key;
            break;
        }
        case S_SH_IN:
            /* the stretch's magnitudes for this frame (onset-interpolated as the phase stage would), and their energy */
            if (a == 0) {
                e0[0] = e0[1] = e1[0] = e1[1] = 0;
                sh_T = std::pow(2.0, cc.transpose / 12.0);
                if (cc.follow > 0 && cc.tn > 0) {   /* 5: the recording's pitch here, toward the chord's nearest note, gliding */
                    const int fi = (int)(fpos / sr / (cc.thop > 0 ? cc.thop : 0.02));
                    double tgt = 0;
                    if (fi >= 0 && fi < cc.tn && cc.tc[fi] >= 0.8f && cc.tf[fi] > 0) {
                        double ch[5]; int m = 0;
                        for (int i = 0; i < 5; i++) if (gnote[i] >= 0) ch[m++] = 440 * std::pow(2.0, (gnote[i] - 69) / 12);
                        /* the pitch as it sounds, transposed: a +7 transpose follows from there (final review 5 I4) */
                        tgt = 1200 * std::log2(harmony::follow_rate(sh_T, cc.tf[fi], ch, m, 1.0) / sh_T);
                    }
                    const double gr = 1.0 - std::exp(-5.0 * H / ((cc.glide > 0.05 ? cc.glide : 0.05) * sr));
                    folog += (tgt - folog) * gr;
                    sh_T *= std::pow(2.0, cc.follow * folog / 1200);
                }
                const int L = (int)std::lround(cc.layers < 0 ? 0 : cc.layers > 4 ? 4 : cc.layers);
                nr = 0;
                for (int i = 0; i < 5 && nr < L; i++) {
                    if (gnote[i] < 0) continue;
                    const double iv = std::fmod(gnote[i] - groot + 1200.0, 12.0);
                    if (iv < 0.25 || iv > 11.75) continue;                   /* the root is the recording itself */
                    ratio[nr++] = std::pow(2.0, iv / 12.0);
                }
                for (double f : { 0.5, 2.0, 0.25 }) if (nr < L) ratio[nr++] = f;   /* octaves when the chord has too few */
                if (cc.harmony <= 0) nr = 0;
            }
            for (int ch = 0; ch < 2; ch++) for (int j = a; j < b; j++) {
                float A = mag[ch][j];
                if (onset_on) A = prev[ch][j] + (A - prev[ch][j]) * (float)tau;
                pre[ch][j] = A; e0[ch] += (double)A * A;
            }
            break;
        case S_SH_T: {
            /* transpose: the spectrum read at j / T */
            const int M = N / 2 + 1;
            const bool move = std::fabs(sh_T - 1) > 1e-6;
            for (int ch = 0; ch < 2; ch++) for (int j = a; j < b; j++) tt[ch][j] = move ? at(pre[ch].data(), j / sh_T, M) : pre[ch][j];
            break;
        }
        case S_SH_L: {
            /* layers (copies at the chord's intervals), then tune (the comb), then blur (smear in time) */
            const int M = N / 2 + 1;
            const float blur_k = 1.0f - cc.blur * 0.92f;
            for (int ch = 0; ch < 2; ch++) for (int j = a; j < b; j++) {
                float x = tt[ch][j];
                if (nr) {
                    float l = 0;
                    for (int r = 0; r < nr; r++) l += at(tt[ch].data(), j / ratio[r], M);
                    x = x * (1 - cc.harmony) + l / nr * cc.harmony;
                }
                if (cc.tune > 0) {
                    const float bc = binc[j];
                    x *= (1 - cc.tune) + cc.tune * (bc < 0 ? 0.0f : table[(int)bc % CENTS]);
                }
                if (cc.blur > 0) { sm[ch][j] = have_sm ? sm[ch][j] + (x - sm[ch][j]) * blur_k : x; x = sm[ch][j]; }
                shp[ch][j] = x; e1[ch] += (double)x * x;
            }
            break;
        }
        case S_SH_NORM:
            /* the frame's energy back to the stretch's own (A-18: these are timbre, not level) */
            if (a == 0) for (int ch = 0; ch < 2; ch++) sh_g[ch] = e1[ch] > 1e-30 ? (float)std::sqrt(e0[ch] / e1[ch]) : 0.0f;
            for (int ch = 0; ch < 2; ch++) for (int j = a; j < b; j++) shp[ch][j] *= sh_g[ch];
            if (b == N / 2 + 1) have_sm = cc.blur > 0;
            break;
        case S_PHASE:
            /* Width: every bin gets a shared phase; each channel adds its own offset in
               [-pi, pi) scaled by width. Magnitudes are untouched at any width, so level is too. */
            for (int j = a; j < b; j++) {
                float A0 = mag[0][j], A1 = mag[1][j];
                if (cc.shaping) { A0 = shp[0][j]; A1 = shp[1][j]; }
                else if (onset_on) { const float t = (float)tau; A0 = prev[0][j] + (A0 - prev[0][j]) * t; A1 = prev[1][j] + (A1 - prev[1][j]) * t; }
                float ps = TWO_PI * rng.uniform();
                float p0 = ps + width * TWO_PI * (rng.uniform() - 0.5f);
                float p1 = ps + width * TWO_PI * (rng.uniform() - 0.5f);
                /* Build conj(W), W = Y0 + i Y1, so the forward passes compute the inverse transform. */
                if (j == 0) { ar[0] = ai[0] = 0; continue; }                     /* DC removed */
                if (j == N / 2) {                                               /* Nyquist: random sign */
                    ar[j] = std::cos(p0) < 0 ? -A0 : A0;
                    ai[j] = -(std::cos(p1) < 0 ? -A1 : A1);
                    continue;
                }
                float y0r = A0 * std::cos(p0), y0i = A0 * std::sin(p0);
                float y1r = A1 * std::cos(p1), y1i = A1 * std::sin(p1);
                ar[j] = y0r - y1i;     ai[j] = -(y0i + y1r);
                ar[N - j] = y0r + y1i; ai[N - j] = y0i - y1r;
            }
            break;
        case S_OUT: {
            /* fft(conj W) = N conj(y0 + i y1): left = Re, right = -Im. Window, overlap-add, hann fix. */
            const float *rr = res_r(), *ri = res_i();
            float *o0 = hop[cur ^ 1][0].data(), *o1 = hop[cur ^ 1][1].data();
            for (int m = a; m < b; m++) {
                float w0 = scale * win[m], w1 = scale * win[m + H], h = hc[m];
                o0[m] = (rr[m] * w0 + tail[0][m]) * h;
                o1[m] = (-ri[m] * w0 + tail[1][m]) * h;
                tail[0][m] = rr[m + H] * w1;
                tail[1][m] = -ri[m + H] * w1;
            }
            break;
        }
        case S_FINISH: {
            frames++;
            if (reading) have_mag = true;
            const double inc = freeze ? 0.0 : std::exp(-log_s);   /* 1/S */
            if (!onset_on) {
                pos += H * inc;
            } else {                                              /* SPEC onset mode */
                if (reading) { pos += H; get_next = false; }
                const double step = inc < 1 ? inc : 1;
                if (credit <= 0) tau += step;
                else {
                    credit -= 0.5 * step;
                    if (credit < 0) credit = 0;
                    tau += 0.5 * step;
                }
                if (tau >= 1) { tau = std::fmod(tau, 1.0); get_next = true; }
            }
            /* drift: the read position wanders round where it should be, smoothly, up to 2 s. No
               random draw at all while it is off, so the phases stay what they were. */
            if (cc.drift > 0) {
                drift_v = drift_v * 0.92 + (rng.uniform() - 0.5) * 0.16;
                const double lim = 2.0 * sr * cc.drift;
                drift_off += drift_v * cc.drift * 0.35 * sr * H / sr;
                if (drift_off > lim) { drift_off = lim; drift_v = -std::fabs(drift_v); }
                if (drift_off < -lim) { drift_off = -lim; drift_v = std::fabs(drift_v); }
            } else if (drift_off != 0) {
                drift_off *= 0.9;
                if (std::fabs(drift_off) < 1) drift_off = 0;
            }
            const int len = src ? src->len : 0;
            set_region(len);
            if (len && pos >= region1) { wraps += (int)((pos - region0) / (region1 - region0)); pos = region0 + std::fmod(pos - region0, (double)(region1 - region0)); }
            else if (len && pos < region0) pos = region0;
            break;
        }
        }
    }

    /* The part of the recording the stretch reads: at least one window long, the whole of it by default. */
    void set_region(int len) {
        if (!len) { region0 = 0; region1 = 1; return; }
        double s0 = cc.start < 0 ? 0 : cc.start > 1 ? 1 : cc.start, s1 = cc.end < 0 ? 0 : cc.end > 1 ? 1 : cc.end;
        if (s1 < s0) std::swap(s0, s1);
        int r0 = (int)std::floor(s0 * len), r1 = (int)std::ceil(s1 * len);
        if (r1 - r0 < N) { r1 = r0 + N; if (r1 > len) { r1 = len; r0 = len - N > 0 ? len - N : 0; } }
        region0 = r0; region1 = r1 > r0 ? r1 : r0 + 1;
    }

    /* Advance the current job until `target` weighted units are done. */
    void work(long long target) {
        while (done < target && si < nsteps) {
            const Step &s = plan[si];
            long long need = (target - done + s.weight - 1) / s.weight;
            int b = (int)(item + need < s.count ? item + need : s.count);
            run(s.stage, s.pass, item, b);
            done += (long long)(b - item) * s.weight;
            item = b;
            if (item == s.count) { si++; item = 0; }
        }
    }

    /* Play n samples; the next frame's work is spread over the first half of each hop. */
    void render(float *o0, float *o1, int n, const Controls &c) {
        for (int i = 0; i < n;) {
            if (play == H) {
                if (si < nsteps) { work(total); late++; }
                cur ^= 1; play = 0;
                begin_job(c);
            }
            int k = n - i < H - play ? n - i : H - play;
            work(2 * (play + k) >= H ? total : total * 2 * (play + k) / H);
            std::memcpy(o0 + i, hop[cur][0].data() + play, sizeof(float) * k);
            std::memcpy(o1 + i, hop[cur][1].data() + play, sizeof(float) * k);
            play += k; i += k;
        }
    }
};

static const fs_param STRETCH_PARAMS[] = {
    { "stretch", "Stretch", "", 0.0f, 1.0f, 0.0f },
    { "window", "Window", "s", 0.02f, 2.0f, 0.34f },
    { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f },
    { "onset", "Onset sensitivity", "", 0.0f, 1.0f, 0.0f },
    { "width", "Width", "", 0.0f, 1.0f, 1.0f },
    { "shape", "Window shape", "", 0.0f, 1.0f, 0.0f },
    { "seed", "Seed", "", 0.0f, 16777216.0f, 1.0f },
    /* Fieldscape's own shaping: all dry by default (A-8) */
    { "transpose", "Transpose", "st", -12.0f, 12.0f, 0.0f },
    { "tune", "Tune", "", 0.0f, 1.0f, 0.0f },
    { "focus", "Focus", "", 0.0f, 1.0f, 0.5f },
    { "partials", "Partials", "", 1.0f, 24.0f, 8.0f },
    { "layers", "Layers", "", 0.0f, 4.0f, 0.0f },
    { "harmony", "Harmony", "", 0.0f, 1.0f, 0.5f },
    { "glide", "Glide", "s", 0.5f, 10.0f, 2.0f },
    { "drift", "Drift", "", 0.0f, 1.0f, 0.0f },
    { "blur", "Blur", "", 0.0f, 1.0f, 0.0f },
    { "start", "Region start", "", 0.0f, 1.0f, 0.0f },
    { "end", "Region end", "", 0.0f, 1.0f, 1.0f },
    /* the chord the tune and the layers follow (MIDI notes; -1 none), set by the host from the route */
    { "chord0", "Chord note 1", "", -1.0f, 127.0f, -1.0f },
    { "chord1", "Chord note 2", "", -1.0f, 127.0f, -1.0f },
    { "chord2", "Chord note 3", "", -1.0f, 127.0f, -1.0f },
    { "chord3", "Chord note 4", "", -1.0f, 127.0f, -1.0f },
    { "chord4", "Chord note 5", "", -1.0f, 127.0f, -1.0f },
    { "root", "Chord root", "", -1.0f, 127.0f, -1.0f },
    { "follow", "Follow", "", 0.0f, 1.0f, 0.0f },   /* 5: a pitched recording onto the chord (0 = as recorded) */
};
enum { P_STRETCH, P_WINDOW, P_FREEZE, P_ONSET, P_WIDTH, P_SHAPE, P_SEED,
       P_TRANSPOSE, P_TUNE, P_FOCUS, P_PARTIALS, P_LAYERS, P_HARMONY, P_GLIDE, P_DRIFT, P_BLUR, P_START, P_END,
       P_CHORD0, P_ROOT = P_CHORD0 + 5, P_FOLLOW, P_COUNT };

struct Stretch : Device {
    float value[P_COUNT];
    double sr = 48000;
    Source src;
    Pcg32 rng;
    Voice v[2];
    int active = 0;
    bool fading = false;
    long long fade = 0;              /* samples since the incoming voice started */
    std::vector<float> tmp[2];
    float cached_window = -1; int cached_n = 0;

    Stretch() { for (int i = 0; i < P_COUNT; i++) value[i] = STRETCH_PARAMS[i].def; }
    void *cast(const char *kind) override { return std::strcmp(kind, "stretch") ? nullptr : this; }
    /* 5: the recording's pitch track; the one before is kept a generation (a voice reads it until its next frame) */
    std::vector<float> trk_f, trk_c, old_f, old_c; double trk_hop = 0.02; bool chord_seen = false;
    void set_track(const float *f0s, const float *confs, int n, double hop) {
        old_f.swap(trk_f); old_c.swap(trk_c);
        trk_f.assign(f0s && confs && n > 0 ? f0s : nullptr, f0s && confs && n > 0 ? f0s + n : nullptr);
        trk_c.assign(f0s && confs && n > 0 ? confs : nullptr, f0s && confs && n > 0 ? confs + n : nullptr);
        trk_hop = hop > 0 ? hop : 0.02;
    }

    const fs_param *params(int &n) override { n = P_COUNT; return STRETCH_PARAMS; }
    void set_param(int i, float x) override { value[i] = x; }

    /* the last chord the host gave; off every route the tune keeps it (Dm9 before any) */
    float last_chord[5] = { 62, 65, 69, 72, 76 }, last_root = 62;
    Controls controls() {
        Controls c{};
        c.log_s = value[P_STRETCH] * std::log(1024.0); c.freeze = value[P_FREEZE] >= 0.5f; c.onset = value[P_ONSET]; c.width = value[P_WIDTH];
        c.transpose = value[P_TRANSPOSE]; c.tune = value[P_TUNE]; c.focus = value[P_FOCUS]; c.partials = value[P_PARTIALS];
        c.layers = value[P_LAYERS]; c.harmony = value[P_HARMONY]; c.glide = value[P_GLIDE]; c.drift = value[P_DRIFT];
        c.blur = value[P_BLUR]; c.start = value[P_START]; c.end = value[P_END];
        bool any = false;
        for (int i = 0; i < 5; i++) any |= value[P_CHORD0 + i] >= 0;
        if (any) { for (int i = 0; i < 5; i++) last_chord[i] = value[P_CHORD0 + i]; last_root = value[P_ROOT] >= 0 ? value[P_ROOT] : value[P_CHORD0]; }
        for (int i = 0; i < 5; i++) c.chord[i] = last_chord[i];
        c.root = last_root;
        if (any) chord_seen = true;
        c.follow = chord_seen ? value[P_FOLLOW] : 0; c.tn = (int)trk_f.size();   /* before any chord, no follow (5 I2) */ c.tf = c.tn ? trk_f.data() : nullptr; c.tc = c.tn ? trk_c.data() : nullptr; c.thop = trk_hop;
        c.shaping = std::fabs(c.transpose) > 1e-4 || c.tune > 0 || (c.layers >= 0.5f && c.harmony > 0) || c.blur > 0 || (c.follow > 0 && c.tn > 0);
        return c;
    }
    int target_n() {
        if (value[P_WINDOW] != cached_window) {
            cached_window = value[P_WINDOW];
            int n0 = (int)std::floor(cached_window * sr);
            cached_n = window_size(n0 < 16 ? 16 : n0);
        }
        return cached_n;
    }
    uint64_t voice_seed() { return ((uint64_t)rng.next() << 32) | rng.next(); }

    void prepare(float sample_rate, int max_block) override {
        sr = sample_rate;
        rng.seed((uint64_t)value[P_SEED], 1);
        cached_window = -1;
        const int nmax = window_size((int)std::floor(STRETCH_PARAMS[P_WINDOW].max * sr));
        for (auto &x : v) { x.reserve(nmax, sr); x.src = &src; }
        for (auto &t : tmp) t.assign(max_block, 0.0f);
        active = 0; fading = false;
        v[0].start(target_n(), value[P_SHAPE] >= 0.5f, 0.0, voice_seed(), controls());
    }

    void set_source(int channels, int frames, const float *const *s) override {
        set_track(nullptr, nullptr, 0, 0.02);    /* a new recording: the last one's pitch track goes (final review 5 I1) */
        if (channels < 1 || frames < 1 || !s) { src = Source(); return; }
        src = Source();
        src.ch[0] = s[0]; src.ch[1] = channels > 1 ? s[1] : s[0]; src.len = frames;
        for (auto &x : v) if (x.pos >= frames) x.pos = std::fmod(x.pos, (double)frames);
    }

    void set_source_i16(int channels, int frames, const int16_t *const *s) override {
        set_track(nullptr, nullptr, 0, 0.02);
        if (channels < 1 || frames < 1 || !s) { src = Source(); return; }
        src = Source();
        src.s16[0] = s[0]; src.s16[1] = channels > 1 ? s[1] : s[0]; src.len = frames;
        for (auto &x : v) if (x.pos >= frames) x.pos = std::fmod(x.pos, (double)frames);
    }

    void stats(fs_stats_t &st) override {
        st.late_frames = v[0].late + v[1].late;
        st.frames = v[active].frames;
        st.wraps = v[active].wraps;
    }

    void process(int n) override {
        const Controls c = controls();
        Voice &a = v[active], &b = v[active ^ 1];
        const int want_n = target_n(), want_shape = value[P_SHAPE] >= 0.5f;
        if (!fading && (want_n != a.N || want_shape != a.shape)) {
            /* Same frame centre: the new voice reads from where the old one is, re-centred. */
            double p = a.pos + (a.N - want_n) / 2;
            if (src.len) { p = std::fmod(p, (double)src.len); if (p < 0) p += src.len; } else p = 0;
            b.start(want_n, want_shape, p, voice_seed(), c);
            fading = true; fade = 0;
        }
        a.render(out[0].data(), out[1].data(), n, c);
        if (!fading) return;
        b.render(tmp[0].data(), tmp[1].data(), n, c);
        /* The new voice needs one window to reach full overlap (silent hop, then a half-window
           rise); then an equal-power crossfade over one window (the voices are uncorrelated). */
        for (int i = 0; i < n; i++, fade++) {
            double x = (double)(fade - b.N) / b.N;
            x = x < 0 ? 0 : x > 1 ? 1 : x;
            float ga = (float)std::cos(1.5707963267948966 * x), gb = (float)std::sin(1.5707963267948966 * x);
            out[0][i] = out[0][i] * ga + tmp[0][i] * gb;
            out[1][i] = out[1][i] * ga + tmp[1][i] * gb;
        }
        if (fade >= 2LL * b.N) { active ^= 1; fading = false; }
    }
};

Device *make_stretch() { return new Stretch(); }

/* The host's side of the shaping, shared by the web engine and both apps' walks, so every platform
   reads a point the same way. The shaping's own names (properties.sound.shape); anything missing
   takes the parameter's default, which is dry (A-8). */
static const char *const SHAPE_KEYS[] = { "transpose", "tune", "focus", "partials", "layers", "harmony", "glide", "drift", "blur", "start", "end", "width", "follow" };
static int stretch_param(const char *id) { for (int i = 0; i < P_COUNT; i++) if (!std::strcmp(STRETCH_PARAMS[i].id, id)) return i; return -1; }

extern "C" void fs_stretch_shape(fs_device *d, const char *shape_json) {
    const Json sh = Json::parse(shape_json);
    for (const char *k : SHAPE_KEYS) {
        const int i = stretch_param(k);
        fs_set_param(d, i, (float)sh.n(k, STRETCH_PARAMS[i].def));
    }
}
/* 5: a stretch's recording's pitch track (f0 and confidence every hop s); follow >= 0 also sets how far it follows */
extern "C" void fs_stretch_track(fs_device *d, const float *f0s, const float *confs, int n, double hop, float follow) {
    Stretch *s = d ? (Stretch *)fs_device_impl(d)->cast("stretch") : nullptr; if (!s) return;
    s->set_track(f0s, confs, n, hop);
    if (follow >= 0) fs_set_param(d, P_FOLLOW, follow);
}
extern "C" void fs_stretch_chord(fs_device *d, fs_device *piece) {
    float notes[5] = { -1, -1, -1, -1, -1 }, root = -1;
    const int n = fs_piece_chord_notes(piece, notes, &root);
    if (n <= 0) return;                          /* no chord yet: the voices keep the last one */
    for (int k = n; k < 5; k++) notes[k] = -1;
    for (int k = 0; k < 5; k++) fs_set_param(d, P_CHORD0 + k, notes[k]);
    fs_set_param(d, P_ROOT, root);
}
