/* The composition's data: the route patch's "comp" read into fixed-size structs (composer.hpp), and its seeded draw. */
#include "composer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace comp {

uint64_t id_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
    return h;
}
double rand01(uint32_t seed, long long bar, int voice, int n) {   /* splitmix64 of (seed, bar, voice, n) */
    uint64_t z = (uint64_t)seed * 0x9E3779B97F4A7C15ull + 0x9E3779B97F4A7C15ull ^ (uint64_t)bar * 0xBF58476D1CE4E5B9ull ^ (uint64_t)(voice * 977 + n) * 0x94D049BB133111EBull;
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull; z ^= z >> 27; z *= 0x94D049BB133111EBull; z ^= z >> 31;
    return (z >> 11) * (1.0 / 9007199254740992.0);
}

static int pick(const std::string &s, const char *const *names, int n, int def) {
    for (int i = 0; i < n; i++) if (s == names[i]) return i;
    return def;
}
static int stride(const std::string &s) {
    static const struct { const char *s; int n; } T[] = { { "16n", 1 }, { "8n", 2 }, { "8n.", 3 }, { "4n", 4 }, { "4n.", 6 }, { "2n", 8 }, { "2n.", 12 }, { "1n", 16 } };
    for (auto &t : T) if (s == t.s) return t.n;
    return 2;
}
static uint8_t clamp8(double x, int lo, int hi) { return (uint8_t)std::max(lo, std::min(hi, (int)std::lround(x))); }
static float clampf(double x, double lo, double hi) { return (float)std::max(lo, std::min(hi, std::isfinite(x) ? x : lo)); }

static const char *const STYLES[NSTYLE] = { "free", "arpeggio", "pedal", "strum", "euclid", "swells" };
static const char *const ROLES[5] = { "melody", "counter", "accomp", "bass", "off" };

/* one voice's settings: every key present sets its bit in mask; `all` (the route's own) sets every bit */
static void voice_of(const Json *j, Voice &v, bool all) {
    if (all) v.mask = F_ALL;
    if (!j || j->kind != Json::OBJ) return;
    auto has = [&](const char *k) { return j->get(k) != nullptr; };
    if (has("style")) { v.style = (uint8_t)pick(j->s("style", "free"), STYLES, NSTYLE, FREE); v.mask |= F_STYLE; }
    if (has("role")) { v.role = (uint8_t)pick(j->s("role", "accomp"), ROLES, 5, ACCOMP); v.mask |= F_ROLE; }
    if (has("rate")) { v.rate = (uint8_t)stride(j->s("rate", "8n")); v.mask |= F_RATE; }
    if (has("lo")) { v.lo = clamp8(j->n("lo", 48), 12, 115); v.mask |= F_LO; }
    if (has("hi")) { v.hi = clamp8(j->n("hi", 84), 12, 115); v.mask |= F_HI; }
    if (has("len")) { v.len = clampf(j->n("len", 0.9), 0.05, 1); v.mask |= F_LEN; }
    if (has("variation")) { v.variation = clampf(j->n("variation", 0), 0, 1); v.mask |= F_VAR; }
    if (const Json *a = j->get("acc")) if (a->kind == Json::ARR) {
        for (int i = 0; i < MAX_COLS; i++) { const Json *x = a->at(i); v.acc[i] = x && x->kind == Json::NUM ? clamp8(x->num, 0, 100) : v.acc[i]; }
        v.mask |= F_ACC; }
    if (const Json *g = j->get("grid")) if (g->kind == Json::ARR) {
        for (int i = 0; i < MAX_COLS; i++) { const Json *x = g->at(i); v.grid[i] = x && x->kind == Json::NUM ? (int8_t)std::max(-1, std::min(4, (int)std::lround(x->num))) : (int8_t)-1; }
        v.mask |= F_GRID; }
    if (has("span")) { v.span = clamp8(j->n("span", 1), 1, 3); v.mask |= F_SPAN; }
    if (const Json *t = j->get("tones")) if (t->kind == Json::ARR) {
        uint8_t b = 0; for (size_t i = 0; i < t->size(); i++) { const Json *x = t->at(i); if (x && x->kind == Json::NUM) { int d = (int)std::lround(x->num); if (d >= 0 && d <= 4) b |= (uint8_t)(1u << d); } }
        v.tones = b ? b : 0x01; v.mask |= F_TONES; }
    static const char *const P[3] = { "root", "fifth", "fixed" };
    if (has("pedal")) { v.pedal = (uint8_t)pick(j->s("pedal", "root"), P, 3, 0); v.mask |= F_PEDAL; }
    if (has("pedal_note")) { v.pedal_note = clamp8(j->n("pedal_note", 50), 12, 115); v.mask |= F_PNOTE; }
    if (has("follow")) { v.follow = j->flag("follow", true) ? 1 : 0; v.mask |= F_FOLLOW; }
    if (has("dir")) { static const char *const D[3] = { "up", "down", "alt" }; v.dir = (uint8_t)pick(j->s("dir", "up"), D, 3, 0); v.mask |= F_DIR; }
    if (has("spread")) { v.spread_ms = clampf(j->n("spread", 40), 0, 400); v.mask |= F_SPREAD; }
    if (has("pulses")) { v.pulses = clamp8(j->n("pulses", 5), 0, 16); v.mask |= F_PULSES; }   /* the player clamps pulses to steps */
    if (has("steps")) { v.steps = clamp8(j->n("steps", 8), 1, 16); v.mask |= F_STEPS; }
    if (has("rot")) { v.rot = clamp8(j->n("rot", 0), 0, 15); v.mask |= F_ROT; }
    if (has("attack")) { v.attack = clampf(j->n("attack", 0.3), 0.01, 0.95); v.mask |= F_ATTACK; }
    if (has("release")) { v.release = clampf(j->n("release", 0.4), 0.01, 0.95); v.mask |= F_RELEASE; }
    if (has("peak")) { v.peak = clampf(j->n("peak", 0.8), 0, 1); v.mask |= F_PEAK; }
}
static void bass_of(const Json *j, Bass &b, bool all) {
    if (all) b.mask = B_ALL;
    if (!j || j->kind != Json::OBJ) return;
    static const char *const M[3] = { "hold", "pedal", "off" }, *const P[3] = { "root", "fifth", "fixed" };
    if (j->get("mode")) { b.mode = (uint8_t)pick(j->s("mode", "hold"), M, 3, HOLD); b.mask |= B_MODE; }
    if (j->get("pedal")) { b.pedal = (uint8_t)pick(j->s("pedal", "root"), P, 3, 0); b.mask |= B_PEDAL; }
    if (j->get("note")) { b.note = clamp8(j->n("note", 38), 12, 72); b.mask |= B_NOTE; }
}
static const char *const VKEYS[NVOICE] = { "voice", "sect", "v3" };
static void over_of(const Json *j, Over &o) {
    if (!j || j->kind != Json::OBJ) return;
    o.used = true;
    for (int q = 0; q < NVOICE; q++) voice_of(j->get(VKEYS[q]), o.v[q], false);
    bass_of(j->get("bass"), o.bass, false);
    o.blend = clamp8(j->n("blend", 0), 0, 16);
    o.when = j->s("when", "inside") == std::string("enter") ? ENTER : INSIDE;
}

