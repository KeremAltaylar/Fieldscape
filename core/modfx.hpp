/* Modulation FX (docs/superpowers/specs/2026-11-17-modulation-fx-design.md): one engine, two places - a route's
   master FX (inside Piece) and the Loop points' instances (inside fs_mix). It makes no sound: silence in, exact 0 out;
   every module at mix 0, exact 0 out. Main thread: cfg_of. Audio thread: apply, clock, chord, process, live - they
   never allocate, lock or throw; every buffer is allocated in init. */
#pragma once
#include "json.hpp"
#include "tone.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace mfx {
using tone::PI;

enum Mod { SHIFT, SWARM, PHASE, COMB, VOWEL, CLOUD, TAPE, ECHO, SPACE, NMOD };
enum Src { S_LFO1, S_LFO2, S_LFO3, S_LFO4, S_ENV, S_CHORD, NSRC };   /* no walk sources: Kerem's decision 3 */
enum Taper { LIN, EXP, CUBIC };                      /* CUBIC: bipolar, x^3 (shift.hz) */
struct ParamDef { const char *key; Mod mod; float min, max, def; Taper taper; bool cont; };   /* cont: a matrix/morph destination */

/* The spec's table, in its order, ONE ROW PER LINE (the page's test reads these lines). Switches (cont false) hold
   their option index (mode ssb 0 / ring 1, tune chord 0 / free 1, zero and freeze off 0 / on 1, stages the count).
   Division rows (cont false) hold beats, 32n 0.125 ... 32m 128; their strings are Cfg::rate. */
inline constexpr ParamDef PARAMS[] = {
    { "shift.mix", SHIFT, 0, 1, 0, LIN, true },
    { "shift.hz", SHIFT, -2000, 2000, 0, CUBIC, true },
    { "shift.mode", SHIFT, 0, 1, 0, LIN, false },
    { "shift.fb", SHIFT, 0, 0.9f, 0, LIN, true },
    { "swarm.mix", SWARM, 0, 1, 0, LIN, true },
    { "swarm.rate", SWARM, 0.125f, 128, 4, EXP, false },
    { "swarm.depth", SWARM, 0, 1, 0.5f, LIN, true },
    { "swarm.delay", SWARM, 0.1f, 30, 8, EXP, true },
    { "swarm.fb", SWARM, -0.9f, 0.9f, 0, LIN, true },
    { "swarm.spread", SWARM, 0, 1, 1, LIN, true },
    { "swarm.zero", SWARM, 0, 1, 0, LIN, false },
    { "phase.mix", PHASE, 0, 1, 0, LIN, true },
    { "phase.stages", PHASE, 2, 12, 6, LIN, false },
    { "phase.hz", PHASE, 100, 8000, 800, EXP, true },
    { "phase.span", PHASE, 0, 4, 2, LIN, true },
    { "phase.rate", PHASE, 0.125f, 128, 2, EXP, false },
    { "phase.fb", PHASE, -0.95f, 0.95f, 0.3f, LIN, true },
    { "comb.mix", COMB, 0, 1, 0, LIN, true },
    { "comb.tune", COMB, 0, 1, 0, LIN, false },
    { "comb.hz", COMB, 40, 2000, 110, EXP, true },
    { "comb.decay", COMB, 0.05f, 20, 2, EXP, true },
    { "comb.damp", COMB, 500, 16000, 6000, EXP, true },
    { "comb.detune", COMB, 0, 50, 8, LIN, true },
    { "vowel.mix", VOWEL, 0, 1, 0, LIN, true },
    { "vowel.pos", VOWEL, 0, 4, 0, LIN, true },
    { "vowel.q", VOWEL, 2, 20, 8, EXP, true },
    { "vowel.size", VOWEL, 0.5f, 2, 1, EXP, true },
    { "cloud.mix", CLOUD, 0, 1, 0, LIN, true },
    { "cloud.size", CLOUD, 20, 500, 120, EXP, true },
    { "cloud.dens", CLOUD, 1, 40, 12, EXP, true },
    { "cloud.pitch", CLOUD, -24, 24, 0, LIN, true },
    { "cloud.spray", CLOUD, 0, 1, 0, LIN, true },
    { "cloud.scatter", CLOUD, 0, 1, 0.3f, LIN, true },
    { "cloud.rev", CLOUD, 0, 1, 0, LIN, true },
    { "cloud.freeze", CLOUD, 0, 1, 0, LIN, false },
    { "tape.mix", TAPE, 0, 1, 0, LIN, true },
    { "tape.wow", TAPE, 0, 1, 0.3f, LIN, true },
    { "tape.flutter", TAPE, 0, 1, 0.2f, LIN, true },
    { "tape.drive", TAPE, 0, 1, 0.2f, LIN, true },
    { "tape.tone", TAPE, 500, 16000, 9000, EXP, true },
    { "echo.mix", ECHO, 0, 1, 0, LIN, true },
    { "echo.time", ECHO, 0.125f, 128, 0.75f, EXP, false },
    { "echo.fb", ECHO, 0, 0.98f, 0.45f, LIN, true },
    { "echo.tone", ECHO, -1, 1, 0, LIN, true },
    { "echo.shift", ECHO, -200, 200, 0, LIN, true },
    { "echo.wobble", ECHO, 0, 1, 0, LIN, true },
    { "echo.spread", ECHO, 0, 1, 0.5f, LIN, true },
    { "space.mix", SPACE, 0, 1, 0, LIN, true },
    { "space.size", SPACE, 0.2f, 1, 0.7f, LIN, true },
    { "space.decay", SPACE, 0.3f, 60, 4, EXP, true },
    { "space.damp", SPACE, 500, 16000, 7000, EXP, true },
    { "space.mod", SPACE, 0, 1, 0.3f, LIN, true },
};
inline constexpr int NPARAM = (int)(sizeof PARAMS / sizeof *PARAMS);
static_assert(NPARAM <= 64, "Cfg::v holds 64");
enum Row { R_SH_HZ = 1, R_SH_MODE = 2, R_SH_FB = 3, R_SW_RATE = 5, R_SW_DEPTH = 6, R_SW_DELAY = 7, R_SW_FB = 8, R_SW_SPREAD = 9,
           R_SW_ZERO = 10, R_PH_STAGES = 12, R_PH_HZ = 13, R_PH_SPAN = 14, R_PH_RATE = 15, R_PH_FB = 16,
           R_CB_TUNE = 18, R_CB_HZ = 19, R_CB_DECAY = 20, R_CB_DAMP = 21, R_CB_DETUNE = 22, R_VW_POS = 24, R_VW_Q = 25, R_VW_SIZE = 26,
           R_CL_SIZE = 28, R_CL_DENS = 29, R_CL_PITCH = 30, R_CL_SPRAY = 31, R_CL_SCATTER = 32, R_CL_REV = 33, R_CL_FREEZE = 34,
           R_TP_WOW = 36, R_TP_FLUTTER = 37, R_TP_DRIVE = 38, R_TP_TONE = 39 };   /* the PARAMS rows the modules read (test_table checks them) */
inline constexpr const char *MOD_NAMES[NMOD] = { "shift", "swarm", "phase", "comb", "vowel", "cloud", "tape", "echo", "space" };
inline constexpr const char *SRC_NAMES[NSRC] = { "lfo1", "lfo2", "lfo3", "lfo4", "env", "chord" };
inline constexpr const char *LFO_SHAPES[] = { "sine", "tri", "up", "down", "square", "steps", "drift" };
inline constexpr int NSHAPE = (int)(sizeof LFO_SHAPES / sizeof *LFO_SHAPES);

