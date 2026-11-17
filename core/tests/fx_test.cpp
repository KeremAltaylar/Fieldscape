/* Modulation FX (docs/superpowers/plans/2026-11-17-modulation-fx.md, Part 1): the engine's own checks.
     sh tools/mac-fx.sh      (builds and runs this on the Mac; prints "fx ok" last) */
#include "../modfx.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <initializer_list>

static int failed = 0;
static void check(const char *name, bool ok) {
    if (!ok) { std::printf("FAIL %s\n", name); failed++; }
}
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
static float val(const mfx::Cfg &c, const char *key) { return c.v[mfx::param_of(key)]; }

/* Paul Kellet's pink filter on a seeded white source: the same noise every run. */
struct Pink {
    uint32_t s = 22222; float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        float w = (float)s / 4294967296.0f * 2 - 1;
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        float y = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f;
        return y * 0.11f;
    }
};

/* The spec's table (docs/superpowers/specs/2026-11-17-modulation-fx-design.md, "The modules"), in its order.
   Division rows: min 32n (0.125 beats), max 32m (128), default the division's beats. */
static void test_table() {
    using namespace mfx;
    struct Row { const char *key; float min, max, def; bool cont; };
    static const Row T[] = {
        { "shift.mix", 0, 1, 0, true }, { "shift.hz", -2000, 2000, 0, true }, { "shift.mode", 0, 1, 0, false }, { "shift.fb", 0, 0.9f, 0, true },
        { "swarm.mix", 0, 1, 0, true }, { "swarm.rate", 0.125f, 128, 4, false }, { "swarm.depth", 0, 1, 0.5f, true }, { "swarm.delay", 0.1f, 30, 8, true },
        { "swarm.fb", -0.9f, 0.9f, 0, true }, { "swarm.spread", 0, 1, 1, true }, { "swarm.zero", 0, 1, 0, false },
        { "phase.mix", 0, 1, 0, true }, { "phase.stages", 2, 12, 6, false }, { "phase.hz", 100, 8000, 800, true }, { "phase.span", 0, 4, 2, true },
        { "phase.rate", 0.125f, 128, 2, false }, { "phase.fb", -0.95f, 0.95f, 0.3f, true },
        { "comb.mix", 0, 1, 0, true }, { "comb.tune", 0, 1, 0, false }, { "comb.hz", 40, 2000, 110, true }, { "comb.decay", 0.05f, 20, 2, true },
        { "comb.damp", 500, 16000, 6000, true }, { "comb.detune", 0, 50, 8, true },
        { "vowel.mix", 0, 1, 0, true }, { "vowel.pos", 0, 4, 0, true }, { "vowel.q", 2, 20, 8, true }, { "vowel.size", 0.5f, 2, 1, true },
        { "cloud.mix", 0, 1, 0, true }, { "cloud.size", 20, 500, 120, true }, { "cloud.dens", 1, 40, 12, true }, { "cloud.pitch", -24, 24, 0, true },
        { "cloud.spray", 0, 1, 0, true }, { "cloud.scatter", 0, 1, 0.3f, true }, { "cloud.rev", 0, 1, 0, true }, { "cloud.freeze", 0, 1, 0, false },
        { "tape.mix", 0, 1, 0, true }, { "tape.wow", 0, 1, 0.3f, true }, { "tape.flutter", 0, 1, 0.2f, true }, { "tape.drive", 0, 1, 0.2f, true },
        { "tape.tone", 500, 16000, 9000, true },
        { "echo.mix", 0, 1, 0, true }, { "echo.time", 0.125f, 128, 0.75f, false }, { "echo.fb", 0, 0.98f, 0.45f, true }, { "echo.tone", -1, 1, 0, true },
        { "echo.shift", -200, 200, 0, true }, { "echo.wobble", 0, 1, 0, true }, { "echo.spread", 0, 1, 0.5f, true },
        { "space.mix", 0, 1, 0, true }, { "space.size", 0.2f, 1, 0.7f, true }, { "space.decay", 0.3f, 60, 4, true }, { "space.damp", 500, 16000, 7000, true },
        { "space.mod", 0, 1, 0.3f, true },
    };
    const int n = (int)(sizeof T / sizeof *T);
    check("table: 52 rows in the spec", n == 52);
    check("table: NPARAM 52", NPARAM == 52);
    int cont = 0;
    for (int i = 0; i < NPARAM; i++) cont += PARAMS[i].cont;
    check("table: 44 continuous rows", cont == 44);
    for (int i = 0; i < n && i < NPARAM; i++) {
        const ParamDef &p = PARAMS[i];
        bool ok = !std::strcmp(p.key, T[i].key) && p.min == T[i].min && p.max == T[i].max && p.def == T[i].def && p.cont == T[i].cont;
        if (!ok) std::printf("  row %d: %s %g %g %g %d (want %s %g %g %g %d)\n", i, p.key, p.min, p.max, p.def, p.cont, T[i].key, T[i].min, T[i].max, T[i].def, T[i].cont);
        check("table: row matches the spec", ok);
        std::string mod(p.key, std::strchr(p.key, '.') - p.key);
        static const char *M[] = { "shift", "swarm", "phase", "comb", "vowel", "cloud", "tape", "echo", "space" };
        check("table: row's module", mod == M[p.mod]);
    }
    check("table: shift.hz cubic", PARAMS[param_of("shift.hz")].taper == CUBIC);
    check("table: the rows the modules read", R_SH_HZ == param_of("shift.hz") && R_SH_MODE == param_of("shift.mode") && R_SH_FB == param_of("shift.fb")
        && R_PH_STAGES == param_of("phase.stages") && R_PH_HZ == param_of("phase.hz") && R_PH_SPAN == param_of("phase.span")
        && R_PH_RATE == param_of("phase.rate") && R_PH_FB == param_of("phase.fb") && R_VW_POS == param_of("vowel.pos")
        && R_VW_Q == param_of("vowel.q") && R_VW_SIZE == param_of("vowel.size"));
    int e = param_of("echo.fb");
    check("table: param_of round-trips", e >= 0 && !std::strcmp(PARAMS[e].key, "echo.fb"));
    check("table: unknown -1", param_of("nope.x") == -1 && param_of("echo") == -1 && param_of("") == -1);
    std::printf("fx table: %d rows, %d continuous\n", NPARAM, cont);
}

