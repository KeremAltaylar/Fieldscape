/* Composition step 1 (docs/superpowers/plans/2026-10-17-composition-1-foundation.md): the composer's core checks.
     python core/tests/run.py      (row C) */
#include "../composer.hpp"
#include "../fieldscape.h"
#include "../tone.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
static Log render(const std::string &comp, double secs, const char *zones_in = nullptr, const char *zone_entered = nullptr, double zone_at = 0.5) {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, comp.c_str());
    unsigned seed = 11; Log l;
    fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, lg, &l);
    const int r = fs_piece_add_route(pc, buf);
    const int blocks = (int)(secs * 48000 / 128);
    for (int b = 0; b < blocks; b++) {
        fs_piece_walk(pc, r, (double)b / blocks, 0);
        if (zones_in && b == (int)(blocks * zone_at)) fs_piece_zones(pc, zones_in, zone_entered ? zone_entered : "");
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
    std::printf("C arpeggio: bar 0 (chord 1) top notes"); for (int m : n) std::printf(" %d", m); std::printf("\n");
    assert(n.size() == 16 && n[0] == 50 && n[1] == 53 && n[2] == 57 && n[3] == 60 && n[4] == 50);
}

static void test_styles() {
    /* Pedal: the fifth of the first chord, every quarter, held across chord changes when follow is off */
    const Log p = render(R"(,"comp":{"sect":{"style":"pedal","rate":"4n","pedal":"fifth","follow":false,"lo":55,"hi":80}})", 24);
    int bad = 0, n = 0; for (size_t i = 0; i < p.f.size(); i++) if (p.role[i] == 2) { n++; if ((int)std::lround(midi(p.f[i])) != 57) bad++; }
    std::printf("C pedal: %d notes, %d not A3 (57)\n", n, bad);
    assert(n >= 30 && bad == 0);
    /* Strum: root, 3rd, 5th rolled up 40 ms apart, every half note */
    const Log s = render(R"(,"comp":{"v3":{"style":"strum","rate":"2n","tones":[0,1,2],"spread":40,"lo":50,"hi":74}})", 6);
    std::vector<double> ts; std::vector<int> ms; for (size_t i = 0; i < s.f.size(); i++) if (s.role[i] == 3) { ts.push_back(s.t[i]); ms.push_back((int)std::lround(midi(s.f[i]))); }
    assert(ts.size() >= 6 && ms[0] == 50 && ms[1] == 53 && ms[2] == 57 && std::fabs((ts[1] - ts[0]) - 0.04) < 1e-6 && std::fabs((ts[2] - ts[1]) - 0.04) < 1e-6);
    std::printf("C strum: D F A rolled 40 ms apart\n");
    /* Euclidean: E(3, 8) in eighths, 3 notes a bar */
    const Log e = render(R"(,"comp":{"voice":{"style":"euclid","rate":"8n","pulses":3,"steps":8,"grid":[0,2,1]}})", 6);
    const std::vector<int> en = bar_notes(e, 1, 1);
    std::printf("C euclid: %zu top notes in bar 2\n", en.size());
    assert(en.size() == 3);
    /* Swells: one long chord a bar, each note as long as the bar x len */
    const Log w = render(R"(,"comp":{"sect":{"style":"swells","rate":"1n","len":1,"tones":[0,1,2,3]}})", 6);
    int sw = 0; double dmin = 1e9; for (size_t i = 0; i < w.f.size(); i++) if (w.role[i] == 2) { sw++; dmin = std::fmin(dmin, w.d[i]); }
    std::printf("C swells: %d notes, shortest %.2f s\n", sw, dmin);
    assert(sw >= 8 && std::fabs(dmin - 2.5) < 1e-6);
    const std::string vr = R"(,"comp":{"seed":5,"voice":{"style":"arpeggio","rate":"16n","variation":0.8}})", vr2 = R"(,"comp":{"seed":6,"voice":{"style":"arpeggio","rate":"16n","variation":0.8}})";
    assert(same(render(vr, 6), render(vr, 6)));
    assert(!same(render(vr, 6), render(vr2, 6)));
    std::printf("C variation: the same seed repeats exactly; another seed differs\n");
    /* Swells on chord 1 only, then Arpeggio: the arpeggio's notes play with the voice's own envelope again. Checked through the
       piece's own hook: a held swell envelope would make its note-ons start at a 0.5 s+ attack; we read the voice's level 30 ms in */
    { char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"sect":{"style":"arpeggio","rate":"8n"},"over":{"chord":{"0":{"sect":{"style":"swells","rate":"1n","len":1}}}}})");
      unsigned sd = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &sd, nullptr, nullptr);
      const int r = fs_piece_add_route(pc, buf);
      float rl[2 + 6 * 3]; double late = -1;
      for (int b = 0; b < (int)(20 * 48000 / 128); b++) { fs_piece_walk(pc, r, b / 3750.0 > 1 ? 1 : b / 3750.0, 0); fs_process(pc, 128);
          if (b == (int)(16 * 48000 / 128)) { fs_piece_roles(pc, rl, 6); late = rl[8]; } }   /* the Second voice's level at 16 s (chord 4) */
      fs_destroy(pc);
      std::printf("C swells release: the Second voice at chord 4 %.1f dB (its own envelope)\n", late);
      assert(late > -40); }
}

