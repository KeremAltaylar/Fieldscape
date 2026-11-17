/* Modulation FX (docs/superpowers/specs/2026-11-17-modulation-fx-design.md): one engine, two places - a route's
   master FX (inside Piece) and the Loop points' instances (inside fs_mix). It makes no sound: silence in, exact 0 out;
   every module at mix 0, exact 0 out. Main thread: cfg_of. Audio thread: apply, clock, chord, process, live - they
   never allocate, lock or throw; every buffer is allocated in init. */
#pragma once
#include "json.hpp"
#include "tone.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace mfx {

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
    c.on = j->flag("on", false);
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

struct Engine {
    double sr = 48000, bpm = 72, beats = 0;
    Cfg cfg;
    Ramp mix[NMOD], gate;                            /* gate: toward the largest mix, so all mixes 0 -> output 0 */
    bool dirty[NMOD] = {};                           /* skipped since it last ran: clear its state before it runs again */
    float chord_midi[5] = { -1, -1, -1, -1, -1 }, chord_root = -1; int chord_serial = 0;
    float lvl[NMOD] = {}, lvl_in = 0, lvl_out = 0;
    int quiet = 0;                                   /* frames the output has stayed under -80 dB */
    float xL[128], xR[128], yL[128], yR[128];

    Engine() { cfg_defaults(cfg); }
    void init(double rate) {
        sr = rate;
        for (auto &m : mix) m.step = (float)(1 / (0.03 * sr));
        gate.step = mix[0].step;
        reset();
    }
    void apply(const Cfg &c) {
        cfg = c;
        float g = 0;
        for (int m = 0; m < NMOD; m++) { mix[m].tgt = c.v[mix_row(m)]; g = std::fmax(g, mix[m].tgt); }
        gate.tgt = g;
    }
    void clock(double b, double pos) { bpm = b; beats = pos; }
    void chord(const float *midi5, float root, int serial) {
        for (int i = 0; i < 5; i++) chord_midi[i] = midi5[i];
        chord_root = root; chord_serial = serial;
    }
    /* Writes the wet only; n <= 128. */
    void process(const float *inL, const float *inR, float *wetL, float *wetR, int n) {
        float pin = 0, pout = 0;
        for (int i = 0; i < n; i++) pin = std::fmax(pin, std::fmax(std::fabs(inL[i]), std::fabs(inR[i])));
        lvl_in = pin;
        if (gate.off()) {
            std::memset(wetL, 0, sizeof(float) * n); std::memset(wetR, 0, sizeof(float) * n);
            for (int m = 0; m < NMOD; m++) { lvl[m] = 0; if (mix[m].off()) dirty[m] = true; }
            lvl_out = 0; quiet = 0;                  /* idle by gate.off(); the tail count starts when a mix rises */
            return;
        }
        std::memcpy(xL, inL, sizeof(float) * n); std::memcpy(xR, inR, sizeof(float) * n);
        for (int m = 0; m < NMOD; m++) {
            if (mix[m].off()) { dirty[m] = true; lvl[m] = 0; continue; }   /* skipped: bit-exact pass-through */
            if (dirty[m]) { clear(m); dirty[m] = false; }
            std::memcpy(yL, xL, sizeof(float) * n); std::memcpy(yR, xR, sizeof(float) * n);
            run(m, yL, yR, n);
            float pk = 0;
            for (int i = 0; i < n; i++) {
                float k = mix[m].next();
                xL[i] += k * (yL[i] - xL[i]); xR[i] += k * (yR[i] - xR[i]);
                pk = std::fmax(pk, std::fmax(std::fabs(yL[i]), std::fabs(yR[i])));
            }
            lvl[m] = pk;
        }
        for (int i = 0; i < n; i++) {
            float g = gate.next();
            wetL[i] = g == 0 ? 0.0f : g * xL[i]; wetR[i] = g == 0 ? 0.0f : g * xR[i];   /* exact 0, never -0.0f */
            pout = std::fmax(pout, std::fmax(std::fabs(wetL[i]), std::fabs(wetR[i])));
        }
        lvl_out = pout;
        quiet = pout < 1e-4f ? quiet + n : 0;
    }
    /* Nothing on, or the tail under -80 dB for 0.5 s. */
    bool idle() const { return gate.off() || quiet >= (int)(0.5 * sr); }
    void reset() {
        for (int m = 0; m < NMOD; m++) { clear(m); dirty[m] = false; mix[m].cur = 0; lvl[m] = 0; }
        gate.cur = 0; quiet = 0; lvl_in = lvl_out = 0;
    }
    void live(Live &out) const {
        std::memset(out.v, 0, sizeof out.v);         /* 0-8, 12, 24-31: the modulators (Task 6) */
        for (int m = 0; m < NMOD; m++) out.v[13 + m] = lvl[m];
        out.v[22] = lvl_in; out.v[23] = lvl_out;
    }

private:
    static int mix_row(int m) {                      /* each module's rows start with its mix */
        for (int i = 0; i < NPARAM; i++) if (PARAMS[i].mod == m) return i;
        return 0;
    }
    /* The modules: pass-through stubs until Tasks 2-5 fill them. */
    void run(int, float *, float *, int) {}
    void clear(int) {}
};

}  // namespace mfx