inline int param_of(const char *key) {
    if (key) for (int i = 0; i < NPARAM; i++) if (!std::strcmp(PARAMS[i].key, key)) return i;
    return -1;
}

/* The division strings (spec A-9) and their beats; anything else is 1 beat. */
struct Division { const char *s; double beats; };
inline constexpr Division DIVISIONS[] = {
    { "32m", 128 }, { "16m", 64 }, { "8m", 32 }, { "4m", 16 }, { "2m", 8 }, { "1n", 4 }, { "2n", 2 }, { "4n", 1 }, { "8n", 0.5 },
    { "16n", 0.25 }, { "32n", 0.125 }, { "2n.", 3 }, { "4n.", 1.5 }, { "8n.", 0.75 }, { "16n.", 0.375 },
    { "4t", 2.0 / 3 }, { "8t", 1.0 / 3 }, { "16t", 1.0 / 6 },
};
inline const Division *division(const char *d) {
    if (d) for (auto &v : DIVISIONS) if (!std::strcmp(v.s, d)) return &v;
    return nullptr;
}
inline double div_beats(const char *d) { const Division *v = division(d); return v ? v->beats : 1; }

struct LfoCfg { uint8_t shape = 0; char rate[5] = "1n"; float phase = 0; };
struct Slot { int8_t src = -1; int16_t dst = -1; float amt = 0; };
struct Cfg {                                         /* POD, no pointers: copied whole between threads */
    float v[64];                                     /* every PARAMS row's value, by index */
    char rate[NMOD][5];                              /* the division rows' strings, by module ("" for the rest) */
    float ret = 0, send[3] = { 1, 1, 1 }, wet = 0; bool on = false;
    LfoCfg lfo[4]; float env_a = 10, env_r = 300, env_s = 1; Slot mods[8];
};
static_assert(std::is_trivially_copyable<Cfg>::value, "Cfg is copied whole");

/* Zeroed first, so two Cfgs with the same settings are byte-identical (padding included). */
inline void cfg_defaults(Cfg &c) {
    std::memset((void *)&c, 0, sizeof c);
    for (int i = 0; i < NPARAM; i++) {
        c.v[i] = PARAMS[i].def;
        if (!PARAMS[i].cont && PARAMS[i].taper == EXP)
            for (auto &d : DIVISIONS) if ((float)d.beats == PARAMS[i].def) std::strcpy(c.rate[PARAMS[i].mod], d.s);
    }
    c.send[0] = c.send[1] = c.send[2] = 1;
    static const char *LFO_RATES[4] = { "1n", "2n", "2m", "4m" };
    for (int i = 0; i < 4; i++) { c.lfo[i].shape = 0; std::strcpy(c.lfo[i].rate, LFO_RATES[i]); c.lfo[i].phase = 0; }
    c.env_a = 10; c.env_r = 300; c.env_s = 1;
    for (auto &s : c.mods) { s.src = -1; s.dst = -1; s.amt = 0; }
}

/* A finite number in [lo, hi], else def. */
inline float num_of(const Json *j, const char *k, float lo, float hi, float def) {
    const Json *v = j ? j->get(k) : nullptr;
    if (!v || v->kind != Json::NUM || !std::isfinite(v->num)) return def;
    return (float)std::fmin(hi, std::fmax(lo, v->num));
}
inline int name_of(const char *s, const char *const *names, int n) {
    for (int i = 0; i < n; i++) if (!std::strcmp(s, names[i])) return i;
    return -1;
}

/* Absent/junk -> default; every number clamped to its row; mods beyond 8 dropped; an unknown src or dst (or a switch
   or division dst) empties that slot. Main thread only (Json allocates). */
inline void cfg_of(const Json *j, Cfg &c) {
    cfg_defaults(c);
    if (!j || j->kind != Json::OBJ) return;
    c.ret = num_of(j, "ret", 0, 1, 0);
    c.wet = num_of(j, "wet", 0, 1, 0);
    if (const Json *o = j->get("on")) c.on = o->kind == Json::BOOL ? o->b : o->kind == Json::NUM && o->num != 0;   /* a string reads false */
    if (const Json *s = j->get("send"))
        for (int q = 0; q < 3; q++) { const Json *v = s->at(q); if (v && v->kind == Json::NUM && std::isfinite(v->num)) c.send[q] = (float)std::fmin(1, std::fmax(0, v->num)); }
    for (int i = 0; i < NPARAM; i++) {
        const ParamDef &p = PARAMS[i];
        const Json *m = j->get(MOD_NAMES[p.mod]);
        const char *k = std::strchr(p.key, '.') + 1;
        const Json *v = m ? m->get(k) : nullptr;
        if (!v) continue;
        if (p.cont) { c.v[i] = num_of(m, k, p.min, p.max, p.def); continue; }
        if (p.taper == EXP) {                        /* a division */
            const Division *d = v->kind == Json::STR ? division(v->str.c_str()) : nullptr;
            if (d) { std::strcpy(c.rate[p.mod], d->s); c.v[i] = (float)d->beats; }
            continue;
        }
        if (!std::strcmp(k, "stages")) {             /* 2/4/6/8/12: the nearest */
            if (v->kind != Json::NUM || !std::isfinite(v->num)) continue;
            static const float ST[] = { 2, 4, 6, 8, 12 };
            float best = p.def;
            for (float s : ST) if (std::fabs(s - v->num) < std::fabs(best - v->num)) best = s;
            c.v[i] = best;
            continue;
        }
        static const char *MODE[] = { "ssb", "ring" }, *TUNE[] = { "chord", "free" };
        int o = -1;
        if (v->kind == Json::STR) o = name_of(v->str.c_str(), !std::strcmp(k, "mode") ? MODE : TUNE, 2);
        else if (v->kind == Json::BOOL) o = v->b;
        else if (v->kind == Json::NUM && std::isfinite(v->num)) o = v->num >= 0.5;
        if (o >= 0) c.v[i] = (float)o;
    }
    if (const Json *l = j->get("lfo"))
        for (int i = 0; i < 4; i++) {
            const Json *o = l->at(i);
            if (!o || o->kind != Json::OBJ) continue;
            if (const Json *s = o->get("shape")) {
                int n = s->kind == Json::STR ? name_of(s->str.c_str(), LFO_SHAPES, NSHAPE)
                      : s->kind == Json::NUM && std::isfinite(s->num) && s->num >= 0 && s->num < NSHAPE ? (int)s->num : -1;
                if (n >= 0) c.lfo[i].shape = (uint8_t)n;
            }
            if (const Division *d = division(o->s("rate", "").c_str())) std::strcpy(c.lfo[i].rate, d->s);
            c.lfo[i].phase = num_of(o, "phase", 0, 1, 0);
        }
    if (const Json *e = j->get("env")) {
        c.env_a = num_of(e, "attack", 1, 200, 10);
        c.env_r = num_of(e, "release", 20, 2000, 300);
        c.env_s = num_of(e, "sens", 0, 4, 1);
    }
    if (const Json *m = j->get("mods"))
        for (int i = 0; i < 8; i++) {
            const Json *o = m->at(i);
            if (!o || o->kind != Json::OBJ) continue;
            int src = name_of(o->s("src", "").c_str(), SRC_NAMES, NSRC), dst = param_of(o->s("dst", "").c_str());
            if (src < 0 || dst < 0 || !PARAMS[dst].cont) continue;
            c.mods[i].src = (int8_t)src; c.mods[i].dst = (int16_t)dst; c.mods[i].amt = num_of(o, "amt", -1, 1, 0);
        }
}