static void test_bass() {
    const Log off = render(R"(,"comp":{"bass":{"mode":"off"}})", 20), ped = render(R"(,"comp":{"bass":{"mode":"pedal","pedal":"root"}})", 20);
    int b0 = 0; for (int r : off.role) b0 += r == 0;
    std::vector<int> pm; for (size_t i = 0; i < ped.f.size(); i++) if (ped.role[i] == 0) pm.push_back((int)std::lround(midi(ped.f[i])));
    std::printf("C bass: off %d notes; pedal %zu notes, first %d\n", b0, pm.size(), pm.empty() ? -1 : pm[0]);
    assert(b0 == 0 && pm.size() == 1 && pm[0] % 12 == 50 % 12);   /* one held D, never re-struck across the 4 chords */
}

/* a chord's pedal, then hold: from chord 2 on the bass is chord 2's root, struck at the bar line, not the pedal hanging on */
static void test_bass_switch() {
    const Log l = render(R"(,"comp":{"bass":{"mode":"hold"},"over":{"chord":{"0":{"bass":{"mode":"pedal","pedal":"fifth"}}}}})", 20);
    std::vector<int> c1, c2; std::vector<double> t2;
    for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 0) {
        const int m = (int)std::lround(midi(l.f[i]));
        if (l.t[i] < 5 - 1e-6) c1.push_back(m); else if (l.t[i] < 10 - 1e-6) { c2.push_back(m); t2.push_back(l.t[i]); } }
    std::printf("C bass switch: chord 1 bass"); for (int m : c1) std::printf(" %d", m); std::printf("; chord 2 bass"); for (int m : c2) std::printf(" %d", m);
    std::printf(" from %.2f s\n", t2.empty() ? -1.0 : t2[0]);
    assert(!c1.empty() && c1[0] % 12 == (50 + 7) % 12);                       /* the pedal: A, the key's fifth */
    assert(!c2.empty() && t2[0] < 5 + 2.5 + 1e-6);                             /* struck by chord 2's first or second bar line */
    for (int m : c2) assert(m % 12 == (50 + 5) % 12);                          /* and only G, chord 2's root */
    /* mid-chord: the route's pedal (D), a zone that holds the root entered at 12 s, inside chord 3 (A, 10-15 s): at the next
       bar line (12.5 s) the pedal is let go and A struck, not left hanging to chord 4 */
    const Log z = render(R"(,"comp":{"bass":{"mode":"pedal","pedal":"root"},"over":{"zone":{"z":{"when":"inside","blend":0,"bass":{"mode":"hold"}}}}})", 21, "z", nullptr, 0.66);
    std::vector<int> mid; for (size_t i = 0; i < z.f.size(); i++) if (z.role[i] == 0 && z.t[i] > 13.8 && z.t[i] < 15.75 - 1e-6) mid.push_back((int)std::lround(midi(z.f[i])));
    std::printf("C bass switch: a zone's hold mid-chord 3 strikes"); for (int m : mid) std::printf(" %d", m); std::printf("\n");
    assert(mid.size() == 1 && mid[0] % 12 == (50 + 7) % 12);
}

