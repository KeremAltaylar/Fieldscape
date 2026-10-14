#pragma once
/* Sample harmony 6 (lab): a voice's five-band EQ - low shelf, three bells, high shelf (RBJ cookbook). Each band's
   frequency, gain and Q glide toward what was set (~30 ms, coefficients per block), so a drag never clicks. A band at
   0 dB that is not gliding passes its input through untouched - a flat EQ is the input, bit for bit - while keeping its
   history, so it comes in without a step. */
#include "json.hpp"
#include <algorithm>
#include <cmath>

struct Eq5 {
    enum { BANDS = 5 };
    struct Band {
        double tf, tg, tq, f, g, q;                        /* target and now: Hz, dB, Q */
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};
    } b[BANDS];
    double sr = 48000;
    void init(double s) {
        static const double F[BANDS] = { 80, 250, 1000, 4000, 10000 }, Q[BANDS] = { 0.7, 1, 1, 1, 0.7 };
        sr = s;
        for (int k = 0; k < BANDS; k++) { Band &x = b[k]; x = Band(); x.tf = x.f = F[k]; x.tg = x.g = 0; x.tq = x.q = Q[k]; }
    }
    void set(int k, double f, double g, double q) {
        if (k < 0 || k >= BANDS) return;
        Band &x = b[k];
        x.tf = std::fmin(16000.0, std::fmax(30.0, f)); x.tg = std::fmin(12.0, std::fmax(-12.0, g)); x.tq = std::fmin(8.0, std::fmax(0.3, q));
    }
    void design(int k, Band &x) {
        const double A = std::pow(10.0, x.g / 40), w = 2 * 3.141592653589793 * std::fmin(x.f, 0.45 * sr) / sr, c = std::cos(w), al = std::sin(w) / (2 * x.q);
        double B0, B1, B2, A0, A1, A2;
        if (k == 0 || k == BANDS - 1) {                     /* shelves */
            const double sA = 2 * std::sqrt(A) * al, s = k == 0 ? -1 : 1;
            B0 = A * ((A + 1) + s * (A - 1) * c + sA); B1 = -2 * s * A * ((A - 1) + s * (A + 1) * c); B2 = A * ((A + 1) + s * (A - 1) * c - sA);
            A0 = (A + 1) - s * (A - 1) * c + sA; A1 = 2 * s * ((A - 1) - s * (A + 1) * c); A2 = (A + 1) - s * (A - 1) * c - sA;
        } else {                                             /* bell */
            B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A;
        }
        x.b0 = B0 / A0; x.b1 = B1 / A0; x.b2 = B2 / A0; x.a1 = A1 / A0; x.a2 = A2 / A0;
    }
    void process(float *L, float *R, int n) {
        const double kg = 1 - std::exp(-n / (0.03 * sr));
        for (int k = 0; k < BANDS; k++) {
            Band &x = b[k];
            const bool moving = x.f != x.tf || x.g != x.tg || x.q != x.tq;
            if (moving) {
                x.f *= std::pow(x.tf / x.f, kg); x.g += (x.tg - x.g) * kg; x.q *= std::pow(x.tq / x.q, kg);
                if (std::fabs(x.f - x.tf) < 1e-3 * x.tf && std::fabs(x.g - x.tg) < 1e-3 && std::fabs(x.q - x.tq) < 1e-4) { x.f = x.tf; x.g = x.tg; x.q = x.tq; }
            }
            float *ch[2] = { L, R };
            if (!moving && x.g == 0) {                       /* flat: the input untouched, its history kept */
                for (int c = 0; c < 2; c++) if (n >= 2) { x.x2[c] = x.y2[c] = ch[c][n - 2]; x.x1[c] = x.y1[c] = ch[c][n - 1]; }
                    else if (n == 1) { x.x2[c] = x.x1[c]; x.y2[c] = x.y1[c]; x.x1[c] = x.y1[c] = ch[c][0]; }
                continue;
            }
            design(k, x);
            for (int c = 0; c < 2; c++) {
                float *s = ch[c];
                for (int i = 0; i < n; i++) {
                    const double in = s[i], y = x.b0 * in + x.b1 * x.x1[c] + x.b2 * x.x2[c] - x.a1 * x.y1[c] - x.a2 * x.y2[c];
                    x.x2[c] = x.x1[c]; x.x1[c] = in; x.y2[c] = x.y1[c]; x.y1[c] = std::fabs(y) < 1e-25 ? 0 : y;
                    s[i] = (float)x.y1[c];
                }
            }
        }
    }
};

/* Test 15: a point's "eq" ([[Hz, dB, Q] x5], absent: flat) and "env" ([attack s, decay s, sustain, release s], absent: none) -
   the route voices' formats (6, 8b), read the same way */
inline void eq_env_of(const Json *j, double eq[5][3], double env[4], bool &env_on) {
    static const double F[5] = { 80, 250, 1000, 4000, 10000 }, Q[5] = { 0.7, 1, 1, 1, 0.7 };
    const Json *e = j ? j->get("eq") : nullptr;
    for (int k = 0; k < 5; k++) {
        const Json *b = e ? e->at(k) : nullptr; const Json *f = b ? b->at(0) : nullptr, *g = b ? b->at(1) : nullptr, *q = b ? b->at(2) : nullptr;
        eq[k][0] = f && f->kind == Json::NUM ? f->num : F[k]; eq[k][1] = g && g->kind == Json::NUM ? g->num : 0; eq[k][2] = q && q->kind == Json::NUM ? q->num : Q[k];
    }
    const Json *ev = j ? j->get("env") : nullptr; env_on = false;
    if (ev && ev->kind == Json::ARR && ev->size() >= 4) {
        bool num = true; for (int k = 0; k < 4; k++) num = num && ev->at(k)->kind == Json::NUM && std::isfinite(ev->at(k)->num);
        if (num) {
            auto cl = [](double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); };
            env[0] = cl(ev->at(0)->num, 0.002, 8); env[1] = cl(ev->at(1)->num, 0.005, 8); env[2] = cl(ev->at(2)->num, 0, 1); env[3] = cl(ev->at(3)->num, 0.01, 12);
            env_on = true;
        }
    }
}
