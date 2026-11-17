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
        { "space.mod", 0, 1, 1, true },
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
        && R_TP_FLUTTER == param_of("tape.flutter") && R_TP_DRIVE == param_of("tape.drive") && R_TP_TONE == param_of("tape.tone")
        && R_CL_SIZE == param_of("cloud.size") && R_CL_DENS == param_of("cloud.dens") && R_CL_PITCH == param_of("cloud.pitch")
        && R_CL_SPRAY == param_of("cloud.spray") && R_CL_SCATTER == param_of("cloud.scatter") && R_CL_REV == param_of("cloud.rev")
        && R_CL_FREEZE == param_of("cloud.freeze") && R_EC_TIME == param_of("echo.time") && R_EC_FB == param_of("echo.fb")
        && R_EC_TONE == param_of("echo.tone") && R_EC_SHIFT == param_of("echo.shift") && R_EC_WOBBLE == param_of("echo.wobble")
        && R_EC_SPREAD == param_of("echo.spread") && R_SP_SIZE == param_of("space.size") && R_SP_DECAY == param_of("space.decay")
        && R_SP_DAMP == param_of("space.damp") && R_SP_MOD == param_of("space.mod"));
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

/* Chunking: n 300 (128 + 128 + 44) gives the same sound as n 100 (Echo and Space on): no overrun, no skipped
   samples. Each module's own linearity in its mix (wet = mix * f(x)) is checked in its test. */
static void test_linear() {
    using namespace mfx;
    Cfg c; cfg_defaults(c);
    c.v[param_of("echo.mix")] = 0.25f; c.v[param_of("space.mix")] = 0.5f;
    float iL[300], iR[300], wL[300], wR[300];
    Engine a, b; a.init(48000); b.init(48000); a.apply(c); b.apply(c);
    Pink pa, pb; double sa = 0, sb = 0, d = 0; bool fin = true;
    for (int done = 0; done < 48000 * 2; done += 300) {
        for (int i = 0; i < 300; i++) { iL[i] = pa.next(); iR[i] = pa.next(); }
        a.process(iL, iR, wL, wR, 300);
        for (int i = 0; i < 300; i++) { sa += (double)wL[i] * wL[i]; fin = fin && std::isfinite(wL[i]) && std::isfinite(wR[i]); }
    }
    for (int done = 0; done < 48000 * 2; done += 300)
        for (int o = 0; o < 300; o += 100) {
            for (int i = 0; i < 100; i++) { iL[i] = pb.next(); iR[i] = pb.next(); }
            b.process(iL, iR, wL, wR, 100);
            for (int i = 0; i < 100; i++) sb += (double)wL[i] * wL[i];
        }
    d = 10 * std::log10(sa / sb);
    check("chunks: n 300 and n 100 finite and the same level (0.1 dB)", fin && std::fabs(d) <= 0.1);
    std::printf("fx linear: n 300 vs n 100, echo 0.25 + space 0.5: level difference %.3f dB\n", d);
}

/* ---- Task 2: Shift, Phase, Vowel ---- */
static const double SR = 48000, TAU = 6.283185307179586;

/* A sound source the tests feed, stereo: 0 a 220 Hz sine at -12 dBFS, 1 pink noise (x g), 2 white noise (x g: g 2 is 0 dBFS), 3 a 1 kHz sine
   at -6 dBFS, 4 a sine at hz and amp, 5 silence, 6 an impulse train (1 kHz, 0.5), 7 a rising chirp (200 -> 1600 Hz
   in 1.5 s, again), 8 a time code (left 0.05 x seconds, right 0.5: left / right x 10 is the time it was played). */
struct Sig {
    int kind; long n = 0; Pink pl, pr; uint32_t w = 777; double hz = 440, amp = 0.5, ph = 0; float g = 1;
    explicit Sig(int k) : kind(k) { pr.s = 99991; }
    void next(float &l, float &r) {
        if (kind == 6) { l = r = n++ % 48 == 0 ? 0.5f : 0.0f; return; }
        if (kind == 7) { ph += TAU * 200 * std::pow(2.0, 2 * std::fmod(n++ / SR, 1.5)) / SR; l = r = (float)(0.5 * std::sin(ph)); return; }
        if (kind == 8) { l = (float)(0.05 * n++ / SR); r = 0.5f; return; }
        if (kind == 0) { l = r = (float)(0.2512 * std::sin(TAU * 220 * n++ / SR)); return; }
        if (kind == 3) { l = r = (float)(0.5 * std::sin(TAU * 1000 * n++ / SR)); return; }
        if (kind == 4) { l = r = (float)(amp * std::sin(TAU * hz * n++ / SR)); return; }
        if (kind == 5) { l = r = 0; return; }
        if (kind == 1) { l = g * pl.next(); r = g * pr.next(); return; }
        w ^= w << 13; w ^= w >> 17; w ^= w << 5; l = g * ((float)w / 4294967296.0f - 0.5f);
        w ^= w << 13; w ^= w >> 17; w ^= w << 5; r = g * ((float)w / 4294967296.0f - 0.5f);
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
static double click_ratio(const std::vector<float> &y, double post_s = 0.05) {
    const int pre = (int)(0.5 * SR), post = (int)(post_s * SR);
    double a = 0, b = 0;
    for (int n = 2; n < pre + post && n < (int)y.size(); n++) {
        double d = std::fabs((double)y[n] - 2.0 * y[n - 1] + y[n - 2]);
        if (n < pre) a = std::fmax(a, d); else b = std::fmax(b, d);
    }
    return a > 0 ? b / a : (b > 0 ? 1e9 : 0);
}
/* From `from`, settle 1 s, then 500 ms recorded, then `to` and 50 ms: the ratio. */
static double click_of(const mfx::Cfg &from, const mfx::Cfg &to, int kind, bool plus_in, double post = 0.05) {
    mfx::Engine e; e.init(SR); e.apply(from);
    Sig s(kind); std::vector<float> y; double beats = 0;
    play(e, s, (int)SR, nullptr, plus_in, 90, &beats);
    play(e, s, (int)(0.5 * SR), &y, plus_in, 90, &beats);
    e.apply(to);
    play(e, s, (int)(post * SR) + 128, &y, plus_in, 90, &beats);
    return click_ratio(y, post);
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

    /* zero: defaults (depth 0.5, 8 ms, 1n) at 120 bpm, a sine at hz: tap 0 crosses the centre at each whole second. The envelope
       (two-period RMS) at its lowest within +-10 ms of the crossings at 2, 3 and 4 s, against its highest: the shallowest. */
    auto null_at_cross = [](float zero, double hz) {
        Engine z; z.init(SR); z.apply(with({ { "swarm.mix", 1 }, { "swarm.zero", zero } }));
        Sig s(4); s.hz = hz; double b = 0;
        play(z, s, (int)SR, nullptr, false, 120, &b);
        std::vector<float> y; play(z, s, 4 * (int)SR, &y, false, 120, &b);
        const int W = (int)std::lround(2 * SR / hz); double mx = 0, mn[3] = { 1e300, 1e300, 1e300 };
        for (size_t o = 0; o + W <= y.size(); o += 8) {
            double s2 = 0; for (int i = 0; i < W; i++) s2 += (double)y[o + i] * y[o + i];
            double r = std::sqrt(s2 / W), t = 1 + (o + W / 2) / SR;
            mx = std::fmax(mx, r);
            for (int c = 0; c < 3; c++) if (std::fabs(t - (2 + c)) <= 0.01) mn[c] = std::fmin(mn[c], r);
        }
        return 20 * std::log10(mx / std::fmax(std::fmax(mn[0], mn[1]), std::fmax(mn[2], 1e-12)));
    };
    /* 300 Hz: there the three taps (zero off) do not cancel at the crossings (at 440 Hz they do, by chance, 29 dB) */
    double nz = null_at_cross(1, 300), nn = null_at_cross(0, 300);
    check("swarm: zero on -> the output nulls >= 20 dB at the sweep's crossing", nz >= 20);
    check("swarm: the null is zero's (>= 10 dB deeper than zero off)", nz - nn >= 10);

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

/* ---- Task 4: Cloud ---- */

static void test_cloud() {
    using namespace mfx;
    /* density: the 1 kHz impulse train, 20 ms grains at scatter 0; a grain is a run of output impulses over 0.2 x the
       peak with gaps under 3 ms (grains start >= 0.8 / dens apart: >= 6 ms of quiet between runs at 40) */
    std::printf("fx cloud: grains/s (want):");
    for (float dens : { 4.0f, 12.0f, 40.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "cloud.mix", 1 }, { "cloud.size", 20 }, { "cloud.scatter", 0 }, { "cloud.dens", dens } }));
        Sig s(6); play(e, s, (int)SR, nullptr);
        std::vector<float> y; play(e, s, 10 * (int)SR, &y);
        float pk = 0; for (float v : y) pk = std::fmax(pk, std::fabs(v));
        int grains = 0; long last = -100000;
        for (long i = 0; i < (long)y.size(); i++)
            if (std::fabs(y[i]) > 0.2f * pk) { if (i - last > (long)(0.003 * SR)) grains++; last = i; }
        const double rate = grains / 10.0;
        check("cloud: grains per second within 20 % of dens", std::fabs(rate / dens - 1) <= 0.2);
        std::printf(" %.1f (%.0f)", rate, dens);
    }
    /* pitch +12 on the 220 Hz sine -> 440 Hz */
    {
        Engine e; e.init(SR); e.apply(with({ { "cloud.mix", 1 }, { "cloud.pitch", 12 } }));
        Sig s(0); play(e, s, (int)SR, nullptr);
        std::vector<float> y; play(e, s, 4 * (int)SR, &y);
        auto P = power(y);
        double up = tone_db(P, 440) - tone_db(P, 220);
        check("cloud: pitch +12 -> 440 Hz >= 20 dB over 220", up >= 20);
        std::printf("; pitch +12: 440 over 220 %.1f dB", up);
    }
    /* rev on the rising chirp: of the 1 ms steps inside grains (both blocks >= a quarter of the loudest's energy), the
       share falling */
    auto falling = [](float rev) {
        Engine e; e.init(SR); e.apply(with({ { "cloud.mix", 1 }, { "cloud.size", 200 }, { "cloud.dens", 2 }, { "cloud.scatter", 0 }, { "cloud.rev", rev } }));
        Sig s(7); play(e, s, (int)SR, nullptr);
        std::vector<float> y; play(e, s, 6 * (int)SR, &y);
        auto f = inst_freq(y);
        std::vector<double> en(f.size());
        double mx = 0;
        for (size_t j = 0; j < f.size(); j++) {
            double s2 = 0; for (int i = 0; i < 48; i++) { double v = y[(size_t)(0.1 * SR) + 48 * j + i]; s2 += v * v; }
            en[j] = s2; mx = std::fmax(mx, s2);
        }
        int down = 0, all = 0;
        for (size_t j = 0; j + 1 < f.size(); j++) {
            if (en[j] < 0.25 * mx || en[j + 1] < 0.25 * mx || std::fabs(f[j + 1] - f[j]) > 20) continue;
            all++; down += f[j + 1] < f[j];
        }
        return all ? (double)down / all : -1;
    };
    double f0 = falling(0), f1 = falling(1);
    check("cloud: rev 0 -> the chirps still rise (<= 10 % of steps fall)", f0 >= 0 && f0 <= 0.1);
    check("cloud: rev 1 -> falling chirps (>= 90 % of steps fall)", f1 >= 0.9);
    std::printf("; chirp steps falling rev 0 %.0f %%, rev 1 %.0f %%\n", 100 * f0, 100 * f1);

