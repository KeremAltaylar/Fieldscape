/* The piece: the web walk's generative sound, as one device (index.html, bedStart onward).

   What it holds, as the web's `bed` does: a Transport on a sixteenth-note grid; the chord model and
   the three route voices (pad = bass + top, the sector counter-line, the third voice), each through
   drive, warp, filter and its own delay-into-room; the zones' one-shots; the morphs; and the rhythm
   points (hits and grains) through their shared rhythm effects. Every step function is index.html's,
   line for line, with Math.random drawn from one stream (core/tests/piece_test.cpp holds the note
   choices to the JS). The point recordings (stretch) stay in fs_mix slots; this device is one more
   slot, at gain 1, and applies the route's own leash (setSynthLevel) inside.

   Threads: fs_piece_* calls come from one non-audio thread and only touch the inbox, under a mutex;
   the audio thread takes the inbox with try_lock at the start of a block. Everything the audio thread
   needs is allocated by the calling thread (patches, synth instances, buffers); anything it lets go
   of is handed back through the inbox and freed there. */
#include "fieldscape.h"
#include "device.hpp"
#include "json.hpp"
#include "synths.hpp"
#include "harmony.hpp"
#include "samplers.hpp"
#include "eq.hpp"
#include "composer.hpp"
#include "levels.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>

using namespace tone;