/* Strum and Swells are chords on a one-voice synth too (final review C1): each chord tone's level in the output - D F A of
   D minor 7, the third voice alone - by Goertzel over a window past the attack, against the same tone struck alone. The
   voice's own chain sits D ~15 dB under A (alone, too), so the check is each tone within 3 dB of itself alone (a strum's
   first note is accented +0.12, ~2 dB); one voice gave D 30+ dB under itself */
static double tone_db(const std::vector<float> &x, double f) {
    double w = 2 * 3.141592653589793 * f / 48000, c = 2 * std::cos(w), s1 = 0, s2 = 0;
    for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return 10 * std::log10((s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)x.size() * x.size()) + 1e-30);
}
static void chord_db(const char *syn, bool swells, const char *tones, double d[3]) {
    char comp[512], buf[8192];
    std::snprintf(comp, sizeof comp, R"(,"comp":{"voice":{"role":"off"},"sect":{"role":"off"},"bass":{"mode":"off"},"v3":{"style":"%s","rate":"1n","tones":%s,"lo":50,"hi":74}})", swells ? "swells" : "strum", tones);
    std::snprintf(buf, sizeof buf, ROUTE, comp);
    std::string b(buf); const std::string was = R"("v3":{"on":true,"synth":"am"})"; b.replace(b.find(was), was.size(), std::string(R"("v3":{"on":true,"synth":")") + syn + "\"}");
    unsigned seed = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    const int r = fs_piece_add_route(pc, b.c_str());
    std::vector<float> x; const double from = swells ? 1.0 : 0.6, to = swells ? 2.2 : 1.8;
    for (int k = 0; k < (int)(2.4 * 48000 / 128); k++) { fs_piece_walk(pc, r, 0, 0); fs_process(pc, 128);
        if (k * 128 >= from * 48000 && k * 128 < to * 48000) { const float *o = fs_out(pc, 0); x.insert(x.end(), o, o + 128); } }
    fs_destroy(pc);
    const int m[3] = { 50, 53, 57 };
    for (int i = 0; i < 3; i++) d[i] = tone_db(x, 440 * std::pow(2, (m[i] - 69) / 12.0));
}
static void test_chord_out() {
    std::printf("C chord tones (in the chord / alone):");
    double worst = 0;
    for (const char *syn : { "fm", "am" }) for (int sw = 0; sw < 2; sw++) {
        double c[3], one[3], t[3]; static const char *const SOLO[3] = { "[0]", "[1]", "[2]" };
        chord_db(syn, sw, "[0,1,2]", c);
        for (int i = 0; i < 3; i++) { chord_db(syn, sw, SOLO[i], t); one[i] = t[i]; worst = std::fmax(worst, std::fabs(c[i] - one[i])); }
        std::printf(" %s %s D %.0f/%.0f F %.0f/%.0f A %.0f/%.0f dB;", syn, sw ? "swells" : "strum", c[0], one[0], c[1], one[1], c[2], one[2]);
    }
    std::printf(" furthest from alone %.1f dB\n", worst);
    assert(worst < 3);
}

/* final review M4: a zone "on entering", entered mid-bar (11 s, inside bar 4), plays a phrase - bars 5 and 6, from the next
   bar line - not one bar */
static void test_enter_phrase() {
    const Log l = render(R"(,"comp":{"voice":{"role":"off"},"over":{"zone":{"z":{"when":"enter","voice":{"role":"accomp","style":"arpeggio","rate":"4n"}}}}})", 20, "z", "z", 0.55);
    int n[8] = {}; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1) { const int b = (int)(l.t[i] / 2.5 + 1e-9); if (b < 8) n[b]++; }
    std::printf("C enter: top notes by bar"); for (int c : n) std::printf(" %d", c); std::printf("\n");
    assert(n[4] == 0 && n[5] == 4 && n[6] == 4 && n[7] == 0);
}
/* final review M8: voice 1's bass row "off" ends the held bass in the roles readout too (it named it until the next note) */
static void test_bass_off_heard() {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"bass":{"mode":"hold"},"over":{"chord":{"1":{"bass":{"mode":"off"}}}}})");
    unsigned seed = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    const int r = fs_piece_add_route(pc, buf);
    float rl[2 + 6 * 3]; float before = 0, after = 0;
    auto low = [&]() { float f = 0; for (int i = 0; i < (int)rl[1]; i++) if (rl[2 + i] < 100) f = rl[2 + i]; return f; };   /* the bass: under 100 Hz */
    for (int b = 0; b < (int)(10 * 48000 / 128); b++) { fs_piece_walk(pc, r, b / 7500.0, 0); fs_process(pc, 128);   /* chord 2 from 5 s */
        if (b == (int)(4 * 48000 / 128)) { fs_piece_roles(pc, rl, 6); before = low(); }
        if (b == (int)(9 * 48000 / 128)) { fs_piece_roles(pc, rl, 6); after = low(); } }
    fs_destroy(pc);
    std::printf("C bass off: the roles readout's bass note %.1f Hz in chord 1, %.1f Hz after the row turns off\n", before, after);
    assert(before > 20 && after == 0);
}