struct Live { float v[32]; };   /* 0-3 lfo phase, 4-7 lfo value, 8 env, 9-11 reserved (0), 12 chord, 13-21 module levels,
                                   22 in, 23 out, 24-31 each slot's destination now (0-1) */

/* A linear ramp: reaches its target exactly, so a mix of 0 is exactly 0 (the skip and the silence depend on it). */
struct Ramp {
    float cur = 0, tgt = 0, step = 1;
    float next() { cur = cur < tgt ? std::fmin(tgt, cur + step) : std::fmax(tgt, cur - step); return cur; }
    bool off() const { return cur == 0 && tgt == 0; }
};

inline float flush(float x) { return std::fabs(x) < 1e-25f ? 0.0f : x; }

/* A-18: Comb, Vowel and Tape hold their level - output gain = the slow (400 ms) in/out RMS ratio, within +-12 dB.
   Each mean square rises at 10 ms (falls at 400), as an AGC's attack: a timbre move that brings a tone onto a
   resonance is caught within the move, not 400 ms later (+12 dB for that long otherwise).
   gain() returns this block's gain; `last` is the previous one, so the caller ramps last -> g across the block.
   A block whose input is under half its average (a tail ringing out after the sound stopped) holds the gain: Comb's
   ring is not ducked by the input going quiet. */
struct LevelMatch {
    double k = 1 / 19200.0, kup = 1 / 1440.0, ein = 0, eout = 0; float g = 1, last = 1; bool fresh = true;
    void init(double sr) { k = 1 / (0.4 * sr); kup = 1 / (0.01 * sr); clear(); }
    void clear() { ein = eout = 0; g = last = 1; fresh = true; }
    static void follow(double &e, double x, double a_up, double a_dn) { double a = x > e ? a_up : a_dn; e = a * e + (1 - a) * x; }
    float gain(const float *in, const float *out, int n) {
        double si = 0, so = 0;
        for (int i = 0; i < n; i++) { si += (double)in[i] * in[i]; so += (double)out[i] * out[i]; }
        const double dn = std::exp(-n * k), up = std::exp(-n * kup);
        const bool falling = si / n < 0.5 * ein;
        follow(ein, si / n, up, dn); follow(eout, so / n, up, dn);
        last = g;
        if (ein > 1e-12 && eout > 1e-12 && (fresh || !falling)) {   /* silence keeps the gain it had */
            g = (float)std::fmin(3.981, std::fmax(0.2512, std::sqrt(ein / eout)));
            if (fresh) { last = g; fresh = false; }
        }
        return g;
    }
};

/* Olli Niemitalo's Hilbert pair: two chains of four second-order all-passes y[n] = c (x[n] + y[n-2]) - x[n-2], c = a^2;
   chain 1 then one sample of delay is I, minus chain 2 is Q: the analytic signal I + jQ across the audio band. */
constexpr float sq(double a) { return (float)(a * a); }
struct Hilbert {
    static constexpr float C[8] = { sq(0.6923878), sq(0.9360654322959), sq(0.9882295226860), sq(0.9987488452737),
                                    sq(0.4021921162426), sq(0.8561710882420), sq(0.9722909545651), sq(0.9952884791278) };
    float z[2][8][4], d[2];                          /* per side, per section: x1 x2 y1 y2; the delay */
    void clear() { std::memset(z, 0, sizeof z); d[0] = d[1] = 0; }
    void run(int ch, float x, float &i, float &q) {
        float a = x, b = x;
        for (int k = 0; k < 8; k++) {
            float *s = z[ch][k], u = k < 4 ? a : b;
            float y = flush(C[k] * (u + s[3]) - s[1]);
            s[1] = s[0]; s[0] = u; s[3] = s[2]; s[2] = y;
            (k < 4 ? a : b) = y;
        }
        i = d[ch]; d[ch] = a; q = -b;               /* chain 2 lags I by 90 degrees: Q = -chain 2 (x = cos gives Q = sin) */
    }
};

struct Engine {
    double sr = 48000, bpm = 72, beats = 0;
    Cfg cfg;
    Ramp mix[NMOD];
    bool dirty[NMOD] = {};                           /* skipped since it last ran: clear its state before it runs again */
    float chord_midi[5] = { -1, -1, -1, -1, -1 }, chord_root = -1; int chord_serial = 0;
    float lvl[NMOD] = {}, lvl_in = 0, lvl_out = 0;
    int quiet = 0;                                   /* frames the output has stayed under -80 dB */
    float xL[128], xR[128], yL[128], yR[128], keep[128];   /* keep: the product of (1 - mix) over the modules run */
    float tA[128], tB[128];                          /* scratch (the level matches: the module input mid, its output mid) */
    float sm[64];                                    /* the continuous rows, glided (30 ms) */
    float sk = 0.4f, sk1 = 0.001f, sk_fast = 1;      /* glide coefficients: 30 ms per 16 samples, 30 ms per sample, 5 ms per 16 (Phase's sweep) */
    float sk100 = 0.15f, sk1s = 0.0003f, oct16 = 0.0067f;   /* 100 ms per 16 (delay times: the tape glide), 1 s per 16, 20 octaves/s per 16 */
    int warm[NMOD] = {};                             /* samples a module has run since it was cleared (its input fades in over 10 ms) */

    /* Shift */
    Hilbert hil; tone::DelayLine sh_dl[2]; double sh_ph = 0; float sh_ssb = 1; int sh_D = 1440;
    /* Phase: 12 first-order all-passes a side; the stage count cross-fades between two taps (30 ms) */
    float ph_x1[2][12], ph_y1[2][12], ph_fb[2], ph_lf[2], ph_a[2] = {}, ph_nf = 0.2679f; int ph_from = 6, ph_to = 6; float ph_x = 1;
    /* Vowel */
    tone::Biquad vw_bp[3]; float vw_c[3][6] = {}; LevelMatch vw_lm;   /* vw_c: the last design (b0 b1 b2 a1 a2, gain) */
    /* Swarm: one line a side, three taps; sw_d the taps' delays (and the zero tap's, [3]) at the last 16 */
    tone::DelayLine sw_dl[2]; double sw_off = 0, sw_rate = 4; float sw_z = 0, sw_d[2][4] = {};
    /* Comb: five lines a side; cb_cur/tgt log2 Hz; cb_k per side and comb: delay, feedback, input gain at the last 16 */
    tone::DelayLine cb_dl[2][5]; float cb_cur[5] = {}, cb_tgt[5] = {}, cb_step[5] = {}, cb_tune = 0, cb_a = 0.5f;
    float cb_k[2][5][3] = {}, cb_lp[2][5] = {}, cb_hx[2] = {}, cb_hy[2] = {}; int cb_serial = 0; LevelMatch cb_lm;
    /* Tape: one moving read a side (wow, flutter), the drive's level follower, the age filter */
    tone::DelayLine tp_dl[2]; double tp_pw = 0, tp_pf = 0; float tp_d = 1, tp_c = 1, tp_dr = 0, tp_dt = 0, tp_ms[2] = {}, tp_lp[2] = {};
    uint32_t tp_rng = 1; int tp_cnt = 0; LevelMatch tp_lm;
    /* Cloud: the capture lines (2.5 s: grains read the last 2 s, and live up to 0.5 s more), eight grains, the onset
       phase, the freeze cross-fade, the seeded random */
    struct Grain { double d = 0; float dd = 0, g = 0; int k = 0, len = 0; double c = 1, s = 0, cr = 1, sr = 0; };
    tone::DelayLine cl_dl[2]; Grain cl_g[8]; double cl_ph = 1; float cl_j = 1, cl_fz = 0; uint32_t cl_rng = 1; int cl_R = 96000;
    static constexpr uint32_t CL_SEED = 0x9E3779B9u;
    static constexpr double TP_WOW = 0.02 / (2 * 3.141592653589793 * 0.4), TP_FLUTTER = 0.004 / (2 * 3.141592653589793 * 7.3);   /* seconds of swing for +-2 %, +-0.4 % */