/* ------------------------------------------------------------------ shared tables */
namespace tone {
const Wave &basic_wave(int type) {
    struct W { Wave w[NTYPES]; W() {
        for (int t = 0; t < NTYPES; t++) {
            std::vector<double> b(Wave::MAXP - 1, 0.0);
            for (int n = 1; n < Wave::MAXP; n++) {
                double pf = 2 / (n * PI), v = 0;
                if (t == SINE) v = n <= 1 ? 1 : 0;
                else if (t == SQUARE) v = (n & 1) ? 2 * pf : 0;
                else if (t == SAWTOOTH) v = pf * ((n & 1) ? 1 : -1);
                else v = (n & 1) ? 2 * pf * pf * ((((n - 1) >> 1) & 1) ? -1 : 1) : 0;
                b[n - 1] = v;
            }
            w[t].build(b);
        } } };
    static W w;
    return w.w[type];
}
/* WAVE_TABLES: eight frames of 32 partials (index.html) */
const Wave &custom_wave(int index) {
    struct W { Wave w[8]; W() {
        auto odd = [](int i) { return i % 2 == 1; };
        for (int k = 0; k < 8; k++) {
            std::vector<double> a(32);
            for (int i = 1; i <= 32; i++) {
                double v;
                switch (k) {
                case 0: v = i == 1 ? 1 : 0; break;
                case 1: v = odd(i) ? std::pow(i, -1.35) : 0; break;
                case 2: v = odd(i) ? std::pow(i, -0.85) : 0; break;
                case 3: v = std::pow(i, -0.85) * (odd(i) ? 1 : 0.35); break;
                case 4: v = 1.0 / i; break;
                case 5: v = std::pow(i, -0.5); break;
                case 6: v = (1.0 / i) * (1 + 4 * std::exp(-std::pow(i - 7, 2) / 8)); break;
                default: v = std::pow(i, -0.7) * std::fabs(std::sin(i * 0.9)); break;
                }
                a[i - 1] = v;
            }
            w[k].build(a);     /* the energy normalisation the JS applies washes out in the peak one */
        } } };
    static W w;
    return w.w[index];
}
const std::vector<float> &pink_noise(int ch) {
    struct P { std::vector<float> c[2]; P() {
        uint64_t s = 0x9E3779B97F4A7C15ULL;
        for (int k = 0; k < 2; k++) {
            c[k].resize(44100 * 5);
            double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (auto &x : c[k]) {
                s ^= s << 13; s ^= s >> 7; s ^= s << 17;
                double white = (s >> 11) * (1.0 / 9007199254740992.0) * 2 - 1;
                b0 = 0.99886 * b0 + white * 0.0555179; b1 = 0.99332 * b1 + white * 0.0750759;
                b2 = 0.969 * b2 + white * 0.153852; b3 = 0.8665 * b3 + white * 0.3104856;
                b4 = 0.55 * b4 + white * 0.5329522; b5 = -0.7616 * b5 - white * 0.016898;
                x = (float)((b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11);
                b6 = white * 0.115926;
            }
        } } };
    static P p;
    return p.c[ch];
}
}  // namespace tone

/* ------------------------------------------------------------------ the patch (patchOf) */
static const struct { const char *name; int n; int iv[6]; } CHORDS[] = {
    { "maj7", 4, { 0, 4, 7, 11 } }, { "maj9", 5, { 0, 4, 7, 11, 14 } }, { "maj7#11", 5, { 0, 4, 6, 11, 14 } },
    { "6/9", 5, { 0, 4, 7, 9, 14 } }, { "m7", 4, { 0, 3, 7, 10 } }, { "m9", 5, { 0, 3, 7, 10, 14 } },
    { "m11", 6, { 0, 3, 7, 10, 14, 17 } }, { "m6", 4, { 0, 3, 7, 9 } }, { "mMaj7", 4, { 0, 3, 7, 11 } },
    { "7", 4, { 0, 4, 7, 10 } }, { "9", 5, { 0, 4, 7, 10, 14 } }, { "13", 6, { 0, 4, 7, 10, 14, 21 } },
    { "7#11", 5, { 0, 4, 6, 10, 14 } }, { "7b9", 5, { 0, 4, 7, 10, 13 } }, { "7alt", 5, { 0, 4, 8, 10, 13 } },
    { "7sus4", 4, { 0, 5, 7, 10 } }, { "m7b5", 4, { 0, 3, 6, 10 } }, { "dim7", 4, { 0, 3, 6, 9 } },
    { "aug", 3, { 0, 4, 8 } }, { "add9", 4, { 0, 4, 7, 14 } }, { "sus2", 3, { 0, 2, 7 } },
    { "sus4", 3, { 0, 5, 7 } }, { "quartal", 4, { 0, 5, 10, 15 } },
};
static const int NCHORDS = sizeof(CHORDS) / sizeof(CHORDS[0]);
enum { Q_M7 = 4, Q_7B9 = 13 };
static bool dom_q(int q) {   /* DOM_Q */
    if (q < 0) return false;
    const char *n = CHORDS[q].name;
    return !std::strcmp(n, "7") || !std::strcmp(n, "9") || !std::strcmp(n, "13") || !std::strcmp(n, "7b9") ||
           !std::strcmp(n, "7alt") || !std::strcmp(n, "7#11") || !std::strcmp(n, "7sus4");
}
static const struct { const char *name; int n; int iv[7]; } MODES[] = {
    { "lydian", 7, { 0, 2, 4, 6, 7, 9, 11 } }, { "dorian", 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "phrygian", 7, { 0, 1, 3, 5, 7, 8, 10 } }, { "mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "aeolian", 7, { 0, 2, 3, 5, 7, 8, 10 } }, { "lydian b7", 7, { 0, 2, 4, 6, 7, 9, 10 } },
    { "altered", 7, { 0, 1, 3, 4, 6, 8, 10 } }, { "harmonic minor", 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "whole tone", 6, { 0, 2, 4, 6, 8, 10 } }, { "pentatonic minor", 5, { 0, 3, 5, 7, 10 } },
    { "in sen", 5, { 0, 1, 5, 7, 10 } }, { "locrian", 7, { 0, 1, 3, 5, 6, 8, 10 } },
    { "ionian", 7, { 0, 2, 4, 5, 7, 9, 11 } },
};
static const int NMODES = sizeof(MODES) / sizeof(MODES[0]), MODE_DORIAN = 1, MODE_AEOLIAN = 4, MODE_IONIAN = 12;
enum MorphDest { M_VOICE_CUTOFF, M_SECT_CUTOFF, M_SECT_INDEX, M_VOICE_GAIN, M_VOICE_INDEX, M_VOICE_HARM, M_VOICE_DRIVE,
                 M_VOICE_WARP, M_SECT_HARM, M_SECT_DRIVE, M_SECT_WARP, M_V3_CUTOFF, M_V3_HARM, M_V3_INDEX, M_V3_DRIVE,
                 M_V3_WARP, NDEST };
static const char *const DEST_KEYS[NDEST] = { "voice.cutoff", "sect.cutoff", "sect.index", "voice.gain", "voice.index",
    "voice.harm", "voice.drive", "voice.warp", "sect.harm", "sect.drive", "sect.warp", "v3.cutoff", "v3.harm",
    "v3.index", "v3.drive", "v3.warp" };
enum Shape { SH_DRIFT, SH_BREATH, SH_PULSE, SH_RAMP, SH_TIDE };

static int find_chord(const std::string &s) { for (int i = 0; i < NCHORDS; i++) if (s == CHORDS[i].name) return i; return -1; }
static int find_mode(const std::string &s) { for (int i = 0; i < NMODES; i++) if (s == MODES[i].name) return i; return -1; }
static int find_synth(const std::string &s, int def) { for (int i = 0; i < NSYNTH; i++) if (s == SYNTH_NAMES[i]) return i; return def; }

/* divSeconds and SECT_STRIDE read the same strings */
struct Div { char s[6] = "8n"; };
static Div div_of(const std::string &s) { Div d; std::strncpy(d.s, s.c_str(), 5); d.s[5] = 0; return d; }
static double div_seconds(const Div &d, double bpm) {
    double beat = 60 / bpm, b = 1;
    std::string s = d.s;
    bool dotted = !s.empty() && s.back() == '.';
    size_t p = s.find('.'); if (p != std::string::npos) s.erase(p, 1);
    if (s == "1n") b = 4; else if (s == "2n") b = 2; else if (s == "4n") b = 1; else if (s == "8n") b = 0.5; else if (s == "16n") b = 0.25;
    return beat * b * (dotted ? 1.5 : 1);
}
static int stride_of(const Div &d, int def) {
    static const struct { const char *s; int n; } T[] = { { "16n", 1 }, { "8n", 2 }, { "8n.", 3 }, { "4n", 4 }, { "4n.", 6 }, { "2n", 8 }, { "2n.", 12 }, { "1n", 16 } };
    for (auto &t : T) if (!std::strcmp(t.s, d.s)) return t.n;
    return def;
}
static const double MAX_DELAY_S = 4;

struct Chord { int r, q, k; bool sd; };
struct Sector { int mode, r; };
struct Morph { bool on; int shape, dest, seed; double period, depth, bias, phase; };
struct FxCfg { Div delayDiv; double delayWet, delayFb, revWet, revDecay; };
struct VoiceCfg { bool on; double gain, density, bass, top, index, harm, drive, warp; int synth; };
struct SectCfg { bool on; int n; double gain; Div rhythm; double chordPull, harm, index, drive, warp; int synth; };
struct V3Cfg { bool on; double gain; Div rhythm; int synth; double harm, index, drive, warp; };
/* a sampler role's settings (3a): the bench's controls; Focus / Colour are its two timbre slots (morphs drive them) */
struct SamplerCfg { int body = 0, excite = 0, method = 0, mode = 0, octave = 0; double focus = 0.5, colour = 0.5, tune = 1; };
struct RfxCfg { double cutoff, drive; Div delayDiv; double delayWet, delayFb, revWet, revDecay; };
struct Patch {
    double tempo = 72; int key = 50, key2 = 55;
    Chord prog[64]; int nprog = 0;
    bool bed_on = true; int bed_voices = 4;
    VoiceCfg voice; FxCfg fx, fx2, fx3; V3Cfg v3; RfxCfg rfx; SectCfg sect;
    bool zones_on = true, morph_on = true, morph_cells = true;   /* morph.cells: the cells shown when it loads */
    Morph morph[24]; int nmorph = 0;
    Sector sectors[32]; int nsectors = 0;
    int tuning = -1;   /* "just" 1, "equal" 0, absent -1: the piece's default (sample harmony) */
    SamplerCfg smp[3];  /* voice, sect, v3: their "sampler" object (3a) */
    double eq[3][5][3] = {};   /* 6 (lab): each role's EQ, [band][Hz, dB, Q]; an absent "eq" is flat */
    EnvSpec env[3] = {}; bool env_on[3] = {};   /* 8b: each voice's note envelope; absent or null: the instrument's own */
    float cgain[1 + comp::NCHORD + comp::NSECTION + comp::NZONE][comp::NVOICE];   /* each layer's gain on each voice's style (comp_gains) */
    float rgain[comp::REL_MAX][comp::NVOICE];   /* composition 3: a relation's material at each member's level (comp_gains); 0: none worked out - the voice's own gain */
    float bgain[17];    /* composition 4: the walking bass's notes at the held root's level, by wrate (sixteenths; comp_gains); 0: none worked out */
    comp::Cfg comp;    /* composition step 1: the voices' styles and overrides ("comp"; absent: off, today's voices) */
};
static SamplerCfg smp_of(const Json *role) {
    SamplerCfg c; const Json *j = role ? role->get("sampler") : nullptr;
    if (!j) return c;
    c.body = (int)j->n("body", c.body); c.excite = (int)j->n("excite", c.excite); c.method = (int)j->n("method", c.method);
    c.mode = (int)j->n("mode", c.mode); c.focus = j->n("focus", c.focus); c.colour = j->n("colour", c.colour); c.tune = j->n("tune", c.tune);
    c.octave = (int)std::lround(j->n("octave", 0));   /* 6 */
    return c;
}
static bool is_sampler(int type) { return type >= S_RETUNE && type < NSYNTH; }

static FxCfg fx_of(const Json *j, const char *div, double wet, double fb, double rw, double rd) {
    FxCfg f;
    f.delayDiv = div_of(j ? j->s("delayDiv", div) : div);
    f.delayWet = j ? j->n("delayWet", wet) : wet; f.delayFb = j ? j->n("delayFb", fb) : fb;
    f.revWet = j ? j->n("revWet", rw) : rw; f.revDecay = j ? j->n("revDecay", rd) : rd;
    return f;
}

/* Task 4: the gains (comp_gains) already worked out, across a Piece's reads - an edit works out only what it changed. Plain
   arrays: on the web the read runs on the audio thread. FNV-1a keys over what decides a gain (the hits as bytes), 4-way
   set-associative, the least recently used of a set goes. Calling thread only, as the read is.
   ponytail: 1024 entries a piece (a route holds up to 93 layers x 3 voices); a piece with more re-solves what overflows */
struct GainMemo {
    static const int SETS = 256, WAYS = 4;
    uint64_t key[SETS][WAYS] = {}; double val[SETS][WAYS] = {}; uint32_t used[SETS][WAYS] = {}, clock = 0;   /* used 0: empty */
    static int set_of(uint64_t k) { return (int)((k ^ (k >> 32)) % SETS); }
    bool get(uint64_t k, double &v) {
        const int s = set_of(k);
        for (int w = 0; w < WAYS; w++) if (used[s][w] && key[s][w] == k) { used[s][w] = ++clock; v = val[s][w]; return true; }
        return false;
    }
    void put(uint64_t k, double v) {
        const int s = set_of(k); int i = 0;
        for (int w = 1; w < WAYS; w++) if (used[s][w] < used[s][i]) i = w;
        key[s][i] = k; val[s][i] = v; used[s][i] = ++clock;
    }
};
/* defaultPatch(), then whatever the feature's patch says over it (patchOf's migrations all end at
   version 17 with these defaults filled in; before version 4, or without a progression, it is the
   default patch). */
static void comp_gains(Patch &P, GainMemo *memo);
static void patch_of(const Json *p, Patch &P, GainMemo *memo = nullptr) {
    static const struct { int r; const char *q; } PROG[16] = {
        { 0, "m9" }, { 0, "m11" }, { 5, "maj7#11" }, { 5, "6/9" }, { 10, "maj9" }, { 10, "maj7" }, { 3, "maj7#11" }, { 3, "6/9" },
        { 8, "maj9" }, { 8, "13" }, { 1, "maj7" }, { 1, "maj9" }, { 2, "m7b5" }, { 7, "7alt" }, { 0, "m9" }, { 0, "m6" } };
    static const struct { const char *mode; int r; } SECT[7] = {
        { "lydian", 0 }, { "dorian", 5 }, { "lydian b7", 10 }, { "harmonic minor", 2 }, { "altered", 6 }, { "in sen", 1 }, { "pentatonic minor", 9 } };
    static const struct { const char *id, *shape; double period, depth, bias, phase; const char *dest; } MORPHS[6] = {
        { "m1", "tide", 660, 0.85, 0.08, 0, "voice.cutoff" }, { "m2", "drift", 190, 0.70, 0.15, 0.31, "voice.cutoff" },
        { "m3", "breath", 47, 0.55, 0.22, 0.62, "voice.cutoff" }, { "m4", "drift", 310, 0.80, 0.10, 0.17, "sect.index" },
        { "m5", "breath", 97, 0.60, 0.30, 0.44, "sect.cutoff" }, { "m6", "breath", 90, 0.55, 0.30, 0, "voice.gain" } };
    auto shape_of = [](const std::string &s) { return s == "breath" ? SH_BREATH : s == "pulse" ? SH_PULSE : s == "ramp" ? SH_RAMP : s == "tide" ? SH_TIDE : SH_DRIFT; };
    auto dest_of = [](const std::string &s) { for (int i = 0; i < NDEST; i++) if (s == DEST_KEYS[i]) return i; return -1; };
    auto seed_of = [](const std::string &id) { int c = id.empty() ? 'm' : (unsigned char)id.back(); return c ? c : 7; };

    int version = p ? (int)p->n("version", 0) : 0;
    const Json *prog = p ? p->get("prog") : nullptr;
    bool use = p && prog && prog->kind == Json::ARR && prog->size() && version >= 4 && version <= 17;
    std::string tu = p ? p->s("tuning", "") : "";           /* read before an old patch is set aside */
    P.tuning = tu == "just" ? harmony::JUST : tu == "equal" ? harmony::EQUAL : -1;
    if (!use) p = nullptr;

    P.tempo = p ? p->n("tempo", 72) : 72; P.key = p ? (int)p->n("key", 50) : 50; P.key2 = p ? (int)p->n("key2", 55) : 55;
    P.nprog = 0;
    if (p) for (size_t i = 0; i < prog->size() && P.nprog < 64; i++) {
        const Json *c = prog->at(i);
        P.prog[P.nprog++] = Chord{ (int)c->n("r", 0), find_chord(c->s("q", "m7")), (int)c->n("k", 1), c->flag("sd", false) };
    } else for (auto &c : PROG) P.prog[P.nprog++] = Chord{ c.r, find_chord(c.q), 1, false };

    const Json *bed = p ? p->get("bed") : nullptr;
    P.bed_on = bed ? bed->flag("on", true) : true; P.bed_voices = bed ? (int)bed->n("voices", 4) : 4;
    const Json *v = p ? p->get("voice") : nullptr;
    P.voice = VoiceCfg{ v ? v->flag("on", true) : true, v ? v->n("gain", 0.45) : 0.45, v ? v->n("density", 0.5) : 0.5,
        v ? v->n("bass", 0.9) : 0.9, v ? v->n("top", 0.8) : 0.8, v ? v->n("index", 4) : 4, v ? v->n("harm", 1) : 1,
        v ? v->n("drive", 0.12) : 0.12, v ? v->n("warp", 0.1) : 0.1, find_synth(v ? v->s("synth", "fm") : "fm", FM) };
    P.fx = fx_of(p ? p->get("fx") : nullptr, "8n.", 0.75, 0.85, 0.18, 3.4);
    P.fx2 = fx_of(p ? p->get("fx2") : nullptr, "8n", 0.4, 0.55, 0.12, 1.8);
    P.fx3 = fx_of(p ? p->get("fx3") : nullptr, "4n", 0.32, 0.5, 0.26, 5);
    const Json *v3 = p ? p->get("v3") : nullptr;
    P.v3 = V3Cfg{ v3 ? v3->flag("on", true) : true, v3 ? v3->n("gain", 0.55) : 0.55, div_of(v3 ? v3->s("rhythm", "1n") : "1n"),
        find_synth(v3 ? v3->s("synth", "am") : "am", AM), v3 ? v3->n("harm", 1.5) : 1.5, v3 ? v3->n("index", 3) : 3,
        v3 ? v3->n("drive", 0.1) : 0.1, v3 ? v3->n("warp", 0.22) : 0.22 };
    const Json *r = p ? p->get("rfx") : nullptr;
    P.rfx = RfxCfg{ r ? r->n("cutoff", 9000) : 9000, r ? r->n("drive", 0.15) : 0.15, div_of(r ? r->s("delayDiv", "8n") : "8n"),
        r ? r->n("delayWet", 0.22) : 0.22, r ? r->n("delayFb", 0.34) : 0.34, r ? r->n("revWet", 0.14) : 0.14, r ? r->n("revDecay", 1.2) : 1.2 };
    const Json *z = p ? p->get("zones") : nullptr;
    P.zones_on = z ? z->flag("on", true) : true;
    const Json *s = p ? p->get("sect") : nullptr;
    P.sect = SectCfg{ s ? s->flag("on", true) : true, s ? (int)s->n("n", 7) : 7, s ? s->n("gain", 0.8) : 0.8,
        div_of(s ? s->s("rhythm", "4n.") : "4n."), s ? s->n("chordPull", 0.35) : 0.35, s ? s->n("harm", 2.02) : 2.02,
        s ? s->n("index", 7.5) : 7.5, s ? s->n("drive", 0.18) : 0.18, s ? s->n("warp", 0.14) : 0.14,
        find_synth(s ? s->s("synth", "fm") : "fm", FM) };
    P.smp[0] = smp_of(v); P.smp[1] = smp_of(s); P.smp[2] = smp_of(v3);
    {   /* 6 (lab): "eq": [[Hz, dB, Q] x5] on a role */
        static const double F[5] = { 80, 250, 1000, 4000, 10000 }, Q[5] = { 0.7, 1, 1, 1, 0.7 };
        const Json *rj[3] = { v, s, v3 };
        for (int q = 0; q < 3; q++) {
            const Json *e = rj[q] ? rj[q]->get("eq") : nullptr;
            for (int k = 0; k < 5; k++) {
                const Json *b = e ? e->at(k) : nullptr; const Json *f = b ? b->at(0) : nullptr, *g = b ? b->at(1) : nullptr, *qq = b ? b->at(2) : nullptr;
                P.eq[q][k][0] = f ? f->num : F[k]; P.eq[q][k][1] = g ? g->num : 0; P.eq[q][k][2] = qq ? qq->num : Q[k];
            }
            /* 8b: "env": [attack s, decay s, sustain, release s] */
            const Json *ev = rj[q] ? rj[q]->get("env") : nullptr; P.env_on[q] = false;
            if (ev && ev->kind == Json::ARR && ev->size() >= 4 && ev->at(0)->kind == Json::NUM && ev->at(1)->kind == Json::NUM && ev->at(2)->kind == Json::NUM && ev->at(3)->kind == Json::NUM) {
                auto cl = [](double x, double lo, double hi) { return std::isfinite(x) ? std::max(lo, std::min(hi, x)) : lo; };
                P.env[q] = EnvSpec{ cl(ev->at(0)->num, 0.002, 8), cl(ev->at(1)->num, 0.005, 8), cl(ev->at(2)->num, 0, 1), cl(ev->at(3)->num, 0.01, 12), false, true };
                P.env_on[q] = true;
            }
        }
    }

    const Json *m = p ? p->get("morph") : nullptr;
    P.nmorph = 0;
    if (m) {
        P.morph_on = m->flag("on", true); P.morph_cells = m->flag("cells", false);
        const Json *list = m->get("list");
        for (size_t i = 0; list && i < list->size() && P.nmorph < 24; i++) {
            const Json *e = list->at(i);
            std::string sh = e->s("shape", "drift"), d = e->s("dest", "");
            if (version < 11 && (sh == "field" || !(d == "voice.cutoff" || d == "voice.gain" || d == "sect.cutoff" || d == "sect.index"))) continue;
            P.morph[P.nmorph++] = Morph{ e->flag("on", false), shape_of(sh), dest_of(d), seed_of(e->s("id", "")),
                e->n("period", 60), e->n("depth", 1), e->n("bias", 0), e->n("phase", 0) };
        }
    }
    if (!m || (version < 11 && P.nmorph == 0)) {
        P.morph_on = true; P.morph_cells = true; P.nmorph = 0;
        for (auto &d : MORPHS) P.morph[P.nmorph++] = Morph{ true, shape_of(d.shape), dest_of(d.dest), seed_of(d.id), d.period, d.depth, d.bias, d.phase };
    }
    const Json *sec = p ? p->get("sectors") : nullptr;
    P.nsectors = 0;
    for (size_t i = 0; sec && i < sec->size() && P.nsectors < 32; i++) {
        const Json *e = sec->at(i);
        P.sectors[P.nsectors++] = Sector{ find_mode(e->s("mode", "")), (int)e->n("r", 0) };
    }
    if (!P.nsectors) for (auto &e : SECT) P.sectors[P.nsectors++] = Sector{ find_mode(e.mode), e.r };
    comp::comp_of(p ? p->get("comp") : nullptr, P.comp);
    comp_gains(P, memo);
}

/* ------------------------------------------------------------------ rhythm (rhythmOf) */
static const char *const HIT_SLOTS[4] = { "low", "mid", "high", "rand" };
struct HitCfg { int pulses, rotate; double gain, pitch, crush, drive; Div delayDiv; double delayFb, delayWet, stretch; };
struct GrainCfg { double pulses, rotate, offset, grain, count, spread, scatter, gain, pitch, vary, roam, attack, decay, sustain, release; };
struct Combo { int pulses, rotate; };
struct RhythmCfg {
    bool on = true; int steps = 64; Div div; double gain = 0.85, idiom = 0.35; int sentence_bars = 8; double follow = 0;
    double resonate = 0; int body = 0;   /* 8c: grains rung by the chord (0 String, 1 Tube, 2 Bell) */
    double eq[5][3] = {}; double env[4] = {}; bool env_on = false;   /* Test 15: the point's EQ; each hit's note envelope */
    HitCfg v[4]; GrainCfg g; Combo sentence[4][4]; bool has_sentence = false;
};

/* ------------------------------------------------------------------ random: one stream */
struct Rng {
    uint64_t s = 0x853c49e6748fea9bULL;
    double (*hook)(void *) = nullptr; void *hook_ctx = nullptr;   /* tests replay the JS's own draws */
    double operator()() {
        if (hook) return hook(hook_ctx);
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (s >> 11) * (1.0 / 9007199254740992.0);
    }
    static double call(void *self) { return (*(Rng *)self)(); }
};

static int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
/* buildSentenceSet / oneComboSet */
static void build_sentence(RhythmCfg &r, int steps, Rng &rnd) {
    static const double BAND[4][2] = { { 0.05, 0.16 }, { 0.16, 0.34 }, { 0.28, 0.55 }, { 0.04, 0.28 } };
    for (int var = 0; var < 4; var++) {
        Combo out[4];
        for (int s = 0; s < 4; s++) {
            int k = (int)std::round(steps * (BAND[s][0] + rnd() * (BAND[s][1] - BAND[s][0])));
            if (k < 1) k = 1;
            for (int tries = 0; tries < 4 && gcd(k, steps) > 1; tries++) {
                int up = k + 1;
                k = (up <= steps && up <= (int)std::ceil(steps * BAND[s][1]) + 1) ? up : k - 1;
                if (k < 1) { k = 1; break; }
            }
            out[s].pulses = k < steps ? k : steps;
            out[s].rotate = rnd() < 0.45 ? 0 : (int)std::floor(rnd() * steps);
        }
        std::vector<std::pair<int, int>> seen;
        for (int s = 0; s < 4; s++) {
            Combo &c = out[s];
            for (auto &q : seen) if (q.first == c.pulses && q.second == c.rotate) { c.rotate = (c.rotate + 1 + (int)std::floor(rnd() * 3)) % steps; break; }
            seen.push_back({ c.pulses, c.rotate });
        }
        for (int s = 0; s < 4; s++) r.sentence[s][var] = out[s];
    }
    r.has_sentence = true;
}

static void rhythm_of(const Json *j, RhythmCfg &r, Rng &rnd) {
    static const HitCfg DV[4] = {
        { 8, 0, 1.00, 0, 0, 0, div_of("8n"), 0, 0, 0 }, { 13, 2, 0.80, 0, 0, 0, div_of("8n"), 0, 0, 0 },
        { 21, 1, 0.65, 0, 0, 0, div_of("8n"), 0, 0, 0 }, { 5, 11, 0.55, 0, 0, 0, div_of("8n"), 0, 0, 0 } };
    r.on = j ? j->flag("on", true) : true;
    r.steps = j ? (int)j->n("steps", 64) : 64;
    r.div = div_of(j ? j->s("div", "16n") : "16n");
    r.gain = j ? j->n("gain", 0.85) : 0.85;
    r.idiom = j ? j->n("idiom", 0.35) : 0.35;
    r.follow = j ? std::max(0.0, std::min(1.0, j->n("follow", 0))) : 0;   /* 5 */
    r.resonate = j ? std::max(0.0, std::min(1.0, j->n("resonate", 0))) : 0;   /* 8c */
    { const Json *b = j ? j->get("body") : nullptr; r.body = b ? (b->kind == Json::NUM ? (int)b->num : std::atoi(j->s("body", "0").c_str())) : 0; r.body = std::max(0, std::min(2, r.body)); }
    r.sentence_bars = j ? (int)j->n("sentenceBars", 8) : 8;
    eq_env_of(j, r.eq, r.env, r.env_on);
    const Json *vs = j ? j->get("voices") : nullptr;
    for (int s = 0; s < 4; s++) {
        const Json *v = vs ? vs->get(HIT_SLOTS[s]) : nullptr;
        const HitCfg &d = DV[s];
        r.v[s] = v ? HitCfg{ (int)v->n("pulses", d.pulses), (int)v->n("rotate", d.rotate), v->n("gain", d.gain), v->n("pitch", 0),
                             v->n("crush", 0), v->n("drive", 0), div_of(v->s("delayDiv", "8n")), v->n("delayFb", 0), v->n("delayWet", 0),
                             v->n("stretch", 0) } : d;
    }
    const Json *ss = j ? j->get("sentenceSet") : nullptr;
    r.has_sentence = false;
    if (ss && ss->kind == Json::OBJ) {
        r.has_sentence = true;
        for (int s = 0; s < 4; s++) {
            const Json *arr = ss->get(HIT_SLOTS[s]);
            for (int k = 0; k < 4; k++) {
                const Json *c = arr ? arr->at(k) : nullptr;
                r.sentence[s][k] = Combo{ c ? (int)c->n("pulses", 1) : r.v[s].pulses, c ? (int)c->n("rotate", 0) : r.v[s].rotate };
            }
        }
    } else build_sentence(r, std::max(4, std::min(64, r.steps)), rnd);
    const Json *g = j ? j->get("grains") : nullptr;
    double attack = g ? g->n("attack", NAN) : NAN, decay = g ? g->n("decay", 0.3) : 0.3, sustain = g ? g->n("sustain", 0.5) : 0.5,
           release = g ? g->n("release", 0.45) : 0.45;
    if (g && std::isnan(attack) && g->get("window")) {          /* the old window/shape pair */
        double w = std::max(0.0, std::min(1.0, g->n("window", 0))), sh = std::max(0.0, std::min(1.0, g->n("shape", 0.2)));
        double span = 0.04 + w * 0.96;
        attack = std::round(span * sh * 1000) / 1000; release = std::round(span * (1 - sh) * 1000) / 1000; decay = 0; sustain = 1;
    }
    if (std::isnan(attack)) attack = 0.04;
    r.g = GrainCfg{ g ? g->n("pulses", 11) : 11, g ? g->n("rotate", 0) : 0, g ? g->n("offset", 0.15) : 0.15, g ? g->n("grain", 90) : 90,
        g ? g->n("count", 6) : 6, g ? g->n("spread", 0.5) : 0.5, g ? g->n("scatter", 0.25) : 0.25, g ? g->n("gain", 0.85) : 0.85,
        g ? g->n("pitch", 0) : 0, g ? g->n("vary", 0.35) : 0.35, g ? g->n("roam", 1) : 1, attack, decay, sustain, release };
}

static void euclid(int k, int n, bool *out) {
    int acc = 0;
    for (int i = 0; i < n; i++) {
        if (k <= 0) out[i] = false;
        else if (k >= n) out[i] = true;
        else { acc += k; if (acc >= n) { acc -= n; out[i] = true; } else out[i] = false; }
    }
}
static bool euclid_at(int k, int n, int rot, int at) {   /* rotated(euclid(k, n), rot)[at] */
    bool p[64]; euclid(k, n, p);
    return p[(((at + rot) % n) + n) % n];
}
static double metric_weight(int at, int steps) {
    if (at == 0) return 1;
    double quarter = steps / 4.0, eighth = steps / 8.0;
    if (quarter == std::floor(quarter) && at % (int)quarter == 0) return 0.6;
    if (eighth == std::floor(eighth) && at % (int)eighth == 0) return 0.35;
    return 0.12;
}

/* ------------------------------------------------------------------ the chains */
static const int SUB = 32;

/* dry -> drive blend (tanh curve) -> warp (Chorus) -> lowpass -> level gains */
struct Layer {
    double sr = 48000;
    Shaper shp;
    float post0 = 0.6f;
    Ctl fade, pre, post, cutoff, g1, g2;
    Chorus warp;
    Biquad lp;
    double q = 1;
    float L[SUB], R[SUB];
    void init(double s, double k, double post_base, double wf, double wdelay, double wdepth, double cut, double a, double b) {
        sr = s; shp.init(k); post0 = (float)post_base;
        fade.init(0); pre.init(1); post.init(post_base); cutoff.init(cut, true); g1.init(a); g2.init(b);
        warp.init(s, wf, wdelay, wdepth, 0);
        lp.type = LOWPASS;
        clear();
    }
    void clear() { std::memset(L, 0, sizeof L); std::memset(R, 0, sizeof R); }
    void drive(double dv, double k1, double k2, double now) {   /* voiceStep's drive block */
        fade.p.linearRampTo(dv, 0.3, now);
        pre.p.linearRampTo(1 + dv * k1, 0.3, now);
        post.p.linearRampTo(post0 / (1 + dv * k2), 0.3, now);
    }
    void process(float *oL, float *oR, int n, double te) {
        fade.block(te); pre.block(te); post.block(te); cutoff.block(te); g1.block(te); g2.block(te);
        for (int i = 0; i < n; i++) {
            float f = fade.at(i, n), pr = pre.at(i, n), po = post.at(i, n);
            L[i] = L[i] * (1 - f) + po * shp.run(pr * L[i]) * f;
            R[i] = R[i] * (1 - f) + po * shp.run(pr * R[i]) * f;
        }
        warp.process(L, R, n, te);
        lp.design(cutoff.a, q, sr);
        for (int i = 0; i < n; i++) {
            float g = g1.at(i, n) * g2.at(i, n);
            oL[i] += lp.run(0, L[i]) * g; oR[i] += lp.run(1, R[i]) * g;
        }
        clear();
    }
};

static double room_size(double decay) { double v = 0.45 + decay * 0.062; return v < 0 ? 0 : (v > 0.95 ? 0.95 : v); }

/* buildFxChain: the input reaches the room dry, and through a high-passed feedback delay and a
   send. The room is Freeverb, the web's own room for a device that cannot afford convolution:
   ponytail: Tone.Reverb's convolution (what a fast browser plays) costs ~7x on a phone; add a
   partitioned convolver if Kerem hears the difference. */
struct FxChain {
    double sr = 48000;
    Biquad hp;
    FeedbackDelay dl;
    Ctl send;
    Freeverb rv;
    float L[SUB], R[SUB];
    void init(double s, const FxCfg &c, double tempo, double hp_hz) {
        sr = s;
        hp.type = HIGHPASS; hp.design(hp_hz, 0.7, s);
        dl.init(s, MAX_DELAY_S, std::min(MAX_DELAY_S - 0.01, div_seconds(c.delayDiv, tempo)), c.delayFb);
        send.init(0);
        rv.init(s, room_size(c.revDecay), 3000);
        rv.wet.p.linearRampTo(c.revWet, 0.3, 0);
        send.p.linearRampTo(c.delayWet, 0.3, 0);
        std::memset(L, 0, sizeof L); std::memset(R, 0, sizeof R);
    }
    void apply(const FxCfg &c, double tempo, double now) {
        dl.time.p.linearRampTo(std::min(MAX_DELAY_S - 0.01, div_seconds(c.delayDiv, tempo)), 0.2, now);
        send.p.linearRampTo(c.delayWet, 0.2, now);
        dl.fb.p.linearRampTo(c.delayFb, 0.2, now);
        rv.wet.p.linearRampTo(c.revWet, 0.2, now);
        rv.room.p.set(room_size(c.revDecay), now);
    }
    void process(float *oL, float *oR, int n, double te) {
        send.block(te);
        float dL[SUB], dR[SUB];
        for (int i = 0; i < n; i++) { dL[i] = hp.run(0, L[i]); dR[i] = hp.run(1, R[i]); }
        dl.process(dL, dR, n, te);
        for (int i = 0; i < n; i++) { float g = send.at(i, n); L[i] += dL[i] * g; R[i] += dR[i] * g; }
        rv.process(L, R, n, te);
        for (int i = 0; i < n; i++) { oL[i] += L[i]; oR[i] += R[i]; }
        std::memset(L, 0, sizeof L); std::memset(R, 0, sizeof R);
    }
};

/* A synth role (pad bass, pad top, sector, third): one instance per instrument it has been asked
   for, made on the calling thread; a replaced one releases and keeps sounding 2.5 s (swapSynth). */
struct Role {
    std::unique_ptr<Synth> inst[NSYNTH];
    double until[NSYNTH] = {};
    int cur = -1;
    int rec = -1;                     /* which role recording a sampler here plays: 0 voice, 1 sect, 2 v3 (3a) */
    Settings st;
    double extra_db = 0;
    Synth *sy() { return cur >= 0 ? inst[cur].get() : nullptr; }
    /* final review C1: Strum and Swells are chords, and a digital synth is one voice - each instrument a composed role plays
       gets NPOOL more of itself (made with it, on the calling thread), dealt the chord's notes round-robin with inst; one is
       rendered only while its note sounds (busy), so an idle one costs nothing. Samplers have their own six voices */
    static const int NPOOL = 4;
    /* Kerem 2026-10-18 (option 1): a heavy instrument gets fewer - metal (12 oscillators) 2; measured, every other type's
       heavy case (strum 8n, strum 8n, swells 2n, 5 tones, one type on all three roles) is under 0.9 ms p99.9 with 4. A chord
       past them takes the oldest ringing voice (the round-robin), with the envelope's ramped retrigger */
    static int pool_n(int type) { return type == METAL ? 2 : NPOOL; }
    std::unique_ptr<Synth> pool[NSYNTH][NPOOL];
    double busy[NSYNTH][NPOOL] = {};
    bool synced[NSYNTH][NPOOL] = {};  /* set up as inst is (audio thread, at a chord's first use after a swap) */
    unsigned deal = 0;
    template <class F> void each(F f) {     /* what plays the role now: inst and its set-up pool */
        if (cur < 0 || !inst[cur]) return;
        f(inst[cur].get()); for (int j = 0; j < NPOOL; j++) if (synced[cur][j]) f(pool[cur][j].get());
    }
};

/* ------------------------------------------------------------------ rhythm points at run time */
/* peak: |sample| max, 0..1 (a hit's effects read it). pk: Test 15, the recording's dominant spectral peak every PK_HOP frames
   (Hz, then its prominence over its surroundings; Hz 0: none), filled by the audio thread; freed with the samples */
struct Src { short *data = nullptr; int ch = 0; long long frames = 0; float peak = 1; float *pk = nullptr; int pkn = 0; };
static const int PK_N = 2048, PK_HOP = 1024;
static inline void src_read(const Src &s, double pos, float &l, float &r) {
    long long i = (long long)pos;
    if (pos < 0 || i >= s.frames) { l = r = 0; return; }
    int c = s.ch > 1 ? 1 : 0;
    const short *a = s.data + i * s.ch;
    if (i + 1 >= s.frames) { l = a[0] / 32768.0f; r = a[c] / 32768.0f; return; }
    const short *b = a + s.ch;
    float f = (float)(pos - i);
    l = (a[0] + (b[0] - a[0]) * f) / 32768.0f;
    r = (a[c] + (b[c] - a[c]) * f) / 32768.0f;
}

/* Test 15: a hit's note envelope - attack, decay to sustain, held to the gate (one step), released exponentially to silence */
struct HitEnv {
    bool on = false; double a = 0, d = 0, s = 1, r = 0, gate = 0;
    double at(double u) const {   /* u: seconds since the hit; -1 once it has ended */
        const double k = 4.6;      /* to within 1 % of the target in the segment's time */
        auto held = [&](double v) { return v < a ? v / a : s + (1 - s) * std::exp(-k * (v - a) / d); };
        if (u < gate) return held(u);
        const double x = (u - gate) / r; if (x >= 1) return -1;
        return held(gate) * (std::exp(-k * x) - std::exp(-k)) / (1 - std::exp(-k));
    }
};
struct PlayV { bool on = false; double pos = 0, rate = 1, start = 0, stop = 1e300, fade = 0.02; HitEnv env; };
struct GrainSrc { bool on = false; double pos = 0, rate = 1, start = 0, stop = 0, fade_in = 0, fade_out = 0; };
struct HitSlot {
    Src src;
    PlayV pv[3];
    float vol = 1, gp_vol = 1;
    /* GrainPlayer (the hit's stretch) */
    bool gp_on = false; double gp_t0 = 0, gp_stop = 0, gp_grain = 0.2, gp_overlap = 0.1, gp_pr = 1, gp_rate = 1, gp_next = 0; long gp_tick = 0;
    GrainSrc gs[8];
    Ctl stretch_gain, blend;
    /* hit fx */
    Ctl crush_fade, bits, drive_fade, pre, post, delay_fade;
    HitEnv genv; double gstart = 0;   /* Test 15: the last hit's envelope, on its stretch (the GrainPlayer) too */
    FeedbackDelay dl;
    Shaper shp;
};
struct GrainV { bool on = false; double pos, rate, at, a, d, hold, s, w, len; };
/* 8c: the chord a grains point rings - a tuned loop per chord note (String: all overtones; Tube: half a period, inverted -
   the odd ones) or four bell modes, its fundamental the lowest octave from 55 Hz; retuned with a 0.3 s glide on a chord change */
struct ChordRes {
    float line[8][2048] = {}; int w = 0, nn = 0, nrun = 0;   /* nrun: the slots still ringing (a chord's notes, and a bigger one's before it) */
    double N[8] = {}, Nt[8] = {}, lp[8] = {};
    double y1[8][4] = {}, y2[8][4] = {};
    double ms_dry = 0, ms_wet = 0, agc = 1, res = 0; long long sig = -1;   /* res: the mix as it glides to the setting */
};
struct Trk { const float *tf = nullptr, *tc = nullptr; int n = 0; double hop = 0.02, f0 = 0; };   /* 5: a source's pitch track */
struct Rhythm {
    bool used = false, dying = false; double free_at = 0;
    bool grains = false;
    RhythmCfg cfg;
    Ctl gain;
    float host_g = 0;                 /* the level the walk last gave it, before any solo */
    HitSlot hit[4];
    Src gsrc;
    Trk trk[4];                       /* 5: each source's pitch (grains: [0]) */
    GrainV gv[192];
    long tick = 0; int step = 0;
    bool live = false; Combo lv[4]; int bars = 0, sentence_idx = 0; bool fx_ready = false;
    double lvl = 0;                   /* 8a: its own output, ~0.1 s mean square, for the point's meter */
    struct GMark { double at, pos, len; } gm[96]; int gmi = 0, gmn = 0;   /* 8a: its recent grains, for the waveform (a dense cloud's 1.4 s) */
    ChordRes cr;                      /* 8c */
    Eq5 eq;                           /* Test 15: the point's EQ, after its ringing, before its level */
    int pk_done = 0;                  /* Test 15: the grains recording's peak track, frames analysed so far */
};

/* ------------------------------------------------------------------ the device */
/* a role's recording (3a): mono, normalised; its pitch track (f0, confidence every hop s) */
struct RoleRec { float *pcm = nullptr; long long n = 0; float *tf = nullptr, *tc = nullptr; int tn = 0; double f0 = 0, hop = 0.02; float *spec = nullptr; int spec_n = 0; };
struct Inbox {
    bool walk = false; int route = -1; double t = 0, dist = 1e300;
    bool sector = false; int sector_v = -1;
    bool zones_set = false; uint64_t zin[16] = {}; int nzin = 0; uint64_t zent = 0;   /* composition: zones inside, one just entered */
    bool character = false; double centroid = 2000, onsets = 1;
    int zones[8]; int nzones = 0;
    bool solo_set = false; int solo_v = -1;    /* Listen on a rhythm point: that handle alone (-1: everything) */
    bool gate_set = false; float gate_v = 1;   /* the mixer / playback choice on the route's own sound (0 or 1) */
    struct ROp { int op = 0; RhythmCfg cfg; bool grains = false; bool gain = false; float g = 0; Src src[4]; bool src_set[4] = {}; Trk trk[4]; bool trk_set[4] = {}; } r[FS_MAX_VOICES];
    RoleRec rrec[3]; bool rrec_set[3] = {};   /* role recordings for the audio thread (3a) */
    std::vector<short *> trash;       /* buffers the audio thread let go of */
    bool freed[FS_MAX_VOICES] = {};
    std::vector<Patch> routes;        /* registered, read by the audio thread only under the lock */
    bool route_dirty[64] = {};        /* a setter changed this route's patch (fs_piece_set_route) */
    /* written back by the audio thread: pacer.routeId, its patch's sect.n and bed voices, sect.idx */
    int taken = -1, sect_n = 7, bed_voices = 4, sector_back = -1;
    int chord = -1, nprog = 0; char chord_label[24] = "";   /* the chord playing, for the screen */
    double clock = 0; int root = 0;   /* the piece's seconds and the chord root's pc, for the cells */
    int chord_n = -1; float chord_notes[5] = { 0 }, chord_root_midi = 0;   /* the chord's notes, for the stretch's tune and layers */
};

/* chordRoot / chordQuality of a patch's step */
static int root_of(const Patch &P, int step) {
    const Chord &c = step < P.nprog ? P.prog[step] : P.prog[0];
    int base = (c.k == 2 ? P.key2 : P.key) + c.r;
    if (!c.sd) return base;
    const Chord &nx = P.prog[(step + 1) % P.nprog];
    return (nx.k == 2 ? P.key2 : P.key) + nx.r + 7;
}
static int quality_of(const Patch &P, int step) {
    const Chord &c = step < P.nprog ? P.prog[step] : P.prog[0];
    if (c.sd && !dom_q(c.q)) return Q_7B9;
    return c.q;
}
static int fold(int m, int lo, int hi) { while (m < lo) m += 12; while (m > hi && m - 12 >= lo) m -= 12; return m; }
static int floor_div(int a, int n) { return a >= 0 ? a / n : -((n - 1 - a) / n); }
/* composition 2: degree d of the key's scale - aeolian on a first chord with a minor 3rd, else ionian (sus2, sus4 and quartal
   count as major) - folded into [lo, hi] */
static int scale_midi(const Patch &P, int d, int oct, int lo, int hi) {
    int q = quality_of(P, 0); if (q < 0) q = Q_M7;
    const auto &md = MODES[CHORDS[q].iv[1] == 3 ? MODE_AEOLIAN : MODE_IONIAN];
    const int o = floor_div(d, md.n);
    return fold(P.key + md.iv[d - o * md.n] + 12 * (o + oct), lo, hi);
}
/* the key's scale tone below MIDI note m */
static int scale_below(const Patch &P, int m) {
    int q = quality_of(P, 0); if (q < 0) q = Q_M7;
    const auto &md = MODES[CHORDS[q].iv[1] == 3 ? MODE_AEOLIAN : MODE_IONIAN];
    for (int d = m - 1; d >= m - 12; d--) for (int i = 0; i < md.n; i++) if (((d - P.key) % 12 + 12) % 12 == md.iv[i]) return d;
    return m - 2;
}
/* the key's scale tone above MIDI note m */
static int scale_above(const Patch &P, int m) {
    int q = quality_of(P, 0); if (q < 0) q = Q_M7;
    const auto &md = MODES[CHORDS[q].iv[1] == 3 ? MODE_AEOLIAN : MODE_IONIAN];
    for (int d = m + 1; d <= m + 12; d++) for (int i = 0; i < md.n; i++) if (((d - P.key) % 12 + 12) % 12 == md.iv[i]) return d;
    return m + 2;
}
/* ------------------------------------------------------------------ composition: each style at Free's level
   Kerem 2026-10-19 (Pedal and Strum crackle: the limiter pinned). Free plays a sparse line, one note at a time; a style
   strikes every step, chords strike 3-5 notes at once, Swells hold at full sustain. Each style's velocities are scaled so
   its power sits at Free's on that voice: both note streams - Free's own logic replayed with a fixed draw (voiceStep's
   density, sectorStep's rests, thirdStep's long notes), and the style's as play_style deals it, chord voices round-robin -
   run through the voice's own amplitude envelope (Swells: its swell), a note's energy its envelope squared, and the style
   takes sqrt(free power / style power). A note's power can also move with its pitch (Synth::note_power: pluck's burst,
   metal's highpass, the formants' lines); pluck has no envelope and its rings add (its level is a gain on its burst).
   Worked out here, on the calling thread, when a route's patch is read (fs_piece_add_route / _set_route): one gain per
   voice for the route and for each chord, section and zone override, from the patch's own numbers (its density, its
   chords, its voices' settings) - the audio thread only reads them (Piece::comp_bar). Measured: composer_test test_levels */
/* the three voices' settings (Piece::settings_for): their oscillators and envelopes */
static const EnvSpec ROLE_ENV[3] = { { 0.03, 0.4, 0.22, 1.6, false, true }, { 0.012, 0.35, 0.18, 1.1, false, true }, { 0.5, 1.2, 0.6, 3.2, false, true } };
static const EnvSpec ROLE_MENV[3] = { { 0.05, 0.6, 0.25, 1.2, false, true }, { 0.04, 0.5, 0.1, 0.8, false, true }, { 1.2, 1.6, 0.4, 2.4, false, true } };
static const int ROLE_OSC[3][2] = { { SINE, SQUARE }, { SINE, TRIANGLE }, { TRIANGLE, SINE } };
static Synth *new_synth(int type) {     /* a digital synth, not yet init()ed (Piece::make sets pluck's and comb's draws) */
    switch (type) {
    case AM: { auto *m = new ModSynth(); m->am = true; return m; }
    case DUO: return new DuoSynth();
    case MONO: return new MonoSynth();
    case SIMPLE: return new SimpleSynth();
    case PLUCK: return new PluckSynth();
    case METAL: return new MetalSynth();
    case MEMBRANE: return new MembraneSynth();
    case WAVETABLE: return new WavetableVoice();
    case COMB: return new CombVoice();
    case FORMANT: return new FormantVoice();
    default: return new ModSynth();
    }
}
namespace levels {
struct Hit { double t, dur, vel; int v, m; };   /* v: the voice dealt to; bit 8 (ORN): an ornament's note */
static const int ORN = 0x100;
static_assert(sizeof(Hit) == 32, "Hit is hashed as bytes: no padding");
/* Task 4: 10 ms steps (were 5): every gain within 0.17 dB of the 5 ms solve (11 synths x the levels styles x 3 voices at
   60/96/140 bpm; 15 ms: 0.38, 20 ms: 0.67). The lengths stay: 8 bars of Free were 0.8 dB off, 4 of the style 0.5 */
static int BARS = 6, FREE_BARS = 16; static double STEP = 0.010;   /* ponytail: not const so fs_piece_test_gains can solve the old way */
static const int MAXH = 1024;
/* the mean power over [from, to) (STEP apart) of these notes' envelopes, dealt to nv voices by Hit::v, each weighted by its
   pitch's note_power; vel x g, held to 1 as note() holds it when `clamp` */
static double env_power(const EnvSpec &es, std::vector<Hit> &h, int nv, double from, double to, const Synth *sy, double g, bool clamp) {
    std::sort(h.begin(), h.end(), [](const Hit &a, const Hit &b) { return a.t < b.t; });
    Envelope env[Role::NPOOL + 1]; double w[Role::NPOOL + 1]; int lm[Role::NPOOL + 1];
    for (int k = 0; k < nv; k++) { env[k].init(48000); env[k].set(es.a, es.d, es.s, es.r, es.aexp, es.rexp); w[k] = 1; lm[k] = -1; }
    double s = 0, np[128]; long long c = 0; size_t j = 0; for (double &x : np) x = -1;   /* note_power, a MIDI note once */
    for (double t = 0; t < to; t += STEP) {
        for (; j < h.size() && h[j].t <= t; j++) {
            const int k = (h[j].v & 0xff) % nv, m = std::max(0, std::min(127, h[j].m)); const double v = clamp ? std::min(1.0, h[j].vel * g) : h[j].vel * g;
            if (np[m] < 0) np[m] = sy->note_power(mtof(m));
            double at = h[j].t;   /* an ornament's note (Hit::v bit 8) on a voice still ringing another pitch: the fade of a period before it (Piece::sound) */
            if ((h[j].v & ORN) && lm[k] >= 0 && lm[k] != m && env[k].sig.value(at) > 1e-3) { const double fd = std::min(0.012, std::max(0.006, 1 / mtof(lm[k]))); Synth::fade_env(env[k], at, fd); at += fd; }
            lm[k] = m;
            env[k].triggerAttack(at, v); env[k].triggerRelease(at + h[j].dur); w[k] = np[m];
        }
        for (int k = 0; k < nv; k++) { const double x = env[k].sig.render(t); if (t >= from) s += x * x * w[k]; }
        if (t >= from) c++;
    }
    return s / std::max(1LL, c);
}
/* pluck: each note's burst (a period long: note_power) rings in the comb, losing (1 - resonance) a period, while it is held
   and through its release; a note struck before the last one's release is under way holds that ring on, so the notes add */
static double pluck_power(const PluckSynth *p, std::vector<Hit> &h, double from, double to, double g) {
    const double r = std::min(0.9999, std::max(0.0, p->resonance)), r2 = r * r, ring = 1 / (1 - r2);
    std::sort(h.begin(), h.end(), [](const Hit &a, const Hit &b) { return a.t < b.t; });
    double s = 0;
    for (size_t i = 0; i < h.size(); i++) {
        if (h[i].t < from || h[i].t >= to) continue;
        double end = h[i].t + h[i].dur;
        for (size_t j = i + 1; j < h.size() && h[j].t < end + 0.1 * p->release_s; j++) if (h[j].v == h[i].v) end = std::max(end, h[j].t + h[j].dur);
        const double f = mtof(h[i].m), held = std::pow(r2, f * (end - h[i].t)), v = h[i].vel * g;
        s += v * v * p->note_power(f) * ((1 - held) * ring + held * std::min(ring, f * p->release_s / 3));
    }
    return s / (to - from);
}
static int tones_of(const Patch &P, int step, int *out) {
    int q = quality_of(P, step); if (q < 0) q = Q_M7;
    const int root = root_of(P, step);
    for (int i = 0; i < CHORDS[q].n; i++) out[i] = root + CHORDS[q].iv[i];
    return CHORDS[q].n;
}
static int deg_midi(const int *tones, int nt, int d, int lo, int hi, int oct = 0) {   /* as Piece::deg_midi */
    nt = std::max(1, nt); const int o = floor_div(d, nt);
    return fold(tones[d - o * nt] + 12 * (o + oct), lo, hi);
}
/* Free's line on voice q, as voiceStep / sectorStep / thirdStep play it, with its own fixed draw (onsets at their resting 1) */
static void free_hits(const Patch &P, int q, bool pl, std::vector<Hit> &h) {
    uint64_t s = 0x9e3779b97f4a7c15ULL; auto r = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (s >> 11) * (1.0 / 9007199254740992.0); };
    const double six = 60.0 / P.tempo / 4; const int ticks = 16 * FREE_BARS;
    auto hit = [&](double t, double d, double v, int m) { h.push_back(Hit{ t, d, pl ? 1 : v, 0, m }); };   /* pluck: velocity ignored */
    if (q == 0) {
        const double density = std::max(0.05, std::min(0.95, P.voice.density * (0.35 + 1 / 2.5)));
        for (int k = 0; k < ticks; k += 2) if (r() <= density) { const int m = P.key + 17 + (int)(r() * 15);   /* voiceStep leads near the upper tones: key + 24 on average (measured) */
            const double d = 0.35 + r() * 0.55; hit(k * six, d, 0.22 + r() * 0.16, m); }
    } else if (q == 1) {
        const int stride = stride_of(P.sect.rhythm, 4); int rest = 0, phrase = 0;
        for (int k = 0; k < ticks; k += stride) {
            if (rest > 0) { rest--; continue; }
            if (r() < 0.18) { rest = 1 + (int)std::floor(r() * 2); continue; }
            if (++phrase >= 5 + (int)std::floor(r() * 4)) { phrase = 0; rest = 1 + (int)std::floor(r() * 2); }
            const int m = P.key + 2 + (int)(r() * 25); const double d = 0.28 + r() * 0.6; hit(k * six, d, 0.42 + r() * 0.2, m);
        }
    } else {
        const int stride = stride_of(P.v3.rhythm, 16);
        for (int k = 0; k < ticks; k += stride) { const int m = P.key - 7 + (int)(r() * 20); const double d = 60 / P.tempo * (1.5 + r() * 2.5); hit(k * six, d, 0.36 + r() * 0.16, m); }
    }
}
/* the style's own stream over BARS bars, the progression's chords a bar each; its velocities before any gain (the strum's
   first tone keeps its accent; a chord's tones share nothing more - the gain gives the whole chord Free's power) */
/* a chorale's voices the instrument can sound at once: the role's own plus its pool (a sampler has six of its own) - chord_voice deals round-robin, a
   bigger chord would take its own first note again at the same time (final review 3) */
static int chorale_cap(int type) { return type >= S_RETUNE ? 6 : Role::pool_n(type) + 1; }
static void style_hits(const Patch &P, const comp::Voice &v, int nv, std::vector<Hit> &h, int q = 0) {
    const int stp = v.style == comp::CHORALE ? std::max(1, (int)v.restrike) : std::max(1, (int)v.rate);   /* chorale: its restrike is the step */
    const double six = 60.0 / P.tempo / 4, dur = six * stp * v.len; unsigned deal = 0, pulse = 0;
    if (v.style == comp::POINTILLIST) {   /* composition 4: one short note, then a seeded silence (Piece::point_step); the chord tone across lo..hi, the level within +-20 % */
        const uint32_t sd = P.comp.seed ^ 0x9017; int n = 0;
        for (long long k = 0; k < 16 * BARS; n++) {
            int tn[8]; const int nt = tones_of(P, (int)((k / 16) % std::max(1, P.nprog)), tn);
            int pool[128], np = 0; for (int m = v.lo; m <= v.hi && np < 128; m++) for (int i = 0; i < nt; i++) if (((m - tn[i]) % 12 + 12) % 12 == 0) { pool[np++] = m; break; }
            const double gap = v.sil_min + (v.sil_max - v.sil_min) * comp::rand01(sd, n, q, 0);
            if (np) h.push_back(Hit{ k * six, v.plen, (0.15 + 0.45 * v.peak) * (1 + 0.2 * (2 * comp::rand01(sd, n, q, 2) - 1)), 0, pool[std::min(np - 1, (int)(comp::rand01(sd, n, q, 1) * np))] });
            k += std::max(1, (int)std::lround(gap / six));
        }
        return;
    }
    const int ncols = std::max(1, (16 + v.rate - 1) / v.rate);
    auto vel = [&](int c) { return 0.22 + 0.4 * v.acc[c % comp::MAX_COLS] / 100.0; };
    int seq[comp::MAX_COLS], ns = 0; for (int i = 0; i < comp::MAX_COLS; i++) if (v.grid[i] >= 0) seq[ns++] = v.grid[i];
    int pedal = -1;
    for (int k = 0; k < 16 * BARS; k += stp) {
        int tn[8]; const int nt = tones_of(P, (k / 16) % std::max(1, P.nprog), tn);
        const int col = (k % 16) / stp; const double t = k * six;
        if (v.style == comp::ARPEGGIO || v.style == comp::OSTINATO) {   /* the ostinato's cell: the grid, as an arpeggio's (a transposed one has the same notes' power) */ const int d = v.grid[col % ncols]; if (d >= 0) h.push_back(Hit{ t, dur, vel(col), 0, deg_midi(tn, nt, d, v.lo, v.hi) }); }
        else if (v.style == comp::PEDAL) {
            if (pedal < 0 || v.follow) pedal = tn[0];
            int m = v.pedal == 2 ? v.pedal_note : pedal + (v.pedal == 1 ? 7 : 0);
            while (m < v.lo) m += 12; while (m > v.hi && m - 12 >= v.lo) m -= 12;
            h.push_back(Hit{ t, dur, vel(col), 0, m });
        } else if (v.style == comp::EUCLID) {
            const int E = std::max(1, (int)v.steps), at = (k / v.rate) % E;
            if (ns && euclid_at(std::min((int)v.pulses, E), E, v.rot % E, at)) h.push_back(Hit{ t, dur, vel(at), 0, deg_midi(tn, nt, seq[pulse++ % ns], v.lo, v.hi) });
        } else if (v.style == comp::STRUM) {
            int nd = 0; for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) { const double off = nd * v.spread_ms / 1000.0;
                h.push_back(Hit{ t + off, std::max(0.03, dur - off), vel(col) + (nd == 0 ? 0.12 : 0), (int)(deal++ % nv), deg_midi(tn, nt, d, v.lo, v.hi) }); nd++; }
        } else if (v.style == comp::SWELLS) {
            for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) h.push_back(Hit{ t, dur, 0.15 + 0.45 * v.peak, (int)(deal++ % nv), deg_midi(tn, nt, d, v.lo, v.hi) });
        } else if (v.style == comp::CHORALE) {   /* nvoices consecutive chord tones from the lowest root in lo..hi, one velocity, through the pool voices */
            int pool[128], np = 0; for (int m = v.lo; m <= v.hi && np < 128; m++) for (int i = 0; i < nt; i++) if (((m - tn[i]) % 12 + 12) % 12 == 0) { pool[np++] = m; break; }
            const int nn = std::min(np, std::min(chorale_cap(q == 0 ? P.voice.synth : q == 1 ? P.sect.synth : P.v3.synth), std::max(2, std::min(6, (int)v.nvoices))));
            int b = 0; while (b < np && ((pool[b] - tn[0]) % 12 + 12) % 12) b++;
            b = std::max(0, std::min(b, np - nn));
            for (int i = 0; i < nn; i++) h.push_back(Hit{ t, dur, vel(col), (int)(deal++ % nv), pool[b + i] });
        }
    }
}
/* a melody voice's stream: chord 0's phrase over BARS bars from its start, the progression's chords a bar each (the cadence's pitch aside) */
static void melody_hits(const Patch &P, const comp::Voice &v, std::vector<Hit> &h) {
    const double six = 60.0 / P.tempo / 4;
    for (int b = 0; b < BARS; b++) {
        comp::Phrase ph; comp::motif_step(P.comp, b % std::max(1, P.nprog), ph);   /* each chord has its own chain step (final review M2) */
        const int nb = std::max(1, ph.len16 / 16);
        int tn[8]; const int nt = tones_of(P, b % std::max(1, P.nprog), tn), cb = b % nb;
        for (int i = 0; i < ph.n; i++) {
            const comp::MNote &x = ph.note[i]; if (x.start / 16 != cb) continue;
            const int m = P.comp.motif.basis == comp::SCALE_DEG ? scale_midi(P, x.deg, x.oct, v.lo, v.hi) : deg_midi(tn, nt, x.deg, v.lo, v.hi, x.oct);
            h.push_back(Hit{ (b * 16 + x.start - cb * 16) * six, std::min((int)x.len, cb * 16 + 16 - x.start) * six * v.len, 0.22 + 0.4 * x.acc / 100.0, 0, m });
        }
    }
}
/* composition 4, ornaments (Piece::ornament): the notes a voice's orn adds, as it plays them - a grace before the note (the scale tone
   above, 1/32 / speed), a trill's alternation, a mordent's three; all inside the note and before the voice's next step, one per note
   (every orn_every-th or by the seeded chance). A slide adds no note. The tone above is two semitones here (its power is the pitch's) */
/* a trill's steps (np fit in the room): odd, so it ends on the main note, capped by what the voice's envelope can schedule (Param::CAP is
   40 events, ~7 an attack): 17 - sixteen alternations and the main note held - when the pitch steps on a freq Param under one attack, else 5
   re-attacks; under 3 is no trill. has_freq: the synths whose glide_param() is a freq Param */