static void test_div() {
    using mfx::div_beats;
    check("div 4n", div_beats("4n") == 1);
    check("div 8n.", div_beats("8n.") == 0.75);
    check("div 8t", near(div_beats("8t"), 1.0 / 3));
    check("div 2m", div_beats("2m") == 8);
    check("div 32m", div_beats("32m") == 128);
    check("div 1n", div_beats("1n") == 4);
    check("div 32n", div_beats("32n") == 0.125);
    check("div 16t", near(div_beats("16t"), 1.0 / 6));
    check("div junk", div_beats("loud") == 1 && div_beats("") == 1 && div_beats(nullptr) == 1 && div_beats("4") == 1 && div_beats("3n") == 1);
    std::printf("fx div: 4n 1, 8n. 0.75, 8t 1/3, 2m 8, 32m 128, junk 1\n");
}

static void test_parse() {
    using namespace mfx;
    Cfg d; cfg_defaults(d);
    check("defaults: dry", d.ret == 0 && d.wet == 0 && !d.on && d.send[0] == 1 && d.send[1] == 1 && d.send[2] == 1);
    for (int i = 0; i < NPARAM; i++) check("defaults: every row its def", d.v[i] == PARAMS[i].def);
    check("defaults: rates", !std::strcmp(d.rate[SWARM], "1n") && !std::strcmp(d.rate[PHASE], "2n") && !std::strcmp(d.rate[ECHO], "8n."));
    check("defaults: env", d.env_a == 10 && d.env_r == 300 && d.env_s == 1);
    for (int i = 0; i < 8; i++) check("defaults: empty slots", d.mods[i].src == -1 && d.mods[i].dst == -1 && d.mods[i].amt == 0);

    /* the spec's example, field for field ("pace" is no source any more: Kerem's decision 3, the slot drops) */
    Json j = Json::parse(R"({ "ret": 0.6, "send": [1, 0.5, 1],
      "phase": { "mix": 1, "stages": 8, "hz": 900, "span": 3, "rate": "2m", "fb": 0.5 },
      "echo":  { "mix": 0.7, "time": "8n.", "fb": 0.7, "shift": 6, "tone": -0.3 },
      "lfo": [ { "shape": "drift", "rate": "4m", "phase": 0 } ],
      "env": { "attack": 10, "release": 300, "sens": 1 }, "smooth": 2,
      "mods": [ { "src": "lfo1", "dst": "echo.shift", "amt": 0.4 }, { "src": "pace", "dst": "phase.span", "amt": 0.5 } ] })");
    Cfg c; cfg_of(&j, c);
    check("parse: ret, send", near(c.ret, 0.6) && c.send[0] == 1 && near(c.send[1], 0.5) && c.send[2] == 1);
    check("parse: phase", val(c, "phase.mix") == 1 && val(c, "phase.stages") == 8 && val(c, "phase.hz") == 900 && val(c, "phase.span") == 3
        && !std::strcmp(c.rate[PHASE], "2m") && val(c, "phase.rate") == 8 && near(val(c, "phase.fb"), 0.5));
    check("parse: echo", near(val(c, "echo.mix"), 0.7) && !std::strcmp(c.rate[ECHO], "8n.") && val(c, "echo.time") == 0.75f
        && near(val(c, "echo.fb"), 0.7) && val(c, "echo.shift") == 6 && near(val(c, "echo.tone"), -0.3));
    check("parse: lfo1", c.lfo[0].shape == 6 && !std::strcmp(c.lfo[0].rate, "4m") && c.lfo[0].phase == 0);
    check("parse: lfo2-4 default", !std::memcmp(&c.lfo[1], &d.lfo[1], sizeof(LfoCfg) * 3));
    check("parse: env", c.env_a == 10 && c.env_r == 300 && c.env_s == 1);
    check("parse: slot 0", c.mods[0].src == S_LFO1 && c.mods[0].dst == param_of("echo.shift") && near(c.mods[0].amt, 0.4));
    check("parse: slot 1 (pace) dropped", c.mods[1].src == -1 && c.mods[1].dst == -1);
    int untouched = 0;
    for (int i = 0; i < NPARAM; i++) {
        const char *k = PARAMS[i].key;
        if (std::strncmp(k, "phase.", 6) && std::strncmp(k, "echo.", 5)) untouched += c.v[i] == d.v[i];
    }
    check("parse: other modules default", untouched == NPARAM - 6 - 7);
    check("parse: echo/phase absent keys default", val(c, "echo.wobble") == 0 && val(c, "echo.spread") == 0.5f);

    Json e = Json::parse("{}"); Cfg ce; cfg_of(&e, ce);
    check("parse: {} is the defaults", !std::memcmp(&ce, &d, sizeof d));
    Cfg cn; cfg_of(nullptr, cn);
    check("parse: null is the defaults", !std::memcmp(&cn, &d, sizeof d));
    Json a = Json::parse("[1,2]"); Cfg ca; cfg_of(&a, ca);
    check("parse: an array is the defaults", !std::memcmp(&ca, &d, sizeof d));

    /* Review Focus 5: junk */
    std::string mods = "[{\"src\":\"lfo2\",\"dst\":\"nope.x\",\"amt\":0.5}";
    for (int i = 1; i < 40; i++) mods += ",{\"src\":\"lfo1\",\"dst\":\"echo.fb\",\"amt\":" + std::to_string(i < 5 ? 7 : -0.25) + "}";
    mods += "]";
    std::string junk = R"({"ret":5,"send":["x",-3],"wet":"a","on":"yes",
        "shift":{"hz":"loud","mode":"ring","fb":nan},"echo":{"fb":40,"time":"3n","tone":-1e999},
        "phase":{"stages":5,"rate":7},"comb":{"tune":"free","hz":1e999},"swarm":"x","cloud":{"freeze":true},
        "lfo":[{"shape":"zap","rate":"nope","phase":4},{"shape":3},1,{},{},{}],"env":{"attack":-5,"release":1e9,"sens":"x"},
        "mods":)" + mods + "}";
    Json k = Json::parse(junk.c_str());
    check("junk: parses", k.kind == Json::OBJ);
    Cfg cj; cfg_of(&k, cj);
    check("junk: ret clamped", cj.ret == 1);
    check("junk: send", cj.send[0] == 1 && cj.send[1] == 0 && cj.send[2] == 1);
    check("junk: wet string -> 0", cj.wet == 0);
    check("junk: on string -> false", !cj.on);
    Json on1 = Json::parse(R"({"on":true})"), on2 = Json::parse(R"({"on":1})"), on3 = Json::parse(R"({"on":"false"})");
    Cfg co; cfg_of(&on1, co); bool a1 = co.on; cfg_of(&on2, co); bool a2 = co.on; cfg_of(&on3, co);
    check("parse: on true / 1 -> on, \"false\" -> off", a1 && a2 && !co.on);
    check("junk: hz string -> default", val(cj, "shift.hz") == 0);
    check("junk: mode ring", val(cj, "shift.mode") == 1);
    check("junk: fb nan -> default", val(cj, "shift.fb") == 0);
    check("junk: echo.fb 40 -> 0.98", val(cj, "echo.fb") == 0.98f);
    check("junk: echo.time 3n -> default", !std::strcmp(cj.rate[ECHO], "8n.") && val(cj, "echo.time") == 0.75f);
    check("junk: tone -inf -> default", val(cj, "echo.tone") == 0);
    check("junk: stages 5 snaps", val(cj, "phase.stages") == 4 || val(cj, "phase.stages") == 6);
    check("junk: rate number -> default", !std::strcmp(cj.rate[PHASE], "2n"));
    check("junk: comb tune free, hz inf -> default", val(cj, "comb.tune") == 1 && val(cj, "comb.hz") == 110);
    check("junk: swarm string -> defaults", val(cj, "swarm.depth") == 0.5f && !std::strcmp(cj.rate[SWARM], "1n"));
    check("junk: freeze true", val(cj, "cloud.freeze") == 1);
    check("junk: lfo1 junk -> defaults, phase clamped", cj.lfo[0].shape == 0 && !std::strcmp(cj.lfo[0].rate, d.lfo[0].rate) && cj.lfo[0].phase == 1);
    check("junk: lfo2 shape by number", cj.lfo[1].shape == 3);
    check("junk: env clamped", cj.env_a == 1 && cj.env_r == 2000 && cj.env_s == 1);
    check("junk: unknown dst slot dropped", cj.mods[0].src == -1 && cj.mods[0].dst == -1 && cj.mods[0].amt == 0);
    bool eight = true;
    for (int i = 1; i < 8; i++) eight = eight && cj.mods[i].src == S_LFO1 && cj.mods[i].dst == param_of("echo.fb");
    check("junk: 40 mods -> 8 slots", eight);
    check("junk: amt clamped", cj.mods[1].amt == 1 && cj.mods[7].amt == -0.25f);
    Json sw = Json::parse(R"({"mods":[{"src":"lfo1","dst":"phase.stages","amt":1},{"src":"env","dst":"echo.time","amt":1},{"src":"chord","dst":"space.mod","amt":"x"}]})");
    Cfg cs; cfg_of(&sw, cs);
    check("parse: a switch or division dst is dropped", cs.mods[0].src == -1 && cs.mods[1].src == -1);
    check("parse: chord source, junk amt 0", cs.mods[2].src == S_CHORD && cs.mods[2].dst == param_of("space.mod") && cs.mods[2].amt == 0);
    std::printf("fx parse: the spec's example field for field, {} = defaults, junk clamped/dropped, 40 mods -> 8 slots\n");
}