    Engine() { cfg_defaults(cfg); std::memcpy(sm, cfg.v, sizeof sm); }
    void init(double rate) {
        sr = rate;
        for (auto &m : mix) m.step = (float)(1 / (0.03 * sr));
        sk = (float)(1 - std::exp(-16 / (0.03 * sr))); sk1 = (float)(1 - std::exp(-1 / (0.03 * sr))); sk_fast = (float)(1 - std::exp(-16 / (0.005 * sr)));
        sh_D = (int)(0.03 * sr);
        for (auto &l : sh_dl) l.init(sh_D + 2);
        sk100 = (float)(1 - std::exp(-16 / (0.1 * sr))); sk1s = (float)(1 - std::exp(-16 / sr)); oct16 = (float)(20 * 16 / sr);
        for (auto &b : vw_bp) b.type = tone::BANDPASS;
        vw_lm.init(sr); cb_lm.init(sr); tp_lm.init(sr);
        for (auto &l : sw_dl) l.init((int)(0.06 * sr) + 2);
        for (auto &s : cb_dl) for (auto &l : s) l.init((int)(sr / 35) + 8);
        for (auto &l : tp_dl) l.init((int)(2 * (TP_WOW + TP_FLUTTER) * sr) + 8);
        cl_R = (int)(2 * sr);
        for (auto &l : cl_dl) l.init(cl_R + (int)(0.5 * sr) + 8);
        reset();
    }
    void apply(const Cfg &c) {
        cfg = c;
        for (int m = 0; m < NMOD; m++) mix[m].tgt = c.v[mix_row(m)];
    }
    void clock(double b, double pos) { bpm = b; beats = pos; }   /* bpm 0 holds the transport (the tests' LFO freeze) */
    /* The caller bumps `serial` on every chord change: Comb glides (80 ms) only on a new serial; new notes under the
       same serial jump the combs. */
    void chord(const float *midi5, float root, int serial) {
        for (int i = 0; i < 5; i++) chord_midi[i] = midi5[i];
        chord_root = root; chord_serial = serial;
    }
    /* Writes the wet only: the chain minus the input it left untouched, wet = chain - prod(1 - mix_m) * in, so the
       return carries only processed sound and one module's wet is mix * f(x) (linear in its mix). Any n: 128 at a time. */
    void process(const float *inL, const float *inR, float *wetL, float *wetR, int n) {
        float pin = 0, pout = 0;
        for (int o = 0; o < n; o += 128) {
            block(inL + o, inR + o, wetL + o, wetR + o, n - o < 128 ? n - o : 128);
            pin = std::fmax(pin, lvl_in); pout = std::fmax(pout, lvl_out);
        }
        lvl_in = pin; lvl_out = pout;
    }
    /* Nothing on, or the tail under -80 dB for 0.5 s. */
    bool idle() const { return all_off() || quiet >= (int)(0.5 * sr); }
    void reset() {
        for (int m = 0; m < NMOD; m++) { clear(m); dirty[m] = true; mix[m].cur = 0; lvl[m] = 0; }
        quiet = 0; lvl_in = lvl_out = 0;
    }
    void live(Live &out) const {
        std::memset(out.v, 0, sizeof out.v);         /* 0-8, 12, 24-31: the modulators (Task 6) */
        for (int m = 0; m < NMOD; m++) out.v[13 + m] = lvl[m];
        out.v[22] = lvl_in; out.v[23] = lvl_out;
    }

private:
    bool all_off() const { for (auto &m : mix) if (!m.off()) return false; return true; }
    void block(const float *inL, const float *inR, float *wetL, float *wetR, int n) {
        float pin = 0, pout = 0;
        for (int i = 0; i < n; i++) pin = std::fmax(pin, std::fmax(std::fabs(inL[i]), std::fabs(inR[i])));
        lvl_in = pin;
        if (all_off()) {
            std::memset(wetL, 0, sizeof(float) * n); std::memset(wetR, 0, sizeof(float) * n);
            for (int m = 0; m < NMOD; m++) { lvl[m] = 0; dirty[m] = true; }
            lvl_out = 0; quiet = 0;                  /* idle by all_off(); the tail count starts when a mix rises */
            beats += n * bpm / 60 / sr;
            return;
        }
        std::memcpy(xL, inL, sizeof(float) * n); std::memcpy(xR, inR, sizeof(float) * n);
        for (int i = 0; i < n; i++) keep[i] = 1;
        for (int m = 0; m < NMOD; m++) {
            if (mix[m].off()) { dirty[m] = true; lvl[m] = 0; continue; }   /* skipped: bit-exact pass-through */
            if (dirty[m]) { clear(m); dirty[m] = false; warm[m] = 0; }
            std::memcpy(yL, xL, sizeof(float) * n); std::memcpy(yR, xR, sizeof(float) * n);
            if (warm[m] < (int)(0.01 * sr)) {        /* cold: its input fades in (smoothstep, 10 ms), so empty delay lines never step */
                const float W = (float)(0.01 * sr);
                for (int i = 0; i < n; i++) { float t = std::fmin(1.0f, (warm[m] + i + 1) / W), r = t * t * (3 - 2 * t); yL[i] *= r; yR[i] *= r; }
                warm[m] += n;
            }
            run(m, yL, yR, n);
            float pk = 0;
            for (int i = 0; i < n; i++) {
                float k = mix[m].next();
                xL[i] += k * (yL[i] - xL[i]); xR[i] += k * (yR[i] - xR[i]);
                keep[i] *= 1 - k;
                pk = std::fmax(pk, std::fmax(std::fabs(yL[i]), std::fabs(yR[i])));
            }
            lvl[m] = pk;
        }
        for (int i = 0; i < n; i++) {
            float g = keep[i];
            wetL[i] = g == 1 ? 0.0f : xL[i] - g * inL[i]; wetR[i] = g == 1 ? 0.0f : xR[i] - g * inR[i];   /* exact +0 */
            if (wetL[i] == 0) wetL[i] = 0.0f;
            if (wetR[i] == 0) wetR[i] = 0.0f;
            pout = std::fmax(pout, std::fmax(std::fabs(wetL[i]), std::fabs(wetR[i])));
        }
        lvl_out = pout;
        quiet = pout < 1e-4f ? quiet + n : 0;
        beats += n * bpm / 60 / sr;
    }
    static int mix_row(int m) {                      /* each module's rows start with its mix */
        for (int i = 0; i < NPARAM; i++) if (PARAMS[i].mod == m) return i;
        return 0;
    }
    /* The modules. Gains glide per sample (sm, 30 ms); filter coefficients are designed every 16 samples from the glided
       rows and interpolated per sample between those designs, so no parameter ever steps. */
    float glide(int row) { return sm[row] += sk1 * (cfg.v[row] - sm[row]); }
    float glide16(int row) { return sm[row] += sk * (cfg.v[row] - sm[row]); }
    float glide100(int row) { return sm[row] += sk100 * (cfg.v[row] - sm[row]); }
    /* A frequency glided in octaves, per 16, at most 20 octaves a second: a 5-octave jump takes 250 ms, not one step of
       brightness (the click metric weighs f^2; a filter opening faster than ~2 octaves in 50 ms reads as a click). */
    float glide_oct(int row, float k) {
        const float c = std::log2(sm[row]), d = k * (std::log2(cfg.v[row]) - c);
        return sm[row] = std::exp2(c + std::fmax(-oct16, std::fmin(oct16, d)));
    }
    static void zero(tone::DelayLine &l) { std::fill(l.buf.begin(), l.buf.end(), 0.0f); }
    void run(int m, float *L, float *R, int n) {
        if (m == SHIFT) shift(L, R, n);
        else if (m == PHASE) phase(L, R, n);
        else if (m == VOWEL) vowel(L, R, n);
        else if (m == SWARM) swarm(L, R, n);
        else if (m == COMB) comb(L, R, n);
        else if (m == TAPE) tape(L, R, n);
        else if (m == CLOUD) cloud(L, R, n);
    }
    void clear(int m) {
        for (int i = 0; i < NPARAM; i++) if (PARAMS[i].mod == m) sm[i] = cfg.v[i];
        if (m == SHIFT) {
            hil.clear(); sh_ph = 0; sh_ssb = cfg.v[R_SH_MODE] < 0.5f;
            for (auto &l : sh_dl) std::fill(l.buf.begin(), l.buf.end(), 0.0f);
        } else if (m == PHASE) {
            std::memset(ph_x1, 0, sizeof ph_x1); std::memset(ph_y1, 0, sizeof ph_y1); ph_fb[0] = ph_fb[1] = 0;
            ph_from = ph_to = (int)cfg.v[R_PH_STAGES]; ph_x = 1; ph_nf = (float)std::tan(PI / (2 * ph_to));
            ph_lf[0] = ph_lf[1] = -1;                /* -1: take the sweep's value at once */
        } else if (m == VOWEL) {
            for (auto &b : vw_bp) { std::memset(b.x1, 0, sizeof b.x1); std::memset(b.x2, 0, sizeof b.x2); std::memset(b.y1, 0, sizeof b.y1); std::memset(b.y2, 0, sizeof b.y2); }
            vw_design(vw_c);
            vw_lm.clear();
        } else if (m == SWARM) {
            for (auto &l : sw_dl) zero(l);
            sw_off = 0; sw_rate = cfg.v[R_SW_RATE] > 0 ? cfg.v[R_SW_RATE] : 4; sw_z = cfg.v[R_SW_ZERO] >= 0.5f;
            sw_delays(beats, sw_d);
        } else if (m == COMB) {
            for (auto &s : cb_dl) for (auto &l : s) zero(l);
            std::memset(cb_lp, 0, sizeof cb_lp); cb_hx[0] = cb_hx[1] = cb_hy[0] = cb_hy[1] = 0;
            cb_serial = chord_serial; cb_tune = cfg.v[R_CB_TUNE];
            cb_targets();
            for (int i = 0; i < 5; i++) { cb_cur[i] = cb_tgt[i]; cb_step[i] = 0; }
            cb_a = cb_coef(); cb_design(cb_k);
            cb_lm.clear();
        } else if (m == TAPE) {
            for (auto &l : tp_dl) zero(l);
            tp_pw = tp_pf = 0; tp_d = 1; tp_c = tp_coef(); tp_dr = tp_dt = 0; tp_rng = 1; tp_cnt = 0;
            tp_ms[0] = tp_ms[1] = tp_lp[0] = tp_lp[1] = 0;
            tp_lm.clear();
        } else if (m == CLOUD) {
            for (auto &l : cl_dl) zero(l);
            for (auto &g : cl_g) g.len = 0;
            cl_ph = 1; cl_j = 1; cl_fz = cfg.v[R_CL_FREEZE] >= 0.5f; cl_rng = CL_SEED;
        }
    }