/* final review I2, core against the page: for each style chosen in the dock's model (web/compose-model.js, run here in node),
   at the route and at a chord, the pattern or tones its inspector shows are what comp_of + resolve play from the JSON it writes */
static void test_model_defaults() {
#ifdef __EMSCRIPTEN__
    const char *out = emscripten_run_script_string(R"JS((function () {
        var q = require(process.cwd() + "/web/compose-model.js"), M = q && q.defaults ? q : globalThis.FsCompose, o = [];   /* the repo is "type": "module": it sets FsCompose */
        ["arpeggio", "euclid", "strum", "swells"].forEach(function (st) { [{ kind: "route" }, { kind: "chord", i: 2 }].forEach(function (sc) {
            var c = M.defaults(); M.setValue(c, sc, "sect", "style", st);
            var e = M.effective(c, sc, "sect").values; o.push({ style: st, chord: sc.kind === "chord", comp: c, grid: e.grid || null, tones: e.tones || null }); }); });
        return JSON.stringify(o); })())JS");
    const Json all = Json::parse(out);
    int n = 0;
    for (size_t i = 0; i < all.size(); i++) {
        const Json &x = *all.at(i);
        comp::Cfg c; comp::comp_of(x.get("comp"), c); comp::Resolved r;
        comp::resolve(c, x.flag("chord", false) ? 2 : 0, 16, -1, 7, nullptr, 0, nullptr, 0, r);
        const comp::Voice &v = r.v[1];
        assert(v.style == (x.s("style", "") == "arpeggio" ? comp::ARPEGGIO : x.s("style", "") == "euclid" ? comp::EUCLID : x.s("style", "") == "strum" ? comp::STRUM : comp::SWELLS));
        if (const Json *g = x.get("grid")) if (g->kind == Json::ARR) for (int k = 0; k < comp::MAX_COLS; k++) { assert(v.grid[k] == (int)g->at(k)->num); }
        if (const Json *t = x.get("tones")) if (t->kind == Json::ARR) { unsigned b = 0; for (size_t k = 0; k < t->size(); k++) b |= 1u << (int)t->at(k)->num; assert(v.tones == b); }
        n++;
    }
    std::printf("C model: %d style choices (route and chord) - the pattern or tones the inspector shows are what plays\n", n);
    assert(n == 8);
#else
    std::printf("C model: skipped (needs node: the em++ build)\n");
#endif
}