static void motif_of(const Json *m, Motif &o) {
    o = Motif();
    if (!m || m->kind != Json::OBJ) return;
    o.basis = m->s("basis", "chord") == std::string("scale") ? SCALE_DEG : CHORD_DEG;
    o.bars = clamp8(m->n("bars", 1), 1, 2);
    const int span = o.bars * 16;
    const Json *a = m->get("notes");
    if (!a || a->kind != Json::ARR) return;
    for (size_t i = 0; i < a->size() && o.n < MOTIF_MAX; i++) {
        const Json *x = a->at(i);
        if (!x || x->kind != Json::OBJ) continue;
        const int start = (int)std::lround(x->n("start", 0));
        if (start < 0 || start >= span) continue;
        MNote &n = o.note[o.n++];
        n.start = (uint8_t)start;
        n.len = clamp8(std::min((double)(span - start), x->n("len", 4)), 1, 32);
        n.deg = (int8_t)std::max(-7, std::min(13, (int)std::lround(x->n("deg", 0))));
        n.oct = (int8_t)std::max(-2, std::min(2, (int)std::lround(x->n("oct", 0))));
        n.acc = clamp8(x->n("acc", 70), 0, 100);
    }
}
static void chain_of(const Json *a, ChainStep *o, int &n) {
    static const char *const OPS[NOP] = { "repeat", "seq", "invert", "retro", "augment", "diminish", "fragment", "cadence" };
    static const char *const CAD[3] = { "full", "half", "deceptive" }, *const OTH[3] = { "hold", "thin", "stop" };
    n = 0;
    if (!a || a->kind != Json::ARR) return;
    for (size_t i = 0; i < a->size() && n < CHAIN_MAX; i++) {
        const Json *x = a->at(i);
        if (!x || x->kind != Json::OBJ) continue;
        const int op = pick(x->s("op", ""), OPS, NOP, NOP);
        const double fd = x->n("from", 0), td = x->n("to", 0);
        const long f = std::isfinite(fd) ? std::lround(fd) : -1, t = std::isfinite(td) ? std::lround(td) : -1;   /* round, then validate */
        if (op == NOP || f < 0 || t < f || t >= NCHORD) continue;
        ChainStep &s = o[n++];
        s = ChainStep();
        s.op = (uint8_t)op;
        s.from = (uint8_t)f; s.to = (uint8_t)t;
        const int arg = (int)std::lround(x->n("arg", 0));
        s.arg = (int8_t)(op == OP_FRAGMENT ? std::max(1, std::min(8, arg)) : std::max(-7, std::min(7, arg)));
        s.cad = (uint8_t)pick(x->s("cad", "full"), CAD, 3, CAD_FULL);
        s.others = (uint8_t)pick(x->s("others", "thin"), OTH, 3, OTH_THIN);
        s.strength = clampf(x->n("strength", 1), 0, 1);
    }
}