    /* Shift: single side band I cos(phi) - Q sin(phi), or ring I cos(phi) (the Q term fades over 30 ms on a mode change);
       feedback through 30 ms re-enters the Hilbert, so every pass moves by hz again (barber-pole). */
    void shift(float *L, float *R, int n) {
        const float xstep = (float)(1 / (0.03 * sr)), want = cfg.v[R_SH_MODE] < 0.5f;
        float *x[2] = { L, R };
        for (int o = 0; o < n; o += 16) {
            const double inc = 2 * PI * glide16(R_SH_HZ) / sr;   /* a frequency step keeps the phase: no click */
            for (int i = o; i < o + 16 && i < n; i++) {
                float fb = glide(R_SH_FB);
                sh_ssb = sh_ssb < want ? std::fmin(want, sh_ssb + xstep) : std::fmax(want, sh_ssb - xstep);
                float c = (float)std::cos(sh_ph), s = (float)std::sin(sh_ph) * sh_ssb;
                sh_ph += inc; if (sh_ph > PI) sh_ph -= 2 * PI; else if (sh_ph < -PI) sh_ph += 2 * PI;
                for (int ch = 0; ch < 2; ch++) {
                    float v = x[ch][i] + fb * sh_dl[ch].read(sh_D), I, Q;
                    hil.run(ch, v, I, Q);
                    float y = I * c - Q * s;
                    sh_dl[ch].push(y);
                    x[ch][i] = y;
                }
            }
        }
    }

    /* Phase: 0.5 (x + the all-pass chain), the chain's input x + fb * its last output. The corner is set so the LOWEST
       notch sits at hz 2^(span (s - 1/2)), s = the LFO (raised cosine of its phase, from the transport); right side 90
       degrees behind. All 12 stages always run (warm), and a stage count change cross-fades the two taps (30 ms) while
       the corner glides to the new count. */
    void phase(float *L, float *R, int n) {
        const int want = (int)cfg.v[R_PH_STAGES];
        const float xstep = (float)(16 / (0.03 * sr));
        if (want != ph_to) { ph_from = ph_to; ph_to = want; ph_x = 0; }   /* ponytail: a change mid-fade restarts from the target tap */
        const float nf_t = (float)std::tan(PI / (2 * ph_to));
        const double rate = cfg.v[R_PH_RATE] > 0 ? cfg.v[R_PH_RATE] : 2, bps = bpm / 60 / sr;
        float *x[2] = { L, R };
        for (int o = 0; o < n; o += 16) {
            const int m = n - o < 16 ? n - o : 16;
            float hz = glide16(R_PH_HZ), span = glide16(R_PH_SPAN), fb0 = sm[R_PH_FB];
            ph_nf += sk * (nf_t - ph_nf);
            const float x0 = ph_x; ph_x = std::fmin(1.0f, ph_x + xstep);
            for (int ch = 0; ch < 2; ch++) {
                double p = (beats + (o + m) * bps) / rate - ch * 0.25; p -= std::floor(p);
                float s = (float)(0.5 - 0.5 * std::cos(2 * PI * p));
                float lf = std::log2(hz) + span * (s - 0.5f);   /* the lowest notch, log2 Hz */
                bool first = ph_lf[ch] < 0;
                ph_lf[ch] = first ? lf : ph_lf[ch] + sk_fast * (lf - ph_lf[ch]);   /* a rate change never jumps the sweep */
                double fn = std::fmin(0.45 * sr, std::exp2((double)ph_lf[ch]));
                double t = std::fmin(1e4, std::tan(PI * fn / sr) / ph_nf);
                const float a1 = (float)((t - 1) / (t + 1)), a0 = first ? a1 : ph_a[ch];
                ph_a[ch] = a1;
                float *x1 = ph_x1[ch], *y1 = ph_y1[ch];
                if (ch) sm[R_PH_FB] = fb0;               /* both sides glide fb alike */
                for (int i = o, j = 1; i < o + m; i++, j++) {
                    const float a = a0 + (a1 - a0) * j / m, xf = x0 + (ph_x - x0) * j / m, fb = glide(R_PH_FB);
                    float u = x[ch][i] + fb * ph_fb[ch], tf = 0, tt = 0;
                    for (int k = 0; k < 12; k++) {
                        float y = flush(a * u + x1[k] - a * y1[k]);
                        x1[k] = u; y1[k] = y; u = y;
                        if (k + 1 == ph_from) tf = y;
                        if (k + 1 == ph_to) tt = y;
                    }
                    float ap = tf + xf * (tt - tf);
                    ph_fb[ch] = ap;
                    x[ch][i] = 0.5f * (x[ch][i] + ap);
                }
            }
        }
    }