static void run(mfx::Engine &e, Pink &p, int frames, float *peak_out, bool *all_zero) {
    float iL[128], iR[128], wL[128], wR[128];
    double beats = 0;
    for (int done = 0; done < frames; done += 128) {
        for (int i = 0; i < 128; i++) { iL[i] = p.next(); iR[i] = p.next(); }
        e.clock(90, beats); beats += 128 / 48000.0 * 1.5;
        e.process(iL, iR, wL, wR, 128);
        for (int i = 0; i < 128; i++) {
            if (all_zero && (wL[i] != 0.0f || wR[i] != 0.0f || std::signbit(wL[i]) || std::signbit(wR[i]))) *all_zero = false;
            if (peak_out) *peak_out = std::fmax(*peak_out, std::fmax(std::fabs(wL[i]), std::fabs(wR[i])));
        }
    }
}

static void test_silent() {
    using namespace mfx;
    Engine e; e.init(48000);
    Cfg c; cfg_defaults(c); e.apply(c);
    Pink p; bool zero = true;
    run(e, p, 2 * 48000, nullptr, &zero);
    check("silent: defaults -> wet exactly 0", zero);
    check("silent: idle", e.idle());
    Live l; e.live(l);
    check("silent: live in > 0, out 0", l.v[22] > 0 && l.v[23] == 0);

    /* the frame: a stub module at mix 1 passes its input; back to 0, the gate gives exact 0 again */
    c.v[param_of("echo.mix")] = 1; e.apply(c);
    check("frame: a mix on -> not idle", !e.idle());
    float pk = 0; run(e, p, 48000, &pk, nullptr);
    e.live(l);
    check("frame: mix 1 -> wet sounds", pk > 0.05f && l.v[23] > 0 && l.v[13 + ECHO] > 0);
    check("frame: other modules skipped", l.v[13 + SHIFT] == 0 && l.v[13 + SPACE] == 0);
    c.v[param_of("echo.mix")] = 0; e.apply(c);
    run(e, p, 48000 / 20, nullptr, nullptr);       /* 50 ms: past the 30 ms fade */
    zero = true; run(e, p, 48000, nullptr, &zero);
    check("frame: all mixes back to 0 -> wet exactly 0", zero);
    check("frame: idle again", e.idle());
    e.reset();
    check("reset: config kept, still dry", e.idle());
    std::printf("fx silent: 2 s pink noise through the defaults -> wet 0.0f every sample, idle; a mix on sounds, off gives 0 again\n");
}

