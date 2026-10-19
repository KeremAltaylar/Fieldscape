#pragma once
/* Composition (docs/superpowers/specs/2026-10-17-composition-design.md): how a route's three voices play - a style per
   voice for the route, overridden per section, chord step and zone (zone > chord > section > route). Step 1: five styles,
   role on/off, voice 1's bass row. Plain data only, no heap: the route's Patch, which holds it, is copied on the audio thread. */
#include "json.hpp"
#include <cstdint>

namespace comp {
enum Style { FREE, ARPEGGIO, PEDAL, STRUM, EUCLID, SWELLS, NSTYLE };
enum Role { MELODY, COUNTER, ACCOMP, BASS, OFF };        /* step 1: OFF silences the voice; the others play their style */
enum BassMode { HOLD, BASS_PEDAL, BASS_OFF };
enum When { INSIDE, ENTER };
enum Field : uint32_t { F_STYLE = 1u, F_ROLE = 2u, F_RATE = 4u, F_LO = 8u, F_HI = 16u, F_LEN = 32u, F_VAR = 64u, F_ACC = 128u,
    F_GRID = 256u, F_SPAN = 512u, F_TONES = 1024u, F_PEDAL = 2048u, F_DIR = 4096u, F_SPREAD = 8192u,
    F_PNOTE = 1u << 14, F_FOLLOW = 1u << 15, F_PULSES = 1u << 16, F_STEPS = 1u << 17, F_ROT = 1u << 18,
    F_ATTACK = 1u << 19, F_RELEASE = 1u << 20, F_PEAK = 1u << 21, F_ALL = 0x3FFFFFu };   /* one bit per key: a layer holds only what it sets */
enum BassField : uint32_t { B_MODE = 1u, B_PEDAL = 2u, B_NOTE = 4u, B_ALL = 7u };
static const int MAX_COLS = 16, NCHORD = 16, NSECTION = 16, NZONE = 12, PHRASE_BARS = 2, NVOICE = 3;

struct Voice {
    uint32_t mask = 0;                                /* which fields this layer sets (the route sets all) */
    uint8_t style = FREE, role = ACCOMP, rate = 2;    /* rate: a step, in sixteenths (1 2 3 4 6 8 12 16) */
    uint8_t lo = 48, hi = 84;                         /* register, MIDI */
    float len = 0.9f, variation = 0;                  /* share of the step a note fills; 0 = exactly as set */
    uint8_t acc[MAX_COLS];                            /* accent per column, 0-100 */
    int8_t grid[MAX_COLS];                            /* chord degree per column (0 root, 1 3rd, 2 5th, 3 7th, 4 9th), -1 rest */
    uint8_t span = 1, tones = 0x07;                   /* arpeggio: octaves; strum, swells: chord degrees, a bit each */
    uint8_t pedal = 0, pedal_note = 50, follow = 1;   /* pedal: 0 root, 1 fifth, 2 a fixed note; follow the chord */
    uint8_t dir = 0; float spread_ms = 40;            /* strum: 0 up, 1 down, 2 alternating; the roll */
    uint8_t pulses = 5, steps = 8, rot = 0;           /* euclidean */
    float attack = 0.3f, release = 0.4f, peak = 0.8f; /* swells: attack and release as shares of the note; level */
    Voice() { for (int i = 0; i < MAX_COLS; i++) { acc[i] = 70; grid[i] = (int8_t)(i % 3); } }
};
struct Bass { uint32_t mask = 0; uint8_t mode = HOLD, pedal = 0, note = 38; };   /* pedal: 0 key root, 1 key fifth, 2 note */
struct Over { bool used = false; Voice v[NVOICE]; Bass bass; uint8_t blend = 0, when = INSIDE; uint64_t zone = 0; };
static const int MOTIF_MAX = 8, CHAIN_MAX = 16;
enum Basis { CHORD_DEG, SCALE_DEG };
enum Op { OP_REPEAT, OP_SEQ, OP_INVERT, OP_RETRO, OP_AUGMENT, OP_DIMINISH, OP_FRAGMENT, OP_CADENCE, NOP };
enum Cadence { CAD_FULL, CAD_HALF, CAD_DECEPTIVE };
enum Others { OTH_HOLD, OTH_THIN, OTH_STOP };            /* what the other voices do on a cadence's last chord */
struct MNote { int8_t deg = 0, oct = 0; uint8_t start = 0, len = 4; uint8_t acc = 70; };   /* start/len in sixteenths */
struct Motif { uint8_t basis = CHORD_DEG, bars = 1, n = 0; MNote note[MOTIF_MAX]; };
struct ChainStep { uint8_t op = OP_REPEAT; int8_t arg = 0; uint8_t from = 0, to = 0;      /* chord steps, inclusive */
                   uint8_t cad = CAD_FULL, others = OTH_THIN; float strength = 1; };
struct Cfg { bool on = false; uint32_t seed = 1; Voice v[NVOICE]; Bass bass;
             Over chord[NCHORD], section[NSECTION], zone[NZONE]; int nzone = 0;
             Motif motif; ChainStep chain[CHAIN_MAX]; int nchain = 0; };
struct Resolved { Voice v[NVOICE]; Bass bass; int blend = 0; };
/* the motif as developed for one chord step: its notes (times in sixteenths from the phrase's start), the phrase's length */
struct Phrase { int n = 0; MNote note[MOTIF_MAX * 2]; uint8_t len16 = 16; bool cadence = false; uint8_t cad = CAD_FULL, others = OTH_THIN; float strength = 1; bool last = false; };

uint64_t id_hash(const char *s);                      /* FNV-1a 64: a zone's point id */
void comp_of(const Json *j, Cfg &c);                  /* the route patch's "comp"; absent: c.on = false */
double rand01(uint32_t seed, long long bar, int voice, int n);   /* the composition's own seeded draw, [0, 1) */
/* the settings in force this bar: route <- section <- chord <- zones (inside, or entered within PHRASE_BARS: `entered`);
   a chord past the progression's length or a section past the place's count is ignored */
void resolve(const Cfg &c, int chord, int nchord, int section, int nsection, const uint64_t *inside, int ninside,
             const uint64_t *entered, int nentered, Resolved &out);
/* once a bar: the discrete settings taken at once, the continuous ones (len, variation, spread, swell) a 1/blend step closer */
void glide(Voice &cur, const Voice &target, int blend);
/* the motif as developed for chord step `chord` (the chain step covering it, a later step winning; else repeat);
   `last`: chord is that step's last (a cadence's: its note is the phrase's last). Plain data, no heap: the audio thread calls it once a bar */
void motif_step(const Cfg &c, int chord, Phrase &out);
}
