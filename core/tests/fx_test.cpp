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
        && R_VW_Q == param_of("vowel.q") && R_VW_SIZE == param_of("vowel.size")
        && R_SW_RATE == param_of("swarm.rate") && R_SW_DEPTH == param_of("swarm.depth") && R_SW_DELAY == param_of("swarm.delay")
        && R_SW_FB == param_of("swarm.fb") && R_SW_SPREAD == param_of("swarm.spread") && R_SW_ZERO == param_of("swarm.zero")
        && R_CB_TUNE == param_of("comb.tune") && R_CB_HZ == param_of("comb.hz") && R_CB_DECAY == param_of("comb.decay")
        && R_CB_DAMP == param_of("comb.damp") && R_CB_DETUNE == param_of("comb.detune") && R_TP_WOW == param_of("tape.wow")
        && R_TP_FLUTTER == param_of("tape.flutter") && R_TP_DRIVE == param_of("tape.drive") && R_TP_TONE == param_of("tape.tone"));
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

/* A sound source the tests feed, stereo: 0 a 220 Hz sine at -12 dBFS, 1 pink noise (x g), 2 white noise, 3 a 1 kHz sine
   at -6 dBFS, 4 a sine at hz and amp, 5 silence. */
struct Sig {
    int kind; long n = 0; Pink pl, pr; uint32_t w = 777; double hz = 440, amp = 0.5; float g = 1;
    explicit Sig(int k) : kind(k) { pr.s = 99991; }
    void next(float &l, float &r) {
        if (kind == 0) { l = r = (float)(0.2512 * std::sin(TAU * 220 * n++ / SR)); return; }
        if (kind == 3) { l = r = (float)(0.5 * std::sin(TAU * 1000 * n++ / SR)); return; }
        if (kind == 4) { l = r = (float)(amp * std::sin(TAU * hz * n++ / SR)); return; }
        if (kind == 5) { l = r = 0; return; }
        if (kind == 1) { l = g * pl.next(); r = g * pr.next(); return; }
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

/* ---- Task 3: Swarm, Tape, Comb ---- */

/* The instantaneous frequency of x (Hz) from its analytic signal (one FFT over the first 2^k samples), averaged per ms
   (48 samples); the first and last 100 ms dropped (the FFT wraps). */
static std::vector<double> inst_freq(const std::vector<float> &x) {
    int N = 1; while (2 * N <= (int)x.size()) N *= 2;
    std::vector<double> re(x.begin(), x.begin() + N), im(N, 0.0);
    fft(re, im);
    for (int k = 1; k < N; k++) { double s = k < N / 2 ? 2 : k == N / 2 ? 1 : 0; re[k] *= s; im[k] *= s; }
    for (auto &v : im) v = -v;
    fft(re, im);                                         /* = N conj(analytic); the conjugate is undone below */
    const int D = 48, edge = (int)(0.1 * SR); std::vector<double> f;
    for (int o = edge; o + D <= N - edge; o += D) {
        double s = 0;
        for (int i = o; i < o + D; i++) {
            const double ar = re[i], ai = -im[i], br = re[i - 1], bi = -im[i - 1];
            s += std::atan2(ai * br - ar * bi, ar * br + ai * bi);
        }
        f.push_back(s / D * SR / TAU);
    }
    return f;
}
/* The strongest bin within +-50 cents of f, refined by a parabola on its dB: its offset from f in cents. */
static double peak_cents(const std::vector<double> &P, double f, int N) {
    int lo = (int)std::floor(f * std::pow(2, -50 / 1200.0) * N / SR), hi = (int)std::ceil(f * std::pow(2, 50 / 1200.0) * N / SR), b = lo;
    for (int k = lo; k <= hi; k++) if (P[k] > P[b]) b = k;
    const double a = 10 * std::log10(P[b - 1]), c = 10 * std::log10(P[b]), d = 10 * std::log10(P[b + 1]);
    const double den = a - 2 * c + d, delta = den != 0 ? 0.5 * (a - d) / den : 0;
    return 1200 * std::log2((b + delta) * SR / N / f);
}
static double band_db(const std::vector<double> &P, double f, double cents, int N) {   /* the strongest bin within +-cents of f */
    int lo = (int)std::floor(f * std::pow(2, -cents / 1200) * N / SR), hi = (int)std::ceil(f * std::pow(2, cents / 1200) * N / SR);
    double m = 1e-30; for (int k = lo; k <= hi; k++) m = std::fmax(m, P[k]);
    return 10 * std::log10(m);
}
static double centroid(const std::vector<double> &P) {
    double num = 0, den = 0;
    for (size_t b = 1; b < P.size(); b++) { num += b * SR / (2 * P.size()) * P[b]; den += P[b]; }
    return num / den;
}
/* Task 1's carried check for a real module: wet at mix 0.37 == 0.37 x wet at mix 1 (pink, after 100 ms). */
static double mix_linearity(mfx::Cfg c1, const char *mixk) {
    mfx::Cfg cm = c1; cm.v[mfx::param_of(mixk)] = 0.37f;
    mfx::Engine a, b; a.init(SR); b.init(SR); a.apply(c1); b.apply(cm);
    Sig sa(1), sb(1); std::vector<float> ya, yb;
    play(a, sa, (int)SR, &ya); play(b, sb, (int)SR, &yb);
    double lin = 0;
    for (size_t i = (size_t)(0.1 * SR); i < ya.size(); i++) lin = std::fmax(lin, std::fabs(yb[i] - 0.37 * ya[i]));
    return lin;
}
/* Level of the wet relative to the input (dB) for a module at mix 1, pink x g (settle 2 s, measure 4 s). */
static double level_rel(const mfx::Cfg &c, float g) {
    mfx::Engine e; e.init(SR); e.apply(c);
    Sig s(1); s.g = g; play(e, s, 2 * (int)SR, nullptr);
    std::vector<float> y, in; play(e, s, 4 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
    return rms_db(y) - rms_db(in);
}

static void test_swarm() {
    using namespace mfx;
    /* depth 1, rate 4n at 120 bpm: the pitch wobble repeats every 0.5 s */
    Cfg c = with({ { "swarm.mix", 1 }, { "swarm.depth", 1 }, { "swarm.rate", 1 } }); std::strcpy(c.rate[SWARM], "4n");
    Engine e; e.init(SR); e.apply(c);
    Sig s(4); s.hz = 440; double beats = 0;
    play(e, s, (int)(0.5 * SR), nullptr, false, 120, &beats);
    std::vector<float> y; play(e, s, 6 * (int)SR, &y, false, 120, &beats);
    auto f = inst_freq(y);
    double mean = 0, dev = 0;
    for (double &v : f) { v = std::fmax(-200.0, std::fmin(200.0, v - 440)); mean += v; }
    mean /= f.size();
    std::vector<double> d(f.size()); for (size_t i = 0; i < f.size(); i++) { d[i] = f[i] - mean; dev += d[i] * d[i]; }
    dev = std::sqrt(dev / d.size());
    int best = 0; double bv = -1e300;
    for (int lag = 300; lag <= 750; lag++) {
        double r = 0; for (size_t i = 0; i + lag < d.size(); i++) r += d[i] * d[i + lag];
        r /= d.size() - lag;
        if (r > bv) { bv = r; best = lag; }
    }
    const double period = best / 1000.0;
    check("swarm: the pitch deviation repeats at 0.5 s (4n at 120 bpm) +-1 %", std::fabs(period / 0.5 - 1) <= 0.01);
    check("swarm: depth 1 moves the pitch (RMS >= 1 %)", dev >= 4.4);

    /* zero: defaults (depth 0.5, 8 ms, 1n) at 120 bpm: tap 0 crosses the centre at each whole second. The envelope
       (two-period RMS) at its lowest within +-10 ms of the crossings at 2, 3 and 4 s, against its highest: the shallowest. */
    auto null_at_cross = [](float zero) {
        Engine z; z.init(SR); z.apply(with({ { "swarm.mix", 1 }, { "swarm.zero", zero } }));
        Sig s(4); s.hz = 440; double b = 0;
        play(z, s, (int)SR, nullptr, false, 120, &b);
        std::vector<float> y; play(z, s, 4 * (int)SR, &y, false, 120, &b);
        const int W = 218; double mx = 0, mn[3] = { 1e300, 1e300, 1e300 };
        for (size_t o = 0; o + W <= y.size(); o += 8) {
            double s2 = 0; for (int i = 0; i < W; i++) s2 += (double)y[o + i] * y[o + i];
            double r = std::sqrt(s2 / W), t = 1 + (o + W / 2) / SR;
            mx = std::fmax(mx, r);
            for (int c = 0; c < 3; c++) if (std::fabs(t - (2 + c)) <= 0.01) mn[c] = std::fmin(mn[c], r);
        }
        return 20 * std::log10(mx / std::fmax(std::fmax(mn[0], mn[1]), std::fmax(mn[2], 1e-12)));
    };
    double nz = null_at_cross(1), nn = null_at_cross(0);
    check("swarm: zero on -> the output nulls >= 20 dB at the sweep's crossing", nz >= 20);

    /* spread */
    auto lr = [](float spread) {
        Engine e; e.init(SR); e.apply(with({ { "swarm.mix", 1 }, { "swarm.spread", spread } }));
        Sig s(4); s.hz = 440; std::vector<float> l, r;
        play(e, s, (int)SR, &l, false, 90, nullptr, &r);
        double d = 0, pk = 0; for (size_t i = 0; i < l.size(); i++) { d = std::fmax(d, std::fabs(l[i] - r[i])); pk = std::fmax(pk, std::fabs(l[i])); }
        return d / pk;
    };
    double s0 = lr(0), s1 = lr(1);
    check("swarm: spread 0 -> L == R", s0 == 0);
    check("swarm: spread 1 -> L != R", s1 > 0.1);
    double lin = mix_linearity(with({ { "swarm.mix", 1 }, { "swarm.fb", 0.5f }, { "swarm.depth", 0.8f } }), "swarm.mix");
    check("swarm: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf("fx swarm: 4n at 120 bpm -> deviation period %.3f s (want 0.500), RMS %.1f Hz; zero on: null %.1f dB at the crossings (off: %.1f dB); spread 0 L-R %.2g, 1 %.2f; mix err %.2g\n",
                period, dev, nz, nn, s0, s1, lin);
}

static void test_tape() {
    using namespace mfx;
    auto dev_of = [](float wow, float fl) {
        Engine e; e.init(SR); e.apply(with({ { "tape.mix", 1 }, { "tape.wow", wow }, { "tape.flutter", fl }, { "tape.drive", 0 }, { "tape.tone", 16000 } }));
        Sig s(3); play(e, s, (int)(0.5 * SR), nullptr);
        std::vector<float> y; play(e, s, 11 * (int)SR, &y);
        auto f = inst_freq(y);
        for (double &v : f) v = v - 1000;
        return f;
    };
    auto w = dev_of(1, 0);
    double wpk = 0; for (double v : w) wpk = std::fmax(wpk, std::fabs(v) / 10);   /* % of 1 kHz */
    check("tape: wow 1 -> pitch deviation 1.5-2.5 % peak", wpk >= 1.5 && wpk <= 2.5);
    auto fl = dev_of(0, 1);
    double fpk = 0, mean = 0; for (double v : fl) { fpk = std::fmax(fpk, std::fabs(v) / 10); mean += v; }
    mean /= fl.size();
    const int N = 8192; std::vector<double> re(N), im(N, 0.0);
    for (int i = 0; i < N; i++) re[i] = (fl[i] - mean) * (0.5 - 0.5 * std::cos(TAU * i / N));
    fft(re, im);
    int kb = 0; double pb = 0;
    for (int k = (int)(1.0 * N / 1000); k <= (int)(40.0 * N / 1000); k++) { double p = re[k] * re[k] + im[k] * im[k]; if (p > pb) { pb = p; kb = k; } }
    const double ff = kb * 1000.0 / N;
    check("tape: flutter 1 -> the deviation's energy at 7.3 Hz +-0.5", std::fabs(ff - 7.3) <= 0.5);

    /* drive 0 -> 1 at -30 and -6 dBFS: the level held, the third harmonic up >= 12 dB at both (A-19) */
    std::printf("fx tape: drive level / H3 (dB):");
    for (double amp : { 0.0316, 0.5 }) {
        double h0 = 0, h1 = 0;
        for (float dr : { 0.0f, 0.5f, 1.0f }) {
            Engine e; e.init(SR); e.apply(with({ { "tape.mix", 1 }, { "tape.wow", 0 }, { "tape.flutter", 0 }, { "tape.drive", dr }, { "tape.tone", 16000 } }));
            Sig s(4); s.hz = 1000; s.amp = amp; play(e, s, (int)SR, nullptr);
            std::vector<float> y, in; play(e, s, 2 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
            auto P = power(y);
            double lv = rms_db(y) - rms_db(in), h3 = tone_db(P, 3000) - tone_db(P, 1000);
            if (dr == 0) h0 = h3; if (dr == 1) h1 = h3;
            check("tape: drive keeps the level within 3 dB", std::fabs(lv) <= 3);
            std::printf(" %s%.1f d%.1f: %.2f/%.0f", dr == 0 ? "| " : "", 20 * std::log10(amp), dr, lv, h3);
        }
        check("tape: drive 0 -> 1 raises the third harmonic >= 12 dB", h1 - h0 >= 12);
    }
    /* A-18: tone moves the spectrum, not the level */
    double lmin = 1e9, lmax = -1e9, cmin = 1e9, cmax = 0;
    for (float t : { 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "tape.mix", 1 }, { "tape.tone", t } }));
        Sig s(1); play(e, s, 2 * (int)SR, nullptr);
        std::vector<float> y, in; play(e, s, 4 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
        double lv = rms_db(y) - rms_db(in), cen = centroid(power(y));
        lmin = std::fmin(lmin, lv); lmax = std::fmax(lmax, lv); cmin = std::fmin(cmin, cen); cmax = std::fmax(cmax, cen);
    }
    check("tape: level within 3 dB across tone", lmax - lmin <= 3);
    check("tape: tone moves the centroid >= 30 %", (cmax - cmin) / cmax >= 0.3);
    double lin = mix_linearity(with({ { "tape.mix", 1 }, { "tape.drive", 0.6f } }), "tape.mix");
    check("tape: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf("\nfx tape: wow 1 peak %.2f %%, flutter 1 at %.2f Hz (peak %.2f %%); tone level range %.2f dB, centroid %.0f..%.0f Hz; mix err %.2g\n",
                wpk, ff, fpk, lmax - lmin, cmin, cmax, lin);
}

static void test_comb() {
    using namespace mfx;
    static const float DM[5] = { 50, 53, 57, 60, -1 }, GM[5] = { 55, 59, 62, -1, -1 };
    const int N = 1 << 18;
    auto spectrum = [&](const Cfg &c, bool chord) {
        Engine e; e.init(SR); e.apply(c);
        if (chord) e.chord(DM, 50, 1);
        Sig s(1); play(e, s, 2 * (int)SR, nullptr);
        std::vector<float> l, r; play(e, s, 16 * (int)SR, &l, false, 90, nullptr, &r);
        for (size_t i = 0; i < l.size(); i++) l[i] = 0.5f * (l[i] + r[i]);
        return power(l, N);
    };
    auto mtof = [](double m) { return 440 * std::pow(2, (m - 69) / 12); };
    /* the chord D F A C: combs on D3 F3 A3 C4, the fifth comb D4 (the root an octave up) */
    auto P = spectrum(with({ { "comb.mix", 1 } }), true);
    double worst = 0;
    std::printf("fx comb: chord peaks (cents):");
    for (double m : { 50.0, 53.0, 57.0, 60.0, 62.0 }) {
        double c = peak_cents(P, mtof(m), N); worst = std::fmax(worst, std::fabs(c));
        std::printf(" %.0f:%+.1f", m, c);
    }
    double contrast = band_db(P, mtof(50), 20, N) - band_db(P, mtof(52), 20, N);
    check("comb: chord peaks within +-10 cents of D F A C", worst <= 10);
    check("comb: E3 (no chord note) >= 6 dB under the D3 peak", contrast >= 6);
    auto F = spectrum(with({ { "comb.mix", 1 }, { "comb.tune", 1 }, { "comb.hz", 110 } }), false);
    double fw = 0;
    std::printf("; free 110:");
    for (double f : { 110.0, 165.0, 220.0, 330.0, 440.0 }) { double c = peak_cents(F, f, N); fw = std::fmax(fw, std::fabs(c)); std::printf(" %.0f:%+.1f", f, c); }
    check("comb: free 110 -> peaks at 110 165 220 330 440 +-10 cents", fw <= 10);
    std::printf("; D3 over E3 %.1f dB\n", contrast);

    /* a chord change glides: D -> G and back, sine and pink */
    double gw = 0;
    for (int kind = 0; kind < 2; kind++)
        for (int dir = 0; dir < 2; dir++) {
            Engine e; e.init(SR); e.apply(with({ { "comb.mix", 1 } }));
            e.chord(dir ? GM : DM, dir ? 55 : 50, 1);
            Sig s(kind); std::vector<float> y; double beats = 0;
            play(e, s, (int)SR, nullptr, false, 90, &beats);
            play(e, s, (int)(0.5 * SR), &y, false, 90, &beats);
            e.chord(dir ? DM : GM, dir ? 50 : 55, 2);
            play(e, s, (int)(0.05 * SR) + 128, &y, false, 90, &beats);
            double r = click_ratio(y); gw = std::fmax(gw, r);
            std::printf("  click comb chord %s %s %.2f\n", dir ? "G->D" : "D->G", kind ? "pink" : "sine", r);
        }
    check("comb: a chord change glides (click <= 4)", gw <= 4);

    /* decay 0.05 -> 20 s: the level held while the peaks sharpen (A-18) */
    double lmin = 1e9, lmax = -1e9;
    std::printf("fx comb: decay level/D3-over-E3 (dB):");
    for (float d : { 0.05f, 0.2f, 0.5f, 2.0f, 8.0f, 20.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "comb.mix", 1 }, { "comb.decay", d } })); e.chord(DM, 50, 1);
        Sig s(1); play(e, s, 4 * (int)SR, nullptr);
        std::vector<float> y, in; play(e, s, 8 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
        auto Q = power(y, 1 << 16);
        double lv = rms_db(y) - rms_db(in), ct = band_db(Q, mtof(50), 20, 1 << 16) - band_db(Q, mtof(52), 20, 1 << 16);
        lmin = std::fmin(lmin, lv); lmax = std::fmax(lmax, lv);
        std::printf(" %.2g s: %.2f/%.1f", d, lv, ct);
    }
    check("comb: level within 3 dB across decay 0.05-20 s", lmax - lmin <= 3);