    /* scatter: the time code read back (left / right x 10 = when it was played): each grain's delay behind now */
    auto delays = [](float scatter, double &lo, double &hi) {
        Engine e; e.init(SR); e.apply(with({ { "cloud.mix", 1 }, { "cloud.dens", 2 }, { "cloud.scatter", scatter } }));
        Sig s(8); std::vector<float> l, r; play(e, s, 12 * (int)SR, &l, false, 90, nullptr, &r);
        lo = 1e9; hi = -1e9;
        for (size_t i = 3 * (size_t)SR; i < l.size(); i++) {
            if (std::fabs(r[i]) < 0.05f) continue;
            double d = i / SR - 10.0 * l[i] / r[i];
            lo = std::fmin(lo, d); hi = std::fmax(hi, d);
        }
    };
    double l0, h0, l1, h1; delays(0, l0, h0); delays(1, l1, h1);
    check("cloud: scatter 0 -> every grain size (120 ms) behind now, +-1 ms", std::fabs(l0 - 0.12) <= 0.001 && std::fabs(h0 - 0.12) <= 0.001);
    check("cloud: scatter 1 -> start times spread >= 1 s", h1 - l1 >= 1);
    std::printf("fx cloud: scatter 0 delays %.4f..%.4f s (want 0.120), scatter 1 %.3f..%.3f s\n", l0, h0, l1, h1);