static bool has_freq(int t) { return t == FM || t == AM || t == MONO || t == SIMPLE || t == METAL || t == WAVETABLE; }
static int trill_steps(int np, bool freq) { int s = std::min(np, freq ? 17 : 5); if (!(s & 1)) s--; return s < 3 ? 0 : s; }
static void orn_hits(const Patch &P, const comp::Voice &v, int type, int q, bool mel, std::vector<Hit> &h) {
    if (v.orn == comp::ORN_NONE || v.orn == comp::ORN_SLIDE) return;
    if (!mel && (v.style == comp::STRUM || v.style == comp::SWELLS || v.style == comp::CHORALE)) return;
    std::sort(h.begin(), h.end(), [](const Hit &a, const Hit &b) { return a.t < b.t; });
    std::vector<Hit> o; o.reserve(h.size() * 2);
    const double six = 60.0 / P.tempo / 4, u = std::max(0.015, six / 2 / v.orn_speed); const int rate = std::max(1, (int)v.rate); int n = 0;
    for (size_t xi = 0; xi < h.size(); xi++) {
        const Hit &x = h[xi];
        const long long k = (long long)std::floor(x.t / six + 1e-6); ++n;
        /* pointillist plays no rate: its note may grow to 3 ornament steps if the gap to the next onset leaves them (Piece::ornament) */
        const double room = mel ? x.dur : v.style == comp::POINTILLIST ? (xi + 1 < h.size() ? std::min(std::max(x.dur, 3 * u), h[xi + 1].t - x.t - 0.016) : x.dur) : std::min(x.dur, (rate - k % rate) * six - 0.016);
        if (!(v.orn_every ? n % v.orn_every == 0 : comp::rand01(P.comp.seed ^ 0x0A4E, k, q, n) < v.orn_prob) || room < 2 * u) { o.push_back(x); continue; }
        auto put = [&](double t, double d, int m) { Hit y = x; y.t = t; y.dur = d; y.m = m; y.v |= ORN; o.push_back(y); };
        if (v.orn == comp::ORN_GRACE) { put(x.t, u, x.m + 2); put(x.t + u, room - u, x.m); }
        else if (v.orn == comp::ORN_TRILL) {
            const bool fq = has_freq(type); const int S = trill_steps((int)(room / u), fq);
            if (!S) o.push_back(x);
            else if (fq) { Hit y = x; y.dur = room; o.push_back(y); }   /* one attack, the pitch stepping under it */
            else for (int i = 0; i < S; i++) put(x.t + i * u, i == S - 1 ? room - (S - 1) * u : u, i & 1 ? x.m + 2 : x.m);
        }
        else if (room < 3 * u) o.push_back(x);
        else { put(x.t, u, x.m); put(x.t + u, u, x.m + 2); put(x.t + 2 * u, room - 2 * u, x.m); }
    }
    h.swap(o);
}
}  // namespace levels

/* the gains (above) for every layer of the patch's comp, into P.cgain */
static uint64_t fnv(uint64_t h, const void *p, size_t n) { const unsigned char *b = (const unsigned char *)p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ULL; return h; }
template <class T> static uint64_t fnv(uint64_t h, const T &x) { return fnv(h, &x, sizeof x); }   /* scalars only: no padding */
/* Task 5: what the solve misses of a synth's own level (core/levels.hpp, measured), as a factor on the gain. Taken after
   the memo, which holds the solve's own gains, so a regenerated table is never stale against it */
static double level_fix(int type, const comp::Voice &v, bool mel) {
    if (type < 0 || type > FORMANT) return 1;
    const int r = v.style == comp::CHORALE ? v.restrike : v.rate;   /* a chorale's rate is its restrike; tintinnabuli and pointillist play none of their own: one class */
    const int c = v.style == comp::TINTINNABULI || v.style == comp::POINTILLIST || r <= 1 ? 0 : r <= 3 ? 1 : r <= 6 ? 2 : 3;
    return std::pow(10.0, -(mel ? LEVEL_FIX_MELODY[type] : LEVEL_FIX[type][v.style][c]) / 20);
}
static void comp_gains(Patch &P, GainMemo *memo) {
    using namespace levels;
    for (auto &l : P.cgain) for (float &x : l) x = 1;
    for (auto &l : P.rgain) for (float &x : l) x = 0;
    for (float &x : P.bgain) x = 0;
    if (!P.comp.on) return;
    bool want[17] = {};   /* the walking bass's wrates in force, on any layer */
    const int types[3] = { P.voice.synth, P.sect.synth, P.v3.synth };
    const double harm[3] = { P.voice.harm, P.sect.harm, P.v3.harm }, index[3] = { P.voice.index, P.sect.index, P.v3.index };
    comp::Resolved r0; comp::resolve(P.comp, -1, 0, -1, 0, nullptr, 0, nullptr, 0, r0);   /* the route's own voices (layer 0) */
    for (int q = 0; q < comp::NVOICE; q++) {
        if (is_sampler(types[q])) continue;
        /* the voice as Piece builds it: its settings, its two numbers, its envelope */
        std::unique_ptr<Synth> sy(new_synth(types[q])); sy->init(48000);
        sy->set(Settings{ harm[q], index[q], ROLE_OSC[q][0], ROLE_OSC[q][1], ROLE_ENV[q], ROLE_MENV[q] });
        sy->timbre(0, harm[q], 0, 0); sy->timbre(1, index[q], 0, 0);
        if (types[q] >= WAVETABLE && types[q] <= FORMANT) static_cast<Followed *>(sy.get())->follow(1);
        sy->shape(P.env_on[q] ? &P.env[q] : nullptr);
        const Envelope *a = sy->amp();
        const EnvSpec own = a ? EnvSpec{ a->attack, a->decay, a->sustain, a->release, a->attack_exp, a->release_exp } : EnvSpec{ 0.01, 0.1, 0.5, 1, false, true };
        const PluckSynth *pk = types[q] == PLUCK ? static_cast<const PluckSynth *>(sy.get()) : nullptr;
        const double six = 60.0 / P.tempo / 4;
        /* what decides this voice's synth (its role, type, two numbers, envelope), the solve's lengths and step, the tempo */
        uint64_t ks = fnv(fnv(fnv(fnv(fnv(fnv(0xcbf29ce484222325ULL, q), types[q]), harm[q]), index[q]), P.env_on[q]), six);
        ks = fnv(fnv(fnv(fnv(fnv(fnv(fnv(fnv(fnv(ks, P.env[q].a), P.env[q].d), P.env[q].s), P.env[q].r), P.env[q].aexp), P.env[q].rexp), BARS), FREE_BARS), STEP);
        std::vector<Hit> fh; fh.reserve(MAXH); free_hits(P, q, pk, fh);
        const uint64_t kf = fnv(ks, fh.data(), fh.size() * sizeof(Hit));
        double pfree;
        if (!memo || !memo->get(kf, pfree)) {
            pfree = pk ? pluck_power(pk, fh, 16 * six, 16 * FREE_BARS * six, 1) : env_power(own, fh, 1, 16 * six, 16 * FREE_BARS * six, sy.get(), 1, false);
            if (memo) memo->put(kf, pfree);
        }
        /* the gain that gives these notes (dealt to nv voices, through this envelope) Free's power; what decides it (the synth's
           key kf, the envelope, the notes as bytes) is the memo's key. note() holds an envelope's velocity to 1: a boosted style
           is solved for with that in (a few steps) */
        auto gain_of = [&](std::vector<Hit> &h, int nv, const EnvSpec &es, std::vector<Hit> *ref = nullptr) {   /* ref: the notes whose power (unclamped, at 1) is the target instead of Free's */
            uint64_t kg = fnv(fnv(fnv(fnv(fnv(fnv(fnv(fnv(kf, nv), es.a), es.d), es.s), es.r), es.aexp), es.rexp), h.data(), h.size() * sizeof(Hit));
            if (ref) kg = fnv(fnv(kg, 1), ref->data(), ref->size() * sizeof(Hit));
            double mg; if (memo && memo->get(kg, mg)) return mg;
            const double from = 16 * six, to = 16 * BARS * six;
            auto power = [&](double g) { return pk ? pluck_power(pk, h, from, to, g) : env_power(es, h, nv, from, to, sy.get(), g, !ref); };
            double target = pfree; if (ref) target = pk ? pluck_power(pk, *ref, from, to, 1) : env_power(es, *ref, nv, from, to, sy.get(), 1, false);
            double g = 1, ps = power(1);
            for (int it = 0; it < 4 && ps > 1e-12; it++) {
                const double ng = std::max(0.05, std::min(4.0, g * std::sqrt(target / ps)));
                if (std::fabs(ng / g - 1) < 0.01) { g = ng; break; }
                g = ng; ps = power(g);
            }
            if (memo) memo->put(kg, g);
            return g;
        };
        /* each layer: the route's voice with that layer's settings over it (comp::resolve, one layer at a time) */
        comp::Voice seen[1 + comp::NCHORD + comp::NSECTION + comp::NZONE]; float got[1 + comp::NCHORD + comp::NSECTION + comp::NZONE]; int nseen = 0;
        for (int L = 0; L < 1 + comp::NCHORD + comp::NSECTION + comp::NZONE; L++) {
            comp::Resolved r; const comp::Over *o = nullptr;
            if (L == 0) comp::resolve(P.comp, -1, 0, -1, 0, nullptr, 0, nullptr, 0, r);
            else if (L <= comp::NCHORD) { o = &P.comp.chord[L - 1]; comp::resolve(P.comp, L - 1, comp::NCHORD, -1, 0, nullptr, 0, nullptr, 0, r); }
            else if (L <= comp::NCHORD + comp::NSECTION) { const int s = L - 1 - comp::NCHORD; o = &P.comp.section[s]; comp::resolve(P.comp, -1, 0, s, comp::NSECTION, nullptr, 0, nullptr, 0, r); }
            else { const int z = L - 1 - comp::NCHORD - comp::NSECTION; if (z >= P.comp.nzone) break; o = &P.comp.zone[z];
                const uint64_t id = o->zone; comp::resolve(P.comp, -1, 0, -1, 0, &id, 1, &id, 1, r); }
            if (q == 0 && r.bass.mode == comp::BASS_WALK && r.bass.wrate >= 1 && r.bass.wrate <= 16) want[r.bass.wrate] = true;
            if (o && (!o->used || !o->v[q].mask)) { P.cgain[L][q] = P.cgain[0][q]; continue; }
            const comp::Voice &v = r.v[q];
            const bool mel = v.role == comp::MELODY && P.comp.motif.n > 0;   /* a melody voice: its phrase, its style aside */
            if (!mel && (v.style == comp::FREE || v.role == comp::OFF || v.role == comp::MELODY)) {   /* a melody with no motif is Free */ P.cgain[L][q] = 1; continue; }
            int same = -1; for (int i = 0; i < nseen && same < 0; i++) if (!std::memcmp(&seen[i], &v, sizeof v)) same = i;   /* the same settings once worked out (padding that differs only works it out again) */
            if (same >= 0) { P.cgain[L][q] = got[same]; continue; }
            const comp::Voice *ld = !mel && v.style == comp::TINTINNABULI && v.tvoice != q ? &r0.v[v.tvoice] : nullptr;   /* tintinnabuli: the voice it answers, at the route's own settings */
            const bool lmel = ld && ld->role == comp::MELODY && P.comp.motif.n > 0;
            const bool lchord = ld && !lmel && (ld->style == comp::STRUM || ld->style == comp::SWELLS || ld->style == comp::CHORALE);   /* its answers go through the chord voices as the leader's chord does */
            const int nv = lchord || (!mel && (v.style == comp::STRUM || v.style == comp::SWELLS || v.style == comp::CHORALE)) ? Role::pool_n(types[q]) + 1 : 1;
            const double dur = six * v.rate * v.len;
            const EnvSpec es = !mel && v.style == comp::SWELLS ? EnvSpec{ std::max(0.005, v.attack * dur), 0.005, 1.0, std::max(0.02, v.release * dur), own.aexp, own.rexp } : own;
            std::vector<Hit> h; h.reserve(MAXH);
            if (ld) {   /* what the answered voice plays, each note to the nearest triad tone (about two semitones) above or below, in this voice's register */
                if (ld->role == comp::OFF) {}
                else if (lmel) melody_hits(P, *ld, h);
                else if (ld->style == comp::FREE || ld->role == comp::MELODY) free_hits(P, v.tvoice, false, h);
                else style_hits(P, *ld, nv, h, v.tvoice);
                for (size_t i = 0; i < h.size(); i++) h[i].m = fold(h[i].m + (v.tmode == comp::TT_ABOVE ? 2 : v.tmode == comp::TT_ALT ? (i & 1 ? 2 : -2) : -2), v.lo, v.hi);
            }
            else if (mel) melody_hits(P, v, h); else style_hits(P, v, nv, h, q);
            if (!ld && !pk) orn_hits(P, v, types[q], q, mel, h);   /* pluck: an ornamented line measures as loud as the plain (its burst is a note's own, whatever the pitch moves) */
            P.cgain[L][q] = (float)(gain_of(h, nv, es) * level_fix(types[q], v, mel));
            if (nseen < (int)(sizeof got / sizeof got[0])) { seen[nseen] = v; got[nseen++] = P.cgain[L][q]; }
        }
        /* composition 4, the walking bass: the voice's own synth (the bass is its role's), each walked note at 0.5 as the held root is
           struck - the bass takes no style's gain - so the walk is as loud as hold: per wrate, the gain that gives a bar of walked
           notes the power of the bar's held root (struck on every chord change; each bar on a percussive synth) */
        for (int wr = 1; q == 0 && wr <= 16; wr++) if (want[wr]) {
            std::vector<Hit> wk, hd; const int lo = P.key - 24, hi = P.key - 5, N = (16 + wr - 1) / wr; static const int OFS[4] = { 0, 2, 4, 2 }; const double bv = pk ? 1 : 0.5;   /* pluck: a note's level is its accent */
            for (int b = 0; b < BARS; b++) {
                const int rt = fold(root_of(P, b % std::max(1, P.nprog)), lo, hi);
                for (int j = 0; j < N; j++) wk.push_back(Hit{ (b * 16 + j * wr) * six, std::max(0.05, 0.9 * wr * six), bv, 0, fold(rt + OFS[j % 4], lo, hi) });
                if (b == 0 || P.nprog > 1 || PERCUSSIVE[types[0]]) hd.push_back(Hit{ b * 16 * six, 16 * six, bv, 0, rt });
            }
            if (P.nprog <= 1 && !PERCUSSIVE[types[0]]) hd[0].dur = 16 * BARS * six;
            P.bgain[wr] = (float)(gain_of(wk, 1, own, &hd) * std::pow(10.0, -LEVEL_FIX_BASS[types[0]] / 20));
        }
        /* composition 3: what a relation gives this voice to play - a canon's or heterophony's follower the leader's notes, a hocket
           member (a, b, c) its share of a's line - at Free's level as the leader's route-level style gives them (a leader that
           has no notes of its own - Free, off - gives none: the voice keeps its own gain). The notes are folded into this
           voice's register, run through its own envelope and gain as the style's own are, and the table is the leader's
           style's (the style the material imitates). A chord style's notes go through the follower's chord voices.
           ponytail: the leader's route-level voice - a chord or zone that overrides the leader is not followed */
        for (int r = 0; r < P.comp.nrel; r++) {
            const comp::Relation &c = P.comp.rel[r];
            const bool hk = c.type == comp::R_HOCKET;
            if ((!hk && c.type != comp::R_CANON && c.type != comp::R_HETERO) || c.a >= comp::NVOICE || c.b >= comp::NVOICE || c.a == c.b) continue;
            const bool c3 = hk && c.c < comp::NVOICE && c.c != c.a && c.c != c.b;
            const int mi = q == c.b ? 1 : !hk ? -1 : q == c.a ? 0 : c3 && q == c.c ? 2 : -1;   /* the member's place in the deal */
            if (mi < 0) continue;
            const comp::Voice &lv = r0.v[c.a];
            const bool mel = lv.role == comp::MELODY && P.comp.motif.n > 0;
            if (!mel && (lv.style == comp::FREE || lv.role == comp::OFF || lv.role == comp::MELODY)) continue;
            const bool chord = !mel && (lv.style == comp::STRUM || lv.style == comp::SWELLS || lv.style == comp::CHORALE);
            const int nv = chord ? Role::pool_n(types[q]) + 1 : 1;
            std::vector<Hit> h; h.reserve(MAXH); if (mel) melody_hits(P, lv, h); else style_hits(P, lv, nv, h, c.a);
            if (hk) {   /* the notes this member keeps: the deal of own_note (the line's n-th note, or the mask's bit at its step) */
                const int nm = c3 ? 3 : 2; size_t n = 0, o = 0;
                for (const Hit &x : h) {
                    const int m = c.split == comp::S_MASK ? ((c.mask >> ((int)std::floor(x.t / six + 1e-6) % 16)) & 1 ? 0 : 1)
                                : c.split == comp::S_EVERY ? (int)((n / std::max(1, (int)c.every)) % nm) : (int)(n % nm);
                    n++; if (m == mi) h[o++] = x;
                }
                h.resize(o);
            }
            if (h.empty()) continue;
            const comp::Voice &mv = r0.v[q];
            for (Hit &x : h) x.m = fold(x.m, mv.lo, mv.hi);
            P.rgain[r][q] = (float)(gain_of(h, nv, own) * level_fix(types[q], lv, mel));
        }
    }
}