/* worst block and p99.9 over 30 s, 128 frames, of a route with this comp and these synths (voice, sect, v3) */
static void cost_of(const char *comp, const char *syn[3], double &worst, double &p999) {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, comp);
    std::string b(buf); const char *was[3] = { R"("voice":{"on":true,"synth":"fm"})", R"("sect":{"on":true,"synth":"fm"})", R"("v3":{"on":true,"synth":"am"})" };
    const char *key[3] = { "voice", "sect", "v3" };
    for (int i = 0; i < 3; i++) { const std::string w = was[i]; b.replace(b.find(w), w.size(), std::string("\"") + key[i] + R"(":{"on":true,"synth":")" + syn[i] + "\"}"); }
    unsigned seed = 3; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    const int r = fs_piece_add_route(pc, b.c_str()); std::vector<double> ms;
    for (int k = 0; k < (int)(30 * 48000 / 128); k++) { fs_piece_walk(pc, r, k / 11250.0, 0);
        auto t0 = std::chrono::steady_clock::now(); fs_process(pc, 128); if (k > 375) ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
    fs_destroy(pc);
    std::sort(ms.begin(), ms.end()); worst = ms.back(); p999 = ms[(size_t)(ms.size() * 0.999)];
}
static void test_cost() {
    const char *fm[3] = { "fm", "fm", "am" }, *metal[3] = { "metal", "metal", "metal" };
    double w1, p1, w2, p2;
    cost_of(R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n"},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})", fm, w1, p1);
    /* final re-review: the heaviest synth (12 oscillators, a sustain-0 decay) on every role, every chord voice busy */
    cost_of(R"(,"comp":{"voice":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})", metal, w2, p2);
    std::printf("C cost: fm/fm/am arpeggio-strum-swells p99.9 %.3f ms worst %.3f ms; metal x3 strum-strum-swells p99.9 %.3f ms worst %.3f ms (every voice busy)\n", p1, w1, p2, w2);
    assert(w1 < 1.33);
}
/* a pool voice stops rendering only once its envelope is under -80 dB for good (Envelope::quiet): never while audible */
static void test_quiet() {
    struct C { double a, d, s, r, rel_at; } cs[4] = { { 0.004, 1.8, 0, 2.6, 0.3 }, { 0.004, 1.8, 0, 2.6, 5 }, { 0.5, 1.2, 0.6, 3.2, 1.0 }, { 0.012, 0.35, 0.18, 1.1, 0.1 } };
    double last = -300;
    for (const C &c : cs) {
        tone::Envelope e; e.init(48000); e.set(c.a, c.d, c.s, c.r, false, true);
        e.triggerAttack(0, 0.8); e.triggerRelease(c.rel_at);
        double from = -1;
        for (int k = 0; k < 12 * 375 && from < 0; k++) { const double t0 = k * 128 / 48000.0; if (e.quiet(t0, t0 + 128 / 48000.0)) from = t0; }
        assert(from > 0);
        double top = 0; for (double t = from; t < 12; t += 1 / 48000.0) top = std::fmax(top, std::fabs(e.sig.value(t)));
        assert(top <= 1e-4);
        last = std::fmax(last, 20 * std::log10(top + 1e-30));
    }
    std::printf("C quiet: a pool voice stops only under -80 dB (loudest after it stops %.0f dB)\n", last);
}

static void test_engine_zone() {
    /* a route with a zone override on point "z1": inside it, voice 1 is off */
    char route[8192]; std::snprintf(route, sizeof route, R"({"type":"Feature","geometry":{"type":"LineString","coordinates":[[29.0,41.0],[29.004,41.0]]},
        "properties":{"id":"r1","kind":"route","name":"R","patch":%s}})",
        R"({"version":17,"tempo":96,"key":50,"prog":[{"r":0,"q":"m7"}],"bed":{"on":false},"zones":{"on":false},"voice":{"on":true,"synth":"fm"},"sect":{"on":false},"v3":{"on":false},"comp":{"voice":{"style":"arpeggio","rate":"16n"},"bass":{"mode":"off"},"over":{"zone":{"z1":{"voice":{"role":"off"}}}}}})");
    const char *pt = R"({"type":"Feature","geometry":{"type":"Point","coordinates":[29.003,41.0]},"properties":{"id":"z1","kind":"point","name":"Z","sound":{"zoneR":40}}})";
    auto level = [&](double lon) {
        fs_engine *e = fs_engine_create(48000, 128); fs_engine_upsert(e, route); fs_engine_upsert(e, pt);
        double ms = 0; long long n = 0;
        for (int b = 0; b < (int)(12 * 48000 / 128); b++) { if (b % 40 == 0) fs_engine_step(e, lon, 41.0); fs_engine_process(e, 128);
            if (b > 4 * 48000 / 128) { const float *l = fs_engine_out(e, 0); for (int i = 0; i < 128; i++) { ms += l[i] * l[i]; n++; } } }
        fs_engine_destroy(e); return 10 * std::log10(ms / n + 1e-30); };
    const double out = level(29.001), in = level(29.003);
    std::printf("C zone: outside the zone %.1f dB, inside (voice 1 off) %.1f dB\n", out, in);
    assert(out > -60 && in < out - 20);
}

int main() {
    test_parse();
    test_resolve();
    test_kept();
    test_off();
    test_arpeggio();
    test_styles();
    test_bass();
    test_bass_switch();
    test_model_defaults();
    test_chord_out();
    test_enter_phrase();
    test_bass_off_heard();
    test_quiet();
    test_cost();
    test_engine_zone();
    std::printf("composer ok\n");
    return 0;
}
