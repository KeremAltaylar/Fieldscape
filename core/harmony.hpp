/* The harmony core (sample harmony, docs/superpowers/specs/2026-09-30-sample-harmony-design.md):
   one place that turns "this note of this chord" into an exact frequency, for every engine.
   Equal temperament is today's mtof; just intonation takes the chord's root equal-tempered from the
   key and every other note as a pure ratio of it, so overtones line up and held notes do not beat. */
#pragma once
#include <cmath>

namespace harmony {

enum Tuning { EQUAL = 0, JUST = 1 };

inline double mtof(double m) { return 440 * std::pow(2.0, (m - 69) / 12); }

/* The chord root's ratio for an interval in semitones (any octave folded away). The minor seventh is
   the harmonic seventh 7/4 in a dominant chord, 9/5 otherwise. */
inline double just_ratio(int iv, bool dominant) {
    static const double R[12] = { 1, 16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3, 45.0 / 32, 3.0 / 2, 8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8 };
    iv = ((iv % 12) + 12) % 12;
    return iv == 10 && dominant ? 7.0 / 4 : R[iv];
}

inline double hz(int tuning, int m, int root_midi, bool dominant) {
    if (tuning != JUST) return mtof(m);
    int d = m - root_midi, oct = (int)std::floor(d / 12.0), iv = d - 12 * oct;
    return mtof(root_midi) * just_ratio(iv, dominant) * std::pow(2.0, oct);
}

/* One caller's snapping: the nearest target, held until the pitch is clearly past the midpoint
   (margin x the gap, rulebook A-14), and a glide toward it in log frequency. */
struct Follower {
    double margin = 0.3, glide_s = 0.5;
    int idx = -1;
    double at = 0, to = 0;
    static double cents(double a, double b) { return 1200 * std::log2(a / b); }
    int choose(double f0, const double *t, int n) {
        if (n <= 0 || !(f0 > 0)) return idx;
        int best = 0;
        for (int i = 1; i < n; i++) if (std::fabs(cents(f0, t[i])) < std::fabs(cents(f0, t[best]))) best = i;
        if (idx < 0 || idx >= n) idx = best;
        else if (best != idx) {
            double gap = std::fabs(cents(t[idx], t[best]));
            if (std::fabs(cents(f0, t[idx])) - std::fabs(cents(f0, t[best])) > margin * gap) idx = best;
        }
        to = t[idx];
        if (!(at > 0)) at = to;
        return idx;
    }
    double step(double dt) {
        if (!(to > 0)) return at;
        if (!(at > 0) || glide_s <= 0) { at = to; return at; }
        double c = cents(at, to) * std::exp(-dt / glide_s);
        at = std::fabs(c) < 0.01 ? to : to * std::pow(2.0, c / 1200);
        return at;
    }
};

}  // namespace harmony