static const char *const NOTE[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static int pc12(int m) { return ((m % 12) + 12) % 12; }

struct Piece : Device {
    double sr = 48000;
    long long frame = 0;
    Rng rnd, main_rnd;               /* the audio thread's stream; the calling thread's (rhythmOf) */
    std::mutex mu;
    Inbox in;
    /* calling-thread view */
    bool busy[FS_MAX_VOICES] = {};
    GainMemo gmemo;                   /* Task 4: the gains its reads worked out */

    /* audio-thread state */
    Patch patch;                      /* pacer.patch */
    std::vector<Patch> routes;        /* copied from the inbox as they arrive */
    int route = -1, pending = -1, reported = -1; double pending_at = 0, t_along = 0, route_dist = 1e300;
    int sector = -1;
    /* composition (step 1): the settings in force, gliding a bar at a time; zones; per-voice state */
    comp::Voice ccur[comp::NVOICE]; comp::Bass cbass; bool comp_live = false;
    int wk_at[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* composition 4: the walking bass - this bar's pitch at each sixteenth, -1 none */
    int wk_l1 = -1, wk_l2 = -1, cdir = 0; double cwt = -1;   /* its last two pitches (a run of one pitch is cut at 2); the walker's direction at the bar line, t_along then */
    uint64_t zin[16] = {}; int nzin = 0; struct Ent { uint64_t id; long long bar; } zent[8]; int nzent = 0;
    int cpulse[comp::NVOICE] = {}, calt[comp::NVOICE] = {}, cped_root[comp::NVOICE] = {-1, -1, -1}; bool swell_shaped[comp::NVOICE] = {};
    int ost_n0[comp::NVOICE] = {}, ost_t0[comp::NVOICE][8] = {};          /* ostinato: the first chord's tones the cell was set on (transpose refit) */
    int cho_n[comp::NVOICE] = {}, cho_prev[comp::NVOICE][6] = {};         /* chorale: the voicing last sounded, ascending (voice leading) */
    long long pt_next[comp::NVOICE] = { -1, -1, -1 }; int pt_n[comp::NVOICE] = {}, tcnt[comp::NVOICE] = {};   /* pointillist: the tick of the next onset, onsets so far; tintinnabuli: notes answered (alt) */
    double cg[comp::NVOICE] = { 1, 1, 1 };   /* each voice's style gain now (comp_bar glides it to its layer's) */
    bool styled(int q) const { return patch.comp.on && comp_live && ((ccur[q].style != comp::FREE && !bare(q)) || ccur[q].role == comp::OFF || melodic(q)); }
    /* composition 2: a melody voice plays the motif (its style aside); the phrase in force this bar; a cadence's last chord on
       the other voices (OTH_HOLD: none), and each voice's notes so far this bar (`thin` keeps a fast voice's first) */
    comp::Phrase mel; int cad_oth = comp::OTH_HOLD, nbar[comp::NVOICE] = {};
    /* "bars": develop - the bars since this chord began (cj), and the melody's bar when it is a variant: its notes (starts within the
       bar) and the last four bars' lists, so no bar repeats one of them */
    int cchord = -2; long long cbar0 = 0, cj = 0;
    struct MBar { int n = 0; comp::MNote note[comp::MOTIF_MAX * 2]; };
    MBar mvar, mhist[4]; bool mvar_on = false; int nhist = 0;
    /* composition 3: each voice's last 256 own notes (not a relation's; 33 sixteenths of a 4-tone strum at 16n is 132), the
       relations in force this step, and per relation the first and last step it was in force (-1: never) - a canon's leader
       ticks are the ones between them. deg: the chord degree played (octave kept; a ninth of a 4-tone chord is 4); off: seconds
       past the step (a strum's roll). A canon's leader that itself follows a canon (a chain) also records what it plays */
    static const int RING = 256;
    struct NR { long long tick = -1; int midi = 0, deg = 0; float len16 = 0, vel = 0, off = 0, cents = 0; };   /* cents: the microtonal offset the note sounded with - a replay keeps it */
    NR ring[comp::NVOICE][RING]; int rhead[comp::NVOICE] = {}; double ctime = 0;
    struct Span { long long a = -1, b = -1; } sp[comp::REL_MAX]; long long ck = -1; bool relmade = false;
    int ract[comp::REL_MAX], nract = 0, hn = 0, mn = 0;
    int chg = 0, ufirst[comp::NVOICE] = { -1, -1, -1 };   /* chord changes so far; the change each voice last sounded a note in (unison: its first note after one) */
    bool cad_chord = false;                               /* this chord is covered by a chain cadence step (its first bar: cj == 0) */
    int hcnt[comp::REL_MAX] = {};                     /* hocket: line notes dealt so far in the relation's span */
    int ownd = -1;                                    /* density's verdict on the voice's own note, taken in own_note before its doubles (-1: not taken) */
    int ocnt[comp::NVOICE] = {}; double oend[comp::NVOICE] = { -1e9, -1e9, -1e9 };   /* ornaments: composed notes so far; the end of a voice's last one (a glide is not carried over a rest) */
    int dcnt[comp::NVOICE] = {};                      /* density: notes each voice has sounded this bar (a strum's tones each count) */
    int ccall[comp::NVOICE] = { -1, -1, -1 };         /* the call (rel index) a voice answers this step, -1: none */
    int dj() const { return patch.comp.develop ? (int)std::min<long long>(cj, 1 << 20) : 0; }
    int mel_step = -2; long long mel_bar0 = 0;   /* the chain step in force, and the bar it began on: the phrase counts from there */
    bool bare(int q) const { return ccur[q].role == comp::MELODY && patch.comp.motif.n == 0; }   /* a melody with no motif plays as Free, whatever style it holds */
    bool chord_style(int q) const { return !melodic(q) && (ccur[q].style == comp::STRUM || ccur[q].style == comp::SWELLS || ccur[q].style == comp::CHORALE); }   /* its notes go through chord voices: no ornaments, no glide */
    bool melodic(int q) const { return ccur[q].role == comp::MELODY && patch.comp.motif.n > 0; }
    bool cad_rest(int q, bool fast) const {
        if (cad_oth == comp::OTH_HOLD || melodic(q)) return false;
        return cad_oth == comp::OTH_STOP || (fast && nbar[q] > 0);
    }
    double centroid = 2000, onsets = 1;
    double bpm = 72, next_tick = 0; long long ticks = 0;
    double tick_base_t = 0; long long tick_base_k = 0; double tick_bpm = 72;   /* ticks from an index, as Tone's tick->seconds: no drift */
    std::string sect_div = "4n.";
    struct { int idx = -1, chord = -1; bool held = false; bool have_bass = false, have_top = false; int bass = 0, top = 0, topDir = 0; int tones[8]; int ntones = -1; } H;
    struct { bool have_last = false, have_prev2 = false; int last = 0, prev2 = 0, restRun = 0, prevInt = 0, phrase = 0; long tick = 0; } C;
    struct { bool have_last = false; int last = 0; long tick = 0; } T3;

    Role bass, top, sectr, v3r;
    /* the role recordings (3a): the host's, normalised, owned here for the device's life (ponytail: never freed before
       the device - a replaced recording may still be read by a sounding note; a lab uses a few). known: the calling
       thread's latest, bound to sampler instances made there; live: what the audio thread has taken from the inbox. */
    RoleRec known[3], live[3];
    struct Keep { std::vector<float *> b; ~Keep() { for (float *q : b) std::free(q); } } keep;
    /* 6b final review I2: a voice's replaced buffers, freed once the audio thread has taken what replaced them (take_inbox
       rebinds every instrument of the voice at once, so nothing reads them after) - the apps send one each route change */
    std::vector<float *> retire[3];
    static void bind(Synth *s, const RoleRec &k) {
        auto *r = static_cast<sampler::Resonator *>(s);
        if (k.pcm) { const float *pp[1] = { k.pcm }; r->set_source(1, k.n, pp); }
        r->set_track_view(k.f0, k.hop, k.tf, k.tc, k.tn);
        r->spec = k.spec; r->spec_n = k.spec_n;   /* 3c.1 F2: notes into the recording's energy (kept with the recording) */
    }
    void bind_role(Role &r, const RoleRec &k) { for (int t = S_RETUNE; t < NSYNTH; t++) if (r.inst[t]) bind(r.inst[t].get(), k); }
    Ctl bass_g, top_g;
    Layer pad, sectL, v3L;
    Eq5 eqs[3];                       /* 6 (lab): each role's EQ, after its layer, before its effects */
    double lvl[3] = {};               /* 3c.1 F4: each role's own output, ~0.1 s mean square (voice, sect, v3) */
    /* 6c: a digital role's notes as played (bass, top, sect, v3; 8 each, the oldest replaced) - for the live row, as a
       sampler names its sounding notes. A held note (dur < 0) lasts until the role's next held note */
    struct Heard { double f = 0, from = 0, until = -1; bool held = false; } heard[4][8];
    FxChain fx, fx2, fx3, rfx;
    Layer rdrive;                     /* the rhythm effects' drive and lowpass (no warp) */
    Ctl synth_level;
    Ctl route_solo;                   /* 0 while a rhythm point is soloed: the route's synths and zones rest */
    int solo = -1;
    float route_gate = 1;             /* fs_piece_route_gate: muted, or soloed away, or "points only" */
    struct OneShot { SimpleSynth s; NoiseSynth ns; bool noise = false; double free_at = 0; } shots[6];
    Rhythm rh[FS_MAX_VOICES];
    std::vector<short *> my_trash;
    float oL[SUB], oR[SUB], rL[SUB], rR[SUB];

    /* hooks for the parity test: every note the steps play */
    void (*on_note)(void *, int role, double f, double dur, double t, double vel) = nullptr; void *note_ctx = nullptr;
    double test_walk_s = 0;           /* tests: t along the route = time / this, as the reference harness walks */
    int default_tuning = harmony::EQUAL;    /* fs_piece_default_tuning */
    /* A note under the chord playing (the harmony core): today's mtof in equal temperament. */
    double note_hz(int m) {
        int tu = patch.tuning >= 0 ? patch.tuning : default_tuning;
        if (tu != harmony::JUST) return harmony::mtof(m);      /* equal: today's mtof, touching nothing else */
        /* the chord playing; before the bar has set one (a route swap), where the walk is - read only:
           chord_index would move the chord's own hysteresis (H.idx) ahead of harmony_bar */
        int step = H.chord >= 0 ? H.chord : (H.idx >= 0 ? H.idx : std::max(0, std::min(patch.nprog - 1, (int)std::floor(t_along * patch.nprog))));
        return harmony::hz(tu, m, chord_root(step), dom_q(chord_quality(step)));
    }

    void *cast(const char *kind) override { return std::strcmp(kind, "piece") ? nullptr : this; }
    fs_param dummy[1] = { { "none", "none", "", 0, 1, 0 } };
    const fs_param *params(int &n) override { n = 0; return dummy; }
    void set_param(int, float) override {}

    /* ---- building (calling thread) ---- */
    Synth *make(int type) {
        Synth *s;
        if (is_sampler(type)) {           /* 3a: the sampler synths, 6 voices a role */
            static const int MAP[8] = { sampler::RETUNE, sampler::RESONATE, sampler::HARMONIC, sampler::FORMANT, sampler::PULSAR, sampler::FREEZE, sampler::SFM, sampler::SAM };
            auto *r = new sampler::Resonator(); r->synth = MAP[type - S_RETUNE]; r->nv = 6;
            r->init(sr);
            return r;
        }
        s = new_synth(type);
        if (type == PLUCK) { auto *p = static_cast<PluckSynth *>(s); p->rnd = Rng::call; p->rnd_ctx = &rnd; }
        if (type == COMB) { auto *c = static_cast<CombVoice *>(s); c->rnd = Rng::call; c->rnd_ctx = &rnd; }
        s->init(sr);
        return s;
    }
    void need(Role &r, int type, bool chords = false) {
        if (!r.inst[type]) {
            r.inst[type].reset(make(type));
            if (is_sampler(type) && r.rec >= 0) bind(r.inst[type].get(), known[r.rec]);
        }
        if (chords && !is_sampler(type)) for (int j = 0; j < Role::pool_n(type); j++) if (!r.pool[type][j]) r.pool[type][j].reset(make(type));   /* C1: a composed route's chords */
    }
    void need_all(const Patch &p) {
        const bool c = p.comp.on;
        need(bass, p.voice.synth); need(top, p.voice.synth, c); need(sectr, p.sect.synth, c); need(v3r, p.v3.synth, c);
    }

    static Settings role_settings(double harm, double index, int osc, int mod, EnvSpec env, EnvSpec menv) { return Settings{ harm, index, osc, mod, env, menv }; }
    void settings_for(const Patch &p) {
        bass.st = role_settings(p.voice.harm, p.voice.index, SINE, TRIANGLE, { 0.06, 0.5, 0.55, 2.4, false, true }, { 1.8, 2.4, 0.5, 3, false, true });
        top.st = role_settings(p.voice.harm, p.voice.index, ROLE_OSC[0][0], ROLE_OSC[0][1], ROLE_ENV[0], ROLE_MENV[0]);
        sectr.st = role_settings(p.sect.harm, p.sect.index, ROLE_OSC[1][0], ROLE_OSC[1][1], ROLE_ENV[1], ROLE_MENV[1]);
        v3r.st = role_settings(p.v3.harm, p.v3.index, ROLE_OSC[2][0], ROLE_OSC[2][1], ROLE_ENV[2], ROLE_MENV[2]);
    }
    /* 8b: the role's voice envelope onto what plays it (rec is the voice: 0 the Voice's bass and top, 1 sect, 2 v3) */
    void shape_role(Role &r) {
        if (r.rec < 0 || r.rec > 2) return;
        r.each([&](Synth *s) { s->shape(patch.env_on[r.rec] ? &patch.env[r.rec] : nullptr); });
    }
    /* swapSynth */
    void swap(Role &r, int type, double now) {
        if (r.cur == type && r.cur >= 0) return;
        if (r.cur >= 0) {
            if (is_sampler(r.cur)) static_cast<sampler::Resonator *>(r.inst[r.cur].get())->release_all(now);   /* every voice (3a I1) */
            else r.inst[r.cur]->release(now);
            r.until[r.cur] = now + 2.5;
            for (int j = 0; j < Role::NPOOL; j++) if (r.busy[r.cur][j] > now) { r.pool[r.cur][j]->release(now); r.busy[r.cur][j] = now + 2.5; }
        }
        for (bool &y : r.synced[type]) y = false;      /* C1: its pool set up afresh, as inst is here, at its next chord */
        Synth *s = r.inst[type].get();
        if (!s) return;
        s->set(r.st);              /* a SELF_VOICED synth reads only the two numbers */
        shape_role(r);             /* 8b */
        /* Kerem 2026-10-06: "Second voice should be louder in general for samplers" - on its short sector notes a sampler sat
           3.4-6.2 dB under the same sampler on the Voice (measured); +7 dB puts every one at or above it */
        const double smp_db = is_sampler(type) && &r == &sectr ? 7 : 0;
        s->vol = (float)db_to_gain(SYNTH_TRIM[type] + r.extra_db + smp_db);
        r.cur = type;
        r.until[type] = 0;
    }

    void prepare(float s, int) override {
        sr = s;
        basic_wave(SINE); custom_wave(0); pink_noise(0);
        bass.rec = top.rec = 0; sectr.rec = 1; v3r.rec = 2;
        std::unique_ptr<Patch> dp(new Patch); Patch &d = *dp; patch_of(nullptr, d);
        patch = d;
        need_all(d);
        for (auto &o : shots) { o.s.init(sr); o.ns.init(sr); o.ns.rnd = Rng::call; o.ns.rnd_ctx = &rnd; }
        build_voice(d);
        routes.reserve(64); my_trash.reserve(64);
        transport_start();
        main_rnd.s = 0x2545F4914F6CDD1DULL;
    }

    /* buildVoice + buildRhythmFx, from a patch */
    void build_voice(const Patch &p) {
        settings_for(p);
        sectr.extra_db = 3;
        fx.init(sr, p.fx, p.tempo, 170); fx2.init(sr, p.fx2, p.tempo, 170); fx3.init(sr, p.fx3, p.tempo, 170);
        pad.init(sr, 2.6, 0.62, 0.55, 5.5, 0.6, 1200, 1, p.voice.gain);
        sectL.init(sr, 3.2, 0.58, 1.7, 3, 0.7, 9000, p.sect.gain, 0.78);
        v3L.init(sr, 2.8, 0.6, 0.35, 7, 0.5, 7000, 1, p.v3.gain);
        for (auto &e : eqs) e.init(sr);
        bass_g.init(0.9); top_g.init(0.8);
        swap(bass, p.voice.synth, 0); swap(top, p.voice.synth, 0); swap(sectr, p.sect.synth, 0); swap(v3r, p.v3.synth, 0);
        /* buildRhythmFx */
        FxCfg rc{ p.rfx.delayDiv, p.rfx.delayWet, p.rfx.delayFb, p.rfx.revWet, p.rfx.revDecay };
        rfx.init(sr, rc, p.tempo, 220);
        rdrive.init(sr, 3, 0.6, 1, 0, 0, p.rfx.cutoff, 1, 1);
        rdrive.q = 1.1;
        apply_rhythm_fx(p, 0);
        synth_level.init(0);          /* silent until a walk says a route is near (world mode starts with none) */
        route_solo.init(1);
        bpm = p.tempo;
        for (auto &r : rh) { r.gain.init(0); init_hits(r, true); }
    }
    void init_hits(Rhythm &r, bool alloc) {
        for (auto &h : r.hit) {
            h.stretch_gain.init(1); h.blend.init(0);
            h.crush_fade.init(0); h.bits.init(16); h.drive_fade.init(0); h.pre.init(1); h.post.init(0.6); h.delay_fade.init(0);
            if (alloc) { h.dl.init(sr, MAX_DELAY_S, 0.2, 0); h.shp.init(3); }
            else { h.dl.time.init(0.2); h.dl.fb.init(0); for (auto &d : h.dl.dl) std::fill(d.buf.begin(), d.buf.end(), 0.0f); }
        }
    }

    /* applyRhythmFx */
    void apply_rhythm_fx(const Patch &p, double now) {
        const RfxCfg &c = p.rfx;
        rdrive.cutoff.p.exponentialRampTo(c.cutoff, 0.2, now);
        rdrive.fade.p.linearRampTo(c.drive, 0.2, now);
        rdrive.pre.p.linearRampTo(1 + c.drive * 12, 0.2, now);
        rdrive.post.p.linearRampTo(0.6 / (1 + c.drive * 3.4), 0.2, now);
        rfx.dl.time.p.linearRampTo(std::min(MAX_DELAY_S - 0.01, div_seconds(c.delayDiv, p.tempo)), 0.2, now);
        rfx.dl.fb.p.linearRampTo(c.delayFb, 0.2, now);
        rfx.send.p.linearRampTo(c.delayWet, 0.2, now);
        rfx.rv.wet.p.linearRampTo(c.revWet, 0.2, now);
    }
        /* applyHitFx */
    void apply_hit_fx(Rhythm &R, double now) {
        for (int s = 0; s < 4; s++) {
            const HitCfg &c = R.cfg.v[s]; HitSlot &h = R.hit[s];
            h.crush_fade.p.linearRampTo(c.crush, 0.2, now);
            /* Test 15: bits at the hit's own scale (see render), heard across the knob - 16 - 14 x crush changed nothing
               audible below 0.75; the drive's make-up keeps a driven hit as loud as it was rather than 17 dB down */
            h.bits.p.linearRampTo(std::round(2 + 10 * std::pow(1 - std::max(0.0, std::min(1.0, c.crush)), 1.5)), 0.2, now);
            h.drive_fade.p.linearRampTo(c.drive, 0.2, now);
            h.pre.p.linearRampTo(1 + c.drive * 12, 0.2, now);
            h.post.p.linearRampTo(1 / (1 + c.drive * 2), 0.2, now);
            h.dl.time.p.linearRampTo(std::min(MAX_DELAY_S - 0.01, div_seconds(c.delayDiv, patch.tempo)), 0.2, now);
            h.dl.fb.p.linearRampTo(c.delayFb, 0.2, now);
            h.delay_fade.p.linearRampTo(c.delayWet, 0.2, now);
        }
    }

    /* applyPatchToVoice */
    void apply_patch(double now) {
        const Patch &p = patch;
        bpm = p.tempo;
        pad.g2.p.linearRampTo(p.voice.on ? p.voice.gain : 0, 0.2, now);
        bass_g.p.linearRampTo(p.voice.bass, 0.2, now);
        top_g.p.linearRampTo(p.voice.top, 0.2, now);
        sectL.g1.p.linearRampTo(p.sect.on ? p.sect.gain : 0, 0.2, now);
        fx.apply(p.fx, p.tempo, now);
        v3L.g2.p.linearRampTo(p.v3.on ? p.v3.gain : 0, 0.2, now);
        fx3.apply(p.fx3, p.tempo, now);
        for (int q = 0; q < 3; q++) for (int k = 0; k < 5; k++) eqs[q].set(k, p.eq[q][k][0], p.eq[q][k][1], p.eq[q][k][2]);
        /* applySynths */
        settings_for(p);
        if (bass.cur != p.voice.synth) { swap(bass, p.voice.synth, now); swap(top, p.voice.synth, now); H.held = false; }
        if (sectr.cur != p.sect.synth) swap(sectr, p.sect.synth, now);
        if (v3r.cur != p.v3.synth) swap(v3r, p.v3.synth, now);
        for (Role *r : { &bass, &top, &sectr, &v3r }) shape_role(*r);   /* 8b: a changed envelope on the same instrument */
        fx2.apply(p.fx2, p.tempo, now);
        if (sect_div != p.sect.rhythm.s) { sect_div = p.sect.rhythm.s; C.tick = 0; }
        apply_rhythm_fx(p, now);
        for (auto &R : rh) if (R.used) apply_hit_fx(R, now);
    }

    /* ---- the chord model ---- */
    int chord_index(double t) {
        int n = patch.nprog;
        double raw = t * n;
        if (H.idx < 0) H.idx = std::max(0, std::min(n - 1, (int)std::floor(raw)));
        else if (raw > H.idx + 1.25 || raw < H.idx - 0.25) H.idx = std::max(0, std::min(n - 1, (int)std::floor(raw)));
        return H.idx;
    }
    int chord_root(int step) { return root_of(patch, step); }
    int chord_quality(int step) { return quality_of(patch, step); }
    int chord_tones(int step, int *out) {
        int q = chord_quality(step);
        if (q < 0) q = Q_M7;
        int root = chord_root(step);
        for (int i = 0; i < CHORDS[q].n; i++) out[i] = root + CHORDS[q].iv[i];
        return CHORDS[q].n;
    }
    static int lead_to(bool have_prev, int prev, const int *tones, int nt, int lo, int hi) {
        bool found = false; int bm = 0; double bd = 0;
        for (int k = 0; k < nt; k++)
            for (int oct = -3; oct <= 3; oct++) {
                int m = tones[k] + oct * 12;
                if (m < lo || m > hi) continue;
                double d = !have_prev ? std::fabs(m - (lo + hi) / 2.0) : std::abs(m - prev);
                if (!found || d < bd) { found = true; bm = m; bd = d; }
            }
        return found ? bm : tones[0];
    }
    static int pc(int m) { return ((m % 12) + 12) % 12; }
    static int sgn(int x) { return (x > 0) - (x < 0); }

    /* ---- morphs ---- */
    static double hash01(double n, double seed) { double x = std::sin(n * 127.1 + seed * 311.7) * 43758.5453; return x - std::floor(x); }
    static double morph_shape(int shape, double u, double seed) {
        double TAU = 2 * PI, p = u - std::floor(u);
        if (shape == SH_RAMP) return p;
        if (shape == SH_PULSE) { double s = std::sin(PI * p); return std::pow(s < 0 ? 0 : s, 12); }
        if (shape == SH_BREATH) { double w = p < 0.35 ? (p / 0.35) * 0.5 : 0.5 + ((p - 0.35) / 0.65) * 0.5; return 0.5 - 0.5 * std::cos(TAU * w); }
        if (shape == SH_TIDE) return 0.5 + (std::sin(TAU * u) + std::sin(TAU * u * 1.6180339887)) / 4;
        double x = u * 5; int i = (int)std::floor(x); double f = x - i;
        double a = hash01(i, seed), b = hash01(i + 1, seed);
        return a + (b - a) * (f * f * (3 - 2 * f));
    }
    bool morph(int dest, double t, double &out) {
        if (!patch.morph_on) return false;
        int n = 0; double sum = 0;
        for (int i = 0; i < patch.nmorph; i++) {
            const Morph &m = patch.morph[i];
            if (!m.on || m.dest != dest) continue;
            double ph = t / std::max(4.0, m.period ? m.period : 60) + m.phase;
            double v = m.bias + m.depth * morph_shape(m.shape, ph, m.seed);
            sum += v < 0 ? 0 : (v > 1 ? 1 : v); n++;
        }
        if (!n) return false;
        out = sum / n;
        return true;
    }
    /* applyTimbre */
    void timbre(Synth *sy, int type, double ph, int mh_dest, double pi, int mi_dest, double ramp, double t) {
        if (!sy) return;
        double m;
        const double h = morph(mh_dest, t, m) ? SYNTH_PARAMS[type][0].min + m * (SYNTH_PARAMS[type][0].max - SYNTH_PARAMS[type][0].min) : ph;
        const double x = morph(mi_dest, t, m) ? SYNTH_PARAMS[type][1].min + m * (SYNTH_PARAMS[type][1].max - SYNTH_PARAMS[type][1].min) : pi;
        auto set = [&](Synth *v) { v->timbre(0, h, ramp, t); v->timbre(1, x, ramp, t); };
        Role *r = sy == top.sy() ? &top : sy == sectr.sy() ? &sectr : sy == v3r.sy() ? &v3r : nullptr;
        if (r) r->each(set); else set(sy);      /* C1: the role's chord voices sound as it does */
    }
    /* a sampler role's Focus and Colour now: its own settings, or a morph aimed at them (as timbre() does each step) */
    void sampler_timbre(sampler::Resonator *rs, int q, const SamplerCfg &c, double t) {
        static const int HD[3] = { M_VOICE_HARM, M_SECT_HARM, M_V3_HARM }, ID[3] = { M_VOICE_INDEX, M_SECT_INDEX, M_V3_INDEX };
        double m; const int type = q == 0 ? patch.voice.synth : q == 1 ? patch.sect.synth : patch.v3.synth;
        rs->focus = morph(HD[q], t, m) ? SYNTH_PARAMS[type][0].min + m * (SYNTH_PARAMS[type][0].max - SYNTH_PARAMS[type][0].min) : c.focus;
        rs->colour = morph(ID[q], t, m) ? SYNTH_PARAMS[type][1].min + m * (SYNTH_PARAMS[type][1].max - SYNTH_PARAMS[type][1].min) : c.colour;
    }
    static bool same_cfg(const SamplerCfg &a, const SamplerCfg &b) {
        return a.body == b.body && a.excite == b.excite && a.method == b.method && a.mode == b.mode && a.focus == b.focus && a.colour == b.colour && a.tune == b.tune && a.octave == b.octave;
    }
    void revoice_changed(const SamplerCfg was[3], double t) {
        Role *rs[3][2] = { { &bass, &top }, { &sectr, nullptr }, { &v3r, nullptr } };
        for (int q = 0; q < 3; q++) {
            if (same_cfg(was[q], patch.smp[q])) continue;
            const SamplerCfg &c = patch.smp[q];
            for (Role *r : rs[q]) {
                if (!r || !is_sampler(r->cur) || !r->inst[r->cur]) continue;
                auto *s = static_cast<sampler::Resonator *>(r->inst[r->cur].get());
                s->body = c.body; s->excite = c.excite; s->method = c.method; s->mode = c.mode; s->tune = c.tune; s->octave = c.octave; sampler_timbre(s, q, c, t);
                s->revoice(t);
            }
        }
    }
    /* the chord degree of MIDI note m: the nearest chord tone, its octave kept (a 4-tone chord's ninth is degree 4) */
    int chord_deg(int m) const {
        const int nt = std::max(1, H.ntones); int best = 0, bd = 999;
        for (int d = 0; d < nt; d++) { const int x = std::abs(m - H.tones[d] - 12 * floor_div(m - H.tones[d] + 6, 12)); if (x < bd) { bd = x; best = d; } }
        return best + nt * floor_div(m - H.tones[best] + 6, 12);
    }
    /* the key's scale degree of m (the inverse of scale_midi, octave kept) */
    int scale_deg_of(int m) const {
        int q = quality_of(patch, 0); if (q < 0) q = Q_M7;
        const auto &md = MODES[CHORDS[q].iv[1] == 3 ? MODE_AEOLIAN : MODE_IONIAN];
        const int o = floor_div(m - patch.key, 12), r = m - patch.key - 12 * o;
        int best = 0; for (int i = 1; i < md.n; i++) if (std::abs(md.iv[i] - r) < std::abs(md.iv[best] - r)) best = i;
        return o * md.n + best;
    }
    void push_ring(int q, int m, double len16, double vel_unit, double off, double cents = 0) {
        NR &r = ring[q][rhead[q]++ % RING];
        r.tick = ck; r.midi = m; r.deg = chord_deg(m); r.len16 = (float)len16; r.vel = (float)vel_unit; r.off = (float)off; r.cents = (float)cents;
    }
    /* the pool of a chord's voices, set up as the role's main instrument is (once, at first use after a swap) */
    void sync_pool(Role &rl) {
        Synth *sy = rl.sy();
        if (sy && !is_sampler(rl.cur))
            for (int j = 0; j < Role::NPOOL; j++) if (rl.pool[rl.cur][j] && !rl.synced[rl.cur][j]) {
                Synth *p = rl.pool[rl.cur][j].get();
                p->set(rl.st); p->vol = sy->vol; p->shape(patch.env_on[rl.rec] ? &patch.env[rl.rec] : nullptr);
                rl.synced[rl.cur][j] = true;
            }
    }
    /* a voice's own note: into its ring (velocity as at unit gain), and doubled by each heterophony in force. A chord style's
       notes (strum, swells) are doubled through the follower's chord voices */
    /* a relation's material at the member's level (comp_gains: rgain), or the voice's own gain when none was worked out */
    double rg(int r, int q) const { return patch.rgain[r][q] > 0 ? patch.rgain[r][q] : cg[q]; }
    bool own_note(int q, double f0, double dur, double t, double &vel, double cents) {
        const double s16 = 60.0 / patch.tempo / 4;
        const int m = (int)std::lround(69 + 12 * std::log2(f0 / 440));
        const double f = cents != 0 ? f0 * std::pow(2.0, cents / 1200) : f0;   /* what sounds; the ring holds the note and its offset */
        push_ring(q, m, dur / s16, vel / std::max(1e-6, cg[q]), t - ctime, cents);
        if (dur <= 0) return false;   /* a held note has no length to double */
        const double vu = vel / std::max(1e-6, cg[q]);
        const bool chord = !melodic(q) && (ccur[q].style == comp::STRUM || ccur[q].style == comp::SWELLS || ccur[q].style == comp::CHORALE);
        /* hocket: this line note sounds on one member only - a keeps it, or it moves to b (or c) at the same time, pitch and
           level (the member's own gain); a member without an instrument leaves it with a, so the line never loses a note */
        for (int i = 0; i < nract; i++) {
            const comp::Relation &c = patch.comp.rel[ract[i]];
            if (c.type != comp::R_HOCKET || c.a != q || c.b == q) continue;
            const int nm = c.c < comp::NVOICE && c.c != q && c.c != c.b ? 3 : 2, n = hcnt[ract[i]]++;
            const int mi = c.split == comp::S_MASK ? ((c.mask >> (ck % 16)) & 1 ? 0 : 1)
                         : c.split == comp::S_EVERY ? (n / std::max(1, (int)c.every)) % nm : n % nm;
            const int tgt = mi == 0 ? q : mi == 1 ? c.b : c.c;
            Role &rb = tgt == 0 ? top : tgt == 1 ? sectr : v3r;
            if (tgt == q || !rb.sy()) { if (patch.rgain[ract[i]][q] > 0) vel = vu * patch.rgain[ract[i]][q]; break; }   /* a's share at a's level; a heterophony still doubles it */
            relmade = true;
            if (chord) sync_pool(rb);
            note(tgt + 1, chord ? chord_voice(rb, t + dur) : rb.sy(), f, dur, t, vu * rg(ract[i], tgt), true);
            relmade = false;
            return true;
        }
        /* the density rule sees a's own note before its doubles: a note it drops has no double */
        ownd = density_ok(q) ? 1 : 0;
        for (int i = 0; i < nract && ownd; i++) {
            const comp::Relation &c = patch.comp.rel[ract[i]];
            if (c.type != comp::R_HETERO || c.a != q) continue;
            const int b = c.b; Role &rb = b == 0 ? top : b == 1 ? sectr : v3r;
            const double v = vu * rg(ract[i], b);
            double to = t + c.offset_ms / 1000.0;
            relmade = true;
            if (c.ornament > 0 && comp::rand01(patch.comp.seed ^ 0x4E7E, ck, b, hn++) < c.ornament) {
                /* a grace: the scale tone above, 1/32 long (15 ms at least), ending exactly where the double starts - so a
                   one-voice synth is not cut. An offset shorter than the grace moves the double to t + grace instead */
                const double g = std::max(s16 / 2, 0.015);
                if (to - g < t) to = t + g;
                if (chord) sync_pool(rb);
                note(b + 1, chord ? chord_voice(rb, to) : rb.sy(), note_hz(scale_above(patch, m)), g, to - g, v, true);
            }
            if (chord) sync_pool(rb);
            note(b + 1, chord ? chord_voice(rb, to + dur) : rb.sy(), f, dur, to, v, true);
            relmade = false;
        }
        return false;
    }
    /* composition 3, pitch relations: before voice q's own note sounds (a relation's own notes - canon, hocket, call - are
       never moved; a chord style's strum and swells are left whole, a moved tone each would collapse the chord). Motion:
       contrary - the chord tone next above/below b's last pitch against a's last interval (b holds when a does not move or
       b is at its register's edge); par3 / par6 - the key's scale tone a third / sixth below a's current pitch. With chance
       `strength`, seeded apart from the piece's draws. Unison: at b's first note after a chord change (cadence: only on a
       cadence chord's first bar) b takes a's pitch - a's own pitch if b's register holds it, else its octave nearest -
       or, `octave`, a's pitch class at the octave nearest the note b would have played. Everything stays in b's register
       and on a chord tone or the scale. Returns the new MIDI note, or -1 */
    int pitch_rel(int q, int m) {
        const comp::Voice &v = ccur[q];
        if (!melodic(q) && (v.style == comp::STRUM || v.style == comp::SWELLS || v.style == comp::CHORALE)) return -1;
        auto last2 = [&](int x, int &cur, int &prev, long long &ct) {   /* x's latest note, and the one before at an earlier tick */
            cur = prev = -1; ct = -1;
            for (int i = 1; i <= RING && i <= rhead[x]; i++) {
                const NR &n = ring[x][(rhead[x] - i) % RING]; if (n.tick < 0) break;
                if (cur < 0) { cur = n.midi; ct = n.tick; } else if (n.tick < ct) { prev = n.midi; break; }
            }
        };
        auto nearest = [&](int pc, int to) {   /* the pitch of class pc in b's register nearest `to`, -1 if none */
            int best = -1; for (int x = v.lo; x <= v.hi; x++) if (((x - pc) % 12 + 12) % 12 == 0 && (best < 0 || std::abs(x - to) < std::abs(best - to))) best = x;
            return best;
        };
        for (int i = 0; i < nract; i++) {
            const comp::Relation &c = patch.comp.rel[ract[i]];
            if (c.b != q || (c.type != comp::R_MOTION && c.type != comp::R_UNISON)) continue;
            int ac, ap; long long at; last2(c.a, ac, ap, at);
            if (ac < 0) continue;
            if (c.type == comp::R_UNISON) {
                if (c.meet == comp::U_NEVER || ufirst[q] == chg || (c.meet == comp::U_CADENCE && !(cad_chord && cj == 0))) continue;
                const int r = c.octave || ac < v.lo || ac > v.hi ? nearest(ac, m) : ac;
                if (r >= 0) return r;
                continue;
            }
            if (c.motion == comp::M_FREE || !(comp::rand01(patch.comp.seed ^ 0x907B, ck, q, mn++) < c.strength)) continue;
            int r = -1;
            if (c.motion == comp::M_CONTRARY) {
                int bc, bp; long long bt; last2(q, bc, bp, bt);
                if (bc < 0) continue;
                const int dir = ap < 0 ? 0 : -((ac > ap) - (ac < ap));
                auto tone = [&](int x) { for (int d = 0; d < H.ntones; d++) if ((x - H.tones[d]) % 12 == 0) return true; return false; };
                for (int x = bc + dir; dir && x >= v.lo && x <= v.hi && r < 0; x += dir) if (tone(x)) r = x;
                if (r < 0) for (int w = 0; w <= 12 && r < 0; w++) for (int s2 = -1; s2 <= 1 && r < 0; s2 += 2) { const int x = bc + s2 * w; if (x >= v.lo && x <= v.hi && tone(x)) r = x; }
            } else {
                const int d = scale_deg_of(ac) - (c.motion == comp::M_PAR3 ? 2 : 5), x = scale_midi(patch, d, 0, v.lo, v.hi);
                if (x >= v.lo && x <= v.hi) r = x;
            }
            if (r >= 0) return r;
        }
        return -1;
    }
    /* composition 3, shared density: the bar's heard notes (every composed voice's, a relation's own - canon, hetero, hocket, call
       - and a strum's chord tones each counted; no look-ahead, no sorting) stay near `cap`, shared by weight. A voice with weight w
       has a quota of ceil(cap * w / W) notes, W the weights of the voices that play (role not off): within it a note always sounds
       (so a voice that has notes keeps at least one, whatever the cap - Review Focus 5); past it a note sounds only while the bar
       is under the cap and no other voice is short of its own quota (the lowest share goes first). A weight-0 voice sounds only
       while the bar is under the cap. A relation's note is dropped by the same rule (its voice's quota). Cap >= what the voices
       play changes nothing. ponytail: a voice that plays little holds its unspent quota back from the busy ones for the bar */
    bool density_ok(int q) {
        for (int i = 0; i < nract; i++) {
            const comp::Relation &c = patch.comp.rel[ract[i]]; if (c.type != comp::R_DENSITY) continue;
            int W = 0, total = 0; for (int x = 0; x < comp::NVOICE; x++) { total += dcnt[x]; if (ccur[x].role != comp::OFF) W += c.weight[x]; }
            auto quota = [&](int x) { return std::max(1, (c.cap * c.weight[x] + W - 1) / W); };
            bool ok;
            if (c.weight[q] == 0 || W == 0) ok = total < c.cap;
            else {
                ok = dcnt[q] < quota(q);
                if (!ok && total < c.cap) { ok = true; for (int x = 0; x < comp::NVOICE; x++) if (x != q && c.weight[x] > 0 && ccur[x].role != comp::OFF && dcnt[x] < quota(x)) ok = false; }
            }
            if (ok) dcnt[q]++;
            return ok;
        }
        return true;
    }
    double micro_cents(const comp::Voice &mv, int m) {   /* a voice's micro offset for the chord degree the pitch is */
        if (!(mv.micro[0] | mv.micro[1] | mv.micro[2] | mv.micro[3] | mv.micro[4])) return 0;
        const int nt = std::max(1, H.ntones), cd = chord_deg(m);
        return mv.micro[std::min(4, ((cd % nt) + nt) % nt)];
    }
    void note(int role, Synth *sy, double f, double dur, double t, double vel, bool composed = false) {
        double cents = 0;
        Role *rr = role == 0 ? &bass : role == 1 ? &top : role == 2 ? &sectr : role == 3 ? &v3r : nullptr;
        if (role >= 1 && role <= 3) nbar[role - 1]++;
        if (!composed && ck < 0 && dur > 0 && role >= 1 && role <= 3 && patch.comp.on && comp_live && H.ntones > 0 && !styled(role - 1)) tint_free(role - 1, f, dur, t, vel);   /* a Free voice's note, answered */
        if (composed && ck >= 0 && !relmade && role >= 1 && role <= 3) {
            if (nract) { const int m = (int)std::lround(69 + 12 * std::log2(f / 440)), r = pitch_rel(role - 1, m); if (r >= 0 && r != m) f = note_hz(r); }
            ufirst[role - 1] = chg;
            const comp::Voice &mv = ccur[role - 1];   /* composition 4: cents by the chord degree played */
            cents = micro_cents(mv, (int)std::lround(69 + 12 * std::log2(f / 440)));
        }
        if (composed && ck >= 0 && !relmade && role >= 1 && role <= 3 && own_note(role - 1, f, dur, t, vel, cents)) return;   /* hocket: it sounds on another member */
        if (cents != 0) f *= std::pow(2.0, cents / 1200);
        const int od = ownd; ownd = -1;
        if (composed && ck >= 0 && nract && role >= 1 && role <= 3 && !(od >= 0 ? od == 1 : density_ok(role - 1))) return;
        if (composed && ck >= 0 && role >= 1 && role <= 3 && ccall[role - 1] >= 0 && dur > 0) {   /* an answer ends before the caller's next note */
            const int n = a_next(patch.comp.rel[ccall[role - 1]].a, ck);
            if (n < 32) dur = std::max(0.02, std::min(dur, ctime + n * 60.0 / patch.tempo / 4 - 0.005 - t));
        }
        if (composed && !(rr && rr->cur == PLUCK)) vel = std::min(vel, 1.0);   /* an envelope's level; pluck's is a gain on its burst */
        double gfrom = 0, gms = 0;
        if (composed && ck >= 0 && !relmade && role >= 1 && role <= 3 && sy && dur > 0) {
            const int q = role - 1; const comp::Voice &v = ccur[q];
            const double t1 = t, d1 = dur;
            /* composition 4, glide: a one-voice synth's pitch ramps from where it is to the new note (no retrigger jump) */
            if (v.glide_ms > 0 && !chord_style(q) && rr && sy == rr->sy() && sy->glide_param() && sy->last_f > 0 && t - oend[q] <= 1.0) {
                const double sm = std::fabs(12 * std::log2(f / sy->last_f));
                if (v.glide_iv == 0 ? sm >= 0.5 : v.glide_iv == 1 ? (sm >= 0.5 && sm <= 2.5) : sm >= 4.5) gms = v.glide_ms;
            }
            const bool done = ornament(q, role, rr, sy, f, dur, t, vel, gfrom, gms);
            oend[q] = t1 + d1;
            if (done) return;
        }
        sound(role, rr, sy, f, dur, t, vel, composed, gfrom, gms);
    }
    /* composition 4, ornaments (the voice's orn: every orn_every-th note, or by seeded chance): grace - the scale tone above, 1/32
       / speed, on the beat with the note following it (a scheduled step cannot reach back before itself); trill - the note's
       length alternating with that tone at 1/32 / speed; mordent - note, tone above, note; slide - a glide into the note from a
       third below over a 1/16. All inside the note and before the voice's next step (a margin for the fade a one-voice synth
       takes between pitches), one per note, in sequence - never two at once. Returns true when the whole note was played */
    bool ornament(int q, int role, Role *rr, Synth *sy, double f, double &dur, double &t, double vel, double &gfrom, double &gms) {
        const comp::Voice &v = ccur[q];
        if (v.orn == comp::ORN_NONE) return false;
        if (chord_style(q)) return false;
        const int n = ++ocnt[q];
        if (!(v.orn_every ? n % v.orn_every == 0 : comp::rand01(patch.comp.seed ^ 0x0A4E, ck, q, n) < v.orn_prob)) return false;
        const double s16 = 60.0 / patch.tempo / 4, u = std::max(0.015, s16 / 2 / v.orn_speed);
        const double room = melodic(q) ? dur : v.style == comp::POINTILLIST ? std::min(std::max(dur, 3 * u), ctime + (pt_next[q] - ck) * s16 - 0.016 - t)   /* no rate: up to the next onset (orn_hits) */
            : std::min(dur, ctime + (v.rate - ck % v.rate) * s16 - 0.016 - t);
        if (room < 2 * u) return false;
        const int m = (int)std::lround(69 + 12 * std::log2(f / 440)); const double up = note_hz(scale_above(patch, m));
        auto put = [&](double ff, double d, double tt) { sound(role, rr, sy, ff, d, tt, vel, true, gfrom, gms); gfrom = 0; gms = 0; };
        switch (v.orn) {
        case comp::ORN_GRACE: put(up, u, t); t += u; dur = room - u; return false;
        case comp::ORN_TRILL: {
            Param *gp = rr && sy == rr->sy() ? sy->glide_param() : nullptr;
            const int S = levels::trill_steps((int)(room / u), gp != nullptr); if (!S) return false;
            if (!gp) { for (int i = 0; i < S; i++) put(i & 1 ? up : f, i == S - 1 ? room - (S - 1) * u : u, t + i * u); return true; }
            /* one attack, the pitch stepping on the freq Param: S attacks' envelope events (7 each) would fold the first ones' (Param::CAP) */
            const auto on = on_note; on_note = nullptr; sound(role, rr, sy, f, room, t, vel, true, gfrom, gms); on_note = on; gfrom = 0; gms = 0;
            for (int i = 0; i < S; i++) {
                if (i) gp->setValueAtTime(i & 1 ? up : f, t + i * u);
                if (on) on(note_ctx, role, i & 1 ? up : f, i == S - 1 ? room - (S - 1) * u : u, t + i * u, vel);
            }
            return true;
        }
        case comp::ORN_MORDENT: if (room < 3 * u) return false; put(f, u, t); put(up, u, t + u); t += 2 * u; dur = room - 2 * u; return false;
        case comp::ORN_SLIDE:
            if (rr && sy == rr->sy() && sy->glide_param()) { gfrom = note_hz(scale_below(patch, scale_below(patch, m))); gms = 1000 * std::min(room, s16 / v.orn_speed); }
            return false;
        default: return false;
        }
    }
    /* a note sounded: the log hook, the sampler's and pluck's settings, a pitch change under a ringing voice faded, the attack
       (gms > 0: its pitch ramps from gfrom - or from where the pitch is now, gfrom <= 0 - to f over gms ms), the heard slots */
    void sound(int role, Role *rr, Synth *sy, double f, double dur, double t, double vel, bool composed, double gfrom, double gms) {
        if (on_note) on_note(note_ctx, role, f, dur, t, vel);
        if (!sy) return;
        /* a composed route's pluck takes its notes' level (level match); everything else of Free's is untouched */
        if (patch.comp.on && rr && rr->cur == PLUCK && sy) static_cast<PluckSynth *>(sy)->accent.setValueAtTime(composed ? vel : 1, t);
        if (rr && is_sampler(rr->cur) && sy == rr->sy()) {   /* a sampler role: its settings for this note (3a) */
            auto *rs = static_cast<sampler::Resonator *>(sy); const SamplerCfg &c = patch.smp[role <= 1 ? 0 : role - 1];
            rs->body = c.body; rs->excite = c.excite; rs->method = c.method; rs->mode = c.mode; rs->tune = c.tune; rs->octave = c.octave;
            sampler_timbre(rs, role <= 1 ? 0 : role - 1, c, t);   /* Focus and Colour too: a held note started before the step set them */
            /* a held note (the bass) ends the one before it: a one-voice digital synth retriggers, a sampler would keep
               every earlier bass sounding (final review 3a C1: six old bass notes piled up) */
            if (dur < 0) rs->release_all(t);
        }
        /* a composed note on a voice still ringing another pitch: a fade of a period of that pitch (6-12 ms), then the note
           (Synth::fade) - shorter than its period, a low note's cut would thump. Free's notes as they were */
        Param *gp = gms > 0 ? sy->glide_param() : nullptr;
        const double f0 = gp ? (gfrom > 0 ? gfrom : gp->value(t)) : f, cur = gp ? gp->value(t) : sy->last_f;
        if (composed && !(rr && rr->cur == PLUCK) && cur > 0 && std::fabs(f0 / cur - 1) > 1e-3 && sy->sounding(t)) {
            const double fd = std::min(0.012, std::max(0.006, 1 / cur)); sy->fade(t, fd); t += fd; }
        if (dur < 0) sy->attack(f, t, vel); else sy->attack_release(f, dur, t, vel);
        if (gp && std::fabs(f0 / f - 1) > 1e-3) { gp->setValueAtTime(f0, t); gp->exponentialRampToValueAtTime(f, t + gms / 1000); }
        sy->last_f = f;
        if (role >= 0 && role <= 3 && !(rr && is_sampler(rr->cur))) {
            Heard *h = heard[role], *slot = &h[0];
            for (int k = 0; k < 8; k++) {
                if (dur < 0 && h[k].held && h[k].until > t) h[k].until = t;      /* a held note ends the one before it */
                if (h[k].until < slot->until) slot = &h[k];
            }
            *slot = Heard{ f, t, dur < 0 ? 1e300 : t + dur, dur < 0 };
        }
    }

    /* ---- harmonyBar ---- */
    void harmony_bar(double time) {
        int step = chord_index(t_along);
        int tones[8]; int nt = chord_tones(step, tones);
        bool changed = H.chord != step;
        H.chord = step;
        int b = lead_to(H.have_bass, H.bass, tones, 1, patch.key - 24, patch.key - 5);
        if ((!patch.comp.on || cbass.mode == comp::HOLD) && (changed || !H.held || PERCUSSIVE[patch.voice.synth])) {
            note(0, bass.sy(), note_hz(b), -1, time, 0.5);
            H.bass = b; H.have_bass = true; H.held = true;
        }
        std::memcpy(H.tones, tones, sizeof tones); H.ntones = nt;
    }

    /* ---- voiceStep ---- */
    void voice_step(double time) {
        if (H.ntones < 0) return;
        const Patch &p = patch;
        double density = std::max(0.05, std::min(0.95, p.voice.density * (0.35 + std::min(2.5, onsets) / 2.5)));
        double T = time, m;
        pad.g1.p.linearRampTo(morph(M_VOICE_GAIN, T, m) ? 0.25 + 0.75 * m : 1, 0.25, T);
        const bool vs = is_sampler(p.voice.synth);   /* a sampler role's two timbre numbers are its Focus and Colour */
        timbre(bass.sy(), p.voice.synth, vs ? p.smp[0].focus : p.voice.harm, M_VOICE_HARM, vs ? p.smp[0].colour : p.voice.index, M_VOICE_INDEX, 0.4, T);
        timbre(top.sy(), p.voice.synth, vs ? p.smp[0].focus : p.voice.harm, M_VOICE_HARM, vs ? p.smp[0].colour : p.voice.index, M_VOICE_INDEX, 0.4, T);
        pad.drive(morph(M_VOICE_DRIVE, T, m) ? m : p.voice.drive, 11, 3.2, T);
        double wp = morph(M_VOICE_WARP, T, m) ? m : p.voice.warp;
        pad.warp.wet.p.linearRampTo(wp * 0.85, 0.3, T); pad.warp.depth = (float)(0.15 + wp * 0.85);
        double cut = std::max(300.0, std::min(12000.0, centroid * 1.6));
        if (morph(M_VOICE_CUTOFF, T, m)) cut = std::max(180.0, std::min(14000.0, cut * 0.35 * std::pow(6.3, m)));
        pad.cutoff.p.exponentialRampTo(cut, 0.25, T);
        if (styled(0) || cad_rest(0, true)) return;   /* Free's line: 8n */
        if (rnd() > density) return;
        int pool[8], np = 0;
        for (int i = H.ntones > 3 ? 2 : 0; i < H.ntones; i++) pool[np++] = H.tones[i];
        int pick = pool[(int)std::floor(rnd() * np)];
        int mm = lead_to(H.have_top, H.top, &pick, 1, p.key + 7, p.key + 31);
        H.topDir = !H.have_top ? 0 : sgn(mm - H.top);
        H.top = mm; H.have_top = true;
        double drift = 1 + (rnd() - 0.5) * 0.006;
        double dur = 0.35 + rnd() * 0.55;
        double f = note_hz(mm) * drift;
        note(1, top.sy(), f, dur, time, 0.22 + rnd() * 0.16);
    }

    /* ---- keyCollection ---- */
    int key_collection(int *pcs, int *stable, int &nstable) {
        int count[12] = {};
        for (int i = 0; i < patch.nprog; i++) {
            int tones[8], nt = chord_tones(i, tones);
            for (int j = 0; j < nt; j++) count[pc(tones[j])] += j == 0 ? 2 : 1;
        }
        int all[12], na = 0;
        for (int k = 0; k < 12; k++) if (count[k]) all[na++] = k;
        for (int i = 1; i < na; i++) { int v = all[i], j = i; while (j > 0 && count[all[j - 1]] < count[v]) { all[j] = all[j - 1]; j--; } all[j] = v; }   /* stable */
        int n = na < 7 ? na : 7;
        std::memcpy(pcs, all, n * sizeof(int));
        int tonic = pc(patch.key); nstable = 0; stable[nstable++] = tonic;
        for (int iv : { 3, 4, 7 }) { int q = (tonic + iv) % 12; for (int k = 0; k < n; k++) if (pcs[k] == q) { stable[nstable++] = q; break; } }
        return n;
    }

    /* ---- sectorStep ---- */
    void sector_step(double time) {
        const Patch &p = patch;
        if (!p.sect.on) return;
        double T = time, m;
        sectL.cutoff.p.exponentialRampTo(morph(M_SECT_CUTOFF, T, m) ? 900 * std::pow(10, m) : 9000, 0.2, T);
        timbre(sectr.sy(), p.sect.synth, is_sampler(p.sect.synth) ? p.smp[1].focus : p.sect.harm, M_SECT_HARM, is_sampler(p.sect.synth) ? p.smp[1].colour : p.sect.index, M_SECT_INDEX, 0.3, T);
        sectL.drive(morph(M_SECT_DRIVE, T, m) ? m : p.sect.drive, 13, 3.6, T);
        double w2 = morph(M_SECT_WARP, T, m) ? m : p.sect.warp;
        sectL.warp.wet.p.linearRampTo(w2 * 0.85, 0.3, T); sectL.warp.depth = (float)(0.2 + w2 * 0.8);
        int stride = stride_of(p.sect.rhythm, 4);
        C.tick++;
        if (styled(1)) return;
        if ((C.tick - 1) % stride != 0) return;
        if (cad_rest(1, stride < 4)) return;
        int i = sector < 0 ? 0 : sector;
        const Sector &sc = p.sectors[i % p.nsectors];
        int mode = sc.mode < 0 ? MODE_DORIAN : sc.mode;
        if (C.restRun > 0) { C.restRun--; return; }
        if (rnd() < 0.18) { C.restRun = 1 + (int)std::floor(rnd() * 2); return; }
        int keyPcs[12], stable[4], nstable; int nk = key_collection(keyPcs, stable, nstable);
        bool chordSet[256] = {}, chordPc[12] = {};
        for (int k = 0; k < (H.ntones > 0 ? H.ntones : 0); k++) {
            chordPc[pc(H.tones[k])] = true;
            for (int o = -3; o <= 3; o++) { int v = H.tones[k] + o * 12; if (v >= 0 && v < 256) chordSet[v] = true; }
        }
        auto in_key = [&](int q) { for (int k = 0; k < nk; k++) if (keyPcs[k] == q) return true; return false; };
        int lo = p.key + 2, hi = p.key + 26, pool[128], np = 0;
        for (int oct = -2; oct <= 3; oct++)
            for (int k = 0; k < MODES[mode].n; k++) {
                int mm = p.key + MODES[mode].iv[k] + oct * 12;
                if (mm < lo || mm > hi) continue;
                if (in_key(pc(mm)) || chordPc[pc(mm)]) pool[np++] = mm;
            }
        if (np < 4) {
            np = 0;
            for (int oc = -2; oc <= 3; oc++)
                for (int q = 0; q < nk; q++) { int nn = p.key - pc(p.key) + keyPcs[q] + oc * 12; if (nn >= lo && nn <= hi) pool[np++] = nn; }
        }
        if (!np) return;
        int prev = C.have_last ? C.last : p.key + 12;
        int leadDir = H.topDir;
        double pull = p.sect.chordPull, centre = (lo + hi) / 2.0;
        C.phrase++;
        bool atRest = C.phrase >= 5 + (int)std::floor(rnd() * 4);
        int lastInt = C.prevInt;
        bool owing = std::abs(lastInt) > 4;
        bool found = false; int bm = 0; double bs = 0;
        for (int k = 0; k < np; k++) {
            int mm = pool[k], d = mm - prev, step = std::abs(d);
            if (step == 0) continue;
            double score = -std::min(step, 14) * 0.6;
            if (owing) {
                if (sgn(d) == -sgn(lastInt) && step <= 2) score += 4;
                else if (sgn(d) == sgn(lastInt)) score -= 2.2;
            }
            if (leadDir != 0 && sgn(d) == -leadDir) score += 1.8;
            if (mm >= 0 && mm < 256 && chordSet[mm]) score += pull * 6;
            if (step > 9) score -= 2.5;
            if (C.have_prev2 && mm == C.prev2) score -= 1.9;
            if (atRest) for (int s = 0; s < nstable; s++) if (stable[s] == pc(mm)) { score += 3.2; break; }
            score -= std::fabs(mm - centre) * 0.07;
            score += (rnd() - 0.5) * 1.5;
            if (!found || score > bs) { found = true; bm = mm; bs = score; }
        }
        if (!found) return;
        C.prev2 = prev; C.have_prev2 = true;
        C.prevInt = bm - prev;
        if (atRest) { C.phrase = 0; C.restRun = 1 + (int)std::floor(rnd() * 2); }
        C.last = bm; C.have_last = true;
        double drift = 1 + (rnd() - 0.5) * 0.005;
        double dur = 0.28 + rnd() * 0.6;
        double f = note_hz(bm) * drift;
        note(2, sectr.sy(), f, dur, time, 0.42 + rnd() * 0.2);
    }

    /* ---- thirdStep ---- */
    /* ---- composition (step 1) ---- */
    void comp_bar(long long bar, double time) {
        uint64_t ent[8]; int ne = 0;
        for (int i = 0; i < nzent; i++) if (bar - zent[i].bar < comp::PHRASE_BARS) ent[ne++] = zent[i].id;
        comp::Resolved r;
        const int chord = chord_index(t_along);
        comp::resolve(patch.comp, chord, patch.nprog, sector, patch.sect.n, zin, nzin, ent, ne, r);
        if (chord != cchord) { cchord = chord; cbar0 = bar; chg++; }
        cad_chord = false; for (int i = patch.comp.nchain - 1; i >= 0; i--) if (chord >= patch.comp.chain[i].from && chord <= patch.comp.chain[i].to) { cad_chord = patch.comp.chain[i].op == comp::OP_CADENCE; break; }
        cj = bar - cbar0;
        for (int q = 0; q < comp::NVOICE; q++) {
            /* the gain of the layer that sets this voice (zone > chord > section > route, as resolve), glided as the settings are */
            int L = 0;
            if (sector >= 0 && sector < comp::NSECTION && sector < patch.sect.n && patch.comp.section[sector].used && patch.comp.section[sector].v[q].mask) L = 1 + comp::NCHORD + sector;
            if (chord >= 0 && chord < comp::NCHORD && chord < patch.nprog && patch.comp.chord[chord].used && patch.comp.chord[chord].v[q].mask) L = 1 + chord;
            for (int z = 0; z < patch.comp.nzone; z++) {
                const comp::Over &o = patch.comp.zone[z]; if (!o.v[q].mask) continue;
                const uint64_t *list = o.when == comp::ENTER ? ent : zin; const int n = o.when == comp::ENTER ? ne : nzin;
                for (int i = 0; i < n; i++) if (list[i] == o.zone) { L = 1 + comp::NCHORD + comp::NSECTION + z; break; }
            }
            const double gt = patch.cgain[L][q];
            if (!comp_live || r.blend <= 1) cg[q] = gt; else cg[q] += (gt - cg[q]) / r.blend;
            if (!comp_live) ccur[q] = r.v[q]; else comp::glide(ccur[q], r.v[q], r.blend);
            if (swell_shaped[q] && (ccur[q].style != comp::SWELLS || melodic(q))) {      /* the voice's own envelope back (8b) */
                (q == 0 ? top : q == 1 ? sectr : v3r).each([&](Synth *sy) { sy->shape(patch.env_on[q] ? &patch.env[q] : nullptr); });
                swell_shaped[q] = false;
            }
        }
        /* pedal to hold mid-chord: harmony_bar, next on this bar line, strikes the root - the same legato handover a chord change
           makes, so the pedal is let go there rather than hanging on to the next chord */
        if (cbass.mode == comp::BASS_PEDAL && r.bass.mode == comp::HOLD) H.held = false;
        /* composition 2: the melody's phrase for this bar (it changes only here), and its cadence on the other voices */
        for (int &x : nbar) x = 0;
        for (int &x : dcnt) x = 0;
        cad_oth = comp::OTH_HOLD;
        int mq = -1; for (int q = 0; q < comp::NVOICE && mq < 0; q++) if (melodic(q)) mq = q;
        if (mq < 0) {
            mel_step = -2; mvar_on = false;
            bool half = false; for (int i = 0; i < patch.comp.nrel; i++) half = half || (patch.comp.rel[i].type == comp::R_CALL && patch.comp.rel[i].source == comp::A_HALF);
            if (half && patch.comp.motif.n > 0) comp::motif_step(patch.comp, chord, mel);   /* a half answer needs the phrase though no voice plays it */
        }
        else {
            comp::motif_step(patch.comp, chord, mel);
            if (mel.step != mel_step) { mel_step = mel.step; mel_bar0 = bar; }
            melody_bar(chord, bar, mq);
            /* drawn once a chord (salted apart from the styles' draws), so a long last chord holds one way */
            if (mel.cadence && mel.last && comp::rand01(patch.comp.seed ^ 0xCAD0u, chord, comp::NVOICE, 0) < mel.strength) cad_oth = mel.others;
        }
        cbass = r.bass; comp_live = true;
        cdir = cwt < 0 || std::fabs(t_along - cwt) < 1e-7 ? 0 : (t_along > cwt ? 1 : -1); cwt = t_along;
        bass_bar(time, bar);
    }
    /* the key scale's step from p (dir +1 / -1); not folded */
    int scale_step(int p, int dir) const {
        const int m = scale_midi(patch, scale_deg_of(p) + dir, 0, -9999, 9999);
        return (m - p) * dir > 0 ? m : p + dir;
    }
    /* composition 4, the walking bass: this bar's notes, one each `wrate` sixteenths. From the chord's root, step-wise through its
       tones and the key's scale (seeded, comp::rand01); the last `before` beats (at most 3 notes) approach the NEXT chord's root -
       the one the walker meets next (its direction at the bar line; still: the following chord of the progression; none left
       that way: this chord) - by half steps, scale steps or its fifth. All inside the bass register, and a pitch never a
       third time running. Bass notes are not a relations' voice (relations name voices 0..2), so no relation sees them. */
    void plan_walk(long long bar) {
        const int lo = patch.key - 24, hi = patch.key - 5, n = patch.nprog, wr = std::max(1, (int)cbass.wrate);
        const int N = (16 + wr - 1) / wr, c = chord_index(t_along);   /* this bar's chord, as comp_bar resolved it (H.chord is still the last bar's until harmony_bar) */
        const int nc = cdir > 0 ? std::min(n - 1, c + 1) : cdir < 0 ? std::max(0, c - 1) : (c + 1) % n;
        const int A = std::max(0, std::min(std::min(3, N - 1), (16 - (16 - 4 * (int)cbass.before) + wr - 1) / wr));
        const uint32_t sd = patch.comp.seed ^ 0xBA55; int rn = 0;
        for (int &x : wk_at) x = -1;
        int tones[8]; const int nt = chord_tones(c, tones);
        int prev = H.bass; bool have = H.have_bass; int j = 0;
        auto put = [&](int m) {
            if (m == wk_l1 && m == wk_l2) { const int u = scale_step(m, m + 2 <= hi ? 1 : -1); m = u; }
            wk_at[j * wr] = m; j++; wk_l2 = wk_l1; wk_l1 = m; prev = m; have = true;
        };
        const int rootc = chord_root(c), rootn = chord_root(nc);
        int rm[1] = { rootc }; int pl[16], pn = 0; pl[pn++] = lead_to(have, prev, rm, 1, lo, hi);
        for (int cur = pl[0]; pn < N - A;) {
            int d = comp::rand01(sd, bar, 3, rn++) < 0.5 ? 1 : -1;
            if ((d > 0 && cur + 2 > hi) || (d < 0 && cur - 2 < lo)) d = -d;
            int m = cur + d;
            if (comp::rand01(sd, bar, 3, rn++) < 0.5) { for (int s = 1; s <= 7; s++) { bool ct = false; for (int k = 0; k < nt; k++) ct = ct || pc(cur + d * s) == pc(tones[k]); if (ct) { m = cur + d * s; break; } } }
            else m = scale_step(cur, d);
            if (m < lo || m > hi) m = scale_step(cur, -d);
            pl[pn++] = cur = m;
        }
        int ap[3], na = 0;
        if (A) {
            int rn1[1] = { rootn }; const int T = lead_to(true, pl[pn - 1], rn1, 1, lo, hi);
            int s = pl[pn - 1] >= T ? 1 : -1;
            if (cbass.approach == comp::AP_FIFTH) {
                const int f1 = T + 7 <= hi ? T + 7 : T - 5, f2 = T - 5 >= lo ? T - 5 : T + 7;
                const int f = std::abs(f1 - pl[pn - 1]) <= std::abs(f2 - pl[pn - 1]) ? f1 : f2;
                for (int a = A - 1; a >= 0; a--) ap[na++] = (A - 1 - a) % 2 ? T : f;   /* ... fifth, root, fifth: ends on the fifth, the root follows at the bar line */
                if (A % 2 == 0) for (int a = 0; a < A; a++) ap[a] = (A - 1 - a) % 2 ? T : f;
            } else {
                auto run = [&](int dir) { int q = T; for (int a = A - 1; a >= 0; a--) { q = cbass.approach == comp::AP_SCALE ? scale_step(q, dir) : q + dir; ap[a] = q; } na = A; };
                run(s); if (ap[0] < lo || ap[0] > hi) run(-s);
            }
            /* the last walked note must not be the approach's first (that, and the next root, would make a run of three) */
            if (pl[pn - 1] == ap[0] && pn > 1) { const int u = scale_step(pl[pn - 1], pl[pn - 1] + 2 <= hi ? 1 : -1); if (u >= lo && u <= hi) pl[pn - 1] = u; }
        }
        for (int i = 0; i < pn; i++) put(pl[i]);
        for (int i = 0; i < na; i++) put(ap[i]);
        H.bass = prev; H.have_bass = true;
    }
    void bass_bar(double time, long long bar = 0) {
        if (cbass.mode == comp::BASS_WALK) {
            if (H.held && bass.sy()) bass.sy()->release(time);      /* a held root from before lets go at the bar line, as off does */
            for (auto &h : heard[0]) if (h.held && h.until > time) h.until = time;
            H.held = false; plan_walk(bar); return;
        }
        for (int &x : wk_at) x = -1;
        if (cbass.mode == comp::BASS_OFF) {
            if (H.held && bass.sy()) bass.sy()->release(time);
            for (auto &h : heard[0]) if (h.held && h.until > time) h.until = time;   /* final review M8: the readout lets it go too */
            H.held = false; return;
        }
        if (cbass.mode != comp::BASS_PEDAL) return;
        int m = cbass.pedal == 2 ? cbass.note : patch.key - 24 + (cbass.pedal == 1 ? 7 : 0);
        if (!(H.held && H.have_bass && H.bass == m)) { note(0, bass.sy(), note_hz(m), -1, time, 0.5); H.bass = m; H.have_bass = true; H.held = true; }
    }
    /* call & response. Does the caller have a note starting at sixteenth j (a melody: its phrase, or the variant bar; else its ring) */
    bool a_at(int a, long long j) const {
        if (melodic(a)) {
            if (mvar_on) { for (int i = 0; i < mvar.n; i++) if (mvar.note[i].start == j % 16) return true; return false; }
            const int cb = (int)((j / 16 - mel_bar0) % std::max(1, mel.len16 / 16)), at = cb * 16 + (int)(j % 16);
            for (int i = 0; i < mel.n; i++) if (mel.note[i].start == at) return true;
            return false;
        }
        for (const NR &n : ring[a]) if (n.tick == j) return true;
        return false;
    }
    /* sixteenths until the caller's next note (0: it plays now), 32 when none is in sight. A melody is looked up in its phrase (a
       variant bar to its end); other styles ring-checked now and assumed at each of their steps after. */
    int a_next(int a, long long k) const {
        const comp::Voice &v = ccur[a];
        if (!melodic(a) && (v.style == comp::FREE || v.role == comp::OFF || v.rate == 0)) return 32;
        for (int j = 0; j < 32; j++)
            if (melodic(a) ? ((mvar_on && (k + j) / 16 != k / 16) || a_at(a, k + j)) : (j == 0 ? a_at(a, k) : (k + j) % v.rate == 0)) return j;
        return 32;
    }
    /* the sixteenth the answer starts: gap16 after the caller's last note ended, not before the relation began */
    long long answer_start(const comp::Relation &c, int r, long long k) const {
        long long e = -1000;
        for (const NR &n : ring[c.a]) if (n.tick >= 0 && n.tick <= k) e = std::max(e, n.tick + std::max(1LL, (long long)std::ceil(n.len16)));
        return std::max(e + c.gap16, sp[r].a);
    }
    /* source half: the motif's second half (notes from len16 / 2) fitted to the chord now playing; mirror: a's last phrase inverted
       about its first note (chord degrees). Either plays from the answer's start - its first note at once, the rest as spaced - in
       b's register, through b's chord voices when a step has several notes. `e`: sixteenths since the answer began */
    void play_answer(int q, const comp::Relation &c, long long e, double time) {
        const comp::Voice &v = ccur[q];
        const double s16 = 60.0 / patch.tempo / 4;
        struct Hit { int m; double dur, vel, off; } hit[8]; int nh = 0;
        if (c.source == comp::A_HALF) {
            const int h = mel.len16 / 2; int first = 999;
            for (int i = 0; i < mel.n; i++) if (mel.note[i].start >= h) first = std::min(first, (int)mel.note[i].start);
            for (int i = 0; i < mel.n && nh < 8; i++) { const comp::MNote &x = mel.note[i];
                if (x.start >= h && x.start - first == e) hit[nh++] = { mel_pitch(x, v, false), x.len * s16 * v.len, (0.22 + 0.4 * x.acc / 100.0) * cg[q], 0 }; }
        } else {
            const NR *lead = ring[c.a]; long long last = -1, t0 = -1;
            for (int i = 0; i < RING; i++) last = std::max(last, lead[i].tick);
            const long long w = melodic(c.a) ? mel.len16 : 16;
            for (int i = 0; i < RING; i++) if (lead[i].tick >= 0 && lead[i].tick > last - w && lead[i].len16 > 0 && (t0 < 0 || lead[i].tick < t0)) t0 = lead[i].tick;
            if (t0 < 0) return;
            int d0 = 0; for (int i = 0; i < RING; i++) if (lead[i].tick == t0) d0 = lead[i].deg;
            for (int i = 0; i < RING && nh < 8; i++) { const NR &n = lead[i];
                if (n.tick < 0 || n.tick <= last - w || n.len16 <= 0 || n.tick - t0 != e) continue;
                hit[nh++] = { deg_midi(2 * d0 - n.deg, v.lo, v.hi, 0), std::max(0.03, n.len16 * s16), n.vel * cg[q], n.off }; }
        }
        Role &rl = q == 0 ? top : q == 1 ? sectr : v3r;
        for (int i = 0; i < nh; i++) {
            const double t = time + hit[i].off;
            relmade = true;
            if (nh > 1) { sync_pool(rl); note(q + 1, chord_voice(rl, t + hit[i].dur), note_hz(hit[i].m), hit[i].dur, t, hit[i].vel, true); }
            else note(q + 1, rl.sy(), note_hz(hit[i].m), hit[i].dur, t, hit[i].vel, true);
            relmade = false;
        }
    }
    /* composition 4, tintinnabuli: voice a answers voice `tvoice`'s note m with the nearest triad tone (the chord's first three
       tones) above or below it, moved by octaves into a's register; -1 when there is none */
    bool tin(int a) const { const comp::Voice &v = ccur[a]; return v.style == comp::TINTINNABULI && v.role != comp::OFF && !bare(a) && !melodic(a); }
    int tint_pitch(int a, int m) {
        const comp::Voice &v = ccur[a];
        const bool up = v.tmode == comp::TT_ABOVE || (v.tmode == comp::TT_ALT && (tcnt[a]++ & 1) == 0);
        int tri = 0; for (int i = 0; i < H.ntones && i < 3; i++) tri |= 1 << pc(H.tones[i]);
        for (int d = 1; d < 12; d++) { const int c = up ? m + d : m - d; if (tri >> pc(c) & 1) return fold(c, v.lo, v.hi); }
        return -1;
    }
    void tint_play(int a, const int *m, const double *dur, const double *off, const double *vel, int n, double t) {
        Role &rl = a == 0 ? top : a == 1 ? sectr : v3r;
        for (int i = 0; i < n; i++) {
            const int p = tint_pitch(a, m[i]); if (p < 0) continue;
            bool dup = false; for (int j = 0; j < i; j++) if (tint_pitch_last[j] == p) dup = true;   /* two leader tones with one answer: sounds once */
            tint_pitch_last[i] = p; if (dup) continue;
            const double d = std::max(0.03, dur[i]), at = t + off[i];
            const bool relm = relmade; relmade = true;
            if (n > 1) { sync_pool(rl); note(a + 1, chord_voice(rl, at + d), note_hz(p), d, at, vel[i] * cg[a], true); }
            else note(a + 1, rl.sy(), note_hz(p), d, at, vel[i] * cg[a], true);
            relmade = relm;
        }
    }
    int tint_pitch_last[8] = {};
    void tint_step(int a, long long k, double time) {   /* the notes voice tvoice composed this sixteenth (its ring) */
        const int q = ccur[a].tvoice; if (q == a) return;
        const double s16 = 60.0 / patch.tempo / 4;
        int m[8]; double dur[8], off[8], vel[8]; int n = 0;
        for (int i = 0; i < RING && n < 8; i++) { const NR &r = ring[q][i]; if (r.tick != k || r.len16 <= 0) continue; m[n] = r.midi; dur[n] = r.len16 * s16; off[n] = r.off; vel[n] = r.vel; n++; }
        tint_play(a, m, dur, off, vel, n, time);
    }
    void tint_free(int q, double f, double dur, double t, double vel) {   /* a Free voice's note, as it is struck */
        for (int a = 0; a < comp::NVOICE; a++) {
            if (a == q || !tin(a) || ccur[a].tvoice != q) continue;
            const int m = (int)std::lround(69 + 12 * std::log2(f / 440)); const double z = 0;
            tint_play(a, &m, &dur, &z, &vel, 1, t);
        }
    }
    /* pointillist: one short note, then a seeded silence - the gap between onsets in sil_min..sil_max (to the nearest sixteenth);
       the pitch a chord tone across lo..hi, the level within +-20 %. The randomness is the style's own (comp::rand01) */
    void point_step(int q, const comp::Voice &v, long long k, double time) {
        if (pt_next[q] >= 0 && k < pt_next[q]) return;
        int pool[128], np = 0; for (int m = v.lo; m <= v.hi && np < 128; m++) for (int i = 0; i < H.ntones; i++) if (pc(m) == pc(H.tones[i])) { pool[np++] = m; break; }
        if (!np) return;
        const uint32_t sd = patch.comp.seed ^ 0x9017; const int n = pt_n[q]++;
        const double s16 = 60.0 / patch.tempo / 4, gap = v.sil_min + (v.sil_max - v.sil_min) * comp::rand01(sd, n, q, 0);
        pt_next[q] = k + std::max(1, (int)std::lround(gap / s16));
        const int p = pool[std::min(np - 1, (int)(comp::rand01(sd, n, q, 1) * np))];
        const double vl = (0.15 + 0.45 * v.peak) * cg[q] * (1 + 0.2 * (2 * comp::rand01(sd, n, q, 2) - 1));
        Role &rl = q == 0 ? top : q == 1 ? sectr : v3r;
        note(q + 1, rl.sy(), note_hz(p), v.plen, time, vl, true);
    }
    void compose_step(double time) {
        if (!patch.comp.on || !comp_live || H.ntones <= 0) return;
        const long long k = ticks - 1, bar = k / 16;
        ck = k; hn = 0; mn = 0; ctime = time;
        if (cbass.mode == comp::BASS_WALK && wk_at[k % 16] >= 0) {      /* the walking bass: a plain bass note, legato to its step */
            const int m = wk_at[k % 16]; note(0, bass.sy(), note_hz(m), std::max(0.05, 0.9 * cbass.wrate * 60.0 / patch.tempo / 4), time, (bass.cur == PLUCK ? 1 : 0.5) * (patch.bgain[std::min(16, (int)cbass.wrate)] > 0 ? patch.bgain[std::min(16, (int)cbass.wrate)] : 1), bass.cur == PLUCK);   /* the level of the held root (comp_gains: bgain); pluck takes its note's level as composed notes' */
        }
        { uint64_t rec[8]; int nr = 0;   /* the zones entered in the last PHRASE_BARS: a call with trigger "enter" answers on them though the walker has left */
          for (int i = 0; i < nzent; i++) if (bar >= zent[i].bar && bar - zent[i].bar < comp::PHRASE_BARS) rec[nr++] = zent[i].id;
          nract = comp::relations_in_force(patch.comp, H.chord, zin, nzin, ract, rec, nr); }
        for (int i = 0; i < nract; i++) {
            const int r = ract[i]; if (sp[r].a < 0 || sp[r].b < k - 1) { sp[r].a = k; hcnt[r] = 0; }
            sp[r].b = k;
        }
        /* the canon (rel index) each voice follows: the first listed wins; a voice with a canon on it plays no style of its own.
           A canon whose leader leads back to its follower (a -> b, b -> a) is dropped, so nothing feeds back; a chain
           (a -> b, b -> c) stands, c following what b plays in its canon */
        int fol[comp::NVOICE] = { -1, -1, -1 };
        for (int r = 0; r < patch.comp.nrel; r++) {
            const comp::Relation &c = patch.comp.rel[r];
            if (c.type != comp::R_CANON || sp[r].b < 0 || sp[r].b < k - c.delay16 || fol[c.b] >= 0) continue;
            int x = c.a; for (int h = 0; h < comp::NVOICE && fol[x] >= 0; h++) x = patch.comp.rel[fol[x]].a;
            if (x != c.b) fol[c.b] = r;
        }
        /* hocket: b and c play nothing of their own (a's line is dealt among them); call: b answers, after the caller has played */
        bool mute[comp::NVOICE] = {};
        for (int &x : ccall) x = -1;
        for (int i = 0; i < nract; i++) {
            const comp::Relation &c = patch.comp.rel[ract[i]];
            if (c.type == comp::R_HOCKET && c.b != c.a) { mute[c.b] = true; if (c.c < comp::NVOICE && c.c != c.a) mute[c.c] = true; }
            else if (c.type == comp::R_CALL && c.b != c.a && ccall[c.b] < 0) ccall[c.b] = ract[i];
        }
        /* a pitch relation's follower plays after its leader in the same sixteenth, whatever their order (it reads the leader's
           current pitch): pass 0 the rest, pass 1 those followers, pass 2 the call's answers */
        bool pf[comp::NVOICE] = {};
        for (int i = 0; i < nract; i++) { const comp::Relation &c = patch.comp.rel[ract[i]]; if ((c.type == comp::R_MOTION || c.type == comp::R_UNISON) && c.b != c.a) pf[c.b] = true; }
        /* tintinnabuli answers after every other voice (pass 3), so it hears this sixteenth's note whatever the voices' order */
        for (int pass = 0; pass < 4; pass++) for (int q = 0; q < comp::NVOICE; q++) {
            const comp::Voice &v = ccur[q];
            if ((ccall[q] < 0 && fol[q] < 0 && !mute[q] && tin(q) ? 3 : ccall[q] >= 0 ? 2 : pf[q] ? 1 : 0) != pass) continue;
            if (fol[q] >= 0) { play_canon(q, fol[q], sp[fol[q]].a, sp[fol[q]].b, k, time); continue; }
            if (mute[q]) continue;
            if (ccall[q] >= 0) {
                const comp::Relation &c = patch.comp.rel[ccall[q]];
                const long long e = k - answer_start(c, ccall[q], k);
                if (e < 0 || a_next(c.a, k) == 0) continue;
                if (c.source != comp::A_OWN) { play_answer(q, c, e, time); continue; }
            }
            if (melodic(q)) { play_melody(q, v, k, bar, time); continue; }
            if (pass == 3) { tint_step(q, k, time); continue; }
            if (v.style == comp::POINTILLIST) { if (!bare(q) && v.role != comp::OFF && !cad_rest(q, false)) point_step(q, v, k, time); continue; }
            if (v.style == comp::FREE || bare(q) || v.role == comp::OFF || v.rate == 0 || k % (v.style == comp::CHORALE ? std::max(1, (int)v.restrike) : v.rate) || cad_rest(q, (v.style == comp::CHORALE ? v.restrike : v.rate) < 4)) continue;
            play_style(q, v, k, bar, time);
        }
        for (int &x : ccall) x = -1;
        ck = -1;
    }
    /* canon: voice q plays the leader's own notes `delay16` sixteenths late - those whose leader tick lies in the relation's span
       (first..last step in force; the last delay's worth sounds just past the span) - moved `trans` degrees (chord degrees; scale
       degrees when the leader is a scale-basis melody), fitted to q's register on the chord now playing, at q's level and with
       the leader's roll (a strum's offsets within its step), through q's chord voices when a step has several notes.
       Invert: about the leader's first note of that bar. Retro: the leader's previous bar, each note's start mirrored
       (16 - start - length), then delayed by `delay16` like any canon. */
    void play_canon(int q, int ri, long long first, long long last, long long k, double time) {
        const comp::Relation &c = patch.comp.rel[ri];
        const comp::Voice &v = ccur[q];
        const double s16 = 60.0 / patch.tempo / 4;
        Role &rl = q == 0 ? top : q == 1 ? sectr : v3r;
        const double g = rg(ri, q);
        const bool sc = patch.comp.motif.basis == comp::SCALE_DEG && melodic(c.a);
        const NR *lead = ring[c.a];
        const long long u = k - c.delay16;   /* the leader tick this step replays */
        const NR *hit[8]; int nh = 0;
        for (int i = 0; i < RING && nh < 8; i++) {
            const NR &n = lead[i]; if (n.tick < 0 || n.tick < first || n.tick > last || n.len16 <= 0) continue;
            if (c.transform == comp::T_RETRO ? (u < 0 || n.tick / 16 != u / 16 - 1 || std::max(0, 16 - (int)(n.tick % 16) - (int)std::lround(n.len16)) != u % 16) : n.tick != u) continue;
            hit[nh++] = &n;
        }
        bool leads = false;
        for (int i = 0; i < nract; i++) if (patch.comp.rel[ract[i]].type == comp::R_CANON && patch.comp.rel[ract[i]].a == q) leads = true;
        for (int i = 0; i < nh; i++) {
            const NR &n = *hit[i];
            int d = sc ? scale_deg_of(n.midi) : n.deg;
            if (c.transform == comp::T_INVERT) {
                const NR *f0 = &n; for (int j = 0; j < RING; j++) if (lead[j].tick >= 0 && lead[j].tick / 16 == n.tick / 16 && lead[j].tick < f0->tick) f0 = &lead[j];
                d = 2 * (sc ? scale_deg_of(f0->midi) : f0->deg) - d;
            }
            d += c.trans;
            const int m = sc ? scale_midi(patch, d, 0, v.lo, v.hi) : deg_midi(d, v.lo, v.hi, 0);
            const double dur = std::max(0.03, n.len16 * s16), t = time + n.off;
            relmade = true;
            const double cn = c.trans ? micro_cents(v, m) : n.cents;   /* a transposed follower: its own table, by the degree it lands on */
            if (nh > 1) { sync_pool(rl); note(q + 1, chord_voice(rl, t + dur), note_hz(m) * cents_ratio(cn), dur, t, n.vel * g, true); }
            else note(q + 1, rl.sy(), note_hz(m) * cents_ratio(cn), dur, t, n.vel * g, true);
            relmade = false;
            if (leads) push_ring(q, m, n.len16, n.vel, n.off, cn);   /* a chain: the next follower hears this voice as it plays */
        }
    }
    static double cents_ratio(double c) { return c != 0 ? std::pow(2.0, c / 1200) : 1.0; }
    /* a chord degree (0 root .. 4 ninth) as a MIDI note, folded into [lo, hi]; degrees past the chord's tones go up an octave */
    int deg_midi(int d, int lo, int hi, int oct) const {
        const int nt = std::max(1, H.ntones), o = floor_div(d, nt);
        return fold(H.tones[d - o * nt] + 12 * (o + oct), lo, hi);
    }
    int mel_pitch(const comp::MNote &x, const comp::Voice &v, bool cad) const {
        return cad ? scale_midi(patch, mel.cad == comp::CAD_FULL ? 0 : mel.cad == comp::CAD_HALF ? 4 : 5, 0, v.lo, v.hi)
                   : patch.comp.motif.basis == comp::SCALE_DEG ? scale_midi(patch, x.deg, x.oct, v.lo, v.hi) : deg_midi(x.deg, v.lo, v.hi, x.oct);
    }
    static bool same_bar(const MBar &a, const MBar &b) {
        if (a.n != b.n) return false;
        for (int i = 0; i < a.n; i++) { const comp::MNote &x = a.note[i], &y = b.note[i]; if (x.start != y.start || x.len != y.len || x.deg != y.deg || x.oct != y.oct) return false; }
        return true;
    }
    static void by_start(MBar &b) { std::stable_sort(b.note, b.note + b.n, [](const comp::MNote &x, const comp::MNote &y) { return x.start < y.start; }); }
    /* "bars": develop - bar j >= 1 of a chord plays a seeded variant of the phrase's bar (answer: its times mirrored, sequence +1
       degree, the first half then the last note held to the bar's end, inversion about the first note), never one of the last four
       bars' notes. A cadence's chord rests after its first bar: the cadence has landed on it (the chord's length is not known ahead,
       so it cannot fall on its last bar), and restating the motif over it would undo that. Bar 0 is the phrase as it always was. */
    void melody_bar(int chord, long long bar, int q) {
        const int j = dj(), cb = (int)((bar - mel_bar0) % std::max(1, mel.len16 / 16));
        MBar b;   /* this bar of the phrase, by start */
        for (int i = 0; i < mel.n; i++) { const comp::MNote &x = mel.note[i]; if (x.start >= cb * 16 && x.start < cb * 16 + 16) { b.note[b.n] = x; b.note[b.n++].start = (uint8_t)(x.start - cb * 16); } }
        by_start(b);
        mvar_on = false;
        if (j == 0) { mhist[0] = b; nhist = 1; return; }
        if (mel.cadence && mel.last) { mvar_on = true; mvar = MBar(); return; }
        if (b.n == 0) return;
        if (nhist == 0) { mhist[0] = b; nhist = 1; }
        const int first = (int)std::floor(comp::rand01(patch.comp.seed ^ 0xDE70u, chord, q, (int)j) * 4);
        for (int t = 0; t <= 4; t++) {
            MBar v = b; const int kind = (first + t) % 4;
            if (t == 4) v = MBar();   /* every variant was just heard: rest rather than repeat */
            else if (kind == 0) {
                for (int i = 0; i < v.n; i++) v.note[i].start = (uint8_t)std::max(0, 16 - (b.note[i].start + b.note[i].len));
                by_start(v);
            } else if (kind == 1) { for (int i = 0; i < v.n; i++) v.note[i].deg = (int8_t)(v.note[i].deg + 1); }
            else if (kind == 2) {
                const int keep = std::max(1, b.n / 2); v.n = keep;
                if (b.n == 1) v.note[0].len = (uint8_t)(16 - v.note[0].start);
                else {
                    comp::MNote tail = b.note[b.n - 1];
                    tail.start = (uint8_t)std::min(15, std::max((int)v.note[keep - 1].start + 1, 8)); tail.len = (uint8_t)(16 - tail.start);
                    v.note[keep - 1].len = (uint8_t)std::max(1, std::min((int)v.note[keep - 1].len, tail.start - v.note[keep - 1].start));
                    v.note[v.n++] = tail;
                }
            } else for (int i = 0; i < v.n; i++) v.note[i].deg = (int8_t)(2 * b.note[0].deg - b.note[i].deg);
            int k = 0;   /* one note a start, the later kept */
            for (int i = 0; i < v.n; i++) { bool dup = false; for (int u = i + 1; u < v.n && !dup; u++) dup = v.note[u].start == v.note[i].start; if (!dup) v.note[k++] = v.note[i]; }
            v.n = k;
            bool seen = false; for (int h = 0; h < nhist && !seen; h++) seen = same_bar(v, mhist[h]);
            if (!seen || t == 4) { mvar = v; break; }
        }
        mvar_on = true;
        for (int h = std::min(nhist, 3); h > 0; h--) mhist[h] = mhist[h - 1];
        mhist[0] = mvar; nhist = std::min(nhist + 1, 4);
    }
    /* the phrase's notes starting on this sixteenth (the phrase taken at the bar line, comp_bar): the cursor is the bars since its
       chain step began, modulo its bars;
       a note is held to its bar's end at most, the cadence's last note is the key's tonic, 5th or 6th */
    void play_melody(int q, const comp::Voice &v, long long k, long long bar, double time) {
        const comp::Phrase &ph = mel;
        const int cb = (int)((bar - mel_bar0) % std::max(1, ph.len16 / 16)), at = cb * 16 + (int)(k % 16);
        Synth *sy = (q == 0 ? top : q == 1 ? sectr : v3r).sy();
        const double s16 = 60.0 / patch.tempo / 4, g = cg[q];
        if (mvar_on) {
            for (int i = 0; i < mvar.n; i++) {
                const comp::MNote &x = mvar.note[i];
                if (x.start == k % 16) note(q + 1, sy, note_hz(mel_pitch(x, v, false)), std::min((int)x.len, 16 - x.start) * s16 * v.len, time, (0.22 + 0.4 * x.acc / 100.0) * g, true);
            }
            return;
        }
        for (int i = 0; i < ph.n; i++) {
            const comp::MNote &x = ph.note[i];
            if (x.start != at) continue;
            const bool cad = ph.cadence && ph.last && i == ph.n - 1;
            const double dur = std::min((int)x.len, cb * 16 + 16 - x.start) * s16 * v.len;
            note(q + 1, sy, note_hz(mel_pitch(x, v, cad)), dur, time, (0.22 + 0.4 * x.acc / 100.0) * g * (cad ? 0.5 + 0.5 * ph.strength : 1), true);
        }
    }
    /* C1: a chord's next note's voice - the role's instrument or one of its pool, in turn; busy until the note has rung out */
    Synth *chord_voice(Role &r, double end) {
        Synth *s = r.sy(); if (!s || is_sampler(r.cur)) return s;
        const int j = (int)(r.deal++ % (Role::pool_n(r.cur) + 1)) - 1;
        if (j < 0 || !r.synced[r.cur][j]) return s;
        Synth *p = r.pool[r.cur][j].get(); const Envelope *e = p->amp();
        r.busy[r.cur][j] = std::max(r.busy[r.cur][j], end + (e ? e->release : 4) + 0.1);
        return p;
    }
    void play_style(int q, const comp::Voice &v, long long k, long long bar, double time) {
        Role &rl = q == 0 ? top : q == 1 ? sectr : v3r;
        Synth *sy = rl.sy();
        if (v.style == comp::STRUM || v.style == comp::SWELLS || v.style == comp::CHORALE) sync_pool(rl);
        const double g = cg[q];   /* each style at Free's level (comp_gains, comp_bar) */
        const int role = q + 1, ncols = std::max(1, (16 + v.rate - 1) / v.rate), j = dj();
        /* "bars": develop - an arpeggio's grid read backwards on odd bars of a chord, two columns on in bar 2 (of 4) */
        int col = (int)((k % 16) / v.rate);
        if (v.style == comp::ARPEGGIO && j > 0) { if (j & 1) col = ncols - 1 - col; else if (j % 4 == 2) col = (col + 2) % ncols; }
        const double step_s = 60.0 / patch.tempo / 4 * v.rate, dur = step_s * v.len;
        auto vel = [&](int c) { return (0.22 + 0.4 * v.acc[c % comp::MAX_COLS] / 100.0) * g; };
        /* variation: a seeded chance to take another chord tone, and a seeded wobble of the level (0 = exactly as set) */
        auto vary = [&](int d, double &vl, int n) {
            if (v.variation <= 0) return d;
            const double h1 = comp::rand01(patch.comp.seed, bar, q, n * 3), h2 = comp::rand01(patch.comp.seed, bar, q, n * 3 + 1), h3 = comp::rand01(patch.comp.seed, bar, q, n * 3 + 2);
            vl *= 1 + (h3 - 0.5) * 0.6 * v.variation;
            return h1 < 0.35 * v.variation ? (d + 1 + (int)(h2 * 3)) % std::max(3, H.ntones) : d;
        };
        switch (v.style) {
        case comp::ARPEGGIO: {
            int d = v.grid[col % ncols]; if (d < 0) return;
            double vl = vel(col); d = vary(d, vl, col);
            note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, v.span > 1 ? (int)(bar % v.span) : 0)), dur, time, vl, true);
            break;
        }
        case comp::PEDAL: {
            if (cped_root[q] < 0 || v.follow) cped_root[q] = H.tones[0];
            int m = v.pedal == 2 ? v.pedal_note : cped_root[q] + (v.pedal == 1 ? 7 : 0);
            while (m < v.lo) m += 12; while (m > v.hi && m - 12 >= v.lo) m -= 12;
            double vl = vel(col); vary(0, vl, col);
            note(role, sy, note_hz(m), dur, time, vl, true);
            break;
        }
        case comp::STRUM: {
            int ds[5], nd = 0; for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) ds[nd++] = d;
            const bool down = (v.dir == 1 || (v.dir == 2 && (calt[q]++ & 1))) != ((j & 1) != 0);   /* develop: the other way on odd bars, a 4-tone chord's top tone dropped */
            const double mk = (j & 1) && nd >= 4 ? std::sqrt(nd / (nd - 1.0)) : 1;   /* a tone fewer, the rest as loud as the chord was (power) */
            if (mk > 1) nd--;
            for (int i = 0; i < nd; i++) {
                const int d = ds[down ? nd - 1 - i : i]; const double off = i * v.spread_ms / 1000.0;
                double vl = (vel(col) + (i == 0 ? 0.12 * g : 0)) * mk; vary(d, vl, col * 5 + i);
                const double nd_s = std::max(0.03, dur - off);
                note(role, chord_voice(rl, time + off + nd_s), note_hz(deg_midi(d, v.lo, v.hi, 0)), nd_s, time + off, vl, true);
            }
            break;
        }
        case comp::EUCLID: {
            const int E = std::max(1, (int)v.steps), at = (int)((k / v.rate) % E);
            if (!euclid_at(std::min((int)v.pulses, E), E, (v.rot + j) % E, at)) return;   /* develop: a step on a bar */
            int seq[comp::MAX_COLS], ns = 0; for (int i = 0; i < comp::MAX_COLS; i++) if (v.grid[i] >= 0) seq[ns++] = v.grid[i];
            if (!ns) return;
            int d = seq[cpulse[q]++ % ns]; double vl = vel(at); d = vary(d, vl, at);
            note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, 0)), dur, time, vl, true);
            break;
        }
        case comp::SWELLS: {
            const EnvSpec e{ std::max(0.005, v.attack * dur), 0.005, 1.0, std::max(0.02, v.release * dur), false, true };
            if (sy) { rl.each([&](Synth *s) { s->shape(&e); }); swell_shaped[q] = true; }
            int nt = 0, drop = -1; for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) nt++;
            if (j > 0 && nt >= 2) drop = (j - 1) % nt;   /* develop: the voicing rotates - one tone silent, a different one each bar */
            int ti = -1;
            for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) { if (++ti == drop) continue; double vl = (0.15 + 0.45 * v.peak) * g * (drop >= 0 ? std::sqrt(nt / (nt - 1.0)) : 1); vary(d, vl, d); note(role, chord_voice(rl, time + dur), note_hz(deg_midi(d, v.lo, v.hi, 0)), dur, time, vl, true); }
            break;
        }
        case comp::OSTINATO: {
            /* The one style that does not develop: its cell repeats every bar on purpose, whatever "bars" says (no dj() here, no
               variation either). Degree refit: the cell's degrees on the chord now playing, as an arpeggio's. Transpose: the cell as
               first heard (the first chord's tones), moved whole by the root's movement (nearest way, -6..+5), then by octaves into
               lo..hi as one piece, so its intervals are exact on every chord. */
            const int d = v.grid[col % ncols]; if (d < 0) return;
            const double vl = vel(col);
            if (v.refit == comp::RF_DEGREE) { note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, 0)), dur, time, vl, true); break; }
            if (ost_n0[q] <= 0) { ost_n0[q] = std::max(1, std::min(8, H.ntones)); for (int i = 0; i < ost_n0[q]; i++) ost_t0[q][i] = H.tones[i]; }
            const int n0 = ost_n0[q];
            auto raw = [&](int dd) { const int o = floor_div(dd, n0); return ost_t0[q][dd - o * n0] + 12 * o; };
            int sh = ((H.tones[0] - ost_t0[q][0]) % 12 + 12) % 12; if (sh > 5) sh -= 12;
            int mn = 1 << 20, mx = -(1 << 20);
            for (int c = 0; c < ncols; c++) if (v.grid[c] >= 0) { const int m = raw(v.grid[c]) + sh; mn = std::min(mn, m); mx = std::max(mx, m); }
            while (mn < v.lo) { mn += 12; mx += 12; sh += 12; }
            while (mx > v.hi && mn - 12 >= v.lo) { mn -= 12; mx -= 12; sh -= 12; }
            note(role, sy, note_hz(raw(d) + sh), dur, time, vl, true);
            break;
        }
        case comp::CHORALE: {
            /* every restrike, nvoices chord tones as one voicing. The candidates are the stacks of consecutive chord tones (in lo..hi, so a
               voicing of more voices than the chord has tones doubles tones in other octaves, never a unison) from each possible bass,
               shaped by close / open (every other tone an octave up) / drop2; a candidate is kept only if all its notes are inside
               lo..hi and distinct. The plain one is the lowest root-position stack; the chosen one minimises
               strict * (total motion from the last voicing, voices paired in order) + (1 - strict) * (distance of its bass from the plain bass).
               ponytail: insertion sorts over <= 6 notes, and a scan of the register (<= 128 notes) for the pool - no heap, no std::sort. */
            const int nn0 = std::min(levels::chorale_cap(rl.cur), std::max(2, std::min(6, (int)v.nvoices)));
            int pcs = 0; for (int i = 0; i < H.ntones; i++) pcs |= 1 << pc(H.tones[i]);
            int pool[128], np = 0; for (int m = v.lo; m <= v.hi && np < 128; m++) if (pcs >> pc(m) & 1) pool[np++] = m;
            if (!np) return;
            const int rpc = pc(H.tones[0]);
            auto build = [&](int b, int nn, int vo, int *out) {
                if (b + nn > np) return false;
                int s[6]; for (int i = 0; i < nn; i++) s[i] = pool[b + i];
                if (vo == comp::VO_OPEN) for (int i = 1; i < nn; i += 2) s[i] += 12;
                else if (vo == comp::VO_DROP2 && nn >= 2) s[nn - 2] -= 12;
                for (int i = 1; i < nn; i++) { const int x = s[i]; int j = i - 1; while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; } s[j + 1] = x; }
                for (int i = 0; i < nn; i++) { if (s[i] < v.lo || s[i] > v.hi || (i && s[i] == s[i - 1])) return false; out[i] = s[i]; }
                return true;
            };
            int nn = nn0, vo = v.voicing, best[6], tmp[6]; bool have = false;
            for (int pass = 0; pass < 3 && !have; pass++) {
                if (pass == 1) vo = comp::VO_CLOSE;
                if (pass == 2) nn = std::min(nn0, np);
                int plain = -1; for (int b = 0; b < np && plain < 0; b++) if (pc(pool[b]) == rpc && build(b, nn, vo, tmp)) plain = b;
                if (plain < 0) continue;
                const bool lead = cho_n[q] == nn;
                double bc = 1e18;
                for (int b = 0; b < np; b++) {
                    if (!build(b, nn, vo, tmp)) continue;
                    double mv = 0; if (lead) for (int i = 0; i < nn; i++) mv += std::abs(tmp[i] - cho_prev[q][i]);
                    const double c = lead ? v.strict * mv + (1 - v.strict) * std::abs(pool[b] - pool[plain]) : (b == plain ? 0 : 1);
                    if (c < bc) { bc = c; have = true; std::memcpy(best, tmp, sizeof tmp); }
                }
            }
            if (!have) return;
            cho_n[q] = nn; std::memcpy(cho_prev[q], best, sizeof best);
            const double sd = 60.0 / patch.tempo / 4 * std::max(1, (int)v.restrike) * v.len, vl = vel((k % 16) / std::max(1, (int)v.restrike));
            for (int i = 0; i < nn; i++) note(role, chord_voice(rl, time + sd), note_hz(best[i]), sd, time, vl, true);
            break;
        }
        default: break;
        }
    }
    void third_step(double time) {
        const Patch &p = patch;
        if (!p.v3.on) return;
        double T = time, m;
        Synth *sy = v3r.sy();
        timbre(sy, p.v3.synth, is_sampler(p.v3.synth) ? p.smp[2].focus : p.v3.harm, M_V3_HARM, is_sampler(p.v3.synth) ? p.smp[2].colour : p.v3.index, M_V3_INDEX, 0.4, T);
        v3L.drive(morph(M_V3_DRIVE, T, m) ? m : p.v3.drive, 11, 3.2, T);
        double wv = morph(M_V3_WARP, T, m) ? m : p.v3.warp;
        v3L.warp.wet.p.linearRampTo(wv * 0.85, 0.3, T); v3L.warp.depth = (float)(0.15 + wv * 0.85);
        v3L.cutoff.p.exponentialRampTo(morph(M_V3_CUTOFF, T, m) ? 500 * std::pow(18, m) : 7000, 0.3, T);
        int stride = stride_of(p.v3.rhythm, 16);
        T3.tick++;
        if (styled(2)) return;
        if ((T3.tick - 1) % stride != 0) return;
        if (cad_rest(2, stride < 4)) return;
        if (H.ntones <= 0 || !sy) return;
        bool taken[12] = {};
        if (H.have_bass) taken[pc(H.bass)] = true;
        if (C.have_last) taken[pc(C.last)] = true;
        int lo = p.key - 7, hi = p.key + 12;
        struct Cand { int m, idx; } pool[64]; int np = 0;
        for (int idx = 0; idx < H.ntones; idx++)
            for (int o = -2; o <= 2; o++) { int mm = H.tones[idx] + o * 12; if (mm >= lo && mm <= hi) pool[np++] = { mm, idx }; }
        if (!np) return;
        int prev = T3.have_last ? T3.last : p.key + 2;
        bool found = false; int bm = 0; double bs = 0;
        for (int k = 0; k < np; k++) {
            double score = -std::abs(pool[k].m - prev) * 0.32;
            if (taken[pc(pool[k].m)]) score -= 3.2;
            if (pool[k].idx == 1 || pool[k].idx == 3) score += 2.2;
            if (pool[k].m == prev) score -= 2.4;
            score += (rnd() - 0.5) * 1.2;
            if (!found || score > bs) { found = true; bm = pool[k].m; bs = score; }
        }
        if (!found) return;
        T3.last = bm; T3.have_last = true;
        double beat = 60 / p.tempo;
        double dur = beat * (1.5 + rnd() * 2.5);
        double f = note_hz(bm) * (1 + (rnd() - 0.5) * 0.004);
        note(3, sy, f, dur, time, 0.36 + rnd() * 0.16);
    }

    /* ---- zoneFire ---- */
    void zone_fire(int icon, double at) {
        static const struct { int osc; int oct; double dur; } TIM[7] = {
            { TRIANGLE, -1, 2.4 }, { SAWTOOTH, 0, 1.1 }, { SINE, 1, 1.8 }, { SQUARE, 2, 0.22 }, { SINE, -1, 3.2 }, { TRIANGLE, -2, 3.6 }, { -1, 0, 2.6 } };
        const auto &tim = TIM[icon >= 0 && icon < 7 ? icon : 0];
        int tones[8], nt;
        if (H.ntones >= 0) { nt = H.ntones; std::memcpy(tones, H.tones, sizeof tones); }
        else nt = chord_tones(chord_index(t_along), tones);
        int midi = tones[(int)std::floor(rnd() * nt)] + tim.oct * 12;
        OneShot *o = &shots[0];
        for (auto &s : shots) if (s.free_at < o->free_at) o = &s;   /* the oldest (ponytail: 6 at once, then the oldest is reused) */
        o->free_at = at + tim.dur + 3;
        if (tim.osc < 0) {
            o->noise = true;
            o->ns.env.set(0.08, 0.6, 0.2, tim.dur);
            if (on_note) on_note(note_ctx, 4, 0, tim.dur, at, 0.35);
            o->ns.attack(0, at, 0.35); o->ns.release(at + tim.dur);
            return;
        }
        o->noise = false;
        o->s.osc.w = &basic_wave(tim.osc);
        o->s.env.set(0.02, 0.5, 0.3, tim.dur);
        note(5, &o->s, note_hz(midi), tim.dur, at, 0.4);
    }

    /* ---- rhythmStep ---- */
    /* 5: a rate toward the chord now (its notes in Hz); off every route the last chord, before any none */
    double follow_chord(double rate, double f0, double follow) {
        double hz[8]; const int n = std::max(0, std::min(8, H.ntones));
        for (int i = 0; i < n; i++) hz[i] = note_hz(H.tones[i]);
        return harmony::follow_rate(rate, f0, hz, n, follow);
    }
    /* Test 15: one frame of a recording's peak track - the strongest spectral peak in 70 Hz - 5 kHz that stands at least 3x
       over its surroundings (+-1/6 octave), interpolated between bins. One frame per block (a 60 s recording takes ~7 s). */
    float pk_re[PK_N], pk_im[PK_N], pk_win[PK_N], pk_mag[PK_N / 2 + 1]; double pk_cum[PK_N / 2 + 2]; bool pk_ready = false;
    void peak_frame(Src &b, int fi) {
        if (!pk_ready) { for (int i = 0; i < PK_N; i++) pk_win[i] = (float)(0.5 - 0.5 * std::cos(2 * PI * i / PK_N)); pk_ready = true; }
        const long long c0 = (long long)fi * PK_HOP - PK_N / 2; const int cr = b.ch > 1 ? 1 : 0;
        for (int i = 0; i < PK_N; i++) {
            const long long k = c0 + i; float x = 0;
            if (k >= 0 && k < b.frames) x = 0.5f * (b.data[k * b.ch] + b.data[k * b.ch + cr]) / 32768.0f;
            pk_re[i] = x * pk_win[i]; pk_im[i] = 0;
        }
        fft_inplace(pk_re, pk_im, PK_N);
        const int M = PK_N / 2 + 1;
        pk_cum[0] = 0;
        for (int j = 0; j < M; j++) { pk_mag[j] = std::sqrt(pk_re[j] * pk_re[j] + pk_im[j] * pk_im[j]); pk_cum[j + 1] = pk_cum[j] + pk_mag[j]; }
        const double binhz = sr / PK_N; int best = -1; double bm = 0, bp = 0;
        for (int j = std::max(2, (int)(70 / binhz)); j < std::min(M - 2, (int)(5000 / binhz)); j++) {
            if (pk_mag[j] < pk_mag[j - 1] || pk_mag[j] < pk_mag[j + 1]) continue;
            const int lo = std::max(0, (int)(j * 0.8909)), hi = std::min(M - 1, (int)(j * 1.1225) + 1);
            const double env = (pk_cum[hi + 1] - pk_cum[lo] - pk_mag[j]) / std::max(1, hi - lo);
            if (env <= 1e-9 || pk_mag[j] < 3 * env || pk_mag[j] <= bm) continue;
            best = j; bm = pk_mag[j]; bp = pk_mag[j] / env;
        }
        float hz = 0;
        if (best > 0 && bm > 1e-4) {
            const double a = pk_mag[best - 1], c = pk_mag[best + 1], den = a - 2 * bm + c;
            hz = (float)((best + (std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0)) * binhz);
        }
        b.pk[2 * fi] = hz; b.pk[2 * fi + 1] = (float)bp;
    }
    static void fft_inplace(float *re, float *im, int n) {   /* radix-2, n a power of two */
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const double ang = -2 * PI / len; const float wr = (float)std::cos(ang), wi = (float)std::sin(ang);
            for (int i = 0; i < n; i += len) {
                float cr = 1, ci = 0;
                for (int k = 0; k < len / 2; k++) {
                    const int u = i + k, v = u + len / 2;
                    const float tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                    re[v] = re[u] - tr; im[v] = im[u] - ti; re[u] += tr; im[u] += ti;
                    const float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
    }
    void fire_grains(Rhythm &R, double time, double step_secs) {
        Src &b = R.gsrc;
        if (!b.data || !b.frames) return;
        GrainCfg g = R.cfg.g;
        double v = std::max(0.0, std::min(1.0, g.vary)), roam = std::max(0.0, std::min(1.0, g.roam));
        if (!(v <= 0 && roam <= 0)) {           /* variedGrains */
            auto r = [&]() { return (rnd() - 0.5) * 2; };
            auto cl = [](double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); };
            auto wrap = [](double x) { return std::fmod(std::fmod(x, 1) + 1, 1); };
            GrainCfg o = g;
            o.offset = wrap(g.offset + r() * roam * 0.5);
            o.grain = cl((g.grain ? g.grain : 90) * std::pow(2, r() * v * 1.4), 20, 500);
            o.count = std::round(cl((g.count ? g.count : 6) * (1 + r() * v * 1.1), 1, 32));
            o.spread = cl(g.spread + r() * v * 0.6, 0, 1);
            o.scatter = cl(g.scatter + r() * v * 0.5, 0, 1);
            const double jump = r() * v * 7;
            if ((R.cfg.resonate > 0 || R.cfg.follow > 0) && H.ntones > 0) {   /* 8c: in harmony - the chord's own intervals, nearest the jump */
                double best = 0, bd = 1e9;
                for (int k = 0; k < H.ntones; k++) { const int iv = ((H.tones[k] - H.tones[0]) % 12 + 12) % 12;
                    for (int c : { iv, iv - 12 }) if (c >= -7 && c <= 7 && std::fabs(c - jump) < bd) { bd = std::fabs(c - jump); best = c; } }
                o.pitch = g.pitch + best;
            } else o.pitch = g.pitch + std::round(jump);
            o.gain = cl(g.gain * (1 + r() * v * 0.45), 0.02, 1);
            o.attack = cl(g.attack * (1 + r() * v * 0.6), 0.002, 1);
            o.decay = cl(g.decay + r() * v * 0.35, 0, 1);
            o.sustain = cl(g.sustain + r() * v * 0.4, 0, 1);
            o.release = cl(g.release * (1 + r() * v * 0.5), 0.02, 1);
            g = o;
        }
        int n = std::max(1, std::min(32, (int)g.count));
        double grainSec = std::max(0.01, std::min(2.0, (g.grain ? g.grain : 90) / 1000));
        /* grainEnvelope */
        auto c01 = [](double x) { return std::max(0.0, std::min(1.0, x)); };
        double a = c01(g.attack), d = c01(g.decay), rr = c01(g.release), sus = c01(g.sustain);
        double floor_ = std::min(0.0008, grainSec / 8);
        double aS = std::max(floor_, a * grainSec), dS = std::max(floor_, d * grainSec), rS = std::max(floor_, rr * grainSec);
        double sum = aS + dS + rS;
        if (sum > grainSec) { double k = grainSec / sum; aS *= k; dS *= k; rS *= k; }
        double hold = std::max(0.0, grainSec - aS - dS - rS);
        double spread = c01(g.spread) * step_secs, scatter = c01(g.scatter);
        double dur = (double)b.frames / sr, base = c01(g.offset) * dur, lvl = std::max(0.02, g.gain);
        for (int i = 0; i < n; i++) {
            double at = time + (n == 1 ? 0 : ((double)i / n) * spread);
            double off = base + (rnd() - 0.5) * scatter * dur;
            off = std::max(0.0, std::min(std::max(0.0, dur - grainSec), off));
            double rate = std::pow(2, g.pitch / 12) * (1 + (rnd() - 0.5) * 0.02);
            if (R.cfg.follow > 0 && R.trk[0].n > 0) {   /* 5: the grain's pitch where it reads, toward the chord */
                const Trk &k = R.trk[0]; const int fi = (int)(off / k.hop);
                if (fi >= 0 && fi < k.n && k.tc[fi] >= 0.8f && k.tf[fi] > 0) rate = follow_chord(rate, k.tf[fi], R.cfg.follow);
            } else if (R.cfg.follow > 0 && b.pk) {      /* Test 15: unpitched - the grain's own dominant peak, at its middle */
                const int fi = (int)std::lround((off + grainSec / 2) * sr / PK_HOP);
                if (fi >= 0 && fi < R.pk_done && b.pk[2 * fi] > 0) rate = follow_chord(rate, b.pk[2 * fi], R.cfg.follow);
            }
            GrainV *gv = nullptr;
            for (auto &x : R.gv) if (!x.on) { gv = &x; break; }
            if (!gv) continue;                  /* ponytail: 192 grains at once per point */
            double w = lvl * (1 - 0.35 * ((double)i / std::max(1, n)));
            *gv = GrainV{ true, off * sr, rate, at, aS, dS, hold, sus, w, grainSec };
            R.gm[R.gmi] = { at, dur > 0 ? off / dur : 0, dur > 0 ? std::min(1.0, grainSec / dur) : 0 }; R.gmi = (R.gmi + 1) % 96; R.gmn = std::min(96, R.gmn + 1);
            if (on_note) on_note(note_ctx, 20, rate, off, at, 0);
        }
    }
    static double stretch_rate(double a) { a = a < 0 ? 0 : (a > 1 ? 1 : a); return 1 - a * 0.85; }
    void rhythm_step(double time) {
        for (auto &R : rh) {
            if (!R.used || R.dying) continue;
            RhythmCfg &r = R.cfg;
            if (!r.on) continue;
            if (!R.live) { R.live = true; for (int s = 0; s < 4; s++) R.lv[s] = Combo{ r.v[s].pulses, r.v[s].rotate }; }
            if (!R.fx_ready && !R.grains) { apply_hit_fx(R, time); R.fx_ready = true; }
            int stride = stride_of(r.div, 1);
            R.tick++;
            if (!R.grains && (R.tick - 1) % 16 == 0) {           /* advanceSentence */
                R.bars++;
                if (R.bars >= std::max(1, r.sentence_bars) && r.has_sentence) {
                    R.bars = 0;
                    R.sentence_idx = (R.sentence_idx + 1) % 4;
                    int steps = std::max(1, std::min(64, r.steps));
                    for (int s = 0; s < 4; s++) { const Combo &c = r.sentence[s][R.sentence_idx]; R.lv[s] = Combo{ std::min(c.pulses, steps), c.rotate % steps }; }
                }
            }
            if ((R.tick - 1) % stride != 0) continue;
            int steps = std::max(1, std::min(64, r.steps));
            int at = R.step % steps;
            R.step = (R.step + 1) % steps;
            if (R.grains) {
                const GrainCfg &g = r.g;
                if (g.pulses > 0 && euclid_at(std::min((int)g.pulses, steps), steps, (int)g.rotate, at))
                    fire_grains(R, time, div_seconds(r.div, patch.tempo) * stride);
                continue;
            }
            for (int s = 0; s < 4; s++) {
                const HitCfg &cfg = r.v[s]; HitSlot &h = R.hit[s];
                if (!h.src.data || R.lv[s].pulses <= 0) continue;
                if (!euclid_at(std::min(R.lv[s].pulses, steps), steps, R.lv[s].rotate, at)) continue;
                double rate = std::pow(2, cfg.pitch / 12) * (1 + (rnd() - 0.5) * 0.012);
                if (r.follow > 0 && R.trk[s].f0 > 0) rate = follow_chord(rate, R.trk[s].f0, r.follow);   /* 5: a pitched hit onto the chord */
                double accent = r.idiom * metric_weight((int)((R.tick - 1) % 16), 16) * 4;
                double vol = 20 * std::log10(std::max(0.02, cfg.gain)) + (rnd() - 0.5) * 1.5 + accent;
                h.vol = (float)db_to_gain(vol);
                if (on_note) on_note(note_ctx, 10 + s, rate, vol, time, 0);
                /* Player.start: the one playing stops with its 20 ms fade; all take the new rate */
                for (auto &pv : h.pv) if (pv.on) { pv.rate = rate; if (pv.stop > time) pv.stop = time; }
                PlayV *nv = &h.pv[0];
                for (auto &pv : h.pv) if (!pv.on) { nv = &pv; break; }
                *nv = PlayV{ true, 0, rate, time, 1e300, 0.02, {} };
                if (r.env_on) {   /* Test 15: the gate is one step of the pattern */
                    nv->env = HitEnv{ true, r.env[0], r.env[1], r.env[2], r.env[3], div_seconds(r.div, patch.tempo) * stride };
                    h.genv = nv->env; h.gstart = time;
                } else h.genv = HitEnv{};
                if (cfg.stretch > 0) {
                    double a = std::max(0.0, std::min(1.0, cfg.stretch));
                    double pr = stretch_rate(a), gs = 0.06 + a * 0.24, period = gs / pr;
                    h.gp_vol = (float)db_to_gain(vol);
                    h.gp_rate = std::pow(2, cfg.pitch / 12);
                    h.gp_pr = pr; h.gp_grain = gs; h.gp_overlap = period * (0.35 + a * 0.4);
                    double stopAt = time + 1.5;
                    h.stretch_gain.p.cancelScheduledValues(time);
                    h.stretch_gain.p.setValueAtTime(1, time);
                    h.stretch_gain.p.setValueAtTime(1, stopAt - 0.025);
                    h.stretch_gain.p.linearRampToValueAtTime(0, stopAt);
                    h.gp_on = true; h.gp_t0 = time; h.gp_stop = stopAt; h.gp_tick = 0; h.gp_next = time;
                }
            }
        }
    }

    /* ---- the Transport ----
       Tone keeps every scheduleRepeat two occurrences ahead in one timeline: an occurrence is
       inserted when the one two intervals before it fires, after anything already at its time. So
       callbacks sharing a tick run in the order their occurrences were inserted - on a bar,
       harmonyBar (inserted two bars back) before voiceStep (inserted a quarter back). This keeps
       the same timeline, in sixteenths: voiceStep 8n, harmonyBar 1m, sectorStep, rhythmStep,
       thirdStep 16n, scheduled in that order at start. */
    struct Due { long long tick, seq; int id; };
    Due due[16]; int ndue = 0; long long seq = 0;
    static int interval(int id) { return id == 0 ? 2 : id == 1 ? 16 : 1; }
    void schedule(long long tick, int id) { due[ndue++] = Due{ tick, seq++, id }; }
    void transport_start() { ndue = 0; for (int id = 0; id < 6; id++) { schedule(0, id); schedule(interval(id), id); } }
    void tick(double time) {
        if (test_walk_s > 0) t_along = std::min(1.0, time / test_walk_s);
        long long k = ticks++;
        if (k % 16 == 0 && patch.comp.on) comp_bar(k / 16, time);   /* the bar's settings before any voice plays on it */
        for (;;) {
            int best = -1;
            for (int i = 0; i < ndue; i++) if (due[i].tick == k && (best < 0 || due[i].seq < due[best].seq)) best = i;
            if (best < 0) break;
            int id = due[best].id;
            due[best] = due[--ndue];
            schedule(k + 2 * interval(id), id);
            switch (id) {
            case 0: voice_step(time); break;
            case 1: harmony_bar(time); break;
            case 2: sector_step(time); break;
            case 3: rhythm_step(time); break;
            case 5: compose_step(time); break;
            case 4: third_step(time); break;
            default: break;
            }
        }
    }

    /* ---- world walking: worldMove's route part, worldSwap, setSynthLevel ---- */
    void synth_to(double level, double now) { synth_level.p.linearRampTo(level, 0.35, now); }
    static double walk_level(double d) { return fs_walk_level(d, FS_GPS_FADE_FROM, FS_GPS_LEASH); }
    void take(double now) {
        int near = pending;
        pending = -1;
        if (reported != near || near < 0 || near >= (int)routes.size()) return;
        route = near;
        patch = routes[near];
        apply_patch(now);
        H.idx = -1; H.chord = -1; H.ntones = -1;
        sector = -1; comp_live = false; for (auto &x : sp) x = Span(); for (int &x : hcnt) x = 0; for (int q = 0; q < comp::NVOICE; q++) { rhead[q] = 0; for (auto &n : ring[q]) n.tick = -1; } nzent = 0; chg = 0; for (int &x : ufirst) x = -1; cad_chord = false; cad_oth = comp::OTH_HOLD; wk_l1 = wk_l2 = -1; cwt = -1; cdir = 0; mel_step = -2; cchord = -2; for (double &x : oend) x = -1e9; mvar_on = false; nhist = 0; for (int q = 0; q < comp::NVOICE; q++) { cped_root[q] = -1; cpulse[q] = 0; calt[q] = 0; ost_n0[q] = 0; cho_n[q] = 0; pt_next[q] = -1; pt_n[q] = 0; tcnt[q] = 0; }
        synth_to(walk_level(route_dist), now);
    }
    void walk(int r, double t, double dist, double now) {
        reported = r;
        if (r >= 0) {
            if (r != route && r != pending) {
                bool was_running = route >= 0;
                synth_to(0, now);
                pending = r;
                if (!was_running) { route_dist = dist; t_along = t; take(now); }
                else pending_at = now + 1.5;
            }
            t_along = t; route_dist = dist;
            if (r == route) synth_to(walk_level(dist), now);
        } else { route_dist = 1e300; synth_to(0, now); }
    }

    /* ---- the inbox ---- */
    void take_inbox(double now) {
        if (!mu.try_lock()) return;
        for (int i = 0; i < 3; i++) if (in.rrec_set[i]) {   /* a role's recording (3a): its sampler instances read it now */
            in.rrec_set[i] = false; live[i] = in.rrec[i];
            for (Role *r : { &bass, &top, &sectr, &v3r }) if (r->rec == i) bind_role(*r, live[i]);
        }
        while (routes.size() < in.routes.size()) routes.push_back(in.routes[routes.size()]);
        for (size_t i = 0; i < routes.size() && i < 64; i++) if (in.route_dirty[i]) {
            in.route_dirty[i] = false;
            routes[i] = in.routes[i];
            if ((int)i == route) {
                /* 3c.1: a sampler role whose settings changed re-voices its sounding notes (they were read only at a note's start) */
                const SamplerCfg was[3] = { patch.smp[0], patch.smp[1], patch.smp[2] };
                patch = routes[i]; apply_patch(now);
                revoice_changed(was, now);
            }   /* heard at once */
        }
        if (in.walk) { in.walk = false; walk(in.route, in.t, in.dist, now); }
        if (in.sector) { in.sector = false; sector = in.sector_v; }
        if (in.zones_set) {
            in.zones_set = false; nzin = in.nzin; std::memcpy(zin, in.zin, sizeof zin);
            if (in.zent) { if (nzent == 8) { std::memmove(zent, zent + 1, sizeof(Ent) * 7); nzent = 7; } zent[nzent++] = Ent{ in.zent, (ticks + 15) / 16 }; in.zent = 0; }
            /* final review M4: the phrase from the next bar line still to play (mid-bar, this bar's comp_bar has run) */
        }
        if (in.character) { in.character = false; centroid = in.centroid; onsets = in.onsets; }
        for (int i = 0; i < in.nzones; i++) if (patch.zones_on) zone_fire(in.zones[i], now + 0.008);
        in.nzones = 0;
        if (in.solo_set) {
            in.solo_set = false; solo = in.solo_v;
            route_solo.p.linearRampTo(solo >= 0 ? 0 : route_gate, 0.35, now);
            for (int h = 0; h < FS_MAX_VOICES; h++) if (rh[h].used && !rh[h].dying) rh[h].gain.p.linearRampTo(rhythm_level(rh[h], h), 0.35, now);
        }
        if (in.gate_set) { in.gate_set = false; route_gate = in.gate_v; route_solo.p.linearRampTo(solo >= 0 ? 0 : route_gate, 0.35, now); }
        for (int h = 0; h < FS_MAX_VOICES; h++) {
            auto &op = in.r[h]; Rhythm &R = rh[h];
            if (op.op == 1) {
                R.used = true; R.dying = false; R.cfg = op.cfg; R.grains = op.grains; for (auto &k : R.trk) k = Trk{};
                R.lvl = 0; R.gmi = R.gmn = 0; R.cr = ChordRes();
                R.eq.init(sr); for (int k = 0; k < 5; k++) R.eq.set(k, R.cfg.eq[k][0], R.cfg.eq[k][1], R.cfg.eq[k][2]);
                R.tick = 0; R.step = 0; R.live = false; R.bars = 0; R.sentence_idx = 0; R.fx_ready = false;
                R.gain.init(0);
                for (auto &g : R.gv) g.on = false;
                init_hits(R, false);
                for (auto &hs : R.hit) { for (auto &pv : hs.pv) pv.on = false; hs.gp_on = false; for (auto &g : hs.gs) g.on = false; }
            } else if (op.op == 3 && R.used && !R.dying) {
                R.cfg = op.cfg; R.fx_ready = false;
                for (int k = 0; k < 5; k++) R.eq.set(k, R.cfg.eq[k][0], R.cfg.eq[k][1], R.cfg.eq[k][2]);
                R.gain.p.linearRampTo(rhythm_level(R, h), 0.35, now);
            } else if (op.op == 2 && R.used && !R.dying) {
                R.dying = true; R.free_at = now + 0.4;
                R.gain.p.linearRampTo(0, 0.25, now);
            }
            op.op = 0;
            if (op.gain) { op.gain = false; R.host_g = op.g; R.gain.p.linearRampTo(rhythm_level(R, h), 0.35, now); }
            for (int s = 0; s < 4; s++) if (op.src_set[s]) {
                op.src_set[s] = false; R.trk[s] = Trk{};      /* a new recording: the last one's track goes (final review 5 I1) */
                Src &dst = R.grains ? R.gsrc : R.hit[s].src;
                if (dst.data) my_trash.push_back(dst.data);
                if (dst.pk) my_trash.push_back((short *)dst.pk);
                dst = op.src[s];
                if (R.grains) R.pk_done = 0;
                if (!R.grains) {       /* ensureRhythm: the stretch blend at the slot's amount */
                    double amt = R.cfg.v[s].stretch;
                    R.hit[s].blend.p.linearRampTo(amt, 0.2, now);
                }
            }
            for (int s = 0; s < 4; s++) if (op.trk_set[s]) { op.trk_set[s] = false; R.trk[s] = op.trk[s]; }
        }
        for (int h = 0; h < FS_MAX_VOICES; h++) {
            Rhythm &R = rh[h];
            if (R.used && R.dying && now >= R.free_at) {
                R.used = false; R.dying = false;
                for (auto &hs : R.hit) if (hs.src.data) { my_trash.push_back(hs.src.data); if (hs.src.pk) my_trash.push_back((short *)hs.src.pk); hs.src = Src(); }
                if (R.gsrc.data) { my_trash.push_back(R.gsrc.data); if (R.gsrc.pk) my_trash.push_back((short *)R.gsrc.pk); R.gsrc = Src(); }
                in.freed[h] = true;
            }
        }
        size_t kept = 0;
        for (short *p : my_trash) { if (in.trash.size() < in.trash.capacity()) in.trash.push_back(p); else my_trash[kept++] = p; }
        my_trash.resize(kept);
        in.taken = route; in.sect_n = patch.sect.n; in.bed_voices = patch.bed_on ? patch.bed_voices : 0;
        in.chord = H.chord; in.nprog = patch.nprog;
        in.clock = now; in.root = pc12(H.chord >= 0 ? chord_root(H.chord) : patch.key);
        if (H.chord >= 0) {
            int q = chord_quality(H.chord), r = chord_root(H.chord);
            int n = q >= 0 ? CHORDS[q].n : 4; if (n > 5) n = 5;
            for (int i = 0; i < n; i++) in.chord_notes[i] = (float)(r + (q >= 0 ? CHORDS[q].iv[i] : (i == 0 ? 0 : i == 1 ? 3 : i == 2 ? 7 : 10)));
            in.chord_n = n; in.chord_root_midi = (float)r;
        } else in.chord_n = -1;
        if (H.chord >= 0) {                /* chordLabel: root name + quality, as the web writes it */
            int q = chord_quality(H.chord);
            std::snprintf(in.chord_label, sizeof in.chord_label, "%s%s", NOTE[pc(chord_root(H.chord))], q >= 0 ? CHORDS[q].name : "m7");
        } else in.chord_label[0] = 0;
        if (!in.sector) in.sector_back = sector;
        mu.unlock();
    }

    /* ---- rendering ---- */
    /* a rhythm point's level: what the walk gave it, unless another point is soloed */
    float rhythm_level(const Rhythm &R, int h) const {
        return (solo < 0 || solo == h) ? R.host_g * (R.cfg.on ? R.cfg.gain : 0) : 0.0f;
    }
    void chord_res(Rhythm &R, float *bl, float *br, int n) {
        ChordRes &c = R.cr; const int body = R.cfg.body; const double rt = R.cfg.resonate;
        static const double BR[4] = { 1.0, 2.76, 5.40, 8.93 }, BW[4] = { 1.0, 0.6, 0.35, 0.2 };
        long long sig = body; for (int k = 0; k < H.ntones; k++) sig = sig * 131 + H.tones[k];
        if (H.ntones > 0 && sig != c.sig) {   /* a new chord: the loops' targets (a first one taken at once); between routes, the last one rings on */
            const bool first = c.sig == -1; c.sig = sig; c.nn = std::min(8, H.ntones); c.nrun = std::max(c.nrun, c.nn);
            for (int k = 0; k < c.nn; k++) {
                double f = 440 * std::pow(2.0, (H.tones[k] - 69) / 12.0); while (f >= 110) f /= 2; while (f < 55) f *= 2;
                while (sr / f > 2000) f *= 2;   /* final review 8c C1: the loop fits its 2048-sample line at any rate (an octave up past ~110 kHz) */
                c.Nt[k] = sr / f * (body == 1 ? 0.5 : 1); if (first || c.N[k] <= 0) c.N[k] = c.Nt[k];
            }
        }
        /* the level matched over 2 s: grains come in bursts and the loops ring on between them - a short window chased each burst */
        const double kg = 1 - std::exp(-n / (0.3 * sr)), T60 = 1.2, kl = 1 - std::exp(-n / (2.0 * sr)), kr = 1 - std::exp(-1 / (0.03 * sr));
        for (int k = 0; k < c.nn; k++) c.N[k] += (c.Nt[k] - c.N[k]) * kg;
        double mb0[8][4], ma1[8][4], ma2[8][4], la[8], ld[8], gk[8];
        for (int k = 0; k < c.nrun; k++) {
            /* the note the loop sounds (a Tube's inverted half-period loop rings at its whole period: tuned and damped at that note
               too - final review 8c I1); its loss per trip made up to a 1.2 s ring, the damping's own delay taken out */
            const double f = sr / c.N[k] * (body == 1 ? 0.5 : 1), w = 2 * 3.141592653589793 * f / sr;
            la[k] = 1 - std::exp(-2 * 3.141592653589793 * 3 * f / sr);   /* the loop's damping at 3x its note */
            ld[k] = sampler::lp_delay(1 - la[k], w);
            gk[k] = std::fmin(0.9995, std::pow(10.0, -3.0 * c.N[k] / (sr * T60)) / sampler::lp_gain(1 - la[k], w));
        }
        if (body == 2) for (int k = 0; k < c.nrun; k++) {   /* bell modes at the gliding pitch */
            const double f = sr / c.N[k];
            for (int m = 0; m < 4; m++) { const double fk = std::fmin(f * BR[m], 0.45 * sr), wk = 2 * 3.141592653589793 * fk / sr, rr = std::pow(10.0, -3.0 / (T60 * (1 - 0.15 * m) * sr));
                ma1[k][m] = -2 * rr * std::cos(wk); ma2[k][m] = rr * rr; mb0[k][m] = (1 - rr) * BW[m]; }
        }
        double ed = 0, ew = 0;
        for (int i = 0; i < n; i++) {
            const double x0 = 0.5 * (bl[i] + br[i]); double wet = 0;
            for (int k = 0; k < c.nrun; k++) {
                const double x = k < c.nn ? x0 : 0;   /* final review 8c I2: a note the chord dropped rings out, unstruck */
                if (body == 2) { for (int m = 0; m < 4; m++) { const double o = mb0[k][m] * x - ma1[k][m] * c.y1[k][m] - ma2[k][m] * c.y2[k][m]; c.y2[k][m] = c.y1[k][m]; c.y1[k][m] = std::fabs(o) < 1e-20 ? 0 : o; wet += o; } continue; }
                const double rp = c.w - (c.N[k] - ld[k]); int i0 = (int)std::floor(rp); const double fr = rp - i0;
                const double rd = c.line[k][i0 & 2047] * (1 - fr) + c.line[k][(i0 + 1) & 2047] * fr;
                c.lp[k] += la[k] * (rd - c.lp[k]); if (std::fabs(c.lp[k]) < 1e-20) c.lp[k] = 0;   /* its first few overtones: an octave, a fifth */
                const double y = x + (body == 1 ? -gk[k] : gk[k]) * c.lp[k];
                c.line[k][c.w] = (float)y; wet += y - x;   /* its ringing only, not the noise that struck it */
            }
            c.w = (c.w + 1) & 2047;
            wet *= c.agc;
            ed += 0.5 * ((double)bl[i] * bl[i] + (double)br[i] * br[i]); ew += wet * wet;   /* per channel, as heard */
            c.res += (rt - c.res) * kr;   /* final review 8c I3: the mix glides (30 ms) - a moved slider, a resonate switched off */
            const float o = (float)(wet * c.res);
            bl[i] = (float)(bl[i] * (1 - c.res)) + o; br[i] = (float)(br[i] * (1 - c.res)) + o;
        }
        /* the two 2 s averages give the gain directly (no lag on top), only while the grains sound: a silence is never raised.
           ponytail: the loops (String, Tube) ring on between bursts, which reads ~3 dB over the grains (measured on noise grains,
           6 per pulse) - a fixed -3 dB trim; a loudness model if sparse clouds show it is off */
        c.ms_dry += (ed / n - c.ms_dry) * kl; c.ms_wet += (ew / n / (c.agc * c.agc + 1e-30) - c.ms_wet) * kl;
        if (c.ms_dry > 1e-10 && c.ms_wet > 1e-14) c.agc = std::fmin(100.0, std::sqrt(c.ms_dry / c.ms_wet)) * (body == 2 ? 1.0 : 0.708);
    }
    void render_rhythm(Rhythm &R, float *L, float *Rr, int n, double t0) {
        double te = t0 + n / sr;
        R.gain.block(te);
        float bl[SUB] = {}, br[SUB] = {};
        if (R.grains) {
            for (auto &g : R.gv) {
                if (!g.on) continue;
                for (int i = 0; i < n; i++) {
                    double t = t0 + i / sr;
                    if (t < g.at) continue;
                    double u = t - g.at, e;
                    if (u >= g.len) { g.on = false; break; }
                    if (u < g.a) e = g.w * u / g.a;
                    else if (u < g.a + g.d) e = g.w + (g.w * g.s - g.w) * (u - g.a) / g.d;
                    else if (u < g.a + g.d + g.hold) e = g.w * g.s;
                    else { double rl = g.len - (g.a + g.d + g.hold); e = rl > 0 ? g.w * g.s * (1 - (u - g.a - g.d - g.hold) / rl) : 0; }
                    float l, r; src_read(R.gsrc, g.pos, l, r);
                    g.pos += g.rate;
                    bl[i] += l * (float)e; br[i] += r * (float)e;
                }
            }
        } else {
            for (auto &h : R.hit) {
                if (!h.src.data) continue;
                h.stretch_gain.block(te); h.blend.block(te); h.crush_fade.block(te); h.bits.block(te);
                h.drive_fade.block(te); h.pre.block(te); h.post.block(te); h.delay_fade.block(te);
                float xl[SUB] = {}, xr[SUB] = {}, sl[SUB] = {}, sr_[SUB] = {};
                for (auto &pv : h.pv) {
                    if (!pv.on) continue;
                    for (int i = 0; i < n; i++) {
                        double t = t0 + i / sr;
                        if (t < pv.start) continue;
                        double g = 1;
                        if (t >= pv.stop) { g = 1 - (t - pv.stop) / pv.fade; if (g <= 0) { pv.on = false; break; } }
                        if (pv.env.on) { const double e = pv.env.at(t - pv.start); if (e < 0) { pv.on = false; break; } g *= e; }
                        if (pv.pos >= h.src.frames) { pv.on = false; break; }
                        float l, r; src_read(h.src, pv.pos, l, r);
                        pv.pos += pv.rate;
                        xl[i] += l * (float)g * h.vol; xr[i] += r * (float)g * h.vol;
                    }
                }
                if (h.gp_on) {         /* GrainPlayer: a grain every grainSize / playbackRate, reading grainSize further on */
                    for (int i = 0; i < n; i++) {
                        double t = t0 + i / sr;
                        if (t >= h.gp_stop) { h.gp_on = false; for (auto &g : h.gs) g.on = false; break; }
                        while (h.gp_on && h.gp_next <= t) {
                            double off = h.gp_tick * h.gp_grain;
                            if (off > (double)h.src.frames / sr) { h.gp_stop = h.gp_next; break; }
                            for (auto &g : h.gs) if (!g.on) {
                                g = GrainSrc{ true, off * sr, h.gp_rate, h.gp_next, h.gp_next + h.gp_grain / h.gp_pr, off < h.gp_overlap ? 0 : h.gp_overlap, h.gp_overlap };
                                break;
                            }
                            h.gp_tick++;
                            h.gp_next = h.gp_t0 + h.gp_tick * h.gp_grain / h.gp_pr;
                        }
                        for (auto &g : h.gs) {
                            if (!g.on || t < g.start) continue;
                            double e = 1;
                            if (g.fade_in > 0 && t - g.start < g.fade_in) e = (t - g.start) / g.fade_in;
                            if (t >= g.stop) { e = g.fade_out > 0 ? 1 - (t - g.stop) / g.fade_out : 0; if (e <= 0) { g.on = false; continue; } }
                            if (h.genv.on) { const double he = h.genv.at(t - h.gstart); e *= he < 0 ? 0 : he; }
                            float l, r; src_read(h.src, g.pos, l, r);
                            g.pos += g.rate;
                            sl[i] += l * (float)e * h.gp_vol; sr_[i] += r * (float)e * h.gp_vol;
                        }
                    }
                }
                float yl[SUB], yr[SUB];
                for (int i = 0; i < n; i++) {
                    float b = h.blend.at(i, n), sg = h.stretch_gain.at(i, n);
                    float l = xl[i] * (1 - b) + sl[i] * sg * b, r = xr[i] * (1 - b) + sr_[i] * sg * b;
                    /* crush and drive at the hit's own scale (its recording's peak at this hit's level), so a quiet field
                       recording is crushed and driven as a loud one is, not rounded to silence (Test 15: it vanished) */
                    const float ref = std::max(1e-4f, h.src.peak * h.vol), inv = 1 / ref;
                    float cf = h.crush_fade.at(i, n), bits = h.bits.at(i, n);
                    l = l * (1 - cf) + ref * crush(l * inv, bits) * cf; r = r * (1 - cf) + ref * crush(r * inv, bits) * cf;
                    float df = h.drive_fade.at(i, n), pr = h.pre.at(i, n), po = h.post.at(i, n);
                    l = l * (1 - df) + ref * po * h.shp.run(pr * l * inv) * df; r = r * (1 - df) + ref * po * h.shp.run(pr * r * inv) * df;
                    yl[i] = l; yr[i] = r;
                }
                float dl_[SUB], dr_[SUB];
                std::memcpy(dl_, yl, sizeof(float) * n); std::memcpy(dr_, yr, sizeof(float) * n);
                h.dl.process(dl_, dr_, n, te);
                for (int i = 0; i < n; i++) {
                    float w = h.delay_fade.at(i, n);
                    bl[i] += yl[i] * (1 - w) + dl_[i] * w; br[i] += yr[i] * (1 - w) + dr_[i] * w;
                }
            }
        }
        /* 8c; and hits too (Kerem 2026-10-13: unpitched hits ring the chord as grains do) */
        if ((R.cfg.resonate > 0 || R.cr.res > 1e-4) && (H.ntones > 0 || R.cr.sig != -1)) chord_res(R, bl, br, n);
        R.eq.process(bl, br, n);   /* Test 15: flat, the input untouched */
        if (R.grains && R.gsrc.pk && R.pk_done < R.gsrc.pkn) peak_frame(R.gsrc, R.pk_done++);
        /* 8a: the point's meter - its own sound at its own gain, not where the walker stands (as a stretch point's is) */
        double e = 0; const double own = R.cfg.on ? R.cfg.gain : 0;
        for (int i = 0; i < n; i++) { float g = R.gain.at(i, n); L[i] += bl[i] * g; Rr[i] += br[i] * g; e += 0.5 * ((double)bl[i] * bl[i] + (double)br[i] * br[i]); }
        R.lvl += (e * own * own / std::max(1, n) - R.lvl) * (1 - std::exp(-n / (0.1 * sr)));
    }

    void render(float *outL, float *outR, int n, double t0) {
        double te = t0 + n / sr;
        std::memset(oL, 0, sizeof oL); std::memset(oR, 0, sizeof oR);
        /* the route: synths into their layers, layers into their chains, chains into the synth bus */
        bass_g.block(te); top_g.block(te);
        float bL[SUB] = {}, bR[SUB] = {}, tL[SUB] = {}, tR[SUB] = {};
        auto run_role = [&](Role &r, float *L, float *R) {
            for (int k = 0; k < NSYNTH; k++) if ((k == r.cur || r.until[k] > t0) && r.inst[k]) {
                r.inst[k]->render(L, R, n, t0);
                for (int j = 0; j < Role::NPOOL; j++) if (r.busy[k][j] > t0) {   /* C1: idle, or under -80 dB, not run */
                    const Envelope *e = r.pool[k][j]->amp();
                    if (!e || !e->quiet(t0, te)) r.pool[k][j]->render(L, R, n, t0);
                }
            }
        };
        run_role(bass, bL, bR); run_role(top, tL, tR);
        for (int i = 0; i < n; i++) {
            pad.L[i] += bL[i] * bass_g.at(i, n) + tL[i] * top_g.at(i, n);
            pad.R[i] += bR[i] * bass_g.at(i, n) + tR[i] * top_g.at(i, n);
        }
        run_role(sectr, sectL.L, sectL.R);
        run_role(v3r, v3L.L, v3L.R);
        pad.process(fx.L, fx.R, n, te);
        sectL.process(fx2.L, fx2.R, n, te);
        v3L.process(fx3.L, fx3.R, n, te);
        eqs[0].process(fx.L, fx.R, n); eqs[1].process(fx2.L, fx2.R, n); eqs[2].process(fx3.L, fx3.R, n);
        {   /* 3c.1 F4: what each role puts out - after its drive, warp and gain, before its effects (each layer alone in its
               chain's input here: the chains clear after every block, and the one-shots are added below), ~0.1 s mean square */
            const double k = 1 - std::exp(-n / (0.1 * sr));
            float *Ls[3] = { fx.L, fx2.L, fx3.L }, *Rs[3] = { fx.R, fx2.R, fx3.R };
            for (int q = 0; q < 3; q++) { double s = 0; for (int i = 0; i < n; i++) s += 0.5 * ((double)Ls[q][i] * Ls[q][i] + (double)Rs[q][i] * Rs[q][i]); lvl[q] += (s / n - lvl[q]) * k; }
        }
        for (auto &o : shots) if (o.free_at > t0) { if (o.noise) o.ns.render(fx.L, fx.R, n, t0); else o.s.render(fx.L, fx.R, n, t0); }
        fx.process(oL, oR, n, te); fx2.process(oL, oR, n, te); fx3.process(oL, oR, n, te);
        synth_level.block(te); route_solo.block(te);
        for (int i = 0; i < n; i++) { float g = synth_level.at(i, n) * route_solo.at(i, n); oL[i] *= g; oR[i] *= g; }
        /* the rhythm points, through the rhythm effects, beside the route (bed.master, not bed.synth) */
        bool any = false;
        for (auto &R : rh) if (R.used) { any = true; render_rhythm(R, rdrive.L, rdrive.R, n, t0); }
        (void)any;
        rdrive.process(rfx.L, rfx.R, n, te);
        rfx.process(oL, oR, n, te);
        for (int i = 0; i < n; i++) { outL[i] = oL[i]; outR[i] = oR[i]; }
    }

    void process(int frames) override {
        float *L = out[0].data(), *R = out[1].data();
        int done = 0;
        while (done < frames) {
            int n = std::min(SUB, frames - done);
            double t0 = (double)frame / sr, te = (double)(frame + n) / sr;
            take_inbox(t0);
            if (pending >= 0 && t0 >= pending_at) take(t0);
            while (next_tick < te) {
                tick(next_tick);
                if (bpm != tick_bpm) { tick_base_t = next_tick; tick_base_k = ticks - 1; tick_bpm = bpm; }
                next_tick = tick_base_t + (double)(ticks - tick_base_k) * 60.0 / tick_bpm / 4;
            }
            render(L + done, R + done, n, t0);
            frame += n; done += n;
        }
    }
};

