/* Modulation FX (docs/superpowers/plans/2026-11-17-modulation-fx.md, Part 1): the engine's own checks.
     sh tools/mac-fx.sh      (builds and runs this on the Mac; prints "fx ok" last) */
#include "../modfx.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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
    check("gate: a mix on -> not idle", !e.idle());
    float pk = 0; run(e, p, 48000, &pk, nullptr);
    e.live(l);
    check("gate: mix 1 -> wet sounds", pk > 0.05f && l.v[23] > 0 && l.v[13 + ECHO] > 0);
    check("gate: other modules skipped", l.v[13 + SHIFT] == 0 && l.v[13 + SPACE] == 0);
    c.v[param_of("echo.mix")] = 0; e.apply(c);
    run(e, p, 48000 / 20, nullptr, nullptr);       /* 50 ms: past the 30 ms fade */
    zero = true; run(e, p, 48000, nullptr, &zero);
    check("gate: all mixes back to 0 -> wet exactly 0", zero);
    check("gate: idle again", e.idle());
    e.reset();
    check("reset: config kept, still dry", e.idle());
    std::printf("fx silent: 2 s pink noise through the defaults -> wet 0.0f every sample, idle; a mix on sounds, off gives 0 again\n");
}

int main() {
    test_table();
    test_div();
    test_parse();
    test_silent();
    if (failed) { std::printf("fx FAILED: %d checks\n", failed); return 1; }
    std::printf("fx ok\n");
    return 0;
}