    /* Vowel: three band-passes (gains 1, 1/2, 1/4, made up by sqrt(q)) on the formants of a e i o u, interpolated in
       log frequency by pos, x size; the level held by LevelMatch on the mid. */
    void vw_design(float c[3][6]) {
        static const float F[3][5] = { { 800, 400, 300, 450, 325 }, { 1150, 1700, 2200, 800, 700 }, { 2900, 2600, 3000, 2830, 2700 } };
        const float pos = sm[R_VW_POS], q = sm[R_VW_Q], size = sm[R_VW_SIZE];
        const int j = pos >= 4 ? 3 : pos <= 0 ? 0 : (int)pos; const float t = std::fmin(1.0f, std::fmax(0.0f, pos - j));
        for (int f = 0; f < 3; f++) {
            tone::Biquad &b = vw_bp[f];
            b.design(std::fmin(0.45 * sr, F[f][j] * std::pow(F[f][j + 1] / F[f][j], t) * size), q, sr);
            c[f][0] = (float)b.b0; c[f][1] = (float)b.b1; c[f][2] = (float)b.b2; c[f][3] = (float)b.a1; c[f][4] = (float)b.a2;
            c[f][5] = std::sqrt(q) * (f == 0 ? 1 : f == 1 ? 0.5f : 0.25f);
        }
    }
    void vowel(float *L, float *R, int n) {
        for (int i = 0; i < n; i++) tA[i] = L[i] + R[i];
        for (int o = 0; o < n; o += 16) {
            const int m = n - o < 16 ? n - o : 16;
            glide16(R_VW_POS); glide16(R_VW_Q); glide16(R_VW_SIZE);
            float c1[3][6]; vw_design(c1);
            for (int i = o, j = 1; i < o + m; i++, j++) {
                const float w = (float)j / m;
                float l = 0, r = 0;
                for (int f = 0; f < 3; f++) {
                    tone::Biquad &b = vw_bp[f]; const float *p = vw_c[f], *q = c1[f];
                    b.b0 = p[0] + (q[0] - p[0]) * w; b.b1 = p[1] + (q[1] - p[1]) * w; b.b2 = p[2] + (q[2] - p[2]) * w;
                    b.a1 = p[3] + (q[3] - p[3]) * w; b.a2 = p[4] + (q[4] - p[4]) * w;
                    const float g = p[5] + (q[5] - p[5]) * w;
                    l += g * b.run(0, L[i]); r += g * b.run(1, R[i]);
                }
                L[i] = l; R[i] = r;
            }
            std::memcpy(vw_c, c1, sizeof vw_c);
        }
        match(vw_lm, L, R, n);
    }
    /* A-18: the level of the module's input (tA, its mid) put back on its output. The gain moves last -> g across the
       block on a smoothstep (flat at both ends): a straight ramp kinks at every block edge, and a level swing (a comb
       filling on a resonant tone) read 3.88 on the click metric that way. */
    void match(LevelMatch &lm, float *L, float *R, int n) {
        for (int i = 0; i < n; i++) tB[i] = L[i] + R[i];
        float g1 = lm.gain(tA, tB, n), g0 = lm.last;
        for (int i = 0; i < n; i++) { float t = (float)(i + 1) / n, g = g0 + (g1 - g0) * t * t * (3 - 2 * t); L[i] *= g; R[i] *= g; }
    }

    /* Swarm: three taps a side, delay x (1 + 0.9 depth sin), the LFO a third apart per tap (weights 0.5 0.4 0.3, so the
       sum repeats at the rate, not at a third of it), the right side spread/4 of a period behind; tap 0 feeds back.
       zero: 0.5 (the line at the centre delay - tap 0), which nulls as tap 0 crosses the centre (through-zero); the
       switch cross-fades 30 ms. The delayed dry lives inside the wet (the frame's parallel return stays exact), so the
       engine's undelayed dry joins at mix < 1: true through-zero only at mix 1 (accepted, controller 2026-11-17). The LFO is transport / rate; a rate change glides the rate (100 ms, in octaves) and keeps
       the phase (an offset that returns to the transport over 1 s). Delays: every 16 samples, interpolated per sample. */
    void sw_delays(double b, float d[2][4]) {
        const double p = b / sw_rate + sw_off;
        const float D0 = (float)(sm[R_SW_DELAY] * 0.001 * sr), dep = 0.9f * sm[R_SW_DEPTH], spr = 0.25f * sm[R_SW_SPREAD];
        for (int ch = 0; ch < 2; ch++) {
            for (int k = 0; k < 3; k++) d[ch][k] = D0 * (1 + dep * (float)std::sin(2 * PI * (p + k / 3.0 + ch * spr)));
            d[ch][3] = D0;
        }
    }
    void swarm(float *L, float *R, int n) {
        const double rate = cfg.v[R_SW_RATE] > 0 ? cfg.v[R_SW_RATE] : 4, bps = bpm / 60 / sr;
        const float want = cfg.v[R_SW_ZERO] >= 0.5f, xstep = (float)(1 / (0.03 * sr));
        float *x[2] = { L, R };
        for (int o = 0; o < n; o += 16) {
            const int m = n - o < 16 ? n - o : 16;
            glide16(R_SW_DEPTH); glide16(R_SW_SPREAD); glide100(R_SW_DELAY);
            const double r0 = sw_rate, b = beats + o * bps, lr = std::log2(r0);
            sw_rate = std::exp2(lr + sk100 * (std::log2(rate) - lr));
            if (std::fabs(sw_rate / rate - 1) < 1e-6) sw_rate = rate;
            sw_off += b / r0 - b / sw_rate;          /* the phase stays where it was; only its speed glides */
            sw_off -= std::floor(sw_off + 0.5);
            sw_off -= sk1s * sw_off;
            float d1[2][4]; sw_delays(beats + (o + m) * bps, d1);
            for (int i = o, j = 1; i < o + m; i++, j++) {
                const float w = (float)j / m, fb = glide(R_SW_FB);
                sw_z = sw_z < want ? std::fmin(want, sw_z + xstep) : std::fmax(want, sw_z - xstep);
                for (int ch = 0; ch < 2; ch++) {
                    tone::DelayLine &dl = sw_dl[ch]; const float *a = sw_d[ch], *b = d1[ch];
                    float t0 = dl.read(a[0] + (b[0] - a[0]) * w), t1 = dl.read(a[1] + (b[1] - a[1]) * w), t2 = dl.read(a[2] + (b[2] - a[2]) * w);
                    float y = 0.5f * t0 + 0.4f * t1 + 0.3f * t2;
                    if (sw_z > 0) y += sw_z * (0.5f * (dl.read(a[3] + (b[3] - a[3]) * w) - t0) - y);
                    dl.push(x[ch][i] + fb * t0);
                    x[ch][i] = y;
                }
            }
            std::memcpy(sw_d, d1, sizeof sw_d);
        }
    }