Device *make_piece() { return new Piece(); }

/* ------------------------------------------------------------------ the C interface */
static Piece *P(fs_device *d) { return d ? (Piece *)fs_device_impl(d)->cast("piece") : nullptr; }

extern "C" {

static void sweep(Piece *p) {    /* under the lock: free what the audio thread let go of */
    for (int r = 0; r < 3; r++) if (!p->in.rrec_set[r] && !p->retire[r].empty()) {   /* its successor taken: nothing reads these */
        for (float *q : p->retire[r]) { auto it = std::find(p->keep.b.begin(), p->keep.b.end(), q); if (it != p->keep.b.end()) p->keep.b.erase(it); std::free(q); }
        p->retire[r].clear();
    }
    for (short *b : p->in.trash) std::free(b);
    p->in.trash.clear();
    for (int h = 0; h < FS_MAX_VOICES; h++) if (p->in.freed[h]) { p->in.freed[h] = false; p->busy[h] = false; }
    if (p->in.trash.capacity() < 64) p->in.trash.reserve(64);
}

int fs_piece_add_route(fs_device *d, const char *patch_json) {
    Piece *p = P(d); if (!p) return -1;
    Json j = Json::parse(patch_json);
    std::unique_ptr<Patch> ptp(new Patch); Patch &pt = *ptp; patch_of(&j, pt, &p->gmemo);
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->need_all(pt);
    if (p->in.routes.capacity() < 64) p->in.routes.reserve(64);
    if (p->in.routes.size() >= 64) return -1;     /* ponytail: 64 routes; the vector must not move under the audio thread */
    p->in.routes.push_back(pt);
    return (int)p->in.routes.size() - 1;
}

/* sample harmony 3a: a role's recording (0 voice, 1 sections, 2 third voice) - mixed to mono and normalised to -20 dBFS
   RMS over its sounding part (50 ms blocks within 40 dB of the loudest), so every recording plays at one level - and its
   analysis (fs_analyse's JSON: f0, hop_s, track). The audio thread takes them at its next block. */
void fs_piece_role_source(fs_device *d, int role, int channels, long long frames, const float *const *pcm) {
    Piece *p = P(d); if (!p || role < 0 || role > 2 || !pcm || frames <= 0 || channels < 1) return;
    float *m = (float *)std::malloc(sizeof(float) * (size_t)frames); if (!m) return;
    const int ch = channels > 1 ? 2 : 1;
    for (long long i = 0; i < frames; i++) { double v = 0; for (int c = 0; c < ch; c++) v += pcm[c][i]; m[i] = (float)(v / ch); }
    const long long B = 2400; double top = 0; std::vector<double> e;
    for (long long a = 0; a < frames; a += B) { const long long z = std::min(frames, a + B); double s = 0; for (long long i = a; i < z; i++) s += (double)m[i] * m[i]; e.push_back(s / (double)(z - a)); top = std::max(top, e.back()); }
    double sum = 0; long cnt = 0; for (double s : e) if (s >= top * 1e-4) { sum += s; cnt++; }
    const double rms = cnt ? std::sqrt(sum / cnt) : 0, gain = rms > 1e-9 ? 0.1 / rms : 0;
    for (long long i = 0; i < frames; i++) m[i] = (float)(m[i] * gain);
    std::vector<float> sp; sampler::spectrum_of(m, frames, p->sr, sp);                 /* 3c.1 F2, before the lock */
    float *spec = sp.empty() ? nullptr : (float *)std::malloc(sizeof(float) * sp.size());
    if (spec) std::copy(sp.begin(), sp.end(), spec);
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    for (float *old : { p->known[role].pcm, p->known[role].spec }) if (old) p->retire[role].push_back(old);
    p->keep.b.push_back(m);
    p->known[role].pcm = m; p->known[role].n = frames;
    if (spec) p->keep.b.push_back(spec);
    p->known[role].spec = spec; p->known[role].spec_n = spec ? (int)sp.size() : 0;
    p->in.rrec[role] = p->known[role]; p->in.rrec_set[role] = true;
}
/* 6b: web/lab-roles.js compactPlan + compactData, line for line (tests/role-compact.test.mjs holds them to each other) */
static std::vector<unsigned char> role_plan(const float *const *pcm, int ch, long long n, double sr, long long &B, std::vector<std::pair<long long, long long>> &segs) {
    B = std::max(1LL, (long long)std::llround(0.05 * sr));
    const long long nb = (n + B - 1) / B; std::vector<double> e((size_t)nb); double top = 0;
    for (long long b = 0; b < nb; b++) {
        double s = 0; const long long z = std::min(n, (b + 1) * B);
        for (int c = 0; c < ch; c++) for (long long i = b * B; i < z; i++) s += (double)pcm[c][i] * pcm[c][i];
        e[(size_t)b] = s / ((double)(z - b * B) * ch); if (e[(size_t)b] > top) top = e[(size_t)b];
    }
    std::vector<unsigned char> kept((size_t)nb);
    for (long long b = 0; b < nb; b++) kept[(size_t)b] = top > 0 && e[(size_t)b] >= top * 1e-4 ? 1 : 0;
    if (top <= 0) std::fill(kept.begin(), kept.end(), 1);
    const long long gap = (long long)std::ceil(0.25 / 0.05);
    for (long long b = 0; b < nb; b++) {
        if (kept[(size_t)b]) continue;
        long long r = b; while (r < nb && !kept[(size_t)r]) r++;
        if (b > 0 && r < nb && r - b < gap) for (long long k = b; k < r; k++) kept[(size_t)k] = 1;
        b = r;
    }
    segs.clear();
    for (long long b = 0; b < nb; b++) if (kept[(size_t)b] && (b == 0 || !kept[(size_t)b - 1])) { long long q = b; while (q < nb && kept[(size_t)q]) q++; segs.push_back({ b * B, std::min(n, q * B) }); }
    return kept;
}
long long fs_role_compact(const float *const *pcm, int ch, long long n, double sr, float *const *out, unsigned char *kept_out, int kept_max) {
    if (!pcm || !out || ch < 1 || n <= 0) return 0;
    long long B; std::vector<std::pair<long long, long long>> segs;
    const std::vector<unsigned char> kept = role_plan(pcm, ch, n, sr, B, segs);
    const long long F = std::max(1LL, (long long)std::llround(0.005 * sr));
    const bool whole = segs.size() == 1 && segs[0].first == 0 && segs[0].second == n;
    long long o = 0;
    for (size_t si = 0; si < segs.size(); si++) {
        const long long a = segs[si].first, len = segs[si].second - a;
        for (int c = 0; c < ch; c++) {
            std::copy(pcm[c] + a, pcm[c] + a + len, out[c] + o);
            if (whole) continue;
            for (long long i = 0; i < std::min(F, len); i++) {
                const float g = (float)(0.5 - 0.5 * std::cos(3.141592653589793 * (i + 0.5) / F));   /* raised cosine: no step at a join */
                if (si > 0 || a > 0) out[c][o + i] *= g;
                if (si < segs.size() - 1 || segs[si].second < n) out[c][o + len - 1 - i] *= g;
            }
        }
        o += len;
    }
    if (kept_out) for (int b = 0; b < kept_max && b < (int)kept.size(); b++) kept_out[b] = kept[(size_t)b];
    return o;
}
static void role_analysis_arrays(Piece *p, int role, double f0, double hop, float *tf, float *tc, int n);
void fs_piece_role_sample(fs_device *d, int role, int channels, long long frames, const float *const *pcm, const char *json) {
    Piece *p = P(d); if (!p || role < 0 || role > 2 || !pcm || frames <= 0 || channels < 1) return;
    const int ch = channels > 1 ? 2 : 1;
    const long long n = std::min(frames, (long long)std::llround(30.0 * p->sr));   /* the first 30 s (lab-roles MAX_S) */
    std::vector<std::vector<float>> o((size_t)ch, std::vector<float>((size_t)n));
    float *op[2] = { o[0].data(), ch > 1 ? o[1].data() : nullptr };
    long long B; std::vector<std::pair<long long, long long>> segs;
    const std::vector<unsigned char> kept = role_plan(pcm, ch, n, p->sr, B, segs);
    const long long m = fs_role_compact(pcm, ch, n, p->sr, op, nullptr, 0);
    const float *cp[2] = { op[0], op[1] };
    fs_piece_role_source(d, role, ch, std::max(1LL, m), cp);
    /* the analysis: its track's frames in kept blocks only (compactData's filter) */
    Json j = Json::parse(json ? json : "{}");
    const Json *tr = j.get("track"); const double hop = j.n("hop_s", 0.02);
    std::vector<float> f, c;
    for (int i = 0; tr && i < (int)tr->size(); i++) {
        const long long b = (long long)std::floor(i * hop * p->sr / B);
        if (b >= (long long)kept.size() || !kept[(size_t)b]) continue;
        const Json *fr = tr->at(i); const Json *a = fr ? fr->at(0) : nullptr, *q = fr ? fr->at(1) : nullptr;
        f.push_back(a ? (float)a->num : 0); c.push_back(q ? (float)q->num : 0);
    }
    float *tf = f.empty() ? nullptr : (float *)std::malloc(sizeof(float) * f.size());
    float *tc = f.empty() ? nullptr : (float *)std::malloc(sizeof(float) * f.size());
    if (tf && tc) { std::copy(f.begin(), f.end(), tf); std::copy(c.begin(), c.end(), tc); }
    role_analysis_arrays(p, role, j.n("f0", 0), hop, tf, tc, (int)f.size());
}
void fs_piece_role_analysis(fs_device *d, int role, const char *json) {
    Piece *p = P(d); if (!p || role < 0 || role > 2 || !json) return;
    Json j = Json::parse(json);
    const Json *tr = j.get("track"); const int n = tr ? (int)tr->size() : 0;
    float *tf = n ? (float *)std::malloc(sizeof(float) * n) : nullptr, *tc = n ? (float *)std::malloc(sizeof(float) * n) : nullptr;
    for (int i = 0; i < n && tf && tc; i++) {
        const Json *fr = tr->at(i); const Json *a = fr ? fr->at(0) : nullptr, *b = fr ? fr->at(1) : nullptr;
        tf[i] = a ? (float)a->num : 0; tc[i] = b ? (float)b->num : 0;
    }
    role_analysis_arrays(p, role, j.n("f0", 0), j.n("hop_s", 0.02), tf, tc, n);
}
static void role_analysis_arrays(Piece *p, int role, double f0, double hop, float *tf, float *tc, int n) {
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    for (float *old : { p->known[role].tf, p->known[role].tc }) if (old) p->retire[role].push_back(old);
    if (tf) p->keep.b.push_back(tf); if (tc) p->keep.b.push_back(tc);
    RoleRec &k = p->known[role];
    k.f0 = f0; k.hop = hop;
    k.tf = tf && tc ? tf : nullptr; k.tc = tf && tc ? tc : nullptr; k.tn = tf && tc ? n : 0;
    p->in.rrec[role] = k; p->in.rrec_set[role] = true;
}
/* 8a: a rhythm point's own level and its recent grains (newest first; only those already sounding) */
int fs_piece_point(fs_device *d, int h, float *out, int max) {
    Piece *p = P(d); if (!p || !out || h < 0 || h >= FS_MAX_VOICES || !p->rh[h].used) return -1;
    const Rhythm &R = p->rh[h]; const double now = (double)p->frame / p->sr;
    out[0] = (float)std::fmax(-120.0, 10 * std::log10(R.lvl + 1e-30)); out[1] = 0;
    int c = 0;
    for (int k = 0; k < R.gmn && c < max; k++) {
        const auto &g = R.gm[((R.gmi - 1 - k) % 96 + 96) % 96];
        if (g.at > now) continue;
        out[2 + 3 * c] = (float)(now - g.at); out[3 + 3 * c] = (float)g.pos; out[4 + 3 * c] = (float)g.len; c++;
    }
    out[1] = (float)c;
    return c;
}
int fs_piece_roles(fs_device *d, float *out, int max_notes) {
    Piece *p = P(d); if (!p || !out || max_notes < 0) return 0;
    Role *rs[3][2] = { { &p->bass, &p->top }, { &p->sectr, nullptr }, { &p->v3r, nullptr } };
    const int W = 2 + max_notes;
    for (int q = 0; q < 3; q++) {
        float *o = out + q * W; o[0] = (float)std::fmax(-120.0, 10 * std::log10(p->lvl[q] + 1e-30)); o[1] = 0;
        for (int i = 0; i < max_notes; i++) o[2 + i] = 0;
        int c = 0; const double now = (double)p->frame / p->sr;
        const int hid[3][2] = { { 0, 1 }, { 2, -1 }, { 3, -1 } };
        for (int ri = 0; ri < 2; ri++) {
            Role *r = rs[q][ri];
            if (!r || c >= max_notes) continue;
            if (!is_sampler(r->cur)) {   /* 6c: a digital role's notes as played, while it is heard (a stopped route names none) */
                if (o[0] < -70) continue;
                for (const auto &h : p->heard[hid[q][ri]]) if (c < max_notes && h.from <= now && now < h.until) o[2 + c++] = (float)h.f;
                continue;
            }
            if (!r->inst[r->cur]) continue;
            double f[32]; const int want = std::min(max_notes - c, 32);
            const int got = static_cast<sampler::Resonator *>(r->inst[r->cur].get())->sounding(f, want);
            for (int i = 0; i < got; i++) o[2 + c++] = (float)f[i];
        }
        o[1] = (float)c;
    }
    return 3 * W;
}

/* A setter changed route i's patch: it replaces the registered one, and is heard at once if that route plays. */
void fs_piece_set_route(fs_device *d, int i, const char *patch_json) {
    Piece *p = P(d); if (!p) return;
    Json j = Json::parse(patch_json);
    std::unique_ptr<Patch> ptp(new Patch); Patch &pt = *ptp; patch_of(&j, pt, &p->gmemo);      /* as fs_piece_add_route reads it */
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    if (i < 0 || i >= (int)p->in.routes.size() || i >= 64) return;
    p->need_all(pt);
    p->in.routes[i] = pt; p->in.route_dirty[i] = true;
}
/* A rhythm point's pattern changed: the voice keeps its place and its recordings. */
void fs_piece_rhythm_config(fs_device *d, int h, const char *rhythm_json) {
    Piece *p = P(d); if (!p || h < 0 || h >= FS_MAX_VOICES) return;
    Json j = Json::parse(rhythm_json);
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    if (!p->busy[h]) return;
    auto &op = p->in.r[h];
    if (op.op == 1) { rhythm_of(j.kind == Json::OBJ ? &j : nullptr, op.cfg, p->main_rnd); return; }   /* not taken yet: its add carries it */
    rhythm_of(j.kind == Json::OBJ ? &j : nullptr, op.cfg, p->main_rnd);
    op.op = 3;
}
void fs_piece_walk(fs_device *d, int route, double t, double dist) {
    Piece *p = P(d); if (!p) return;
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.walk = true; p->in.route = route; p->in.t = t; p->in.dist = dist;
}

/* the chord playing (0-based, -1 before the first bar), the progression's length, and its label */
int fs_piece_chord(fs_device *d, int *count, char *label, int label_size) {
    Piece *p = P(d); if (!p) return -1;
    std::lock_guard<std::mutex> g(p->mu);
    if (count) *count = p->in.nprog;
    if (label && label_size > 0) { std::strncpy(label, p->in.chord_label, label_size - 1); label[label_size - 1] = 0; }
    return p->in.chord;
}

/* A route's patch for the screen, as JSON: {"tempo":72,"key":"D","key2":"G","sections":7,"voices":4,
   "prog":[[root pc, "Dm7"], ...16], "sectors":[root pc, ...]} - the chord segments along the route and the
   section colours follow the roots (the web's rootColour). Returns the length written, 0 if no such route. */
int fs_piece_route_info(fs_device *d, int route, char *out, int size) {
    Piece *p = P(d); if (!p || !out || size < 64) return 0;
    std::lock_guard<std::mutex> g(p->mu);
    if (route < 0 || route >= (int)p->in.routes.size()) { out[0] = 0; return 0; }
    const Patch &pt = p->in.routes[route];
    std::string o = "{\"tempo\":" + std::to_string((int)std::lround(pt.tempo)) + ",\"key\":\"" + NOTE[pc12(pt.key)] +
                    "\",\"key2\":\"" + NOTE[pc12(pt.key2)] + "\",\"sections\":" + std::to_string(pt.sect.n) +
                    ",\"voices\":" + std::to_string(pt.bed_on ? pt.bed_voices : 0) + ",\"prog\":[";
    for (int i = 0; i < pt.nprog; i++) {
        int q = quality_of(pt, i), r = root_of(pt, i);
        o += (i ? ",[" : "[") + std::to_string(pc12(r)) + ",\"" + NOTE[pc12(r)] + (q >= 0 ? CHORDS[q].name : "m7") + "\"]";
    }
    o += "],\"sectors\":[";
    for (int i = 0; i < pt.nsectors; i++) o += (i ? "," : "") + std::to_string(pc12(pt.key + pt.sectors[i].r));
    o += "]}";
    std::snprintf(out, size, "%s", o.c_str());
    return (int)std::strlen(out);
}

int fs_piece_route(fs_device *d) { Piece *p = P(d); if (!p) return -1; std::lock_guard<std::mutex> g(p->mu); return p->in.taken; }
int fs_piece_sect_n(fs_device *d) { Piece *p = P(d); if (!p) return 7; std::lock_guard<std::mutex> g(p->mu); return p->in.sect_n; }
int fs_piece_bed_voices(fs_device *d) { Piece *p = P(d); if (!p) return 4; std::lock_guard<std::mutex> g(p->mu); return p->in.bed_voices; }

void fs_piece_sector(fs_device *d, int sector) {
    Piece *p = P(d); if (!p) return;
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.sector = true; p->in.sector_v = sector;
}
int fs_piece_sector_now(fs_device *d) {
    Piece *p = P(d); if (!p) return -1;
    std::lock_guard<std::mutex> g(p->mu);
    return p->in.sector ? p->in.sector_v : p->in.sector_back;
}

/* localCharacter */
void fs_piece_character(fs_device *d, int n, const double *dist, const double *radius, const double *centroid_hz, const double *onset_rate) {
    Piece *p = P(d); if (!p) return;
    double sc = 0, so = 0, w = 0;
    for (int i = 0; i < n; i++) {
        if (dist[i] > radius[i]) continue;
        double k = std::max(0.0, 1 - dist[i] / radius[i]);
        sc += (centroid_hz[i] > 0 ? centroid_hz[i] : 2000) * k;
        so += (onset_rate[i] >= 0 ? onset_rate[i] : 1) * k;
        w += k;
    }
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.character = true;
    p->in.centroid = w > 0 ? sc / w : 2000; p->in.onsets = w > 0 ? so / w : 1;
}

void fs_piece_zones(fs_device *d, const char *inside_csv, const char *entered) {
    Piece *p = P(d); if (!p) return;
    uint64_t ids[16]; int n = 0;
    std::string s = inside_csv ? inside_csv : "";
    for (size_t a = 0; a < s.size() && n < 16;) { size_t b = s.find(',', a); if (b == std::string::npos) b = s.size(); if (b > a) ids[n++] = comp::id_hash(s.substr(a, b - a).c_str()); a = b + 1; }
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.zones_set = true; p->in.nzin = n; std::memcpy(p->in.zin, ids, sizeof(uint64_t) * n);
    if (entered && *entered) p->in.zent = comp::id_hash(entered);
}

void fs_piece_zone(fs_device *d, const char *icon) {
    static const char *const ICONS[7] = { "tree", "animal", "flower", "insect", "fish", "soil", "water" };
    Piece *p = P(d); if (!p) return;
    int k = 0;
    for (int i = 0; i < 7; i++) if (icon && !std::strcmp(icon, ICONS[i])) k = i;
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    if (p->in.nzones < 8) p->in.zones[p->in.nzones++] = k;
}

int fs_piece_rhythm_add(fs_device *d, const char *rhythm_json, int grains) {
    Piece *p = P(d); if (!p) return -1;
    Json j = Json::parse(rhythm_json);
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    for (int h = 0; h < FS_MAX_VOICES; h++) if (!p->busy[h]) {
        p->busy[h] = true;
        auto &op = p->in.r[h];
        rhythm_of(j.kind == Json::OBJ ? &j : nullptr, op.cfg, p->main_rnd);   /* rhythmOf's sentence draw */
        op.op = 1; op.grains = grains != 0; op.gain = false;
        for (auto &s : op.src_set) s = false;
        for (auto &s : op.trk_set) s = false;
        return h;
    }
    return -1;
}
void fs_piece_rhythm_gain(fs_device *d, int h, float gain) {
    Piece *p = P(d); if (!p || h < 0 || h >= FS_MAX_VOICES) return;
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.r[h].gain = true; p->in.r[h].g = gain;
}
short *fs_alloc_i16(size_t n) { return (short *)std::malloc(n * sizeof(short) + 1); }
void fs_piece_rhythm_source(fs_device *d, int h, int slot, int channels, long long frames, short *interleaved) {
    Piece *p = P(d);
    if (!p || h < 0 || h >= FS_MAX_VOICES || slot < 0 || slot > 3) { std::free(interleaved); return; }
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    auto &op = p->in.r[h];
    if (op.src_set[slot]) { std::free(op.src[slot].data); std::free(op.src[slot].pk); }
    {   /* Test 15: the peak track, filled later by the audio thread (a grains recording reads it) */
        const int c = channels < 1 ? 1 : channels, pkn = (int)(frames / PK_HOP) + 1;
        float *pk = (float *)std::calloc(2 * (size_t)pkn, sizeof(float));
        op.src[slot] = Src{ interleaved, c, frames, 1, pk, pk ? pkn : 0 };
    }
    {   /* Test 15: the crush and drive work at the recording's own scale - a quiet hit under 2 bits rounded to silence */
        int pk = 0; const long long n = frames * op.src[slot].ch;
        for (long long i = 0; i < n; i++) { int v = interleaved[i] < 0 ? -interleaved[i] : interleaved[i]; if (v > pk) pk = v; }
        op.src[slot].peak = pk / 32768.0f;
    }
    op.src_set[slot] = true;
}
void fs_piece_rhythm_track(fs_device *d, int h, int slot, const char *json) {
    Piece *p = P(d);
    if (!p || h < 0 || h >= FS_MAX_VOICES || slot < 0 || slot > 3 || !json) return;
    Json j = Json::parse(json);
    /* a recording judged unpitched (f0 0) does not follow, even on its confident frames (final review 5 I5) */
    const Json *tr = j.n("f0", 0) > 0 ? j.get("track") : nullptr; const int n = tr ? (int)tr->size() : 0;
    float *tf = n ? (float *)std::malloc(sizeof(float) * 2 * n) : nullptr, *tc = tf ? tf + n : nullptr;
    for (int i = 0; i < n && tf; i++) { const Json *fr = tr->at(i); const Json *a = fr ? fr->at(0) : nullptr, *b = fr ? fr->at(1) : nullptr; tf[i] = a ? (float)a->num : 0; tc[i] = b ? (float)b->num : 0; }
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    if (tf) p->keep.b.push_back(tf);
    auto &op = p->in.r[h];
    op.trk[slot] = Trk{ tf, tc, tf ? n : 0, j.n("hop_s", 0.02), j.n("f0", 0) };
    op.trk_set[slot] = true;
}
/* The morph cells (the web's cellsDraw): the playing route's morphs that are on, at the piece's clock -
   the same numbers the voices take. 7 doubles a cell: shape (0 drift 1 breath 2 pulse 3 ramp 4 tide), voice
   (0 voice, 1 sect, 2 v3: the hue offset), dest (cells aimed at one gather), seed, value 0-1, its rate of
   change per second (breath's lean), phase unwrapped (pulse/ramp events, tide's beat). */
int fs_piece_morphs(fs_device *d, double *out, int max, double *clock, int *root, int *shown) {
    Piece *p = P(d); if (!p) return -1;
    std::lock_guard<std::mutex> g(p->mu);
    if (clock) *clock = p->in.clock;
    if (root) *root = p->in.root;
    if (p->in.taken < 0 || p->in.taken >= (int)p->in.routes.size()) { if (shown) *shown = 0; return -1; }
    const Patch &pt = p->in.routes[p->in.taken];
    if (shown) *shown = pt.morph_cells;
    if (!pt.morph_on) return 0;
    double t = p->in.clock;
    auto value = [](const Morph &m, double at) {
        double v = m.bias + m.depth * Piece::morph_shape(m.shape, at / std::max(4.0, m.period ? m.period : 60) + m.phase, m.seed);
        return v < 0 ? 0 : v > 1 ? 1 : v;
    };
    int n = 0;
    for (int i = 0; i < pt.nmorph && n < max; i++) {
        const Morph &m = pt.morph[i];
        if (!m.on) continue;
        const char *k = m.dest >= 0 ? DEST_KEYS[m.dest] : "";
        double *o = out + 7 * n++;
        o[0] = m.shape; o[1] = !std::strncmp(k, "sect.", 5) ? 1 : !std::strncmp(k, "v3.", 3) ? 2 : 0; o[2] = m.dest; o[3] = m.seed;
        o[4] = value(m, t); o[5] = (o[4] - value(m, t - 0.6)) / 0.6;
        o[6] = t / std::max(4.0, m.period ? m.period : 60) + m.phase;
    }
    return n;
}
/* The chord playing, as MIDI notes (root first, at most 5), for the stretch's tune and layers; -1 before any. */
int fs_piece_chord_notes(fs_device *d, float *notes, float *root) {
    Piece *p = P(d); if (!p) return -1;
    std::lock_guard<std::mutex> g(p->mu);
    if (p->in.chord_n < 0) return -1;
    for (int i = 0; i < p->in.chord_n; i++) notes[i] = p->in.chord_notes[i];
    if (root) *root = p->in.chord_root_midi;
    return p->in.chord_n;
}
void fs_piece_route_gate(fs_device *d, float g) {
    Piece *p = P(d); if (!p) return;
    std::lock_guard<std::mutex> lk(p->mu); sweep(p);
    p->in.gate_set = true; p->in.gate_v = g > 0 ? 1.0f : 0.0f;
}
void fs_piece_solo(fs_device *d, int h) {
    Piece *p = P(d); if (!p) return;
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.solo_set = true; p->in.solo_v = h >= 0 && h < FS_MAX_VOICES ? h : -1;
}
void fs_piece_rhythm_remove(fs_device *d, int h) {
    Piece *p = P(d); if (!p || h < 0 || h >= FS_MAX_VOICES) return;
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    auto &op = p->in.r[h];
    for (int s = 0; s < 4; s++) if (op.src_set[s]) { std::free(op.src[s].data); std::free(op.src[s].pk); op.src_set[s] = false; }
    op.op = 2;
}

void fs_piece_test_gains(const char *patch_json, int free_bars, int bars, double step, float *out) {
    const int fb = levels::FREE_BARS, b = levels::BARS; const double st = levels::STEP;
    if (free_bars) levels::FREE_BARS = free_bars; if (bars) levels::BARS = bars; if (step > 0) levels::STEP = step;
    Json j = Json::parse(patch_json); std::unique_ptr<Patch> P(new Patch); patch_of(&j, *P);
    levels::FREE_BARS = fb; levels::BARS = b; levels::STEP = st;
    for (int q = 0; q < comp::NVOICE; q++) out[q] = P->cgain[0][q];
}
void fs_piece_test_route_gains(fs_device *d, int i, float *out) {
    Piece *p = P(d); if (!p) return; std::lock_guard<std::mutex> g(p->mu);
    if (i >= 0 && i < (int)p->in.routes.size()) for (int q = 0; q < comp::NVOICE; q++) out[q] = p->in.routes[i].cgain[0][q];
}
void fs_piece_test_rgains(const char *patch_json, float *out) {
    Json j = Json::parse(patch_json); std::unique_ptr<Patch> P(new Patch); patch_of(&j, *P);
    for (int r = 0; r < comp::REL_MAX; r++) for (int q = 0; q < comp::NVOICE; q++) out[r * comp::NVOICE + q] = P->rgain[r][q];
}
void fs_piece_test_route_rgains(fs_device *d, int i, float *out) {
    Piece *p = P(d); if (!p) return; std::lock_guard<std::mutex> g(p->mu);
    if (i >= 0 && i < (int)p->in.routes.size()) for (int r = 0; r < comp::REL_MAX; r++) for (int q = 0; q < comp::NVOICE; q++) out[r * comp::NVOICE + q] = p->in.routes[i].rgain[r][q];
}
int fs_piece_test_kept(fs_device *d) { Piece *p = P(d); if (!p) return -1; std::lock_guard<std::mutex> g(p->mu); sweep(p); return (int)p->keep.b.size(); }
void fs_piece_test_hooks(fs_device *d, double (*rnd)(void *), void *rnd_ctx,
                         void (*on_note)(void *, int, double, double, double, double), void *note_ctx) {
    Piece *p = P(d); if (!p) return;
    p->rnd.hook = rnd; p->rnd.hook_ctx = rnd_ctx; p->on_note = on_note; p->note_ctx = note_ctx;
}
void fs_piece_test_walk(fs_device *d, double seconds) { Piece *p = P(d); if (p) p->test_walk_s = seconds; }
void fs_piece_default_tuning(fs_device *d, int tuning) { Piece *p = P(d); if (p) p->default_tuning = tuning == harmony::JUST ? harmony::JUST : harmony::EQUAL; }

int fs_harmony_progression(const char *patch_json, int tuning, int sector, char *out, int size) {
    Json j = Json::parse(patch_json);
    std::unique_ptr<Patch> Pp(new Patch); Patch &P = *Pp; patch_of(&j, P);
    std::string s = "{\"tempo\":" + std::to_string((int)std::lround(P.tempo)) + ",\"key\":" + std::to_string(P.key) +
                    ",\"tuning\":\"" + (tuning == harmony::JUST ? "just" : "equal") + "\",\"scale\":[";
    const Sector *sc = P.nsectors > 0 ? &P.sectors[std::max(0, std::min(P.nsectors - 1, sector))] : nullptr;
    const int mode = sc && sc->mode >= 0 ? sc->mode : MODE_DORIAN;
    if (sc) for (int i = 0; i < MODES[mode].n; i++) s += (i ? "," : "") + std::to_string(((P.key + sc->r + MODES[mode].iv[i]) % 12 + 12) % 12);
    s += "],\"chords\":[";
    char num[32];
    for (int step = 0; step < P.nprog; step++) {
        int q = quality_of(P, step); if (q < 0) q = Q_M7;
        int root = root_of(P, step);
        bool dom = dom_q(q);
        s += std::string(step ? "," : "") + "{\"label\":\"" + CHORDS[q].name + "\",\"root\":" + std::to_string(root) + ",\"notes\":[";
        for (int i = 0; i < CHORDS[q].n; i++) s += (i ? "," : "") + std::to_string(root + CHORDS[q].iv[i]);
        s += "],\"hz\":[";
        for (int i = 0; i < CHORDS[q].n; i++) { std::snprintf(num, sizeof num, "%.6f", harmony::hz(tuning, root + CHORDS[q].iv[i], root, dom)); s += (i ? "," : "") + std::string(num); }
        s += "],\"scale_hz\":[";                    /* the section scale from this chord's root, in the same tuning */
        if (sc) for (int i = 0; i < MODES[mode].n; i++) {
            int pcn = ((P.key + sc->r + MODES[mode].iv[i]) % 12 + 12) % 12, m = root + ((pcn - root) % 12 + 12) % 12;
            std::snprintf(num, sizeof num, "%.6f", harmony::hz(tuning, m, root, dom)); s += (i ? "," : "") + std::string(num);
        }
        s += "]}";
    }
    s += "]}";
    int need = (int)s.size();
    if (out && size > need) std::memcpy(out, s.c_str(), need + 1);
    return need;
}

}