/* wet = chain - prod(1 - mix) * in: with the pass-through stubs (f(x) = x) the wet is (1 - prod(1 - mix)) * in, so one
   module's wet is linear in its mix and no untouched input reaches the return. Task 2's first real module must assert
   wet == mix * f(x) for itself (its own output scaled linearly by its mix). */
static float worst(mfx::Engine &e, float expect, int frames, int n) {
    Pink p; float iL[512], iR[512], wL[512], wR[512], err = 0;
    for (int done = 0; done < frames; done += n) {
        for (int i = 0; i < n; i++) { iL[i] = p.next(); iR[i] = p.next(); }
        e.process(iL, iR, wL, wR, n);
        for (int i = 0; i < n; i++) err = std::fmax(err, std::fmax(std::fabs(wL[i] - expect * iL[i]), std::fabs(wR[i] - expect * iR[i])));
    }
    return err;
}
static void test_linear() {
    using namespace mfx;
    Cfg c; cfg_defaults(c);
    c.v[param_of("echo.mix")] = 0.5f;
    Engine e; e.init(48000); e.apply(c);
    worst(e, 0, 4800, 128);                                   /* past the 30 ms ramp */
    float e1 = worst(e, 0.5f, 48000, 128);
    check("linear: one stub at mix 0.5 -> wet == 0.5 in", e1 < 1e-6f);
    c.v[param_of("echo.mix")] = 0.25f; e.apply(c);
    worst(e, 0, 4800, 128);
    float e2 = worst(e, 0.25f, 48000, 128);
    check("linear: mix 0.25 -> wet == 0.25 in", e2 < 1e-6f);
    c.v[param_of("space.mix")] = 0.5f; e.apply(c);
    worst(e, 0, 4800, 128);
    float e3 = worst(e, 1 - 0.75f * 0.5f, 48000, 128);
    check("linear: two stubs 0.25, 0.5 -> wet == (1 - 0.75 * 0.5) in", e3 < 1e-6f);
    Engine f; f.init(48000); f.apply(c);
    worst(f, 0, 4800, 300);                                   /* n > 128: chunked, no overrun */
    float e4 = worst(f, 1 - 0.75f * 0.5f, 48000 - 48000 % 300, 300);
    check("chunks: n 300 gives the same", e4 < 1e-6f);
    std::printf("fx linear: stub wet = (1 - prod(1 - mix)) * in, max error %.2g %.2g %.2g, n 300 %.2g\n", e1, e2, e3, e4);
}

