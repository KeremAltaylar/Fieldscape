/* Composition step 1 (docs/superpowers/plans/2026-10-17-composition-1-foundation.md): the composer's core checks.
     python core/tests/run.py      (row C) */
#include "../composer.hpp"
#include "../fieldscape.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static void test_parse() {
    comp::Cfg c;
    comp::comp_of(nullptr, c);
    assert(!c.on);
    Json j = Json::parse(R"({"seed":7,"voice":{"style":"arpeggio","rate":"16n","lo":60,"hi":84,"grid":[0,1,2,-1]},
        "sect":{"style":"pedal","pedal":"fifth"},"bass":{"mode":"off"},
        "over":{"chord":{"3":{"voice":{"style":"strum"}},"40":{"voice":{"style":"swells"}}},
                "section":{"2":{"v3":{"role":"off"}}},
                "zone":{"p-1":{"when":"enter","blend":2,"sect":{"variation":0.5}}}}})");
    comp::comp_of(&j, c);
    assert(c.on && c.seed == 7);
    assert(c.v[0].style == comp::ARPEGGIO && c.v[0].rate == 1 && c.v[0].lo == 60 && c.v[0].grid[3] == -1 && c.v[0].grid[4] == -1);
    assert(c.v[0].mask == comp::F_ALL && c.v[2].style == comp::FREE);
    assert(c.v[1].style == comp::PEDAL && c.v[1].pedal == 1 && c.bass.mode == comp::BASS_OFF);
    assert(c.chord[3].used && c.chord[3].v[0].mask == comp::F_STYLE && c.chord[3].v[0].style == comp::STRUM);
    assert(c.section[2].used && c.section[2].v[2].role == comp::OFF);
    assert(c.nzone == 1 && c.zone[0].zone == comp::id_hash("p-1") && c.zone[0].when == comp::ENTER && c.zone[0].blend == 2);
    for (int i = 0; i < comp::NCHORD; i++) if (i != 3) assert(!c.chord[i].used);   /* "40": out of range, ignored */
    assert(c.bass.mask == comp::B_ALL);
    Json k = Json::parse(R"({"over":{"section":{"1":{"voice":{"rot":3},"bass":{"pedal":"fifth"}}}}})");
    comp::Cfg d; comp::comp_of(&k, d);
    assert(d.section[1].v[0].mask == comp::F_ROT && d.section[1].v[0].rot == 3 && d.section[1].bass.mask == comp::B_PEDAL && d.section[1].bass.pedal == 1);
    assert(comp::rand01(0, 0, 0, 0) != 0 && comp::rand01(1, 2, 3, 4) == comp::rand01(1, 2, 3, 4));
    std::printf("C parse: route, chord, section and zone layers read; out-of-range chord 40 ignored\n");
}

static void test_resolve() {
    Json j = Json::parse(R"({"voice":{"style":"arpeggio","rate":"8n"},
        "over":{"section":{"1":{"voice":{"style":"pedal","lo":40}}},
                "chord":{"5":{"voice":{"style":"strum"}}},
                "zone":{"z":{"voice":{"style":"swells"}},"e":{"when":"enter","voice":{"rate":"4n"}}}}})");
    comp::Cfg c; comp::comp_of(&j, c);
    comp::Resolved r;
    const uint64_t z = comp::id_hash("z"), e = comp::id_hash("e");
    comp::resolve(c, 0, 16, -1, 7, nullptr, 0, nullptr, 0, r);  assert(r.v[0].style == comp::ARPEGGIO && r.v[0].lo == 48);
    comp::resolve(c, 0, 16, 1, 7, nullptr, 0, nullptr, 0, r);   assert(r.v[0].style == comp::PEDAL && r.v[0].lo == 40);
    comp::resolve(c, 5, 16, 1, 7, nullptr, 0, nullptr, 0, r);   assert(r.v[0].style == comp::STRUM && r.v[0].lo == 40);   /* chord over section; section's lo kept */
    comp::resolve(c, 5, 16, 1, 7, &z, 1, nullptr, 0, r);        assert(r.v[0].style == comp::SWELLS);                     /* zone over chord */
    comp::resolve(c, 0, 16, -1, 7, &e, 1, nullptr, 0, r);       assert(r.v[0].rate == 2);                                 /* an "enter" zone does nothing while merely inside */
    comp::resolve(c, 0, 16, -1, 7, nullptr, 0, &e, 1, r);       assert(r.v[0].rate == 4);                                 /* ... it acts for the phrase after entering */
    { Json k = Json::parse(R"({"voice":{"style":"arpeggio"},"over":{"chord":{"15":{"voice":{"style":"pedal"}}}}})");
      comp::Cfg d; comp::comp_of(&k, d);
      comp::resolve(d, 15, 12, -1, 7, nullptr, 0, nullptr, 0, r); assert(r.v[0].style == comp::ARPEGGIO); }   /* 12 chords: no chord 16 */
    { Json k = Json::parse(R"({"voice":{"style":"arpeggio","len":0.2},"over":{"section":{"9":{"voice":{"style":"pedal"}}},"chord":{"0":{"blend":4,"voice":{"len":1.0}}}}})");
      comp::Cfg d; comp::comp_of(&k, d);
      comp::resolve(d, 0, 16, 9, 7, nullptr, 0, nullptr, 0, r); assert(r.v[0].style == comp::ARPEGGIO && r.blend == 4);   /* 7 sections: no section 10 */
      comp::Voice cur = d.v[0];
      comp::glide(cur, r.v[0], r.blend); assert(std::fabs(cur.len - 0.4f) < 1e-4f);     /* a quarter of the way per bar */
      for (int b = 0; b < 40; b++) comp::glide(cur, r.v[0], r.blend);
      assert(cur.len == 1.0f); }
    { Json k = Json::parse(R"({"voice":{"pulses":6,"steps":12},"over":{"chord":{"0":{"voice":{"rot":3}}}}})");
      comp::Cfg d; comp::comp_of(&k, d);
      comp::resolve(d, 0, 16, -1, 7, nullptr, 0, nullptr, 0, r); assert(r.v[0].pulses == 6 && r.v[0].steps == 12 && r.v[0].rot == 3); }   /* one bit per key */
    std::printf("C resolve: zone > chord > section > route, field by field; an enter-zone acts only after entering\n");
}

int main() {
    test_parse();
    test_resolve();
    std::printf("composer ok\n");
    return 0;
}
