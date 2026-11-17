/* Modulation FX, Part 1 Task 7: the engine's cost (wasm -O1, Node, 128-frame blocks, 48 kHz).
     sh tools/mac-tests.sh && node build/stretch-tests/fx_bench.js          (the cost; exits 1 past a budget)
     node build/stretch-tests/fx_bench.js check                              (fx_test's checks, in this build)
   Budgets (plan, Global Constraints): typical <= 3.0 % of realtime, all nine <= 8.0 %, p99.9 block <= 0.35 ms,
   idle <= 0.05 %, a module's first block after its mix leaves 0 <= 1.5 ms. Run it alone, the PC idle. */
#include <cstdlib>
#include <new>
static size_t g_bytes = 0;                           /* every byte operator new hands out: an Engine's memory is init's share */
void *operator new(size_t n) { g_bytes += n; if (void *p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

#define main fx_test_main
#include "fx_test.cpp"
#undef main

#include <chrono>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static double now_ms() { return emscripten_get_now(); }
#else
static double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
#endif

using namespace mfx;
static const int N = 128, BLOCKS = 20 * 48000 / N, SKIP = 375;   /* 20 s; the first second is warm-up (as composer_test's cost) */
static const float CHORD[5] = { 50, 53, 57, 60, 64 };
static int over = 0;
static int mix_row(int m) { for (int i = 0; i < NPARAM; i++) if (PARAMS[i].mod == m) return i; return 0; }   /* a module's first row */

static Engine *fresh(const Cfg &c) {
    Engine *e = new Engine; e->init(SR); e->clock(120, 0); e->chord(CHORD, 50, 0); e->apply(c);
    return e;
}

/* 20 s of pink (or silence) through c: mean % of realtime (a second engine, the whole loop timed once: a timer call
   from wasm costs ~0.4 us, as much as an idle block), p50/p99/p99.9/max block (each block timed), after the first
   second. The input cycles through 32 blocks of pink (32 KB, in cache, as a host's fresh block is) */
static void cost(const char *name, const Cfg &c, bool silent, double pct_limit, bool gate = true, int sub = N) {   /* sub: frames a call (the piece calls 32) */
    static const int RING = 32;
    static std::vector<float> inL, inR;
    if (inL.empty()) { Pink p; inL.resize(RING * N); inR.resize(RING * N); for (int i = 0; i < RING * N; i++) { inL[i] = p.next(); inR[i] = p.next(); } }
    static const std::vector<float> zero(RING * N, 0.0f);
    const float *L = silent ? zero.data() : inL.data(), *R = silent ? zero.data() : inR.data();
    float wL[N], wR[N];
    Engine *e = fresh(c);
    std::vector<double> ms;
    for (int k = 0; k < BLOCKS; k++) {
        const double t0 = now_ms();
        for (int o = 0; o < N; o += sub) e->process(L + k % RING * N + o, R + k % RING * N + o, wL + o, wR + o, sub);
        const double t = now_ms() - t0;
        if (k >= SKIP) ms.push_back(t);
    }
    delete e;
    e = fresh(c);
    for (int k = 0; k < SKIP; k++) for (int o = 0; o < N; o += sub) e->process(L + k % RING * N + o, R + k % RING * N + o, wL + o, wR + o, sub);
    const double t0 = now_ms();
    for (int k = SKIP; k < BLOCKS; k++) for (int o = 0; o < N; o += sub) e->process(L + k % RING * N + o, R + k % RING * N + o, wL + o, wR + o, sub);
    const double pct = 100 * (now_ms() - t0) / ((BLOCKS - SKIP) * N * 1000.0 / SR);
    delete e;
    std::sort(ms.begin(), ms.end());
    const double p50 = ms[ms.size() / 2], p99 = ms[(size_t)(ms.size() * 0.99)], p999 = ms[(size_t)(ms.size() * 0.999)], mx = ms.back();
    const bool ok = pct <= pct_limit && p999 <= 0.35;
    if (gate) over += !ok;
    std::printf("RESULT fxcost case=%s pct=%.3f p50=%.4f p99=%.4f p999=%.4f max=%.4f limit_pct=%.2f %s\n", name, pct, p50, p99, p999, mx, pct_limit, !gate ? "info" : ok ? "ok" : "OVER");
}

/* The block in which module m's mix leaves 0 (its lines cleared): the worst of 3, each on an engine that ran 1 s dry */
static void first_block(int m, const Cfg &all) {
    double worst = 0;
    for (int r = 0; r < 3; r++) {
        Cfg c = all;
        for (int k = 0; k < NMOD; k++) c.v[mix_row(k)] = 0;
        for (auto &s : c.mods) s = Slot{};
        Engine *e = fresh(c);
        Pink p; float iL[N], iR[N], wL[N], wR[N];
        for (int k = 0; k < 375; k++) { for (int i = 0; i < N; i++) { iL[i] = p.next(); iR[i] = p.next(); } e->process(iL, iR, wL, wR, N); }
        c.v[mix_row(m)] = 1;
        e->apply(c);
        for (int i = 0; i < N; i++) { iL[i] = p.next(); iR[i] = p.next(); }
        const double t0 = now_ms();
        e->process(iL, iR, wL, wR, N);
        worst = std::fmax(worst, now_ms() - t0);
        delete e;
    }
    const bool ok = worst <= 1.5;
    over += !ok;
    std::printf("RESULT fxfirst mod=%s ms=%.4f %s\n", MOD_NAMES[m], worst, ok ? "ok" : "OVER");
}

int main(int argc, char **argv) {
    if (argc > 1 && !std::strcmp(argv[1], "check")) return fx_test_main();

    const size_t b0 = g_bytes; Engine *e = new Engine; e->init(SR);
    std::printf("RESULT fxmem bytes=%zu (sizeof Engine %zu + init %zu)\n", g_bytes - b0, sizeof(Engine), g_bytes - b0 - sizeof(Engine));
    delete e;

    Cfg idle; cfg_defaults(idle);
    /* typical (the plan): phase + comb + cloud + echo + space at mix 1, two slots */
    Cfg typ = with({ { "phase.mix", 1 }, { "comb.mix", 1 }, { "cloud.mix", 1 }, { "echo.mix", 1 }, { "space.mix", 1 } });
    typ.mods[0] = { S_LFO1, (int16_t)R_CB_HZ, 0.2f }; typ.mods[1] = { S_ENV, (int16_t)R_CL_DENS, 0.3f };
    /* typical + a slot on shift.mix at mix 0: Shift runs silent every block (mix_mod) */
    Cfg tmm = typ; tmm.mods[2] = { S_LFO2, (int16_t)param_of("shift.mix"), 0.5f };
    /* all nine at mix 1, eight slots, the costly settings: Shift and Echo's shifter on (Hilbert), 12 stages, Swarm's
       zero tap, eight grains, drive, the wobble, a slot on space.size (its 100 ms cross-fades) */
    Cfg all = with({ { "shift.mix", 1 }, { "swarm.mix", 1 }, { "phase.mix", 1 }, { "comb.mix", 1 }, { "vowel.mix", 1 }, { "cloud.mix", 1 },
                     { "tape.mix", 1 }, { "echo.mix", 1 }, { "space.mix", 1 }, { "shift.hz", 30 }, { "shift.fb", 0.3f }, { "swarm.zero", 1 },
                     { "phase.stages", 12 }, { "cloud.dens", 40 }, { "cloud.size", 500 }, { "cloud.spray", 0.5f }, { "tape.drive", 0.5f },
                     { "echo.shift", 5 }, { "echo.wobble", 0.5f }, { "echo.tone", 0.3f } });
    all.mods[0] = { S_LFO1, (int16_t)R_SH_HZ, 0.1f };  all.mods[1] = { S_LFO2, (int16_t)R_PH_HZ, 0.3f };
    all.mods[2] = { S_LFO3, (int16_t)R_CB_HZ, 0.2f };  all.mods[3] = { S_LFO4, (int16_t)R_VW_POS, 0.5f };
    all.mods[4] = { S_ENV, (int16_t)R_CL_PITCH, 0.2f }; all.mods[5] = { S_CHORD, (int16_t)R_TP_TONE, 0.3f };
    all.mods[6] = { S_LFO1, (int16_t)R_EC_TONE, 0.3f }; all.mods[7] = { S_LFO1, (int16_t)R_SP_SIZE, 0.3f };

    cost("idle", idle, true, 0.05);                   /* ret > 0, nothing on, silent input */
    cost("defaults", idle, false, 0.05);              /* nothing on, pink in */
    cost("typical", typ, false, 3.0);
    cost("typical+modmix", tmm, false, 3.0);
    cost("all", all, false, 8.0);
    cost("typical@32", typ, false, 3.0, false, 32);   /* as the piece calls it: 32 frames at a time (info) */
    cost("all@32", all, false, 8.0, false, 32);
    { Cfg c = all; for (auto &s : c.mods) s = Slot{}; cost("all-nomods@32", c, false, 8.0, false, 32); }   /* designs memoised: what the slots cost */
    /* each module of a case alone, at that case's settings and slots (not a budget: where the cost is) */
    for (const Cfg *b : { &typ, &all })
        for (int m = 0; m < NMOD; m++) {
            Cfg c = *b;
            if (c.v[mix_row(m)] == 0) continue;
            for (int k = 0; k < NMOD; k++) if (k != m) c.v[mix_row(k)] = 0;
            for (auto &s : c.mods) if (s.dst >= 0 && PARAMS[s.dst].mod != m) s = Slot{};
            char nm[32]; std::snprintf(nm, sizeof nm, "%s-%s", b == &typ ? "typical" : "all", MOD_NAMES[m]);
            cost(nm, c, false, 8.0, false);
        }
    for (int m = 0; m < NMOD; m++) first_block(m, all);
    std::printf(over ? "fx bench OVER: %d budgets\n" : "fx bench ok\n", over);
    return over ? 1 : 0;
}