    /* freeze: pink, freeze on, then 5 s of silence: every second within 3 dB of the level before */
    {
        Engine e; e.init(SR); Cfg c = with({ { "cloud.mix", 1 } }); e.apply(c);
        Sig s(1); play(e, s, 3 * (int)SR, nullptr);
        std::vector<float> y; play(e, s, (int)SR, &y);
        const double before = rms_db(y);
        c.v[param_of("cloud.freeze")] = 1; e.apply(c);
        play(e, s, (int)(0.1 * SR), nullptr);
        s.kind = 5;
        double worst = 0; std::printf("fx cloud: freeze, silence in, level vs before (dB):");
        for (int sec = 0; sec < 5; sec++) {
            y.clear(); play(e, s, (int)SR, &y);
            double d = rms_db(y) - before; worst = std::fmax(worst, std::fabs(d));
            std::printf(" %.2f", d);
        }
        check("cloud: frozen, 5 s of silence in -> level within 3 dB of before", worst <= 3);
        /* the toggle, both ways: on after 3 s live, off after 1.5 s more frozen (the loop full: frozen sooner, it still
           holds the silence from before the engine started). The click metric over the 0.8 s after, not 50 ms: a splice in
           the line is heard only when a grain reaches it, >= size (120 ms) behind the head. */
        double ck = 0;
        Cfg off = with({ { "cloud.mix", 1 } }), on = with({ { "cloud.mix", 1 }, { "cloud.freeze", 1 } });
        for (int kind = 0; kind < 2; kind++) {
            Engine f; f.init(SR); f.apply(off);
            Sig t(kind); std::vector<float> za, zb; double bt = 0;
            play(f, t, (int)(2.5 * SR), nullptr, false, 90, &bt);
            play(f, t, (int)(0.5 * SR), &za, false, 90, &bt);
            f.apply(on); play(f, t, (int)(0.8 * SR) + 128, &za, false, 90, &bt);
            play(f, t, (int)(0.2 * SR), nullptr, false, 90, &bt);
            play(f, t, (int)(0.5 * SR), &zb, false, 90, &bt);
            f.apply(off); play(f, t, (int)(0.8 * SR) + 128, &zb, false, 90, &bt);
            const double a = click_ratio(za, 0.8), b = click_ratio(zb, 0.8);
            ck = std::fmax(ck, std::fmax(a, b));
            std::printf("; toggle %s on %.2f off %.2f", kind ? "pink" : "sine", a, b);
        }
        check("cloud: the freeze toggle passes the click metric (both ways)", ck <= 4);
        std::printf("\n");
        /* freeze already on when mix leaves 0 (the line just cleared): it sounds from the start, freezes once the line has
           filled (2.5 s), then holds through silence */
        Engine g; g.init(SR); g.apply(on);
        Sig u(1); std::vector<float> early, late, held, ie, il;
        play(g, u, (int)SR, nullptr); play(g, u, (int)SR, &early, false, 90, nullptr, nullptr, &ie);
        play(g, u, (int)SR, nullptr); play(g, u, (int)SR, &late, false, 90, nullptr, nullptr, &il);
        u.kind = 5; play(g, u, (int)SR, nullptr); play(g, u, 2 * (int)SR, &held);
        const double de = rms_db(early) - rms_db(ie), dl = rms_db(late) - rms_db(il), dh = rms_db(held) - rms_db(late);
        check("cloud: freeze on before mix rises -> sounds at once (1-2 s and 3-4 s within 3 dB of the input)", std::fabs(de) <= 3 && std::fabs(dl) <= 3);
        check("cloud: ... and freezes once the line has filled (silence in: within 3 dB)", std::fabs(dh) <= 3);
        std::printf("fx cloud: freeze set before mix rises: 1-2 s %.2f dB, 3-4 s %.2f dB (vs the input), silence in 5-7 s %.2f dB (vs 3-4 s)\n", de, dl, dh);
    }
    /* the same input twice -> the same output (seeded per engine) */
    {
        Cfg c = with({ { "cloud.mix", 1 }, { "cloud.spray", 0.5f }, { "cloud.rev", 0.5f }, { "cloud.scatter", 0.7f } });
        Engine a, b; a.init(SR); b.init(SR); a.apply(c); b.apply(c);
        Sig sa(1), sb(1); std::vector<float> ya, yb;
        play(a, sa, 3 * (int)SR, &ya); play(b, sb, 3 * (int)SR, &yb);
        check("cloud: the same input twice -> identical output", ya == yb);
        Cfg off = c; off.v[param_of("cloud.mix")] = 0;      /* skipped (cleared), back on: the same cloud again */
        a.apply(off); play(a, sa, (int)(0.1 * SR), nullptr);
        a.apply(c); Sig sc(1); std::vector<float> yc; play(a, sc, 3 * (int)SR, &yc);
        check("cloud: skipped and back on -> the same cloud again", yc == ya);
    }
    /* dens x size, dens size >= 1 to dense (the sparse end, under one grain at a time, is quieter by its duty: controller
       2026-11-17), at scatter 0.3 and at scatter 0 (pitch 0: the grains read the same sound, in phase): the level held */
    double lmin = 1e9, lmax = -1e9;
    for (float sc : { 0.3f, 0.0f }) {
        std::printf("fx cloud: dens x size level (dB) at scatter %.1f:", sc);
        for (auto ds : { std::pair<float, float>{ 2, 500 }, { 4, 250 }, { 12, 120 }, { 20, 200 }, { 40, 200 }, { 40, 500 } }) {
            double lv = level_rel(with({ { "cloud.mix", 1 }, { "cloud.dens", ds.first }, { "cloud.size", ds.second }, { "cloud.scatter", sc } }), 1);
            lmin = std::fmin(lmin, lv); lmax = std::fmax(lmax, lv);
            std::printf(" %gx%g: %.2f", ds.first, ds.second, lv);
        }
        std::printf("\n");
    }
    check("cloud: level range <= 4 dB, dens size 1 to dense, scatter 0.3 and 0", lmax - lmin <= 4);
    /* A-18: scatter and pitch are timbre: on a dense cloud (40 x 200) the level within 3 dB across scatter 0 -> 1 and
       pitch -12 -> +12 (at scatter 0, where the in-phase sum is strongest) */
    double smin = 1e9, smax = -1e9;
    std::printf("fx cloud: 40x200 level (dB) by scatter:");
    for (float sc : { 0.0f, 0.001f, 0.003f, 0.01f, 0.03f, 0.1f, 0.3f, 1.0f }) {
        double lv = level_rel(with({ { "cloud.mix", 1 }, { "cloud.dens", 40 }, { "cloud.size", 200 }, { "cloud.scatter", sc } }), 1);
        smin = std::fmin(smin, lv); smax = std::fmax(smax, lv);
        std::printf(" %g: %.2f", sc, lv);
    }
    std::printf("; by pitch at scatter 0:");
    for (float pt : { -12.0f, -1.0f, -0.1f, 0.1f, 1.0f, 12.0f }) {
        double lv = level_rel(with({ { "cloud.mix", 1 }, { "cloud.dens", 40 }, { "cloud.size", 200 }, { "cloud.scatter", 0 }, { "cloud.pitch", pt } }), 1);
        smin = std::fmin(smin, lv); smax = std::fmax(smax, lv);
        std::printf(" %g: %.2f", pt, lv);
    }
    check("cloud: A-18 level within 3 dB across scatter 0 -> 1 and pitch (dense, scatter 0)", smax - smin <= 3);
    std::printf("; range %.2f dB\n", smax - smin);
    double lin = mix_linearity(with({ { "cloud.mix", 1 }, { "cloud.spray", 0.5f }, { "cloud.rev", 0.3f } }), "cloud.mix");
    check("cloud: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf("; range %.2f dB; mix err %.2g\n", lmax - lmin, lin);

    /* every parameter min -> max and back (y = the wet), mix 0 -> 1 -> 0 (y = in + wet), sine and pink; freeze is the
       toggle above (from a frozen start, "off" would unfreeze an empty loop) */
    double worst = 0; std::string wk;
    for (int i = 0; i < NPARAM; i++) {
        const ParamDef &p = PARAMS[i];
        if (p.mod != CLOUD || i == R_CL_FREEZE) continue;
        for (int kind = 0; kind < 2; kind++) {
            const bool mx = !std::strcmp(p.key, "cloud.mix");
            Cfg a = mx ? with({}) : with({ { "cloud.mix", 1 }, { p.key, p.min } }), b = mx ? with({ { p.key, 1 } }) : with({ { "cloud.mix", 1 }, { p.key, p.max } });
            const double r = click_of(a, b, kind, mx), rb = click_of(b, a, kind, mx), m = std::fmax(r, rb);
            std::printf("  click %-13s %s %.2f back %.2f%s\n", p.key, kind ? "pink" : "sine", r, rb, m > 4 ? "  FAIL" : "");
            check("cloud clicks: every parameter both ways <= 4", m <= 4);
            if (m > worst) { worst = m; wk = p.key; }
        }
    }
    std::printf("fx cloud clicks: worst %.2f (%s)\n", worst, wk.c_str());
}

/* A-19: the LevelMatch modules (Vowel, Comb, Tape, at mix 1) hold the level at -30 dBFS as at -6 (pink 24 dB apart). */
static void test_levels() {
    std::printf("fx levels: module out-in (dB) loud/quiet:");
    for (const char *m : { "vowel.mix", "comb.mix", "tape.mix", "cloud.mix" }) {
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
    static const M MODS[] = { { SHIFT, false }, { PHASE, false }, { VOWEL, false }, { SWARM, true }, { COMB, true }, { TAPE, true }, { ECHO, true }, { SPACE, true } };
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
                    const bool pi = i == R_EC_TIME;   /* echo.time: y = in + wet (at 32m the wet is still silent before the change) */
                    r = click_of(a, b, kind, pi);
                    if (md.back) rb = click_of(b, a, kind, pi);
                }
                std::printf("  click %-13s %s %.2f", p.key, kind ? "pink" : "sine", r);
                if (md.back && !is_mix) std::printf(" back %.2f", rb);
                std::printf("%s\n", std::fmax(r, rb) > 4 ? "  FAIL" : "");
                check("clicks: every parameter (both ways for swarm/comb/tape) and mix <= 4", std::fmax(r, rb) <= 4);
                if (std::fmax(r, rb) > worst) { worst = std::fmax(r, rb); wk = p.key; }
            }
        }
    std::printf("fx clicks: eight modules - every parameter min -> max (swarm/comb/tape/echo/space also back), mix 0 -> 1 -> 0, sine and pink: worst %.2f (%s)\n",
                worst, wk.c_str());
}

/* ---- Task 5: Echo, Space, stability ---- */