static void rel_of(const Json *a, Relation *o, int &n) {
    static const char *const TYPES[NREL] = { "canon", "call", "hocket", "hetero", "motion", "unison", "density" };
    static const char *const TR[3] = { "none", "invert", "retro" }, *const SRC[3] = { "own", "half", "mirror" }, *const SPL[3] = { "alt", "every", "mask" },
        *const MOT[4] = { "contrary", "par3", "par6", "free" }, *const MEET[3] = { "cadence", "change", "never" };
    n = 0;
    if (!a || a->kind != Json::ARR) return;
    auto rnd = [](double d) { return std::isfinite(d) ? std::lround(d) : -1L; };
    for (size_t i = 0; i < a->size() && n < REL_MAX; i++) {
        const Json *x = a->at(i);
        if (!x || x->kind != Json::OBJ) continue;
        const int type = pick(x->s("type", ""), TYPES, NREL, NREL);
        const long f = rnd(x->n("from", 0)), t = rnd(x->n("to", NCHORD - 1));   /* round, then validate */
        const long la = rnd(x->n("a", 0)), lb = rnd(x->n("b", 1));   /* a voice past 0..2 drops the relation, not clamps it */
        if (type == NREL || f < 0 || t < f || t >= NCHORD || la < 0 || la > 2 || lb < 0 || lb > 2 || la == lb) continue;
        const uint8_t va = (uint8_t)la, vb = (uint8_t)lb;
        Relation &r = o[n++];
        r = Relation();
        r.type = (uint8_t)type; r.a = va; r.b = vb;
        if (x->get("c")) { const long lc = rnd(x->n("c", -1)); if (lc >= 0 && lc <= 2 && lc != la && lc != lb) r.c = (uint8_t)lc; }
        r.from = (uint8_t)f; r.to = (uint8_t)t;
        if (const Json *z = x->get("zone")) if (z->kind == Json::STR) r.zone = id_hash(z->str.c_str());
        r.delay16 = clamp8(x->n("delay", 8), 1, 32);
        r.trans = (int8_t)std::max(-7, std::min(7, (int)std::lround(x->n("trans", 0))));
        r.transform = (uint8_t)pick(x->s("transform", "none"), TR, 3, T_NONE);
        r.source = (uint8_t)pick(x->s("source", "own"), SRC, 3, A_OWN);
        r.gap16 = clamp8(x->n("gap", 2), 1, 16);
        r.on_enter = x->s("trigger", "phrase") == std::string("enter") ? 1 : 0;
        r.split = (uint8_t)pick(x->s("split", "alt"), SPL, 3, S_ALT);
        r.every = clamp8(x->n("every", 2), 2, 4);
        if (x->get("mask")) { const std::string m = x->s("mask", ""); r.mask = 0; for (int k = 0; k < 16 && k < (int)m.size(); k++) if (m[k] == 'x') r.mask |= (uint16_t)(1u << k); }
        r.offset_ms = clampf(x->n("offset", 30), 0, 120);
        r.ornament = clampf(x->n("ornament", 0), 0, 1);
        r.motion = (uint8_t)pick(x->s("motion", "contrary"), MOT, 4, M_CONTRARY);
        r.strength = clampf(x->n("strength", 1), 0, 1);
        r.meet = (uint8_t)pick(x->s("meet", "cadence"), MEET, 3, U_CADENCE);
        r.octave = x->s("octave", "unison") == std::string("octave") ? 1 : 0;
        r.cap = clamp8(x->n("cap", 8), 1, 32);
        if (const Json *w = x->get("weight")) if (w->kind == Json::ARR)
            for (int k = 0; k < 3; k++) { const Json *y = w->at(k); if (y && y->kind == Json::NUM) r.weight[k] = clamp8(y->num, 0, 4); }
    }
}
int relations_in_force(const Cfg &c, int chord, const uint64_t *inside, int ninside, int *out) {
    int n = 0;
    for (int i = 0; i < c.nrel && n < REL_MAX; i++) {
        const Relation &r = c.rel[i];
        if (chord < r.from || chord > r.to) continue;
        bool in = r.zone == 0;
        for (int k = 0; k < ninside && !in; k++) in = inside[k] == r.zone;
        if (in) out[n++] = i;
    }
    return n;
}