/* ---- Task 2: Shift, Phase, Vowel ---- */
static const double SR = 48000, TAU = 6.283185307179586;

/* A sound source the tests feed, stereo: 0 a 220 Hz sine at -12 dBFS, 1 pink noise, 2 white noise, 3 a 1 kHz sine at -6 dBFS. */
struct Sig {
    int kind; long n = 0; Pink pl, pr; uint32_t w = 777;
    explicit Sig(int k) : kind(k) { pr.s = 99991; }
    void next(float &l, float &r) {
        if (kind == 0) { l = r = (float)(0.2512 * std::sin(TAU * 220 * n++ / SR)); return; }
        if (kind == 3) { l = r = (float)(0.5 * std::sin(TAU * 1000 * n++ / SR)); return; }
        if (kind == 1) { l = pl.next(); r = pr.next(); return; }
        w ^= w << 13; w ^= w >> 17; w ^= w << 5; l = (float)w / 4294967296.0f - 0.5f;
        w ^= w << 13; w ^= w >> 17; w ^= w << 5; r = (float)w / 4294967296.0f - 0.5f;
    }
};

/* Runs `frames` through e (128 at a time), appending the left wet (or in + wet: what a return of 1 adds to the dry) to
   out. bpm 0 freezes the transport (the LFO test hook): clock(0, pos) never advances. */
static void play(mfx::Engine &e, Sig &s, int frames, std::vector<float> *out, bool plus_in = false, double bpm = 90,
                 double *beats = nullptr, std::vector<float> *outR = nullptr, std::vector<float> *inL = nullptr) {
    float iL[128], iR[128], wL[128], wR[128];
    double b0 = 0; double &b = beats ? *beats : b0;
    for (int done = 0; done < frames; done += 128) {
        for (int i = 0; i < 128; i++) s.next(iL[i], iR[i]);
        e.clock(bpm, b); b += 128 / SR * bpm / 60;
        e.process(iL, iR, wL, wR, 128);
        for (int i = 0; i < 128; i++) {
            if (out) out->push_back(plus_in ? iL[i] + wL[i] : wL[i]);
            if (outR) outR->push_back(wR[i]);
            if (inL) inL->push_back(iL[i]);
        }
    }
}

/* The click metric (brief, constraints): over the 50 ms after a change, max |y[n] - 2y[n-1] + y[n-2]| <= 4x its
   maximum over the 500 ms before. y holds the 500 ms before then the 50 ms after; returns after / before. */