/* Plays the given input through e (128 at a time, 90 bpm from beat 0). */
static void feed(mfx::Engine &e, const std::vector<float> &iL, const std::vector<float> &iR, std::vector<float> &oL, std::vector<float> &oR) {
    oL.assign(iL.size(), 0.0f); oR.assign(iL.size(), 0.0f); double b = 0;
    for (size_t o = 0; o + 128 <= iL.size(); o += 128) {
        e.clock(90, b); b += 128 / SR * 1.5;
        e.process(&iL[o], &iR[o], &oL[o], &oR[o], 128);
    }
}
static double win_energy(const std::vector<float> &y, long c, long h) {
    double s = 1e-30;
    for (long i = c - h; i <= c + h; i++) if (i >= 0 && i < (long)y.size()) s += (double)y[i] * y[i];
    return s;
}
/* The power centroid (Hz) of y[from, from + N) (rectangular: an echo of an impulse lies inside it). */
static double centroid_at(const std::vector<float> &y, long from, int N) {
    std::vector<double> re(N), im(N, 0.0);
    for (int i = 0; i < N; i++) re[i] = y[from + i];
    fft(re, im);
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; k++) { double p = re[k] * re[k] + im[k] * im[k]; num += k * SR / N * p; den += p; }
    return num / den;
}
/* The strongest frequency within +-30 Hz of f in y[from, from + len), zero-padded to 2^16, refined by a parabola on dB. */
static double peak_hz(const std::vector<float> &y, long from, int len, double f) {
    const int N = 1 << 16; std::vector<double> re(N, 0.0), im(N, 0.0);
    for (int i = 0; i < len; i++) re[i] = y[from + i] * (0.5 - 0.5 * std::cos(TAU * i / len));
    fft(re, im);
    int lo = (int)((f - 30) * N / SR), hi = (int)((f + 30) * N / SR), b = lo;
    auto P = [&](int k) { return 10 * std::log10(re[k] * re[k] + im[k] * im[k] + 1e-300); };
    for (int k = lo; k <= hi; k++) if (P(k) > P(b)) b = k;
    const double a = P(b - 1), c = P(b), d = P(b + 1), den = a - 2 * c + d;
    return (b + (den != 0 ? 0.5 * (a - d) / den : 0)) * SR / N;
}
/* RT60 (Schroeder, -5 to -35 dB, x2) of the stereo tail l, r. */
static double rt60_of(const std::vector<float> &l, const std::vector<float> &r) {
    std::vector<double> edc(l.size() + 1, 0.0);
    for (size_t i = l.size(); i-- > 0;) edc[i] = edc[i + 1] + (double)l[i] * l[i] + (double)r[i] * r[i];
    const size_t i0 = (size_t)(0.01 * SR); double t5 = -1, t35 = -1;
    for (size_t i = i0; i < l.size(); i++) {
        double db = 10 * std::log10(edc[i] / edc[i0] + 1e-300);
        if (t5 < 0 && db <= -5) t5 = i / SR;
        if (t35 < 0 && db <= -35) { t35 = i / SR; break; }
    }
    return t35 > 0 && t5 >= 0 ? 2 * (t35 - t5) : -1;
}

static void test_echo() {
    using namespace mfx;
    const long T0 = 4800, D = 16000;     /* the impulse after the 10 ms cold-start fade; 8n at 90 bpm = 1/3 s */
    auto ecfg = [](std::initializer_list<std::pair<const char *, float>> kv) {
        Cfg c = with(kv); c.v[param_of("echo.time")] = 0.5f; std::strcpy(c.rate[ECHO], "8n"); return c;
    };
    auto impulse = [&](const Cfg &c, int reps, std::vector<float> &l, std::vector<float> &r) {
        Engine e; e.init(SR); e.apply(c);
        size_t N = (size_t)(T0 + D * reps + 4000); N = (N + 127) / 128 * 128;
        std::vector<float> iL(N, 0.0f), iR(N, 0.0f); iL[T0] = iR[T0] = 0.5f;
        feed(e, iL, iR, l, r);
    };
    std::vector<float> l, r;
    const float fb = 0.6f;
    impulse(ecfg({ { "echo.mix", 1 }, { "echo.fb", fb }, { "echo.spread", 0 } }), 6, l, r);
    long pk = T0 + D - 200;
    for (long i = T0 + D - 200; i <= T0 + D + 200; i++) if (std::fabs(l[i]) > std::fabs(l[pk])) pk = i;
    check("echo: 8n at 90 bpm -> the first repeat at 16000 +-2 samples", std::labs(pk - (T0 + D)) <= 2);
    const double e1 = win_energy(l, T0 + D, 8); double werr = 0;
    std::printf("fx echo: first repeat at +%ld samples; repeat k vs fb^(k-1) (dB):", pk - T0);
    for (int k = 2; k <= 6; k++) {
        double db = 10 * std::log10(win_energy(l, T0 + k * D, 8) / e1), want = 20 * std::log10(std::pow(fb, k - 1));
        werr = std::fmax(werr, std::fabs(db - want)); std::printf(" %d: %+.2f", k, db - want);
    }
    check("echo: repeat k is fb^(k-1) x the first, +-1 dB (k 2-6, tone 0)", werr <= 1);

    /* shift +10: a 100 ms 1 kHz burst; repeat k's partial at 1000 + 10 k Hz */
    {
        Engine e; e.init(SR); e.apply(ecfg({ { "echo.mix", 1 }, { "echo.fb", 0.7f }, { "echo.spread", 0 }, { "echo.shift", 10 } }));
        const long B = 4800, BL = 4800; size_t N = (size_t)(B + 5 * D + 8000); N = (N + 127) / 128 * 128;
        std::vector<float> iL(N, 0.0f), iR;
        for (long i = 0; i < BL; i++) iL[B + i] = (float)(0.5 * (0.5 - 0.5 * std::cos(TAU * i / BL)) * std::sin(TAU * 1000 * i / SR));
        iR = iL; feed(e, iL, iR, l, r);
        double worst = 0; std::printf("\nfx echo: shift +10, repeat partials (Hz):");
        for (int k = 1; k <= 4; k++) {
            double f = peak_hz(l, B + k * D - 400, (int)BL + 800, 1000 + 10 * k);
            worst = std::fmax(worst, std::fabs(f - (1000 + 10 * k))); std::printf(" %d: %.2f", k, f);
        }
        check("echo: shift +10 -> repeat k's partial at 1000 + 10 k Hz +-2 (k 1-4)", worst <= 2);
    }
    /* tone: -1 darkens every repeat, +1 thins every repeat */
    auto cents = [&](float tone) {
        impulse(ecfg({ { "echo.mix", 1 }, { "echo.fb", 0.8f }, { "echo.spread", 0 }, { "echo.tone", tone } }), 5, l, r);
        std::vector<double> c;
        for (int k = 1; k <= 5; k++) c.push_back(centroid_at(l, T0 + k * D - 64, 2048));
        return c;
    };
    auto dk = cents(-1), th = cents(1);
    bool fall = true, rise = true;
    std::printf("\nfx echo: centroid by repeat, tone -1:");
    for (size_t k = 0; k < dk.size(); k++) { std::printf(" %.0f", dk[k]); if (k) fall = fall && dk[k] < dk[k - 1]; }
    std::printf("; tone +1:");
    for (size_t k = 0; k < th.size(); k++) { std::printf(" %.0f", th[k]); if (k) rise = rise && th[k] > th[k - 1]; }
    check("echo: tone -1 -> each repeat darker (centroid falls)", fall);
    check("echo: tone +1 -> each repeat thinner (centroid rises)", rise);
    /* spread 1: the repeats alternate sides */
    impulse(ecfg({ { "echo.mix", 1 }, { "echo.fb", 0.6f }, { "echo.spread", 1 } }), 4, l, r);
    bool alt = true; std::printf("\nfx echo: spread 1, L-R by repeat (dB):");
    for (int k = 1; k <= 4; k++) {
        double el = win_energy(l, T0 + k * D, 8), er = win_energy(r, T0 + k * D, 8), d = 10 * std::log10(el / er);
        alt = alt && (k % 2 ? d >= 20 : d <= -20); std::printf(" %d: %+.0f", k, d);
    }
    check("echo: spread 1 -> repeats alternate sides (>= 20 dB)", alt);
    /* a time change: a second head cross-faded in over 100 ms - 150 ms after the change an impulse's first repeat is at
       the new time (8n -> 4n, 32n -> 32m = 4 s, 32m -> 32n), and no click over 1 s after it, both ways */
    struct TC { const char *from, *to; };
    static const TC TCS[] = { { "8n", "4n" }, { "32n", "32m" }, { "32m", "32n" } };
    auto tcfg = [](const char *d) { Cfg c = with({ { "echo.mix", 1 }, { "echo.fb", 0.5f }, { "echo.spread", 0 } });
                                    c.v[param_of("echo.time")] = (float)div_beats(d); std::strcpy(c.rate[ECHO], d); return c; };
    double tw = 0; bool at = true;
    std::printf("\nfx echo: time change, first repeat 150 ms after (want) / click 1 s (sine pink, both ways):");
    for (const TC &tc : TCS) {
        Engine e; e.init(SR); e.apply(tcfg(tc.from));
        const long C = 24064, I0 = C + 7168;               /* the change at 0.5 s; the impulse 150 ms later */
        const long want = (long)std::lround(std::fmin(4.0, div_beats(tc.to) * 60 / 90) * SR);
        size_t N = (size_t)(I0 + want + 4000); N = (N + 127) / 128 * 128;
        std::vector<float> iL(N, 0.0f), iR(N, 0.0f), oL, oR; iL[I0] = iR[I0] = 0.5f;
        oL.assign(N, 0.0f); oR.assign(N, 0.0f); double b = 0;
        for (size_t o = 0; o + 128 <= N; o += 128) {
            if ((long)o == C) e.apply(tcfg(tc.to));
            e.clock(90, b); b += 128 / SR * 1.5; e.process(&iL[o], &iR[o], &oL[o], &oR[o], 128);
        }
        long pk = I0 + 1;
        for (long i = I0 + 1; i < (long)N; i++) if (std::fabs(oL[i]) > std::fabs(oL[pk])) pk = i;
        at = at && std::labs(pk - I0 - want) <= 2;
        double x = 0, y = 0;
        for (int kind = 0; kind < 2; kind++) {
            x = std::fmax(x, click_of(tcfg(tc.from), tcfg(tc.to), kind, true, 1.0));   /* in + wet: at 4 s the wet is still silent before */
            y = std::fmax(y, click_of(tcfg(tc.to), tcfg(tc.from), kind, true, 1.0));
        }
        tw = std::fmax(tw, std::fmax(x, y));
        std::printf(" %s->%s %ld (%ld) %.2f/%.2f;", tc.from, tc.to, pk - I0, want, x, y);
    }
    check("echo: a time change settles within 150 ms (first repeat at the new time +-2)", at);
    check("echo: a time change passes the click metric over 1 s, both ways", tw <= 4);
    /* A-18: tone moves the spectrum, not the level */
    double lmin = 1e9, lmax = -1e9, cmin = 1e9, cmax = 0;
    std::printf("\nfx echo: tone level/centroid:");
    for (float t : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "echo.mix", 1 }, { "echo.tone", t } }));
        Sig s(1); play(e, s, 2 * (int)SR, nullptr);
        std::vector<float> y, in; play(e, s, 4 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
        double lv = rms_db(y) - rms_db(in), cen = centroid(power(y));
        lmin = std::fmin(lmin, lv); lmax = std::fmax(lmax, lv); cmin = std::fmin(cmin, cen); cmax = std::fmax(cmax, cen);
        std::printf(" %+.1f: %.2f/%.0f", t, lv, cen);
    }
    check("echo: level within 3 dB across tone", lmax - lmin <= 3);
    check("echo: tone moves the centroid >= 30 %", (cmax - cmin) / cmax >= 0.3);
    double lin = mix_linearity(with({ { "echo.mix", 1 }, { "echo.fb", 0.6f }, { "echo.shift", 30 }, { "echo.wobble", 0.5f }, { "echo.tone", -0.5f } }), "echo.mix");
    check("echo: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf("\nfx echo: tone level range %.2f dB, centroid %.0f..%.0f Hz; mix err %.2g\n", lmax - lmin, cmin, cmax, lin);
}

