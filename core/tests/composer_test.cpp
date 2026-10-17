/* Composition step 1 (docs/superpowers/plans/2026-10-17-composition-1-foundation.md): the composer's core checks.
     python core/tests/run.py      (row C) */
#include "../composer.hpp"
#include "../fieldscape.h"
#include <cassert>
#include <chrono>
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

struct Log { std::vector<double> f, t, d; std::vector<int> role; };
static void lg(void *p, int role, double f, double dur, double t, double) { Log *l = (Log *)p; l->f.push_back(f); l->t.push_back(t); l->d.push_back(dur); l->role.push_back(role); }
static double draw(void *p) { unsigned *s = (unsigned *)p; *s = *s * 1664525u + 1013904223u; return (*s >> 8) / 16777216.0; }
static const char *ROUTE = R"({"version":17,"tempo":96,"key":50,"prog":[{"r":0,"q":"m7"},{"r":5,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}],
    "voice":{"on":true,"synth":"fm"},"sect":{"on":true,"synth":"fm"},"v3":{"on":true,"synth":"am"},"zones":{"on":false},"bed":{"on":false}%s})";
/* the notes a route plays in `secs`, walked from 0 to 1 along it */
static Log render(const std::string &comp, double secs, const char *zones_in = nullptr, const char *zone_entered = nullptr) {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, comp.c_str());
    unsigned seed = 11; Log l;
    fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, lg, &l);
    const int r = fs_piece_add_route(pc, buf);
    const int blocks = (int)(secs * 48000 / 128);
    for (int b = 0; b < blocks; b++) {
        fs_piece_walk(pc, r, (double)b / blocks, 0);
        if (zones_in && b == blocks / 2) fs_piece_zones(pc, zones_in, zone_entered ? zone_entered : "");
        fs_process(pc, 128);
    }
    fs_destroy(pc); return l;
}
static bool same(const Log &a, const Log &b) { return a.f == b.f && a.t == b.t && a.d == b.d && a.role == b.role; }
static const char *DEFAULTS = R"(,"comp":{"seed":1,"voice":{"style":"free","role":"accomp"},"sect":{"style":"free","role":"accomp"},"v3":{"style":"free","role":"accomp"},"bass":{"mode":"hold"},"over":{"chord":{},"section":{},"zone":{}}})";
static void test_kept() {
    const Log none = render("", 40), defaults = render(DEFAULTS, 40);
    std::printf("C kept: %zu notes without comp, %zu with today's defaults written out; identical %d\n", none.f.size(), defaults.f.size(), (int)same(none, defaults));
    assert(none.f.size() > 50 && same(none, defaults));
}
static void test_off() {
    const Log off = render(R"(,"comp":{"sect":{"role":"off"}})", 40);
    int sect = 0, others = 0; for (int r : off.role) { if (r == 2) sect++; else others++; }
    std::printf("C off: Second voice off - %d notes from it, %d from the others\n", sect, others);
    assert(sect == 0 && others > 20);
    /* two routes, the second composed: walking the first, its notes are the plain route's (Review Focus 1) */
    char a[8192], b[8192]; std::snprintf(a, sizeof a, ROUTE, ""); std::snprintf(b, sizeof b, ROUTE, R"(,"comp":{"voice":{"role":"off"}})");
    unsigned s1 = 11, s2 = 11; Log l1, l2;
    fs_device *p1 = fs_create("piece"); fs_prepare(p1, 48000, 128); fs_piece_test_hooks(p1, draw, &s1, lg, &l1); int r1 = fs_piece_add_route(p1, a);
    fs_device *p2 = fs_create("piece"); fs_prepare(p2, 48000, 128); fs_piece_test_hooks(p2, draw, &s2, lg, &l2); int r2 = fs_piece_add_route(p2, a); fs_piece_add_route(p2, b);
    for (int k = 0; k < (int)(30 * 48000 / 128); k++) { fs_piece_walk(p1, r1, k / 11250.0, 0); fs_piece_walk(p2, r2, k / 11250.0, 0); fs_process(p1, 128); fs_process(p2, 128); }
    fs_destroy(p1); fs_destroy(p2);
    assert(same(l1, l2));
}

static double midi(double f) { return 69 + 12 * std::log2(f / 440); }
/* the notes of `role` within one bar starting at bar `b` (seconds per bar at 96 bpm = 2.5) */
static std::vector<int> bar_notes(const Log &l, int role, int b) {
    std::vector<int> o; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == role && l.t[i] >= b * 2.5 - 1e-6 && l.t[i] < (b + 1) * 2.5 - 1e-6) o.push_back((int)std::lround(midi(l.f[i])));
    return o;
}
static void test_arpeggio() {
    /* D minor 7 (D F A C, key 50): up 1-3-5-7 in sixteenths, from D3 (50) */
    const Log l = render(R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n","lo":50,"hi":74,"grid":[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]}})", 6);
    const std::vector<int> n = bar_notes(l, 1, 0);
    std::printf("C arpeggio: bar 0 (chord 1) top notes"); for (int m : n) std::printf(" %d", m); std::printf("\\n");
    assert(n.size() == 16 && n[0] == 50 && n[1] == 53 && n[2] == 57 && n[3] == 60 && n[4] == 50);
}