    /* Comb: five feedback combs a side, each delay -> one-pole damping -> x g back in; the delay is 1/f less the damping's
       phase delay at f, so the peak sits on f. g = 10^(-3 / (decay f)): decay is the RT60 where damping does nothing.
       The input, after a 20 Hz high-pass (no DC ring), is scaled by sqrt((1 - g^2) / 5) (white noise keeps its power);
       LevelMatch does the rest. Tuning: the chord the walker stands in (root first, the others in order, the rest
       repeated an octave up, each folded into 55-880 Hz) or - free, or no chord yet - hz x 1, 1.5, 2, 3, 4. A chord
       change or the tune switch glides each comb in log frequency over 80 ms; hz glides 100 ms (it sets delay times) and
       decay and damp 30 ms, all three in octaves at most 20 a second. Detune: left -detune/2, right +detune/2 cents.
       ponytail: decay is exact where damping does nothing; damp shortens high notes (880 Hz at damp 6000 rings <= 0.8 s). */
    void cb_targets() {
        static const float R5[5] = { 0, 0.5849625f, 1, 1.5849625f, 2 };   /* log2 of 1, 1.5, 2, 3, 4 */
        float N[5]; int k = 0;
        if (cfg.v[R_CB_TUNE] < 0.5f) {
            if (chord_root >= 0) N[k++] = chord_root;
            for (int i = 0; i < 5; i++) if (chord_midi[i] >= 0 && chord_midi[i] != chord_root && k < 5) N[k++] = chord_midi[i];
        }
        for (int i = 0; i < 5; i++) {
            if (!k) { cb_tgt[i] = std::log2(sm[R_CB_HZ]) + R5[i]; continue; }
            float f = (N[i % k] + 12 * (i / k) - 69) / 12 + 8.7813597f;   /* log2 Hz; log2 440 */
            while (f > 9.7813597f) f -= 1;                                /* 880 */
            while (f < 5.7813597f) f += 1;                                /* 55 */
            cb_tgt[i] = f;
        }
    }
    float cb_coef() { return (float)(1 - std::exp(-2 * PI * std::fmin(sm[R_CB_DAMP], 0.45 * sr) / sr)); }
    void cb_design(float k[2][5][3]) {
        const double dec = sm[R_CB_DECAY], det = sm[R_CB_DETUNE] / 2400.0, b = 1 - cb_a;
        for (int ch = 0; ch < 2; ch++)
            for (int i = 0; i < 5; i++) {
                const double f = std::fmin(0.45 * sr, std::exp2(cb_cur[i] + (ch ? det : -det))), w = 2 * PI * f / sr;
                const double tau = std::atan2(b * std::sin(w), 1 - b * std::cos(w)) / w, g = std::pow(10.0, -3 / (dec * f));
                k[ch][i][0] = (float)std::fmax(1.0, sr / f - tau); k[ch][i][1] = (float)g; k[ch][i][2] = (float)std::sqrt((1 - g * g) / 5);
            }
    }
    void comb(float *L, float *R, int n) {
        for (int i = 0; i < n; i++) tA[i] = L[i] + R[i];
        const float R20 = (float)(1 - 2 * PI * 20 / sr), gstep = (float)(16 / (0.08 * sr));
        if (chord_serial != cb_serial || cfg.v[R_CB_TUNE] != cb_tune) {   /* a chord change or the switch: glide 80 ms */
            cb_serial = chord_serial; cb_tune = cfg.v[R_CB_TUNE];
            cb_targets();
            for (int i = 0; i < 5; i++) cb_step[i] = std::fabs(cb_tgt[i] - cb_cur[i]) * gstep;
        }
        float *x[2] = { L, R };
        for (int o = 0; o < n; o += 16) {
            const int m = n - o < 16 ? n - o : 16;
            glide_oct(R_CB_HZ, sk100);
            glide_oct(R_CB_DECAY, sk); glide_oct(R_CB_DAMP, sk); glide16(R_CB_DETUNE);
            cb_targets();
            for (int i = 0; i < 5; i++) {
                if (cb_step[i] == 0) { cb_cur[i] = cb_tgt[i]; continue; }
                const float d = cb_tgt[i] - cb_cur[i];
                if (std::fabs(d) <= cb_step[i]) { cb_cur[i] = cb_tgt[i]; cb_step[i] = 0; }
                else cb_cur[i] += d > 0 ? cb_step[i] : -cb_step[i];
            }
            const float a0 = cb_a; cb_a = cb_coef();
            float k1[2][5][3]; cb_design(k1);
            for (int i = o, j = 1; i < o + m; i++, j++) {
                const float w = (float)j / m, a = a0 + (cb_a - a0) * w;
                for (int ch = 0; ch < 2; ch++) {
                    cb_hy[ch] = flush(R20 * (cb_hy[ch] + x[ch][i] - cb_hx[ch])); cb_hx[ch] = x[ch][i];
                    float acc = 0;
                    for (int c = 0; c < 5; c++) {
                        const float *p = cb_k[ch][c], *q = k1[ch][c];
                        tone::DelayLine &dl = cb_dl[ch][c];
                        const float y = dl.read(p[0] + (q[0] - p[0]) * w);
                        float &lp = cb_lp[ch][c]; lp = flush(lp + a * (y - lp));
                        dl.push((p[2] + (q[2] - p[2]) * w) * cb_hy[ch] + (p[1] + (q[1] - p[1]) * w) * lp);
                        acc += y;
                    }
                    x[ch][i] = acc;
                }
            }
            std::memcpy(cb_k, k1, sizeof cb_k);
        }
        match(cb_lm, L, R, n);
    }