static void test_space() {
    using namespace mfx;
    std::printf("fx space: RT60 measured:");
    for (float d : { 1.0f, 4.0f, 20.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "space.mix", 1 }, { "space.decay", d } }));
        Sig s(1); play(e, s, 2 * (int)SR, nullptr);
        s.kind = 5; std::vector<float> l, r; play(e, s, (int)((1.5 * d + 1) * SR), &l, false, 90, nullptr, &r);
        double rt = rt60_of(l, r);
        check("space: RT60 within 20 % of decay (1, 4, 20 s)", rt > 0 && std::fabs(rt / d - 1) <= 0.2);
        std::printf(" %.0f s -> %.2f s;", d, rt);
    }
    /* the room fix's held D3: no pitch falls into a hole */
    /* the wet's power, both sides, over 8 s (the swings' periods are 2.4-7.7 s) */
    auto sine = [](double hz, float mod) {
        Engine e; e.init(SR); e.apply(with({ { "space.mix", 1 }, { "space.mod", mod } }));
        Sig s(4); s.hz = hz; s.amp = 0.25; play(e, s, 6 * (int)SR, nullptr);
        std::vector<float> y, r; play(e, s, 8 * (int)SR, &y, false, 90, nullptr, &r);
        y.insert(y.end(), r.begin(), r.end());
        return rms_db(y);
    };
    double range[2] = {};
    for (int k = 0; k < 2; k++) {
        const float mod = k ? 0.3f : PARAMS[R_SP_MOD].def; double hmin = 1e9, hmax = -1e9;   /* the default (1, +-100), then 0.3 (printed) */
        std::printf("\nfx space: held sine wet level (dB), mod %.1f (+-%.0f samples):", mod, 100 * mod);
        for (double hz : { 110.0, 146.83, 165.0, 196.0, 220.0, 262.0, 294.0, 440.0 }) {
            double v = sine(hz, mod); hmin = std::fmin(hmin, v); hmax = std::fmax(hmax, v); std::printf(" %.0f: %.2f", hz, v);
        }
        range[k] = hmax - hmin; std::printf("; range %.2f dB", range[k]);
    }
    check("space: default mod -> held sines 110-440 Hz (D3 147 among them) within 6 dB of each other", PARAMS[R_SP_MOD].def == 1 && range[0] <= 6);
    /* A-18: damp moves the spectrum, not the level; size holds the level */
    double lmin = 1e9, lmax = -1e9, cmin = 1e9, cmax = 0;
    std::printf("\nfx space: damp level/centroid:");
    for (float dp : { 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f }) {
        Engine e; e.init(SR); e.apply(with({ { "space.mix", 1 }, { "space.damp", dp } }));
        Sig s(1); play(e, s, 3 * (int)SR, nullptr);
        std::vector<float> y, in; play(e, s, 4 * (int)SR, &y, false, 90, nullptr, nullptr, &in);
        double lv = rms_db(y) - rms_db(in), cen = centroid(power(y));
        lmin = std::fmin(lmin, lv); lmax = std::fmax(lmax, lv); cmin = std::fmin(cmin, cen); cmax = std::fmax(cmax, cen);
        std::printf(" %.0f: %.2f/%.0f", dp, lv, cen);
    }
    check("space: level within 3 dB across damp", lmax - lmin <= 3);
    check("space: damp moves the centroid >= 30 %", (cmax - cmin) / cmax >= 0.3);
    double smin = 1e9, smax = -1e9;
    std::printf("\nfx space: size level:");
    for (float sz : { 0.2f, 0.4f, 0.7f, 1.0f }) {
        double lv = level_rel(with({ { "space.mix", 1 }, { "space.size", sz } }), 1);
        smin = std::fmin(smin, lv); smax = std::fmax(smax, lv); std::printf(" %.1f: %.2f", sz, lv);
    }
    check("space: level within 3 dB across size", smax - smin <= 3);
    double lin = mix_linearity(with({ { "space.mix", 1 }, { "space.mod", 1 } }), "space.mix");
    check("space: wet at mix 0.37 == 0.37 x wet at mix 1", lin < 1e-5);
    std::printf("\nfx space: damp level range %.2f dB, centroid %.0f..%.0f Hz; size level range %.2f dB; mix err %.2g\n",
                lmax - lmin, cmin, cmax, smax - smin, lin);
}