static double click_ratio(const std::vector<float> &y) {
    const int pre = (int)(0.5 * SR), post = (int)(0.05 * SR);
    double a = 0, b = 0;
    for (int n = 2; n < pre + post && n < (int)y.size(); n++) {
        double d = std::fabs((double)y[n] - 2.0 * y[n - 1] + y[n - 2]);
        if (n < pre) a = std::fmax(a, d); else b = std::fmax(b, d);
    }
    return a > 0 ? b / a : (b > 0 ? 1e9 : 0);
}
/* From `from`, settle 1 s, then 500 ms recorded, then `to` and 50 ms: the ratio. */
static double click_of(const mfx::Cfg &from, const mfx::Cfg &to, int kind, bool plus_in) {
    mfx::Engine e; e.init(SR); e.apply(from);
    Sig s(kind); std::vector<float> y; double beats = 0;
    play(e, s, (int)SR, nullptr, plus_in, 90, &beats);
    play(e, s, (int)(0.5 * SR), &y, plus_in, 90, &beats);
    e.apply(to);
    play(e, s, (int)(0.05 * SR) + 128, &y, plus_in, 90, &beats);
    return click_ratio(y);
}

/* Radix-2 FFT, in place. */
static void fft(std::vector<double> &re, std::vector<double> &im) {
    const int n = (int)re.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -TAU / len, wr = std::cos(a), wi = std::sin(a);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int p = i + k, q = p + len / 2;
                double tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr; im[q] = im[p] - ti; re[p] += tr; im[p] += ti;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
/* Averaged Hann power spectrum of x, frames of N (bin k = k * SR / N). */
static std::vector<double> power(const std::vector<float> &x, int N = 4096) {
    std::vector<double> P(N / 2, 0.0), re(N), im(N);
    for (size_t o = 0; o + N <= x.size(); o += N / 2) {
        for (int i = 0; i < N; i++) { re[i] = x[o + i] * (0.5 - 0.5 * std::cos(TAU * i / N)); im[i] = 0; }
        fft(re, im);
        for (int k = 0; k < N / 2; k++) P[k] += re[k] * re[k] + im[k] * im[k];
    }
    return P;
}
/* Level of a tone at f (dB, relative): the power summed over its +-3 bins. */
static double tone_db(const std::vector<double> &P, double f, int N = 4096) {
    int k = (int)std::lround(f * N / SR); double s = 1e-30;
    for (int i = k - 3; i <= k + 3; i++) if (i >= 0 && i < (int)P.size()) s += P[i];
    return 10 * std::log10(s);
}
static double rms_db(const std::vector<float> &x, size_t from = 0) {
    double s = 1e-30; for (size_t i = from; i < x.size(); i++) s += (double)x[i] * x[i];
    return 10 * std::log10(s / (x.size() - from));
}
static mfx::Cfg with(std::initializer_list<std::pair<const char *, float>> kv) {
    mfx::Cfg c; mfx::cfg_defaults(c);
    for (auto &p : kv) c.v[mfx::param_of(p.first)] = p.second;
    return c;
}

static void test_shift() {
    using namespace mfx;
    auto spec = [](float hz, float mode, float fb) {
        Engine e; e.init(SR); e.apply(with({ { "shift.mix", 1 }, { "shift.hz", hz }, { "shift.mode", mode }, { "shift.fb", fb } }));
        Sig s(3); play(e, s, (int)(0.5 * SR), nullptr);
        std::vector<float> y; play(e, s, 2 * (int)SR, &y);
        return power(y);
    };
    auto up = spec(100, 0, 0), dn = spec(-100, 0, 0), ring = spec(100, 1, 0);
    double u = tone_db(up, 1100) - tone_db(up, 900), d = tone_db(dn, 900) - tone_db(dn, 1100), r = tone_db(ring, 1100) - tone_db(ring, 900);
    double carrier = tone_db(up, 1000) - tone_db(up, 1100);
    check("shift: +100 -> 1100 Hz, 900 >= 35 dB under", u >= 35);
    check("shift: -100 mirrors", d >= 35);
    check("shift: +100 leaves no 1 kHz carrier (>= 35 dB under)", carrier <= -35);
    check("shift: ring -> both sides within 1 dB", std::fabs(r) <= 1);

    Engine e; e.init(SR); e.apply(with({ { "shift.mix", 1 }, { "shift.hz", 7 }, { "shift.fb", 0.9f } }));
    Sig s(1); float pk = 0; bool fin = true; std::vector<float> y;
    for (int sec = 0; sec < 20; sec++) {
        y.clear(); play(e, s, (int)SR, &y);
        for (float v : y) { fin = fin && std::isfinite(v); pk = std::fmax(pk, std::fabs(v)); }
    }
    check("shift: fb 0.9 for 20 s finite, |out| <= 4", fin && pk <= 4);

    /* Task 1's carried check: one real module's wet is mix * f(x) (linear in its mix) */
    Cfg c1 = with({ { "shift.mix", 1 }, { "shift.hz", 230 }, { "shift.fb", 0.5f } }), cm = c1;
    cm.v[param_of("shift.mix")] = 0.37f;
    Engine a, b; a.init(SR); b.init(SR); a.apply(c1); b.apply(cm);
    Sig sa(1), sb(1); std::vector<float> ya, yb;
    play(a, sa, (int)SR, &ya); play(b, sb, (int)SR, &yb);
    double lin = 0;
    for (size_t i = (size_t)(0.1 * SR); i < ya.size(); i++) lin = std::fmax(lin, std::fabs(yb[i] - 0.37 * ya[i]));
    check("shift: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf("fx shift: +100 Hz upper over lower %.1f dB, -100 %.1f dB, carrier %.1f dB, ring sides %.2f dB; fb 0.9 20 s peak %.2f; mix linearity err %.2g\n",
                u, d, carrier, r, pk, lin);
}

/* The notches of the phaser (white noise in, LFO frozen at `ph` of its period): the runs of the averaged power
   spectrum lying >= 10 dB under the median, as frequencies. */
static std::vector<double> notches(float stages, double ph) {
    using namespace mfx;
    Engine e; e.init(SR);
    e.apply(with({ { "phase.mix", 1 }, { "phase.stages", stages } }));
    Sig s(2); double beats = ph * 2;                  /* phase.rate 2n = 2 beats */
    play(e, s, (int)(0.5 * SR), nullptr, false, 0, &beats);
    std::vector<float> y; play(e, s, 8 * (int)SR, &y, false, 0, &beats);
    const int N = 4096; auto P = power(y, N);
    std::vector<double> L(P.size());
    for (size_t k = 0; k < P.size(); k++) L[k] = 10 * std::log10(P[k] + 1e-30);
    std::vector<double> srt(L.begin() + 4, L.end()); std::sort(srt.begin(), srt.end());
    double med = srt[srt.size() / 2];
    std::vector<double> f;
    for (size_t k = 4; k < L.size(); k++) {          /* a notch: under med - 10 dB until back over med - 3 dB; at its deepest bin */
        if (L[k] >= med - 10) continue;
        size_t lo = k;
        while (k + 1 < L.size() && L[k + 1] < med - 3) k++;
        size_t b = lo; for (size_t i = lo; i <= k; i++) if (L[i] < L[b]) b = i;
        f.push_back(b * SR / N);
    }
    return f;
}
static void test_phase() {
    auto a = notches(6, 0), b = notches(6, 0.5), c = notches(12, 0);
    double lo = 800 * std::pow(2, -1.0), hi = 800 * std::pow(2, 1.0);   /* defaults: hz 800, span 2 */
    check("phase: 6 stages -> three notches (phase 0)", a.size() == 3);
    check("phase: 6 stages -> three notches (phase 0.5)", b.size() == 3);
    check("phase: lowest at hz 2^(-span/2) at phase 0 (10 %)", !a.empty() && std::fabs(a[0] / lo - 1) <= 0.1);
    check("phase: lowest at hz 2^(span/2) at phase 0.5 (10 %)", !b.empty() && std::fabs(b[0] / hi - 1) <= 0.1);
    check("phase: 12 stages -> six notches", c.size() == 6);
    std::printf("fx phase: 6 stages %d notches, lowest %.0f Hz (want %.0f) at phase 0, %d / %.0f Hz (want %.0f) at 0.5; 12 stages %d notches\n",
                (int)a.size(), a.empty() ? 0 : a[0], lo, (int)b.size(), b.empty() ? 0 : b[0], hi, (int)c.size());

    /* the skip rule with state: Phase runs, mix to 0 (skipped), back to 1 into silence -> no stale tail; and no click */
    using namespace mfx;
    Engine e; e.init(SR);
    Cfg on = with({ { "phase.mix", 1 }, { "phase.fb", 0.9f } }), off = on;
    off.v[param_of("phase.mix")] = 0;
    e.apply(on); Sig s(1); play(e, s, (int)SR, nullptr);
    e.apply(off); play(e, s, (int)(0.1 * SR), nullptr);
    float zL[128] = {}, zR[128] = {}, wL[128], wR[128]; bool zero = true;
    e.apply(on);
    for (int k = 0; k < 40; k++) {
        e.clock(90, 0); e.process(zL, zR, wL, wR, 128);
        for (int i = 0; i < 128; i++) zero = zero && wL[i] == 0 && wR[i] == 0;
    }
    check("phase: skipped then back on into silence -> no stale tail (exact 0)", zero);
    std::vector<float> y; Engine f; f.init(SR); f.apply(on);
    Sig t(0); double beats = 0;
    play(f, t, (int)SR, nullptr, true, 90, &beats);
    f.apply(off); play(f, t, (int)(0.1 * SR), nullptr, true, 90, &beats);
    play(f, t, (int)(0.5 * SR), &y, true, 90, &beats);
    f.apply(on); play(f, t, (int)(0.05 * SR) + 128, &y, true, 90, &beats);
    double r = click_ratio(y);
    check("phase: back on after the skip -> no click", r <= 4);
    std::printf("fx skip: Phase skipped and back on -> exact 0 into silence, click %.2f\n", r);
}

static void test_vowel() {
    using namespace mfx;
    /* The centroid is power-weighted: with two-pole skirts on pink noise the magnitude-weighted one is ruled by the
       tail above F3 (its integral grows as sqrt(f)), so it is printed beside it but not tested. */
    double lmin = 1e9, lmax = -1e9, cmin = 1e9, cmax = 0, mmin = 1e9, mmax = 0;
    std::printf("fx vowel: pos level(dB) centroid power/magnitude (Hz):");
    for (int k = 0; k <= 8; k++) {
        float pos = k * 0.5f;
        Engine e; e.init(SR); e.apply(with({ { "vowel.mix", 1 }, { "vowel.pos", pos } }));
        Sig s(1); play(e, s, 2 * (int)SR, nullptr);
        std::vector<float> y, in; play(e, s, 4 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
        double lv = rms_db(y) - rms_db(in);
        auto P = power(y);
        double num = 0, den = 0, mn = 0, md = 0;
        for (size_t b = 1; b < P.size(); b++) {
            double f = b * SR / 4096, m = std::sqrt(P[b]);
            num += f * P[b]; den += P[b]; mn += f * m; md += m;
        }
        double cen = num / den, mc = mn / md;
        lmin = std::fmin(lmin, lv); lmax = std::fmax(lmax, lv); cmin = std::fmin(cmin, cen); cmax = std::fmax(cmax, cen);
        mmin = std::fmin(mmin, mc); mmax = std::fmax(mmax, mc);
        std::printf(" %.1f:%.1f/%.0f/%.0f", pos, lv, cen, mc);
    }
    double crange = (cmax - cmin) / cmax;
    std::printf("\nfx vowel: level range %.2f dB, centroid range %.0f..%.0f Hz = %.0f %% (magnitude-weighted %.0f..%.0f = %.0f %%)\n",
                lmax - lmin, cmin, cmax, 100 * crange, mmin, mmax, 100 * (mmax - mmin) / mmax);
    check("vowel: level range <= 3 dB across pos", lmax - lmin <= 3);
    check("vowel: centroid range >= 30 % across pos", crange >= 0.3);
}

/* Every parameter of the three stepped min -> max (module at mix 1, its other parameters default; y = the wet), and
   each mix 0 -> 1 -> 0 (y = in + wet, since the wet alone is 0 before), on the sine and on pink noise. */
static void test_clicks() {
    using namespace mfx;
    const char *mods[3] = { "shift", "phase", "vowel" };
    double worst = 0; const char *wk = "";
    for (int i = 0; i < NPARAM; i++) {
        const ParamDef &p = PARAMS[i];
        if (p.mod != SHIFT && p.mod != PHASE && p.mod != VOWEL) continue;
        const char *mk = mods[p.mod == SHIFT ? 0 : p.mod == PHASE ? 1 : 2];
        std::string mixk = std::string(mk) + ".mix";
        bool is_mix = !std::strcmp(p.key, mixk.c_str());
        for (int kind = 0; kind < 2; kind++) {
            double r;
            if (is_mix) {
                Cfg off = with({}), on = with({ { p.key, 1 } });
                r = std::fmax(click_of(off, on, kind, true), click_of(on, off, kind, true));
            } else {
                Cfg a = with({ { mixk.c_str(), 1 }, { p.key, p.min } }), b = with({ { mixk.c_str(), 1 }, { p.key, p.max } });
                if (!p.cont && p.taper == EXP) { std::strcpy(a.rate[p.mod], "32n"); std::strcpy(b.rate[p.mod], "32m"); }
                r = click_of(a, b, kind, false);
            }
            std::printf("  click %-13s %s %.2f%s\n", p.key, kind ? "pink" : "sine", r, r > 4 ? "  FAIL" : "");
            check("clicks: every shift/phase/vowel parameter and mix <= 4", r <= 4);
            if (r > worst) { worst = r; wk = p.key; }
        }
    }
    std::printf("fx clicks: shift, phase, vowel - every parameter min -> max and mix 0 -> 1 -> 0, sine and pink: worst %.2f (%s)\n", worst, wk);
}

int main() {
    test_table();
    test_div();
    test_parse();
    test_silent();
    test_linear();
    test_shift();
    test_phase();
    test_vowel();
    test_clicks();
    if (failed) { std::printf("fx FAILED: %d checks\n", failed); return 1; }
    std::printf("fx ok\n");
    return 0;
}