    /* Tape: one read a side moving by wow (0.4 Hz, its rate drifting +-15 % on a smoothed random; +-2 % pitch at 1) and
       flutter (7.3 Hz, +-0.4 %); the delay is 1 + swing x (1 - cos) samples, so at 0 nothing is delayed; the amounts glide
       100 ms. Drive: s tanh(a x / s) / a, a = 3 drive, s the signal's own peak (mean square, 10 ms rise / 200 ms fall), so
       it saturates alike at -30 and -6 dBFS (A-19). Age: a one-pole low-pass at tone (glided in octaves). LevelMatch holds
       the level. ponytail: the follower rides low bass a little (a 50 Hz tone ripples its gain); slower if it is heard. */
    float tp_coef() { return (float)(1 - std::exp(-2 * PI * std::fmin(sm[R_TP_TONE], 0.45 * sr) / sr)); }
    void tape(float *L, float *R, int n) {
        for (int i = 0; i < n; i++) tA[i] = L[i] + R[i];
        const float Aw = (float)(TP_WOW * sr), Af = (float)(TP_FLUTTER * sr);
        const float kr = (float)(1 - std::exp(-1 / (0.01 * sr))), kf = (float)(1 - std::exp(-1 / (0.2 * sr)));
        float *x[2] = { L, R };
        for (int o = 0; o < n; o += 16) {
            const int m = n - o < 16 ? n - o : 16;
            const float wow = glide100(R_TP_WOW), fl = glide100(R_TP_FLUTTER);
            glide_oct(R_TP_TONE, sk); const float c0 = tp_c; tp_c = tp_coef();
            if ((tp_cnt += m) >= (int)(2.5 * sr)) { tp_cnt = 0; tp_rng = tp_rng * 1664525u + 1013904223u; tp_dt = (float)(tp_rng >> 8) / 8388608.0f - 1; }
            tp_dr += sk1s * (tp_dt - tp_dr);
            tp_pw += 2 * PI * 0.4 * (1 + 0.15 * tp_dr) * m / sr; if (tp_pw > 2 * PI) tp_pw -= 2 * PI;
            tp_pf += 2 * PI * 7.3 * m / sr; if (tp_pf > 2 * PI) tp_pf -= 2 * PI;
            const float d0 = tp_d; tp_d = 1 + Aw * wow * (float)(1 - std::cos(tp_pw)) + Af * fl * (float)(1 - std::cos(tp_pf));
            for (int i = o, j = 1; i < o + m; i++, j++) {
                const float w = (float)j / m, d = d0 + (tp_d - d0) * w, c = c0 + (tp_c - c0) * w, a = 3 * glide(R_TP_DRIVE);
                for (int ch = 0; ch < 2; ch++) {
                    tp_dl[ch].push(x[ch][i]);
                    float v = tp_dl[ch].read(d), &ms = tp_ms[ch];
                    ms = flush(ms + (v * v > ms ? kr : kf) * (v * v - ms));
                    if (a > 1e-4f) { const float s = std::sqrt(2 * ms) + 1e-9f; v = s * std::tanh(a * v / s) / a; }
                    tp_lp[ch] = flush(tp_lp[ch] + c * (v - tp_lp[ch]));
                    x[ch][i] = tp_lp[ch];
                }
            }
        }
        match(tp_lm, L, R, n);
    }

    /* Cloud: up to eight Hann grains read from the last 2 s. Onsets come dens a second, each interval x 1 / (0.8 + 0.4 u).
       Each grain takes its settings at its onset (so no parameter can step a sounding grain): rate 2^(pitch / 12), x 2,
       0.5 or 1.5 by the chance spray; reversed by the chance rev; the material it reads (rate x size samples) ends at
       scatter x u x (2 s - that span) behind now, so at scatter 0 a grain plays the last size (a plain delay at pitch 0).
       Gain sqrt(8/3) (the Hann's mean square) / sqrt(max(1, min(8, dens size))): the level holds from one grain to eight
       overlapping. A grain reads only what was written before its onset and the line holds 0.5 s past the 2 s, so the
       write head never reaches a sounding grain. Freeze: the write cross-fades (30 ms) from the input to the slot's own
       old sample, so the line keeps turning as a 2.5 s loop of what it held, joined by that 30 ms cross-fade; no grain
       reads the slot being written, so the toggle changes nothing a grain is playing. The pitch row glides at most 10
       octaves a second (a 4-octave step takes 400 ms). A full pool skips the onset.
       ponytail: linear interpolation (aliases pitched up on bright input); a grain whose span outgrows 2 s (x8 at 500 ms)
       is shortened to fit. */
    float cl_u() { cl_rng ^= cl_rng << 13; cl_rng ^= cl_rng >> 17; cl_rng ^= cl_rng << 5; return (float)(cl_rng >> 8) / 16777216.0f; }
    void cl_spawn() {
        Grain *g = nullptr;
        for (auto &q : cl_g) if (!q.len) { g = &q; break; }
        const float u1 = cl_u(), u2 = cl_u(), u3 = cl_u(), u4 = cl_u();   /* drawn even when the pool is full: one stream */
        if (!g) return;
        const float size = cfg.v[R_CL_SIZE] * 0.001f, dens = cfg.v[R_CL_DENS];
        double r = std::exp2(sm[R_CL_PITCH] / 12.0);
        if (u1 < cfg.v[R_CL_SPRAY]) r *= u2 < 1 / 3.0f ? 2 : u2 < 2 / 3.0f ? 0.5 : 1.5;
        int len = std::max(2, (int)(size * sr));
        if (r * len > cl_R - 2) len = std::max(2, (int)((cl_R - 2) / r));
        const double span = r * len, e = 1 + cfg.v[R_CL_SCATTER] * u3 * (cl_R - 2 - span);
        const bool rev = u4 < cfg.v[R_CL_REV];
        g->d = rev ? e : e + span;                   /* delay behind the write head; it moves 1 - (+-r) a sample */
        g->dd = (float)(rev ? 1 + r : 1 - r);
        g->g = (float)(1.6329932 / std::sqrt(std::fmax(1.0f, std::fmin(8.0f, dens * size))));
        g->len = len; g->k = 0; g->c = 1; g->s = 0;
        g->cr = std::cos(2 * PI / len); g->sr = std::sin(2 * PI / len);
    }
    void cloud(float *L, float *R, int n) {
        const float want = cfg.v[R_CL_FREEZE] >= 0.5f, xstep = (float)(1 / (0.03 * sr)), pstep = (float)(120 * 16 / sr);
        for (int o = 0; o < n; o += 16) {
            const float d = cfg.v[R_CL_PITCH] - sm[R_CL_PITCH];
            sm[R_CL_PITCH] += std::fmax(-pstep, std::fmin(pstep, sk * d));
            if (std::fabs(cfg.v[R_CL_PITCH] - sm[R_CL_PITCH]) < 1e-4f) sm[R_CL_PITCH] = cfg.v[R_CL_PITCH];
            for (int i = o; i < o + 16 && i < n; i++) {
                cl_fz = cl_fz < want ? std::fmin(want, cl_fz + xstep) : std::fmax(want, cl_fz - xstep);
                if ((cl_ph += cfg.v[R_CL_DENS] * cl_j / sr) >= 1) { cl_ph -= 1; cl_j = 1 / (0.8f + 0.4f * cl_u()); cl_spawn(); }
                for (int ch = 0; ch < 2; ch++) {
                    tone::DelayLine &dl = cl_dl[ch];
                    const float x = ch ? R[i] : L[i];
                    dl.push(x + cl_fz * (dl.buf[dl.w] - x));   /* buf[w]: the oldest slot, about to be written */
                }
                float l = 0, r = 0;
                for (auto &g : cl_g) {
                    if (!g.len) continue;
                    const double c = g.c * g.cr - g.s * g.sr; g.s = g.c * g.sr + g.s * g.cr; g.c = c;
                    g.d += g.dd;
                    const float w = g.g * (float)(0.5 - 0.5 * c);
                    l += w * cl_dl[0].read(g.d); r += w * cl_dl[1].read(g.d);
                    if (++g.k >= g.len - 1) g.len = 0;
                }
                L[i] = l; R[i] = r;
            }
        }
    }
};

}  // namespace mfx