/* Review Focus 3: everything on, the feedback at its edges, white noise at 0 dBFS for 120 s; then NaN. */
static void scan(const std::vector<float> &l, const std::vector<float> &r, bool &fin, float &pk) {
    for (size_t i = 0; i < l.size(); i++) {
        fin = fin && std::isfinite(l[i]) && std::isfinite(r[i]);
        pk = std::fmax(pk, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
    }
}
static void test_stable() {
    using namespace mfx;
    {   /* Task 2's carried case: Shift alone, fb 0.9 at hz 0 (its 30 ms comb rings +20 dB) */
        Engine e; e.init(SR); e.apply(with({ { "shift.mix", 1 }, { "shift.fb", 0.9f } }));
        Sig s(2); s.g = 2; bool fin = true; float pk = 0; std::vector<float> l, r;
        for (int sec = 0; sec < 20; sec++) { l.clear(); r.clear(); play(e, s, (int)SR, &l, false, 90, nullptr, &r); scan(l, r, fin, pk); }
        check("stable: shift fb 0.9 at hz 0, white 0 dBFS 20 s: finite, |out| <= 8", fin && pk <= 8);
        std::printf("fx stable: shift alone fb 0.9 hz 0, 20 s white 0 dBFS: peak %.2f\n", pk);
    }
    Cfg c = with({ { "shift.mix", 1 }, { "swarm.mix", 1 }, { "phase.mix", 1 }, { "comb.mix", 1 }, { "vowel.mix", 1 }, { "cloud.mix", 1 },
                   { "tape.mix", 1 }, { "echo.mix", 1 }, { "space.mix", 1 }, { "echo.fb", 0.98f }, { "shift.fb", 0.9f }, { "space.decay", 60 } });
    Engine e; e.init(SR); e.apply(c);
    Sig s(2); s.g = 2; bool fin = true; float pk = 0; double beats = 0; std::vector<float> l, r;
    for (int sec = 0; sec < 120; sec++) { l.clear(); r.clear(); play(e, s, (int)SR, &l, false, 90, &beats, &r); scan(l, r, fin, pk); }
    check("stable: all nine at mix 1, echo fb 0.98, shift fb 0.9, decay 60, white 0 dBFS 120 s: every sample finite", fin);
    const float raw = pk <= 4 ? pk : 4 + 4 * std::atanh((pk - 4) / 4);   /* guard() undone: the peak the modules made */
    check("stable: ... the modules' own peak (before the guard) <= 8", raw <= 8);
    /* a NaN in the input: the copy f runs the same input without it */
    Engine f = e; Sig t = s; double bf = beats;
    float iL[128], iR[128], jL[128], jR[128], wL[128], wR[128], vL[128], vR[128];
    for (int i = 0; i < 128; i++) { s.next(iL[i], iR[i]); t.next(jL[i], jR[i]); }
    iL[5] = NAN; iR[70] = NAN; iL[71] = INFINITY;
    e.clock(90, beats); f.clock(90, bf); beats += 128 / SR * 1.5; bf = beats;
    e.process(iL, iR, wL, wR, 128); f.process(jL, jR, vL, vR, 128);
    bool fin1 = true; for (int i = 0; i < 128; i++) fin1 = fin1 && std::isfinite(wL[i]) && std::isfinite(wR[i]);
    std::vector<float> a, ar, b, br; fin = true;
    play(e, s, 3 * (int)SR, &a, false, 90, &beats, &ar); play(f, t, 3 * (int)SR, &b, false, 90, &bf, &br); scan(a, ar, fin, pk);
    a.clear(); b.clear(); play(e, s, (int)SR, &a, false, 90, &beats); play(f, t, (int)SR, &b, false, 90, &bf);
    const double rec = rms_db(a) - rms_db(b);
    check("stable: NaN in the input -> that block finite", fin1);
    check("stable: ... every block after finite", fin);
    check("stable: ... 3 s later within 1 dB of the run without it", std::fabs(rec) <= 1);
    /* a NaN from inside a module resets that module only: the output finite within one block, the mix still sounding */
    struct Inj { const char *name; std::vector<float> *buf; };
    Inj inj[] = { { "echo", &e.ec_dl[0].buf }, { "space", &e.sp_dl[3].buf }, { "shift", &e.sh_dl[1].buf } };
    bool ok = true;
    std::printf("fx stable: 120 s peak %.2f (%.2f before the guard); NaN in the input: 3-4 s after vs without %+.3f dB; inside:", pk, raw, rec);
    for (auto &q : inj) {
        std::fill(q.buf->begin(), q.buf->end(), NAN);
        bool fq = true; float pq = 0; a.clear(); ar.clear();
        play(e, s, 128, &a, false, 90, &beats, &ar); scan(a, ar, fq, pq);
        a.clear(); ar.clear(); float p2 = 0; play(e, s, (int)(0.5 * SR), &a, false, 90, &beats, &ar); scan(a, ar, fq, p2);
        ok = ok && fq && p2 > 0.01f;
        std::printf(" %s %s (then peak %.2f)", q.name, fq ? "finite" : "NOT FINITE", p2);
    }
    check("stable: a NaN inside echo/space/shift -> finite within one block, the mix still sounds", ok);
    std::printf("\n");
}

/* The frame with two real modules at partial mix: wet = chain - (1 - k_echo)(1 - k_space) in, the chain rebuilt from
   each module run alone at mix 1 (a module's processing does not depend on its own mix; the ramps replayed). */
static void test_frame() {
    using namespace mfx;
    const float ke = 0.25f, ks = 0.5f;
    const size_t N = 2 * 48000;
    std::vector<float> iL(N), iR(N), wL, wR, eL, eR, sL, sR, xL(N), xR(N);
    Sig s(1); for (size_t i = 0; i < N; i++) s.next(iL[i], iR[i]);
    Engine a, e, sp; a.init(SR); e.init(SR); sp.init(SR);
    a.apply(with({ { "echo.mix", ke }, { "space.mix", ks } })); e.apply(with({ { "echo.mix", 1 } })); sp.apply(with({ { "space.mix", 1 } }));
    feed(a, iL, iR, wL, wR); feed(e, iL, iR, eL, eR);
    const float step = (float)(1 / (0.03 * SR));
    std::vector<float> k1(N), k2(N); float c1 = 0, c2 = 0;
    for (size_t i = 0; i < N; i++) {
        k1[i] = c1 = std::fmin(ke, c1 + step); k2[i] = c2 = std::fmin(ks, c2 + step);
        xL[i] = iL[i] + k1[i] * (eL[i] - iL[i]); xR[i] = iR[i] + k1[i] * (eR[i] - iR[i]);   /* echo alone at mix 1: its own output (0 before its first repeat) */
    }
    feed(sp, xL, xR, sL, sR);
    double err = 0;
    for (size_t i = (size_t)(0.1 * SR); i < N; i++) {
        const float keep = (1 - k1[i]) * (1 - k2[i]);
        const float yl = xL[i] + k2[i] * (sL[i] - xL[i]) - keep * iL[i], yr = xR[i] + k2[i] * (sR[i] - xR[i]) - keep * iR[i];
        err = std::fmax(err, std::fmax(std::fabs(wL[i] - yl), std::fabs(wR[i] - yr)));
    }
    check("frame: echo 0.25 + space 0.5 -> wet == chain - (1 - 0.25)(1 - 0.5) in", err < 1e-5);
    std::printf("fx frame: two real modules (echo 0.25, space 0.5): max error %.2g\n", err);
}

/* The input stops: idle() within decay + 6 s, then exact 0 out. */
static void test_tail() {
    using namespace mfx;
    struct Case { const char *name; Cfg c; double decay; };
    Case cases[] = {
        { "echo + space", with({ { "echo.mix", 1 }, { "space.mix", 1 } }), 4 },
        { "all nine", with({ { "shift.mix", 1 }, { "swarm.mix", 1 }, { "phase.mix", 1 }, { "comb.mix", 1 }, { "vowel.mix", 1 }, { "cloud.mix", 1 },
                             { "tape.mix", 1 }, { "echo.mix", 1 }, { "space.mix", 1 } }), 4 },
        { "space decay 20", with({ { "space.mix", 1 }, { "space.decay", 20 } }), 20 },
    };
    std::printf("fx tail: idle after the input stops:");
    for (auto &k : cases) {
        Engine e; e.init(SR); e.apply(k.c);
        Sig s(1); play(e, s, 3 * (int)SR, nullptr);
        s.kind = 5; double t = 0;
        while (!e.idle() && t < k.decay + 20) { play(e, s, 128, nullptr); t += 128 / SR; }
        bool zero = true;
        std::vector<float> l, r; play(e, s, 3 * (int)SR, &l, false, 90, nullptr, &r);
        for (size_t i = 0; i < l.size(); i++) zero = zero && l[i] == 0 && r[i] == 0 && !std::signbit(l[i]) && !std::signbit(r[i]);
        check("tail: idle within decay + 6 s", e.idle() && t <= k.decay + 6);
        check("tail: then exactly 0 out", zero && e.idle());
        std::printf(" %s %.2f s (decay %.0f)%s;", k.name, t, k.decay, zero ? "" : " NOT ZERO");
    }
    /* after the tail went idle, a new onset is neither cleared nor faded in: its first 5 ms (Shift, 1 kHz) as loud as
       after a short silence that never went idle */
    auto onset = [](bool idle_first, bool &was_idle) {
        Engine e; e.init(SR); e.apply(with({ { "shift.mix", 1 }, { "shift.hz", 100 } }));
        Sig s(1); play(e, s, (int)SR, nullptr);
        s.kind = 5; play(e, s, (int)((idle_first ? 1.5 : 0.1) * SR), nullptr);
        was_idle = e.idle();
        s.kind = 3; std::vector<float> y; play(e, s, 1280, &y);
        double en = 0; for (int i = 0; i < 240; i++) en += (double)y[i] * y[i];
        return 10 * std::log10(en + 1e-30);
    };
    bool i1, i0; const double a = onset(true, i1), b = onset(false, i0);
    check("tail: idle, then a new onset's first 5 ms within 1 dB of one after a short silence (no clear, no fade-in)", i1 && !i0 && std::fabs(a - b) <= 1);
    std::printf(" onset after idle vs after 0.1 s: %+.2f dB\n", a - b);
}

/* ---- Task 6: the modulators and the matrix ---- */

/* One block of silence through e at (bpm, b); its live read. */
static mfx::Live step(mfx::Engine &e, double bpm, double b, int kind = 5) {
    static Sig s(5); s.kind = kind;
    float iL[128], iR[128], wL[128], wR[128];
    for (int i = 0; i < 128; i++) s.next(iL[i], iR[i]);
    e.clock(bpm, b); e.process(iL, iR, wL, wR, 128);
    mfx::Live l; e.live(l); return l;
}

static void test_lfo() {
    using namespace mfx;
    Cfg c; cfg_defaults(c); std::strcpy(c.lfo[0].rate, "4n");
    Engine e; e.init(SR); e.apply(c); e.clock(120, 0);
    /* sample-exact: 4n at 120 bpm crosses zero upward every 0.5 s (24000 samples) */
    long last = 0; int cross = 0; long worst = 0; float prev = e.lfo_at(0, 0);
    for (long n = 1; n <= 100L * 24000 + 10; n++) {
        const float v = e.lfo_at(0, n * 2.0 / SR);
        if (prev < 0 && v >= 0) { if (cross++) worst = std::max(worst, std::labs(n - last - 24000)); last = n; }
        prev = v;
    }
    check("lfo: 100 cycles, upward crossings every 24000 samples +-1", cross >= 100 && worst <= 1);
    /* through the engine: the live phase per block, 50 s at 120 bpm; crossings from the wraps */
    double b = 0, t_first = -1, t_last = 0, pph = 0; int wraps = 0;
    for (long blk = 0; blk < (long)(50 * SR / 128); blk++) {
        Live l = step(e, 120, b);
        const double ph = l.v[0];
        if (blk && ph < pph) {
            const double t = ((blk - 1) + (1 - pph) / (ph + 1 - pph)) * 128 / SR;
            if (t_first < 0) t_first = t; t_last = t; wraps++;
        }
        pph = ph; b += 128 / SR * 2;
    }
    const double err = 100 * std::fabs((t_last - t_first) / (wraps - 1) / 0.5 - 1);
    check("lfo: tempo error 0.00 %", wraps >= 99 && err < 0.005);
    /* a tempo change mid-run: no phase jump beyond one block's worth (at the faster tempo) */
    Engine f; f.init(SR); f.apply(c); b = 0; double jump = 0; pph = -1;
    for (int blk = 0; blk < 750; blk++) {
        const double bpm = blk < 375 ? 120 : 90;
        Live l = step(f, bpm, b);
        if (pph >= 0) { double d = l.v[0] - pph; d -= std::floor(d + 0.5); jump = std::fmax(jump, std::fabs(d)); }
        pph = l.v[0]; b += 128 / SR * bpm / 60;
    }
    check("lfo: tempo change 120 -> 90, no jump > one block", jump <= 128 / SR * 2 + 1e-6);
    /* the shapes at phase 0, 0.25, 0.5, 0.75 (the lfo's phase offset, transport frozen at 0) */
    static const float EXPECT[5][4] = { { 0, 1, 0, -1 }, { 0, 1, 0, -1 }, { -1, -0.5f, 0, 0.5f }, { 1, 0.5f, 0, -0.5f }, { 0, 1, 0, -1 } };
    bool shapes = true; float stp[2][4];
    for (int sh = 0; sh < NSHAPE; sh++)
        for (int q = 0; q < 4; q++) {
            Cfg k = c; k.lfo[0].shape = (uint8_t)sh; k.lfo[0].phase = q * 0.25f;
            for (int run = 0; run < 2; run++) {
                Engine g; g.init(SR); g.apply(k);
                Live l = step(g, 0, 0);
                const float v = l.v[4];
                if (sh < 5) shapes = shapes && near(v, EXPECT[sh][q], 1e-5) && near(l.v[0], q * 0.25, 1e-6);
                if (sh == 5) stp[run][q] = v;
            }
        }
    check("lfo: sine tri up down square at 0 .25 .5 .75", shapes);
    check("lfo: steps holds for the period, the same on a second run", stp[0][0] == stp[0][1] && stp[0][1] == stp[0][2] && stp[0][2] == stp[0][3]
          && !std::memcmp(stp[0], stp[1], sizeof stp[0]) && std::fabs(stp[0][0]) <= 1);
    /* steps: a new value next period; drift: continuous and within -1..1 */
    Cfg k = c; k.lfo[0].shape = 5; Engine g; g.init(SR); g.apply(k); g.clock(120, 0);
    bool held = true, moved = false; const float s0 = g.lfo_at(0, 0);
    for (int i = 0; i < 1000; i++) held = held && g.lfo_at(0, i / 1000.0) == s0;
    for (int p = 1; p < 8; p++) moved = moved || g.lfo_at(0, p) != s0;
    check("lfo: steps holds 1000 points of a period and changes in the next ones", held && moved);
    k.lfo[0].shape = 6; g.apply(k); float dmax = 0, lo = 1, hi = -1, pv = g.lfo_at(0, 0);
    for (int i = 1; i <= 10000; i++) { const float v = g.lfo_at(0, i / 1000.0); dmax = std::fmax(dmax, std::fabs(v - pv)); lo = std::fmin(lo, v); hi = std::fmax(hi, v); pv = v; }
    check("lfo: drift continuous (step <= 0.01 at 1/1000 period), within -1..1, moves", dmax <= 0.01f && lo >= -1 && hi <= 1 && hi - lo > 0.2f);
    std::printf("fx lfo: 4n at 120 bpm crossings every 24000 +-%ld samples (%d), engine tempo error %.4f %%, tempo change jump %.5f (block %.5f); "
                "steps %.3f; drift %.3f..%.3f, max step %.4f\n", worst, cross, err, jump, 128 / SR * 2, stp[0][0], lo, hi, dmax);
}

static void test_env() {
    using namespace mfx;
    struct Case { float a, r; } cases[] = { { 10, 300 }, { 100, 1000 } };
    std::printf("fx env:");
    for (auto &k : cases) {
        Cfg c; cfg_defaults(c); c.env_a = k.a; c.env_r = k.r;
        Engine e; e.init(SR); e.apply(c);
        std::vector<float> v; double b = 0;
        const int on = (int)(0.5 * SR / 128), off = on + (int)(1.5 * SR / 128), end = off + (int)(4 * SR / 128);
        for (int blk = 0; blk < end; blk++) { Live l = step(e, 90, b, blk >= on && blk < off ? 3 : 5); v.push_back(l.v[8]); b += 128 / SR * 1.5; }
        const float plat = v[off - 1];
        int rise = -1, fall = -1;
        for (int i = on; i < off && rise < 0; i++) if (v[i] >= 0.9f * plat) rise = i - on + 1;
        for (int i = off; i < end && fall < 0; i++) if (v[i] <= 0.1f * plat) fall = i - off + 1;
        const double tr = rise * 128 / SR * 1000, tf = fall * 128 / SR * 1000;
        check("env: rises to 90 % within attack x 3", rise > 0 && tr <= 3 * k.a + 128 / SR * 1000);
        check("env: falls to 10 % within release x 3", fall > 0 && tf <= 3 * k.r + 128 / SR * 1000);
        check("env: plateau = the burst's peak x sens (0.5)", near(plat, 0.5, 0.02));
        check("env: silence before the burst reads 0", v[on - 1] == 0);
        std::printf(" attack %.0f -> 90 %% in %.1f ms, release %.0f -> 10 %% in %.1f ms, plateau %.3f;", k.a, tr, k.r, tf, plat);
    }
    std::printf("\n");
}

static void test_matrix() {
    using namespace mfx;
    Cfg c = with({ { "echo.mix", 1 }, { "phase.hz", 100 } });
    c.lfo[0].phase = 0.25f;                          /* sine at its top: lfo1 = 1 while the transport is frozen */
    c.mods[0] = { S_LFO1, (int16_t)R_EC_FB, 0.2f }; c.mods[1] = { S_LFO1, (int16_t)R_EC_FB, 0.1f };
    c.mods[2] = { S_LFO1, (int16_t)R_PH_HZ, -1 };   c.mods[3] = { S_LFO1, (int16_t)R_SH_MODE, 1 };
    c.mods[4] = { S_CHORD, (int16_t)R_VW_POS, 1 };
    Engine e; e.init(SR); e.apply(c);
    Live l = step(e, 0, 0);
    const float fb = 0.45f + 0.3f * 0.98f;
    check("matrix: two slots on echo.fb sum (base + 0.3 of the range)", near(e.cfg.v[R_EC_FB], fb, 1e-4) && near(l.v[24], fb / 0.98f, 1e-4) && l.v[24] == l.v[25]);
    check("matrix: the stored base never changes", e.base.v[R_EC_FB] == 0.45f);
    check("matrix: amt -1 at the minimum stays clamped", e.cfg.v[R_PH_HZ] == 100 && l.v[26] == 0);
    check("matrix: a slot on a switch row is ignored", e.cfg.v[R_SH_MODE] == 0 && l.v[27] == 0);
    check("matrix: live 24-31 = each slot's destination now (0-1)", near(l.v[28], e.cfg.v[R_VW_POS] / 4, 1e-5) && l.v[29] == 0 && l.v[31] == 0);
    /* chord: changes only when the serial changes, gliding at most 200 ms across the range */
    const float c0 = l.v[12], m1[5] = { 50, 53, 57, 60, -1 }, m2[5] = { 52, 55, 59, -1, -1 };
    e.chord(m1, 50, 0); for (int i = 0; i < 200; i++) l = step(e, 0, 0);
    const bool same = l.v[12] == c0;
    e.chord(m2, 52, 1); Live l1 = step(e, 0, 0);
    const bool glided = std::fabs(l1.v[12] - c0) <= 128 / (0.2 * SR) + 1e-6;
    for (int i = 0; i < 100; i++) l1 = step(e, 0, 0);
    check("matrix: chord holds under new notes, same serial", same);
    check("matrix: chord moves on a new serial, gliding", l1.v[12] != c0 && glided && near(e.cfg.v[R_VW_POS], 4 * l1.v[12], 1e-4));
    Engine e2; e2.init(SR); e2.apply(c); e2.chord(m2, 52, 1); Live l2 = step(e2, 0, 0); for (int i = 0; i < 100; i++) l2 = step(e2, 0, 0);
    check("matrix: chord is seeded (same serial, same value)", l2.v[12] == l1.v[12]);
    /* a mix row modulated from 0: the engine is not idle */
    Cfg m = with({}); m.lfo[0].phase = 0.25f; m.mods[0] = { S_LFO1, (int16_t)param_of("echo.mix"), 1 };
    Engine e3; e3.init(SR); e3.apply(m);
    check("matrix: a slot on a mix row keeps the engine awake", !e3.idle());
    float pk = 0; Sig s(1); std::vector<float> y; play(e3, s, 48000, &y, false, 0);   /* the echo repeats 0.625 s later (72 bpm) */
    for (float v : y) pk = std::fmax(pk, std::fabs(v));
    check("matrix: lfo -> echo.mix from 0 sounds", pk > 0.01f);
    std::printf("fx matrix: echo.fb 0.45 + 0.2 + 0.1 of range -> %.4f; phase.hz at min - 1 -> %.0f; switch slot ignored; chord %.3f -> %.3f on serial 1\n",
                e.cfg.v[R_EC_FB], e.cfg.v[R_PH_HZ], c0, l1.v[12]);
}

/* Review Focus 4 (no walk sources): an LFO-free preset, env -> echo.fb, steady pink for 60 s: the level holds and
   nothing drifts to an extreme. Then the click metric on a slot's amt 0 -> 1 and back. */
static void test_hold() {
    using namespace mfx;
    Cfg c = with({ { "echo.mix", 1 } }); c.mods[0] = { S_ENV, (int16_t)R_EC_FB, 0.3f };
    Engine e; e.init(SR); e.apply(c);
    Sig s(1); double b = 0; std::vector<double> db; std::vector<float> fbs;
    for (int sec = 0; sec < 60; sec++) {
        std::vector<float> y; play(e, s, (int)SR, &y, false, 90, &b);
        db.push_back(rms_db(y)); fbs.push_back(e.cfg.v[R_EC_FB]);
    }
    double mean = 0; for (int i = 10; i < 60; i++) mean += db[i] / 50;
    double dev = 0, h1 = 0, h2 = 0; float flo = 1, fhi = 0;
    for (int i = 10; i < 60; i++) {
        dev = std::fmax(dev, std::fabs(db[i] - mean)); flo = std::fmin(flo, fbs[i]); fhi = std::fmax(fhi, fbs[i]);
        (i < 35 ? h1 : h2) += fbs[i] / 25.0;
    }
    check("hold: 60 s steady pink, env -> echo.fb: level within +-1 dB", dev <= 1);
    check("hold: echo.fb above its base, below its max, no trend (halves' means within 0.01)", flo > 0.45f && fhi < 0.97f && std::fabs(h1 - h2) <= 0.01);
    std::printf("fx hold: env -> echo.fb 0.3, 60 s pink: level %.2f dB max deviation, echo.fb %.3f..%.3f (block peaks), halves %.4f / %.4f\n", dev, flo, fhi, h1, h2);
    /* onsets */
    struct D { const char *dst, *mixk; float base; };
    static const D DS[] = { { "phase.hz", "phase.mix", -1 }, { "echo.fb", "echo.mix", -1 }, { "vowel.pos", "vowel.mix", -1 }, { "echo.mix", "echo.mix", 0.3f } };
    double worst = 0;
    for (auto &d : DS)
        for (int kind = 0; kind < 2; kind++) {
            Cfg a = with({ { d.mixk, d.base < 0 ? 1 : d.base } }); std::strcpy(a.lfo[0].rate, "4m"); a.lfo[0].phase = 0.25f;
            a.mods[0] = { S_LFO1, (int16_t)param_of(d.dst), 0 };
            Cfg z = a; z.mods[0].amt = 1;
            const bool pi = d.base >= 0;
            const double r = std::fmax(click_of(a, z, kind, pi), click_of(z, a, kind, pi));
            check("modclicks: a slot's amt 0 -> 1 and back <= 4", r <= 4);
            worst = std::fmax(worst, r);
            std::printf("  modclick %-9s %s %.2f\n", d.dst, kind ? "pink" : "sine", r);
        }
    std::printf("fx modclicks: lfo1 amt 0 <-> 1 on phase.hz, echo.fb, vowel.pos, echo.mix: worst %.2f\n", worst);
}

int main() {
    test_lfo();
    test_env();
    test_matrix();
    test_hold();
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
    test_cloud();
    test_levels();
    test_echo();
    test_space();
    test_frame();
    test_stable();
    test_tail();
    test_clicks();
    if (failed) { std::printf("fx FAILED: %d checks\n", failed); return 1; }
    std::printf("fx ok\n");
    return 0;
}
