#pragma once
/* Composition 4, task 1: a hash of the Cfg fields that existed before step 4 (field by field - Cfg's size changed), of a
   step-3 route parsed, resolved and glided. Recorded on the build before the new keys; an old route must not move it. */
#include "../composer.hpp"
#include <cstring>
namespace fp {
static const char *const ROUTE3 = R"({"seed":7,"voice":{"style":"arpeggio","rate":"16n","lo":60,"hi":84,"grid":[0,1,2,-1],"variation":0.3},"sect":{"style":"strum","rate":"2n","tones":[0,1,2,3],"spread":40,"dir":"alt"},
 "v3":{"style":"euclid","pulses":5,"steps":8,"rot":1,"attack":0.2,"release":0.5,"peak":0.7},"bass":{"mode":"pedal","pedal":"fifth","note":40},"bars":"repeat",
 "motif":{"basis":"scale","bars":2,"notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4,"acc":90},{"deg":4,"oct":1,"start":20,"len":8}]},
 "chain":[{"op":"seq","arg":2,"from":0,"to":1},{"op":"cadence","cad":"half","others":"stop","strength":0.5,"from":2,"to":3}],
 "relations":[{"type":"canon","a":0,"b":1,"delay":6,"trans":2,"transform":"invert"},{"type":"hocket","a":0,"b":1,"c":2,"split":"mask","mask":"x.x.xx"},{"type":"density","a":0,"b":1,"cap":6,"weight":[1,2,3]}],
 "over":{"chord":{"1":{"voice":{"style":"pedal","rate":"4n"},"bass":{"mode":"off"},"blend":2},"3":{"sect":{"role":"off"},"v3":{"style":"swells","tones":[0,2,4]}}},
         "section":{"1":{"voice":{"rot":3,"len":0.5},"bass":{"pedal":"root"}}},
         "zone":{"p-1":{"when":"enter","blend":3,"sect":{"variation":0.5,"style":"arpeggio"},"bass":{"note":50}}}}})";   /* a step-3 route: every old key, motif, chain, relations, layers */
struct H { uint64_t h = 1469598103934665603ull;
    template <class T> void add(const T &x) { const unsigned char *b = (const unsigned char *)&x; for (size_t i = 0; i < sizeof x; i++) h = (h ^ b[i]) * 1099511628211ull; }
    void arr(const void *p, size_t n) { const unsigned char *b = (const unsigned char *)p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; } };
inline void voice(H &o, const comp::Voice &v, bool mask) {
    if (mask) o.add((uint32_t)(v.mask & 0x3FFFFFu));
    o.add(v.style); o.add(v.role); o.add(v.rate); o.add(v.lo); o.add(v.hi); o.add(v.len); o.add(v.variation); o.arr(v.acc, sizeof v.acc); o.arr(v.grid, sizeof v.grid);
    o.add(v.span); o.add(v.tones); o.add(v.pedal); o.add(v.pedal_note); o.add(v.follow); o.add(v.dir); o.add(v.spread_ms);
    o.add(v.pulses); o.add(v.steps); o.add(v.rot); o.add(v.attack); o.add(v.release); o.add(v.peak);
}
inline void bass(H &o, const comp::Bass &b) { o.add((uint32_t)(b.mask & 7u)); o.add(b.mode); o.add(b.pedal); o.add(b.note); }
inline uint64_t of(const comp::Cfg &c) {
    H o; o.add(c.on); o.add(c.seed); o.add(c.develop); o.add(c.nzone); o.add(c.nchain); o.add(c.nrel);
    for (auto &v : c.v) voice(o, v, true);
    bass(o, c.bass);
    auto over = [&](const comp::Over &x) { o.add(x.used); o.add(x.blend); o.add(x.when); o.add(x.zone); for (auto &v : x.v) voice(o, v, true); bass(o, x.bass); };
    for (auto &x : c.chord) over(x);
    for (auto &x : c.section) over(x);
    for (auto &x : c.zone) over(x);
    o.add(c.motif.basis); o.add(c.motif.bars); o.add(c.motif.n);
    for (int i = 0; i < c.motif.n; i++) { auto &n = c.motif.note[i]; o.add(n.deg); o.add(n.oct); o.add(n.start); o.add(n.len); o.add(n.acc); }
    for (int i = 0; i < c.nchain; i++) { auto &s = c.chain[i]; o.add(s.op); o.add(s.arg); o.add(s.from); o.add(s.to); o.add(s.cad); o.add(s.others); o.add(s.strength); }
    for (int i = 0; i < c.nrel; i++) { auto &r = c.rel[i]; o.add(r.type); o.add(r.a); o.add(r.b); o.add(r.c); o.add(r.from); o.add(r.to); o.add(r.zone); o.add(r.delay16); o.add(r.trans);
        o.add(r.transform); o.add(r.source); o.add(r.gap16); o.add(r.on_enter); o.add(r.split); o.add(r.every); o.add(r.mask); o.add(r.offset_ms); o.add(r.ornament);
        o.add(r.motion); o.add(r.strength); o.add(r.meet); o.add(r.octave); o.add(r.cap); o.arr(r.weight, 3); }
    /* resolve at a few places, and a glide: the merge and glide sites */
    static const uint64_t in[1] = { comp::id_hash("p-1") };
    for (int chord = 0; chord < 4; chord++) for (int sect = -1; sect < 3; sect++) {
        comp::Resolved r; comp::resolve(c, chord, 4, sect, 3, in, chord & 1, in, (chord >> 1) & 1, r);
        for (auto &v : r.v) voice(o, v, false);
        o.add(r.bass.mode); o.add(r.bass.pedal); o.add(r.bass.note); o.add(r.blend);
        comp::Voice g = c.v[0]; comp::glide(g, r.v[0], 4); voice(o, g, false);
    }
    return o.h;
}
}