    /* RT60 (Schroeder, -5 to -35 dB, x2) after 3 s of pink, the chord D */
    std::printf("\nfx comb: RT60 measured:");
    for (float d : { 0.5f, 2.0f, 8.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "comb.mix", 1 }, { "comb.decay", d } })); e.chord(DM, 50, 1);
        Sig s(1); play(e, s, 3 * (int)SR, nullptr);
        s.kind = 5; std::vector<float> l, r; play(e, s, (int)((1.5 * d + 0.5) * SR), &l, false, 90, nullptr, &r);
        std::vector<double> edc(l.size() + 1, 0.0);
        for (size_t i = l.size(); i-- > 0;) edc[i] = edc[i + 1] + (double)l[i] * l[i] + (double)r[i] * r[i];
        const size_t i0 = (size_t)(0.01 * SR); double t5 = -1, t35 = -1;
        for (size_t i = i0; i < l.size(); i++) {
            double db = 10 * std::log10(edc[i] / edc[i0] + 1e-300);
            if (t5 < 0 && db <= -5) t5 = i / SR;
            if (t35 < 0 && db <= -35) { t35 = i / SR; break; }
        }
        double rt = t35 > 0 && t5 >= 0 ? 2 * (t35 - t5) : -1;
        check("comb: RT60 within 25 % of decay", rt > 0 && std::fabs(rt / d - 1) <= 0.25);
        std::printf(" %.1f s -> %.2f s;", d, rt);
    }
    double lin = mix_linearity(with({ { "comb.mix", 1 } }), "comb.mix");
    check("comb: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf(" decay level range %.2f dB; mix err %.2g\n", lmax - lmin, lin);
}