void comp_of(const Json *j, Cfg &c) {
    static const Cfg kDefault;   /* static, not a ~25 KB temporary on the wasm stack */
    c = kDefault;
    if (!j || j->kind != Json::OBJ) return;
    c.on = true;
    c.seed = (uint32_t)std::max(0.0, j->n("seed", 1));
    c.develop = j->s("bars", "develop") == std::string("repeat") ? 0 : 1;
    for (int q = 0; q < NVOICE; q++) voice_of(j->get(VKEYS[q]), c.v[q], true);
    bass_of(j->get("bass"), c.bass, true);
    motif_of(j->get("motif"), c.motif);
    chain_of(j->get("chain"), c.chain, c.nchain);
    rel_of(j->get("relations"), c.rel, c.nrel);
    const Json *ov = j->get("over");
    if (!ov || ov->kind != Json::OBJ) return;
    if (const Json *ch = ov->get("chord")) if (ch->kind == Json::OBJ)
        for (auto &kv : ch->obj) { int i = std::atoi(kv.first.c_str()); if (i >= 0 && i < NCHORD) over_of(&kv.second, c.chord[i]); }
    if (const Json *se = ov->get("section")) if (se->kind == Json::OBJ)
        for (auto &kv : se->obj) { int i = std::atoi(kv.first.c_str()); if (i >= 0 && i < NSECTION) over_of(&kv.second, c.section[i]); }
    if (const Json *zo = ov->get("zone")) if (zo->kind == Json::OBJ)
        for (auto &kv : zo->obj) { if (c.nzone >= NZONE) break; Over &o = c.zone[c.nzone]; over_of(&kv.second, o); if (o.used) { o.zone = id_hash(kv.first.c_str()); c.nzone++; } }
}