static void test_styles() {
    /* Pedal: the fifth of the first chord, every quarter, held across chord changes when follow is off */
    const Log p = render(R"(,"comp":{"sect":{"style":"pedal","rate":"4n","pedal":"fifth","follow":false,"lo":55,"hi":80}})", 24);
    int bad = 0, n = 0; for (size_t i = 0; i < p.f.size(); i++) if (p.role[i] == 2) { n++; if ((int)std::lround(midi(p.f[i])) != 57) bad++; }
    std::printf("C pedal: %d notes, %d not A3 (57)\\n", n, bad);
    assert(n >= 30 && bad == 0);
    /* Strum: root, 3rd, 5th rolled up 40 ms apart, every half note */
    const Log s = render(R"(,"comp":{"v3":{"style":"strum","rate":"2n","tones":[0,1,2],"spread":40,"lo":50,"hi":74}})", 6);
    std::vector<double> ts; std::vector<int> ms; for (size_t i = 0; i < s.f.size(); i++) if (s.role[i] == 3) { ts.push_back(s.t[i]); ms.push_back((int)std::lround(midi(s.f[i]))); }
    assert(ts.size() >= 6 && ms[0] == 50 && ms[1] == 53 && ms[2] == 57 && std::fabs((ts[1] - ts[0]) - 0.04) < 1e-6 && std::fabs((ts[2] - ts[1]) - 0.04) < 1e-6);
    std::printf("C strum: D F A rolled 40 ms apart\\n");
    /* Euclidean: E(3, 8) in eighths, 3 notes a bar */
    const Log e = render(R"(,"comp":{"voice":{"style":"euclid","rate":"8n","pulses":3,"steps":8,"grid":[0,2,1]}})", 6);
    const std::vector<int> en = bar_notes(e, 1, 1);
    std::printf("C euclid: %zu top notes in bar 2\\n", en.size());
    assert(en.size() == 3);
    /* Swells: one long chord a bar, each note as long as the bar x len */
    const Log w = render(R"(,"comp":{"sect":{"style":"swells","rate":"1n","len":1,"tones":[0,1,2,3]}})", 6);
    int sw = 0; double dmin = 1e9; for (size_t i = 0; i < w.f.size(); i++) if (w.role[i] == 2) { sw++; dmin = std::fmin(dmin, w.d[i]); }
    std::printf("C swells: %d notes, shortest %.2f s\\n", sw, dmin);
    assert(sw >= 8 && std::fabs(dmin - 2.5) < 1e-6);
    const std::string vr = R"(,"comp":{"seed":5,"voice":{"style":"arpeggio","rate":"16n","variation":0.8}})", vr2 = R"(,"comp":{"seed":6,"voice":{"style":"arpeggio","rate":"16n","variation":0.8}})";
    assert(same(render(vr, 6), render(vr, 6)));
    assert(!same(render(vr, 6), render(vr2, 6)));
    std::printf("C variation: the same seed repeats exactly; another seed differs\\n");
    /* Swells on chord 1 only, then Arpeggio: the arpeggio's notes play with the voice's own envelope again. Checked through the
       piece's own hook: a held swell envelope would make its note-ons start at a 0.5 s+ attack; we read the voice's level 30 ms in */
    { char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"sect":{"style":"arpeggio","rate":"8n"},"over":{"chord":{"0":{"sect":{"style":"swells","rate":"1n","len":1}}}}})");
      unsigned sd = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &sd, nullptr, nullptr);
      const int r = fs_piece_add_route(pc, buf);
      float rl[2 + 6 * 3]; double late = -1;
      for (int b = 0; b < (int)(20 * 48000 / 128); b++) { fs_piece_walk(pc, r, b / 3750.0 > 1 ? 1 : b / 3750.0, 0); fs_process(pc, 128);
          if (b == (int)(16 * 48000 / 128)) { fs_piece_roles(pc, rl, 6); late = rl[8]; } }   /* the Second voice's level at 16 s (chord 4) */
      fs_destroy(pc);
      std::printf("C swells release: the Second voice at chord 4 %.1f dB (its own envelope)\\n", late);
      assert(late > -40); }
}

static void test_bass() {
    const Log off = render(R"(,"comp":{"bass":{"mode":"off"}})", 20), ped = render(R"(,"comp":{"bass":{"mode":"pedal","pedal":"root"}})", 20);
    int b0 = 0; for (int r : off.role) b0 += r == 0;
    std::vector<int> pm; for (size_t i = 0; i < ped.f.size(); i++) if (ped.role[i] == 0) pm.push_back((int)std::lround(midi(ped.f[i])));
    std::printf("C bass: off %d notes; pedal %zu notes, first %d\\n", b0, pm.size(), pm.empty() ? -1 : pm[0]);
    assert(b0 == 0 && pm.size() == 1 && pm[0] % 12 == 50 % 12);   /* one held D, never re-struck across the 4 chords */
}

static void test_cost() {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n"},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})");
    unsigned seed = 3; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    const int r = fs_piece_add_route(pc, buf); double worst = 0;
    for (int b = 0; b < (int)(30 * 48000 / 128); b++) { fs_piece_walk(pc, r, b / 11250.0, 0);
        auto t0 = std::chrono::steady_clock::now(); fs_process(pc, 128); if (b > 375) worst = std::fmax(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
    fs_destroy(pc);
    std::printf("C cost: worst block %.3f ms with every voice busy\\n", worst);
    assert(worst < 1.33);
}

int main() {
    test_parse();
    test_resolve();
    test_kept();
    test_off();
    test_arpeggio();
    test_styles();
    test_bass();
    test_cost();
    std::printf("composer ok\n");
    return 0;
}