/* A-19: the LevelMatch modules (Vowel, Comb, Tape, at mix 1) hold the level at -30 dBFS as at -6 (pink 24 dB apart). */
static void test_levels() {
    std::printf("fx levels: module out-in (dB) loud/quiet:");
    for (const char *m : { "vowel.mix", "comb.mix", "tape.mix" }) {
        mfx::Cfg c = with({ { m, 1 }, { "vowel.pos", 2 } });
        double a = level_rel(c, 1), b = level_rel(c, 1 / 16.0f);
        check("levels: within 3 dB of the input, loud and quiet", std::fabs(a) <= 3 && std::fabs(b) <= 3);
        check("levels: quiet within 1 dB of loud", std::fabs(a - b) <= 1);
        std::printf(" %s %.2f/%.2f", m, a, b);
    }
    std::printf("\n");
}

/* Every parameter stepped min -> max (module at mix 1, its other parameters default; y = the wet) - and for Swarm, Comb,
   Tape also max -> min - and each mix 0 -> 1 -> 0 (y = in + wet, since the wet alone is 0 before), on the sine and pink. */
static void test_clicks() {
    using namespace mfx;
    struct M { Mod mod; bool back; };
    static const M MODS[] = { { SHIFT, false }, { PHASE, false }, { VOWEL, false }, { SWARM, true }, { COMB, true }, { TAPE, true } };
    double worst = 0; std::string wk;
    for (const M &md : MODS)
        for (int i = 0; i < NPARAM; i++) {
            const ParamDef &p = PARAMS[i];
            if (p.mod != md.mod) continue;
            std::string mixk = std::string(MOD_NAMES[p.mod]) + ".mix";
            bool is_mix = !std::strcmp(p.key, mixk.c_str());
            for (int kind = 0; kind < 2; kind++) {
                double r, rb = 0;
                if (is_mix) {
                    Cfg off = with({}), on = with({ { p.key, 1 } });
                    r = std::fmax(click_of(off, on, kind, true), click_of(on, off, kind, true));
                } else {
                    Cfg a = with({ { mixk.c_str(), 1 }, { p.key, p.min } }), b = with({ { mixk.c_str(), 1 }, { p.key, p.max } });
                    if (!p.cont && p.taper == EXP) { std::strcpy(a.rate[p.mod], "32n"); std::strcpy(b.rate[p.mod], "32m"); }
                    r = click_of(a, b, kind, false);
                    if (md.back) rb = click_of(b, a, kind, false);
                }
                std::printf("  click %-13s %s %.2f", p.key, kind ? "pink" : "sine", r);
                if (md.back && !is_mix) std::printf(" back %.2f", rb);
                std::printf("%s\n", std::fmax(r, rb) > 4 ? "  FAIL" : "");
                check("clicks: every parameter (both ways for swarm/comb/tape) and mix <= 4", std::fmax(r, rb) <= 4);
                if (std::fmax(r, rb) > worst) { worst = std::fmax(r, rb); wk = p.key; }
            }
        }
    std::printf("fx clicks: all six modules - every parameter min -> max (swarm/comb/tape also back), mix 0 -> 1 -> 0, sine and pink: worst %.2f (%s)\n",
                worst, wk.c_str());
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
    test_swarm();
    test_tape();
    test_comb();
    test_levels();
    test_clicks();
    if (failed) { std::printf("fx FAILED: %d checks\n", failed); return 1; }
    std::printf("fx ok\n");
    return 0;
}