static void merge(Voice &d, const Voice &s) {
    const uint32_t m = s.mask;
    if (m & F_STYLE) d.style = s.style;
    if (m & F_ROLE) d.role = s.role;
    if (m & F_RATE) d.rate = s.rate;
    if (m & F_LO) d.lo = s.lo;
    if (m & F_HI) d.hi = s.hi;
    if (m & F_LEN) d.len = s.len;
    if (m & F_VAR) d.variation = s.variation;
    if (m & F_ACC) std::memcpy(d.acc, s.acc, sizeof d.acc);
    if (m & F_GRID) std::memcpy(d.grid, s.grid, sizeof d.grid);
    if (m & F_SPAN) d.span = s.span;
    if (m & F_TONES) d.tones = s.tones;
    if (m & F_PEDAL) d.pedal = s.pedal;
    if (m & F_DIR) d.dir = s.dir;
    if (m & F_SPREAD) d.spread_ms = s.spread_ms;
    if (m & F_PNOTE) d.pedal_note = s.pedal_note;
    if (m & F_FOLLOW) d.follow = s.follow;
    if (m & F_PULSES) d.pulses = s.pulses;
    if (m & F_STEPS) d.steps = s.steps;
    if (m & F_ROT) d.rot = s.rot;
    if (m & F_ATTACK) d.attack = s.attack;
    if (m & F_RELEASE) d.release = s.release;
    if (m & F_PEAK) d.peak = s.peak;
}
static void apply(Resolved &r, const Over &o) {
    for (int q = 0; q < NVOICE; q++) merge(r.v[q], o.v[q]);
    if (o.bass.mask & B_MODE) r.bass.mode = o.bass.mode;
    if (o.bass.mask & B_PEDAL) r.bass.pedal = o.bass.pedal;
    if (o.bass.mask & B_NOTE) r.bass.note = o.bass.note;
    r.blend = std::max(r.blend, (int)o.blend);
}
void resolve(const Cfg &c, int chord, int nchord, int section, int nsection, const uint64_t *inside, int ninside,
             const uint64_t *entered, int nentered, Resolved &out) {
    out = Resolved();
    for (int q = 0; q < NVOICE; q++) out.v[q] = c.v[q];
    out.bass = c.bass;
    if (section >= 0 && section < NSECTION && section < nsection && c.section[section].used) apply(out, c.section[section]);
    if (chord >= 0 && chord < NCHORD && chord < nchord && c.chord[chord].used) apply(out, c.chord[chord]);
    for (int k = 0; k < c.nzone; k++) {
        const Over &o = c.zone[k];
        const uint64_t *list = o.when == ENTER ? entered : inside; const int n = o.when == ENTER ? nentered : ninside;
        for (int i = 0; i < n; i++) if (list[i] == o.zone) { apply(out, o); break; }
    }
}
void glide(Voice &cur, const Voice &t, int blend) {
    const float a = blend > 0 ? 1.0f / (float)blend : 1.0f;
    auto toward = [&](float &x, float y) { x += (y - x) * a; if (std::fabs(y - x) < 1e-4f) x = y; };
    const float len = cur.len, var = cur.variation, sp = cur.spread_ms, at = cur.attack, re = cur.release, pk = cur.peak;
    cur = t;                                            /* the discrete settings: at once, on this bar line */
    cur.len = len; cur.variation = var; cur.spread_ms = sp; cur.attack = at; cur.release = re; cur.peak = pk;
    toward(cur.len, t.len); toward(cur.variation, t.variation); toward(cur.spread_ms, t.spread_ms);
    toward(cur.attack, t.attack); toward(cur.release, t.release); toward(cur.peak, t.peak);
}
void motif_step(const Cfg &c, int chord, Phrase &o) {
    o = Phrase();
    const Motif &m = c.motif; const int L = m.bars * 16;
    ChainStep st; bool found = false;
    for (int i = c.nchain - 1; i >= 0 && !found; i--) if (chord >= c.chain[i].from && chord <= c.chain[i].to) { st = c.chain[i]; found = true; o.step = i; }
    o.len16 = (uint8_t)L; o.n = m.n;
    for (int i = 0; i < m.n; i++) o.note[i] = m.note[i];
    switch (st.op) {
    case OP_SEQ: for (int i = 0; i < o.n; i++) o.note[i].deg = (int8_t)(o.note[i].deg + st.arg); break;
    case OP_INVERT: for (int i = 0; i < o.n; i++) o.note[i].deg = (int8_t)(2 * m.note[0].deg - o.note[i].deg); break;
    case OP_RETRO:
        for (int i = 0; i < o.n; i++) { o.note[i] = m.note[o.n - 1 - i]; o.note[i].start = (uint8_t)std::max(0, L - (o.note[i].start + o.note[i].len)); }
        break;
    case OP_AUGMENT:
        o.len16 = (uint8_t)std::min(2 * L, 32);
        for (int i = 0; i < o.n; i++) { o.note[i].start = (uint8_t)(o.note[i].start * 2); o.note[i].len = (uint8_t)(o.note[i].len * 2); }
        break;
    case OP_DIMINISH:
        for (int i = 0; i < o.n; i++) { MNote &x = o.note[i]; x.start = (uint8_t)(x.start / 2); x.len = (uint8_t)std::max(1, x.len / 2);
            o.note[o.n + i] = x; o.note[o.n + i].start = (uint8_t)(x.start + L / 2); }
        o.n *= 2;
        break;
    case OP_FRAGMENT: o.n = std::min(o.n, (int)st.arg); break;
    default: break;
    }
    o.last = found && chord == st.to;
    o.cadence = st.op == OP_CADENCE; o.cad = st.cad; o.others = st.others; o.strength = st.strength;
    int k = 0;   /* clip to the phrase: past its end dropped, a note held to its end at most */
    for (int i = 0; i < o.n; i++) if (o.note[i].start < o.len16) { o.note[k] = o.note[i]; o.note[k].len = (uint8_t)std::min((int)o.note[k].len, o.len16 - o.note[k].start); k++; }
    o.n = k;
    k = 0;       /* one note per start - a mono voice's second note would cut the first (diminish lands two on one): the later kept */
    for (int i = 0; i < o.n; i++) { bool dup = false; for (int j = i + 1; j < o.n && !dup; j++) dup = o.note[j].start == o.note[i].start; if (!dup) o.note[k++] = o.note[i]; }
    o.n = k;
    if (o.cadence && o.last && o.n) {   /* the cadence note - the latest - held to the phrase's end and put last (its pitch: the player's) */
        int j = 0; for (int i = 1; i < o.n; i++) if (o.note[i].start >= o.note[j].start) j = i;
        std::swap(o.note[j], o.note[o.n - 1]);
        o.note[o.n - 1].len = (uint8_t)(o.len16 - o.note[o.n - 1].start);
    }
}
}
