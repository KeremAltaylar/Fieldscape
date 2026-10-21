/* Composition step 1 (docs/superpowers/plans/2026-10-17-composition-1-foundation.md): the composer's core checks.
     python core/tests/run.py      (row C) */
#include "../composer.hpp"
#include "../fieldscape.h"
#include "../tone.hpp"
#include "../levels.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
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
        "over":{"chord":{"3":{"voice":{"style":"strum"}},"70":{"voice":{"style":"swells"}}},
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
    for (int i = 0; i < comp::NCHORD; i++) if (i != 3) assert(!c.chord[i].used);   /* "70": out of range, ignored */
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

struct Log { std::vector<double> f, t, d, v; std::vector<int> role; };
static void lg(void *p, int role, double f, double dur, double t, double vel) { Log *l = (Log *)p; l->f.push_back(f); l->t.push_back(t); l->d.push_back(dur); l->v.push_back(vel); l->role.push_back(role); }
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
static void chord_db(const char *syn, bool swells, const char *tones, double d[4]) {
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
    double e = 0; for (float v : x) e += (double)v * v; d[3] = 10 * std::log10(e / x.size() + 1e-30);   /* the whole output */
}
static void test_chord_out() {
    /* 2026-10-19 (level match): a chord's tones share one note's power, and a style sits at Free's level whatever it plays - so
       each of 3 tones is its own level alone less 10 log10 3 (4.8 dB), and the chord as loud as one tone alone */
    const double share = 10 * std::log10(3.0);
    std::printf("C chord tones (in the chord / alone, less a third's share):");
    double worst = 0, mworst = 0;
    for (const char *syn : { "fm", "am" }) for (int sw = 0; sw < 2; sw++) {
        double c[4], one[3], t[4]; static const char *const SOLO[3] = { "[0]", "[1]", "[2]" };
        chord_db(syn, sw, "[0,1,2]", c);
        for (int i = 0; i < 3; i++) { chord_db(syn, sw, SOLO[i], t); one[i] = t[i]; worst = std::fmax(worst, std::fabs(c[i] - (one[i] - share))); }
        std::printf(" %s %s D %.0f/%.0f F %.0f/%.0f A %.0f/%.0f dB;", syn, sw ? "swells" : "strum", c[0], one[0] - share, c[1], one[1] - share, c[2], one[2] - share);
    }
    /* metal: 1 + 2 voices, so a 3-tone chord is still whole. Its inharmonic partials leave nothing at the notes themselves, so
       the check is the whole output: the chord's energy is its tones' alone, on average (one voice would be ~5 dB under) */
    for (int sw = 0; sw < 2; sw++) {
        double c[4], t[4], sum = 0; static const char *const SOLO[3] = { "[0]", "[1]", "[2]" };
        chord_db("metal", sw, "[0,1,2]", c);
        for (int i = 0; i < 3; i++) { chord_db("metal", sw, SOLO[i], t); sum += std::pow(10, t[3] / 10); }
        const double alone = 10 * std::log10(sum / 3); mworst = std::fmax(mworst, alone - c[3]);   /* under: tones lost (it sits over: metal's chords are brighter than its notes) */
        std::printf(" metal %s whole %.1f / a tone alone %.1f dB;", sw ? "swells" : "strum", c[3], alone);
    }
    std::printf(" furthest from alone %.1f dB (metal %.1f dB)\n", worst, mworst);
    assert(worst < 3 && mworst < 2);
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
/* fix round 1 (review C1): a style's level was worked out on the audio thread, again whenever the walk moved the onsets or
   the chord changed - 4.5-9 ms blocks. Now comp_gains does it when the patch is read; the audio thread only reads a gain.
   Every block of 30 s of a walk, morphs on and the onset mix moving every 40 blocks (as the walk between recordings does),
   strum-strum-swells on all three voices, 5 tones */
static void walk_cost(const char *syn, double &worst, int &over, double &read_ms) {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"voice":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})");
    std::string b(buf); const char *was[3] = { R"("voice":{"on":true,"synth":"fm"})", R"("sect":{"on":true,"synth":"fm"})", R"("v3":{"on":true,"synth":"am"})" };
    const char *key[3] = { "voice", "sect", "v3" };
    for (int i = 0; i < 3; i++) { const std::string w = was[i]; b.replace(b.find(w), w.size(), std::string("\"") + key[i] + R"(":{"on":true,"synth":")" + syn + "\"}"); }
    unsigned seed = 3; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    auto r0 = std::chrono::steady_clock::now();
    const int r = fs_piece_add_route(pc, b.c_str());
    read_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();   /* the calling thread's share */
    worst = 0; over = 0;
    for (int k = 0; k < (int)(30 * 48000 / 128); k++) {
        fs_piece_walk(pc, r, k / 11250.0, 0);
        if (k % 40 == 0) { double d[2] = { 10 + 5 * std::sin(k / 300.0), 20 }, rad[2] = { 50, 50 }, c[2] = { 2000, 2000 }, o[2] = { 0.5, 2.0 }; fs_piece_character(pc, 2, d, rad, c, o); }
        auto t0 = std::chrono::steady_clock::now(); fs_process(pc, 128);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (k > 375) { worst = std::max(worst, ms); over += ms > 1.5; }
    }
    fs_destroy(pc);
}
static void test_cost() {
    const char *fm[3] = { "fm", "fm", "am" }, *metal[3] = { "metal", "metal", "metal" };
    double w1, p1, w2, p2;
    cost_of(R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n"},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})", fm, w1, p1);
    /* final re-review: the heaviest synth (12 oscillators, a sustain-0 decay) on every role, every chord voice busy */
    cost_of(R"(,"comp":{"voice":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})", metal, w2, p2);
    std::printf("C cost: fm/fm/am arpeggio-strum-swells p99.9 %.3f ms worst %.3f ms; metal x3 strum-strum-swells p99.9 %.3f ms worst %.3f ms (every voice busy)\n", p1, w1, p2, w2);
    assert(p1 < 1.0 && p2 < 1.0 && w1 < 1.33);   /* the worst block alone for fm/fm/am: OS spikes reach any block (the old build too) */
    int all = 0;
    for (const char *syn : { "fm", "formant", "metal" }) {
        double w, rd; int over; walk_cost(syn, w, over, rd); all += over;
        std::printf("C cost: %s x3 strum-strum-swells, morphs on, onsets moving - slowest block %.3f ms, %d over 1.5 ms; the patch read %.1f ms\n", syn, w, over, rd);
    }
    /* a block is 2.67 ms; 1.5 ms leaves the device its slack. Two blocks in 3 x 11250 for the OS (a spike lands anywhere) */
    assert(all <= 2);
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

/* Kerem 2026-10-19 (clicks and crackle in Pedal and Strum: the limiter working): each style as loud as Free, through the
   whole engine (route gain, mix, the -1 dBFS limiter), one voice sounding (voice 2), each synth at the page's defaults for
   its two controls. Chord notes share a note's power (1/sqrt n) and every style's velocities are scaled to Free's power
   (piece.cpp, "level match"). The route's echo and room are dry and its morphs off here: a held or repeated pitch rings
   the room's and the echo's combs (a pedal sat -5 to +5 dB by its pitch alone) and a note struck on every bar line meets a
   morph at one phase - colour the level match does not try to follow. Pluck's bursts are a few ms of pink noise, so each
   note's energy is a draw: it is measured over longer */
static double hd(const char *sy, int w) {     /* index.html SYNTH_PARAMS: picking a synth sets its two controls to these */
    static const struct { const char *n; double h, i; } D[] = { { "fm", 1, 4 }, { "am", 1.5, 3 }, { "duo", 1.5, 0.2 }, { "mono", 2, 3 }, { "simple", 0, 1.6 },
        { "pluck", 3200, 0.92 }, { "metal", 4.1, 1.2 }, { "membrane", 0.12, 3.2 }, { "wavetable", 0.35, 0.5 }, { "comb", 0.4, 0.5 }, { "formant", 0.5, 0.5 } };
    for (auto &d : D) if (!std::strcmp(d.n, sy)) return w ? d.i : d.h;
    return 0;
}
static const char *MOT = R"("motif":{"basis":"chord","notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4},{"deg":1,"start":8,"len":4},{"deg":4,"start":12,"len":4}]})";
/* the engine's output rms (dBFS) from 1 s, walking the route in `secs` with voice 2 alone on synth `sy`, style `style` ("": Free) */
static double level_db(const char *sy, const char *style, double secs, double from_s = 1) {
    char patch[4096], route[8192];
    std::snprintf(patch, sizeof patch, R"({"version":17,"tempo":96,"key":50,"prog":[{"r":0,"q":"m7"},{"r":5,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}],"bed":{"on":false},"zones":{"on":false},
        "fx":{"delayWet":0,"revWet":0},"fx2":{"delayWet":0,"revWet":0},"fx3":{"delayWet":0,"revWet":0},"morph":{"on":false},
        "voice":{"on":false},"sect":{"on":true,"synth":"%s","harm":%g,"index":%g},"v3":{"on":false},"comp":{"bass":{"mode":"off"}%s%s}})",
        sy, hd(sy, 0), hd(sy, 1), *style ? R"(,"sect":)" : "", style);
    std::snprintf(route, sizeof route, R"({"type":"Feature","geometry":{"type":"LineString","coordinates":[[29.0,41.0],[29.004,41.0]]},"properties":{"id":"r1","kind":"route","name":"R","patch":%s}})", patch);
    fs_engine *e = fs_engine_create(48000, 128); fs_engine_upsert(e, route);
    const int blocks = (int)(secs * 48000 / 128); double s = 0; long long n = 0;
    for (int b = 0; b < blocks; b++) {
        if (b % 40 == 0) fs_engine_step(e, 29.0 + 0.004 * b / blocks, 41.0);
        fs_engine_process(e, 128);
        if (b >= (int)(from_s * 375)) { const float *l = fs_engine_out(e, 0); for (int i = 0; i < 128; i++) { s += (double)l[i] * l[i]; n++; } }
    }
    fs_engine_destroy(e); return 10 * std::log10(s / std::max(1LL, n) + 1e-30);
}
/* the dock's default rate (1/8) for each style, then the templates' (web/compose-model.js), their registers too */
static const char *const LEVEL_ST[][2] = {
        { "arp 8n", R"({"style":"arpeggio"})" }, { "pedal 8n", R"({"style":"pedal"})" }, { "strum 8n", R"({"style":"strum"})" },
        { "euclid 8n", R"({"style":"euclid"})" }, { "swells 8n", R"({"style":"swells"})" },
        { "arp 16n", R"({"style":"arpeggio","rate":"16n","lo":55,"hi":84})" },
        { "strum 2n", R"({"style":"strum","rate":"2n","tones":[0,1,2,3],"spread":50,"lo":50,"hi":74})" },
        { "pedal 1n", R"({"style":"pedal","rate":"1n","pedal":"fifth","lo":43,"hi":62})" },
        { "euclid 16n", R"({"style":"euclid","rate":"16n","pulses":3,"steps":8,"rot":2,"grid":[2,4],"lo":62,"hi":86})" },
        { "swells 1n", R"({"style":"swells","rate":"1n","len":1,"tones":[0,2,4]})" },
        { "swells 2n.", R"({"style":"swells","rate":"2n.","len":1,"tones":[1,3],"lo":60,"hi":84,"attack":0.6,"release":0.5})" } };
static const char *const LEVEL_SY[] = { "fm", "am", "duo", "mono", "simple", "pluck", "metal", "membrane", "wavetable", "comb", "formant" };
static void test_levels() {
    const auto &ST = LEVEL_ST; const auto &SY = LEVEL_SY;
    /* 2 dB on every synth. The gains are worked out from the envelope when the patch is read (a render of the synth, 50-350
       ms a style, would stall the web's audio thread); what the envelope misses - mono's filter envelope, comb's resonator,
       the formants' norm 120 ms behind the pitch, metal's highpass riding its envelope (up to 4.1 dB, measured 2026-10-19) -
       is measured offline into core/levels.hpp (Task 5: node tools/levels.mjs) */
    double worst = 0, over = -1; const char *ws = "", *wy = "";
    std::printf("C levels (style - Free, dB, voice 2):");
    for (const char *sy : SY) {
        const bool pl = !std::strcmp(sy, "pluck");
        const double fr = level_db(sy, "", pl ? 61 : 41), secs = pl ? 31 : 9;   /* Free's sparse line over its whole walk */
        std::printf("\n  %-9s free %5.1f dBFS", sy, fr);
        double mine = 0;
        for (auto &st : ST) {
            const double d = level_db(sy, st[1], secs) - fr; std::printf(" %+.1f", d);
            if (std::fabs(d) > std::fabs(worst)) { worst = d; ws = st[0]; wy = sy; }
            if (std::fabs(d) > std::fabs(mine)) mine = d;
            over = std::max(over, std::fabs(d) - 2.0);
        }
        {   /* a melody voice too (Task 5: formant's sat +2.3 before the table) */
            const std::string mel = std::string(R"({"role":"melody","lo":50,"hi":81}, )") + MOT;
            const double d = level_db(sy, mel.c_str(), secs) - fr; std::printf(" mel %+.1f", d);
            if (std::fabs(d) > std::fabs(worst)) { worst = d; ws = "melody"; wy = sy; }
            if (std::fabs(d) > std::fabs(mine)) mine = d;
            over = std::max(over, std::fabs(d) - 2.0);
        }
        std::printf("  worst %+.1f", mine);
    }
    std::printf("\nC levels: the furthest from Free %+.1f dB (%s %s); every synth within 2 dB\n", worst, wy, ws);
    assert(over <= 0);
}

/* final review I1: a melody voice with no motif plays as Free (the dock says so), whatever style it holds - Generate's
   Arpeggio included: its notes are the same route's with the voice's style Free */
static void test_empty_motif() {
    const Log a = render(R"(,"comp":{"voice":{"role":"melody","style":"arpeggio","rate":"16n"}})", 20),
              f = render(R"(,"comp":{"voice":{"role":"melody","style":"free"}})", 20);
    Log a1, f1;
    for (size_t i = 0; i < a.f.size(); i++) if (a.role[i] == 1) { a1.f.push_back(a.f[i]); a1.t.push_back(a.t[i]); a1.d.push_back(a.d[i]); a1.role.push_back(1); }
    for (size_t i = 0; i < f.f.size(); i++) if (f.role[i] == 1) { f1.f.push_back(f.f[i]); f1.t.push_back(f.t[i]); f1.d.push_back(f.d[i]); f1.role.push_back(1); }
    std::printf("C empty motif: melody + arpeggio %zu notes, melody + free %zu; identical %d\n", a1.f.size(), f1.f.size(), (int)same(a1, f1));
    assert(f1.f.size() > 5 && same(a1, f1));
}
/* final review M2: the melody's gain is solved over the progression's chords, each with its own chain step - a fragment of
   one note on chord 0 left chords 1..3 (repeat, four notes) several dB loud */
static void test_melody_gain_chain() {
    for (const char *sy : { "fm", "am" }) {
        const double fr = level_db(sy, "", 41);
        const std::string mel = std::string(R"({"role":"melody","lo":50,"hi":81}, )") + MOT + R"(, "chain":[{"op":"fragment","arg":1,"from":0,"to":0}])";
        const double d = level_db(sy, mel.c_str(), 20, 8) - fr;   /* chord 1 on from bar 3 (7.5 s) of a 20 s walk */
        std::printf("C melody gain, fragment 1 on chord 0: %s chords 1..3 %+.1f dB from Free\n", sy, d);
        assert(std::fabs(d) <= 2);
    }
}

/* Kerem 2026-10-19 (clicks in Pedal and Strum): a retrigger must not click. Voice 2 alone, Pedal or Strum at 1/8 on fm, am
   and pluck, silent through chord 1 (an override), so chord 2's first strike is a clean attack - in silence, the same synth,
   pitch and velocity as what follows. For each strike after it in chord 2: the high band (x minus a 2-pole 2.5 kHz low-pass)
   as it starts, over what it was just before, against the clean attack's, per unit of velocity. A pedal re-strikes its
   ringing note on a one-voice synth; a strum's chord voices are taken again while they ring */
struct Onsets { std::vector<float> hf; std::vector<double> t, vel, f; };
static void onv(void *p, int role, double f, double, double t, double vel) { if (role == 2) { Onsets *o = (Onsets *)p; o->t.push_back(t); o->vel.push_back(vel); o->f.push_back(f); } }
static Onsets onset_run(const char *sy, const char *st) {
    char patch[4096];
    std::snprintf(patch, sizeof patch, R"({"version":17,"tempo":96,"key":50,"prog":[{"r":0,"q":"m7"},{"r":5,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}],"bed":{"on":false},"zones":{"on":false},
        "fx":{"delayWet":0,"revWet":0},"fx2":{"delayWet":0,"revWet":0},"fx3":{"delayWet":0,"revWet":0},"morph":{"on":false},
        "voice":{"on":false},"sect":{"on":true,"synth":"%s","harm":%g,"index":%g},"v3":{"on":false},
        "comp":{"bars":"repeat","bass":{"mode":"off"},"sect":%s,"over":{"chord":{"0":{"sect":{"role":"off"}}}}}})", sy, hd(sy, 0), hd(sy, 1), st);
    unsigned seed = 11; Onsets o; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, onv, &o);
    const int r = fs_piece_add_route(pc, patch); const int blocks = (int)(24 * 48000 / 128);
    std::vector<float> x; x.reserve(blocks * 128);
    for (int b = 0; b < blocks; b++) { fs_piece_walk(pc, r, (double)b / blocks, 0); fs_process(pc, 128); const float *out = fs_out(pc, 0); x.insert(x.end(), out, out + 128); }
    fs_destroy(pc);
    const double a = std::exp(-2 * 3.141592653589793 * 2500 / 48000.0); double y1 = 0, y2 = 0;
    o.hf.resize(x.size());
    for (size_t i = 0; i < x.size(); i++) { y1 = (1 - a) * x[i] + a * y1; y2 = (1 - a) * y1 + a * y2; o.hf[i] = (float)std::fabs(x[i] - y2); }
    return o;
}
/* the high band over note k's first 15 ms (a stolen voice's fade, up to 12 ms, then its attack; a pluck's comb speaks a
   period late, so its period too): its peak, and its rise over the 3 ms before (a ringing note's own highs are not a
   click). A fade shorter than the note's period cut it (a 3 ms one: x1.08-1.10 here, over a clean attack) */
static double onset_peak(const Onsets &o, size_t k, bool pluck, bool rise) {
    const long long i = (long long)std::llround(o.t[k] * 48000), W = 720 + (pluck ? (long long)(48000 / o.f[k]) : 0), n = (long long)o.hf.size();
    auto peak = [&](long long i0, long long i1) { float m = 0; for (long long j = std::max(0LL, i0); j < std::min(n, i1); j++) m = std::max(m, o.hf[j]); return (double)m; };
    return peak(i, i + W) - (rise ? peak(i - 144, i) : 0);
}
/* "bars":"repeat" here: the clean attacks are chord 2's first strum, an up strum with its accent (+0.12) on the lowest note; the
   develop mode's odd bars strum down, the accent on the highest, so the lowest struck unaccented reads x1.76 against an accented
   reference (velocity does not scale the highs linearly) - no retrigger, a reference of another accent. A down strum is measured
   here on its own, against its own first strum. */
static void test_onsets() {
    double worst = 0, plw = 0; const char *wsy = "", *wst = "";
    for (const char *sy : { "fm", "am", "pluck" }) for (const char *st : { R"({"style":"pedal"})", R"({"style":"strum"})", R"({"style":"strum","lo":36,"hi":60})", R"({"style":"strum","dir":"down"})" }) {
        const Onsets o = onset_run(sy, st);
        assert(!o.t.empty() && o.t[0] > 3.9);                   /* chord 1 silent */
        const bool pl = !std::strcmp(sy, "pluck");
        const double end = o.t[0] + 5;
        /* the clean attacks: chord 2's first strike, each of its tones on a voice silent until then - their highs per unit
           of velocity, a pitch each (a higher note of a bright synth is brighter, struck clean or not) */
        std::vector<double> cf, cl; size_t k = 0;
        for (; k < o.t.size() && o.t[k] < o.t[0] + 0.25; k++) { cf.push_back(o.f[k]); cl.push_back(onset_peak(o, k, pl, false) / o.vel[k]); assert(cl.back() > 0); }
        if (pl) {   /* a pluck's burst is a few ms of pink noise from a random point: one draw is no measure - its clean attack is
                       the brightest of a run of short notes, each rung out (a pedal a beat) */
            const Onsets a = onset_run(sy, R"({"style":"pedal","rate":"4n","len":0.05})"); double top = 0;
            for (size_t j = 0; j < a.t.size(); j++) top = std::max(top, onset_peak(a, j, true, true) / a.vel[j]);
            for (double &c : cl) c = std::max(c, top);
        }
        double most = 0; int n = 0;
        for (; k < o.t.size() && o.t[k] < end; k++) {
            size_t c = 0; while (c < cf.size() && cf[c] != o.f[k]) c++;
            if (c == cf.size()) break;                           /* a pitch chord 2 has not: chord 3 */
            most = std::max(most, onset_peak(o, k, pl, true) / o.vel[k] / cl[c]); n++;
        }
        std::printf("C onsets: %s %s - %d strikes after the clean ones, the largest rise in the highs x%.2f its pitch's clean attack\n", sy, st, n, most);
        assert(n >= 8);
        if (!pl && most > worst) { worst = most; wsy = sy; wst = st; }
        if (pl) plw = std::max(plw, most);
    }
    std::printf("C onsets: no retrigger past a clean attack (largest x%.2f, %s %s; pluck x%.2f)\n", worst, wsy, wst, plw);
    /* pluck is no envelope (nothing to fade): each strike is a fresh few ms of pink noise, its highs a draw - x1.6 */
    assert(worst <= 1.0 && plw <= 1.6);
}

static void test_motif_parse() {
    const char *J = R"({"comp":{"voice":{"role":"melody"},
      "motif":{"basis":"chord","bars":1,"notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4},{"deg":1,"oct":1,"start":8,"len":2,"acc":100},{"deg":4,"start":12,"len":8}]},
      "chain":[{"op":"repeat","from":0,"to":1},{"op":"seq","arg":2,"from":2,"to":3},{"op":"cadence","cad":"half","others":"stop","from":3,"to":3},
               {"op":"bogus","from":4,"to":5},{"op":"repeat","from":9,"to":2},{"op":"invert","from":70,"to":80}]}})";
    Json j = Json::parse(J); comp::Cfg c; comp::comp_of(j.get("comp"), c);
    assert(c.on && c.v[0].role == comp::MELODY && (c.v[0].mask & comp::F_ROLE));
    assert(c.motif.n == 4 && c.motif.basis == comp::CHORD_DEG && c.motif.bars == 1);
    assert(c.motif.note[2].deg == 1 && c.motif.note[2].oct == 1 && c.motif.note[2].acc == 100 && c.motif.note[2].len == 2);
    assert(c.motif.note[3].start == 12 && c.motif.note[3].len == 4);       /* clipped to the bar: 12 + 4 = 16 */
    assert(c.nchain == 3);                                                   /* bogus op, from > to, out of range: dropped */
    assert(c.chain[1].op == comp::OP_SEQ && c.chain[1].arg == 2 && c.chain[2].cad == comp::CAD_HALF && c.chain[2].others == comp::OTH_STOP);
    /* more than 8 notes: the first 8; a note starting past the motif: dropped */
    Json k = Json::parse(R"({"comp":{"motif":{"bars":1,"notes":[{"start":0},{"start":1},{"start":2},{"start":3},{"start":4},{"start":5},{"start":6},{"start":16},{"start":7},{"start":8}]}}})");
    comp::Cfg d; comp::comp_of(k.get("comp"), d);
    assert(d.motif.n == 8 && d.motif.note[7].start == 7);
    /* from/to rounded before they are checked: 63.6 -> 64 is past the chords, dropped */
    Json r = Json::parse(R"({"comp":{"chain":[{"op":"repeat","from":63.6,"to":63.7},{"op":"repeat","from":62.6,"to":62.7}]}})");
    comp::Cfg e; comp::comp_of(r.get("comp"), e);
    assert(e.nchain == 1 && e.chain[0].from == 63 && e.chain[0].to == 63);
    std::printf("C motif parse: 4 notes (one clipped to the bar), 3 of 6 chain steps kept, melody role\n");
}

static void test_relations_parse() {
    const char *J = R"({"comp":{"relations":[
      {"type":"canon","a":0,"b":2,"from":1,"to":5,"zone":"zA","delay":12,"trans":-3,"transform":"retro"},
      {"type":"call","a":2,"b":1,"source":"mirror","gap":4,"trigger":"enter"},
      {"type":"hocket","a":0,"b":1,"c":2,"split":"mask","every":9,"mask":"x..x.x..x......xx"},
      {"type":"hetero","a":1,"b":0,"offset":200,"ornament":0.4},
      {"type":"motion","a":0,"b":1,"motion":"par6","strength":0.5},
      {"type":"unison","a":0,"b":2,"meet":"never","octave":"octave"},
      {"type":"density","a":0,"b":1,"c":2,"cap":99,"weight":[4,0,9]},
      {"type":"canon","a":1,"b":1},
      {"type":"bogus","a":0,"b":1},
      {"type":"canon","a":0,"b":1,"from":9,"to":2},
      {"type":"canon","a":0,"b":1,"from":63.6,"to":63.7},
      {"type":"canon","a":0,"b":1,"from":62.6,"to":62.7,"delay":0}]}})";
    Json j = Json::parse(J); comp::Cfg c; comp::comp_of(j.get("comp"), c);
    assert(c.nrel == 8);                    /* a == b, unknown type, from > to, 63.6 -> 64 out of range: dropped */
    const comp::Relation &k = c.rel[0];
    assert(k.type == comp::R_CANON && k.a == 0 && k.b == 2 && k.c == 255 && k.from == 1 && k.to == 5 && k.zone == comp::id_hash("zA"));
    assert(k.delay16 == 12 && k.trans == -3 && k.transform == comp::T_RETRO);
    const comp::Relation &l = c.rel[1];
    assert(l.type == comp::R_CALL && l.source == comp::A_MIRROR && l.gap16 == 4 && l.on_enter == 1 && l.zone == 0 && l.from == 0 && l.to == comp::NCHORD - 1);
    const comp::Relation &h = c.rel[2];
    assert(h.type == comp::R_HOCKET && h.c == 2 && h.split == comp::S_MASK && h.every == 4 && h.mask == (1u | 8u | 32u | 256u | 0x8000u));
    const comp::Relation &e = c.rel[3];
    assert(e.type == comp::R_HETERO && e.a == 1 && e.b == 0 && e.offset_ms == 120 && std::fabs(e.ornament - 0.4f) < 1e-6f);
    const comp::Relation &m = c.rel[4];
    assert(m.type == comp::R_MOTION && m.motion == comp::M_PAR6 && m.strength == 0.5f);
    const comp::Relation &u = c.rel[5];
    assert(u.type == comp::R_UNISON && u.meet == comp::U_NEVER && u.octave == 1);
    const comp::Relation &d = c.rel[6];
    assert(d.type == comp::R_DENSITY && d.c == 2 && d.cap == 32 && d.weight[0] == 4 && d.weight[1] == 0 && d.weight[2] == 4);
    assert(c.rel[7].from == 63 && c.rel[7].to == 63 && c.rel[7].delay16 == 1);
    /* in force: span and zone */
    int out[comp::REL_MAX]; const uint64_t zA = comp::id_hash("zA"), zB = comp::id_hash("zB");
    int n = comp::relations_in_force(c, 3, nullptr, 0, out);
    assert(n == 6 && out[0] == 1 && out[1] == 2 && out[5] == 6);       /* not 0 (zone zA), not 7 (chord 63) */
    n = comp::relations_in_force(c, 3, &zB, 1, out); assert(n == 6);
    n = comp::relations_in_force(c, 3, &zA, 1, out); assert(n == 7 && out[0] == 0);
    n = comp::relations_in_force(c, 0, &zA, 1, out); assert(n == 6 && out[0] == 1);   /* chord 0: before rel 0's span */
    n = comp::relations_in_force(c, 63, nullptr, 0, out); assert(n == 7 && out[6] == 7);
    Json q = Json::parse(R"({"comp":{}})"); comp::Cfg z; comp::comp_of(q.get("comp"), z); assert(z.nrel == 0);
    { Json v = Json::parse(R"({"comp":{"relations":[{"type":"canon","a":5,"b":0},{"type":"canon","a":0,"b":-1},{"type":"hocket","a":0,"b":1,"c":1},{"type":"hocket","a":0,"b":1,"c":7}]}})");
      comp::Cfg y; comp::comp_of(v.get("comp"), y);
      assert(y.nrel == 2 && y.rel[0].c == 255 && y.rel[1].c == 255); }   /* a voice past 0..2 drops the relation; a bad or repeated c is ignored */
    std::string many = R"({"comp":{"relations":[)";
    for (int i = 0; i < 13; i++) many += std::string(i ? "," : "") + R"({"type":"unison","a":0,"b":1,"cap":)" + std::to_string(i + 1) + "}";
    Json mj = Json::parse((many + "]}}").c_str()); comp::Cfg w; comp::comp_of(mj.get("comp"), w);
    assert(w.nrel == comp::REL_MAX);                                    /* the 13th valid entry dropped */
    std::printf("C relations parse: 8 of 12 entries kept in order, fields clamped, span/zone filter, cap %d\n", comp::REL_MAX);
}

/* D minor 7 (key 50), the walk's first chord for >= 2 bars: a 4-note chord-degree motif root-5th-3rd-9th, each a quarter */
static std::string melody(const char *chain, const char *extra = "") {
    char b[2048]; std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":%s,"bars":"repeat"%s})", MOT, chain, extra);
    return b;
}
static void test_melody() {
    /* repeat: D (50) A (57) F (53) and the 9th of a 4-tone chord = degree 4 -> root an octave up (62) on the quarters of bar 0 */
    const Log l = render(melody("[]"), 6);
    std::vector<int> n = bar_notes(l, 1, 0);
    std::printf("C melody repeat bar 0:"); for (int m : n) std::printf(" %d", m); std::printf("\n");
    assert(n.size() == 4 && n[0] == 50 && n[1] == 57 && n[2] == 53 && n[3] == 62);
    /* sequence +1 degree on chord 0: 3rd 7th 5th, root+octave+1 -> F C A (53 60 57) and degree 5 -> 3rd up an octave (65) */
    const Log s = render(melody(R"([{"op":"seq","arg":1,"from":0,"to":3}])"), 6);
    n = bar_notes(s, 1, 0); assert(n.size() == 4 && n[0] == 53 && n[1] == 60 && n[2] == 57 && n[3] == 65);
    /* retrograde: 9th, 3rd, 5th, root in time order */
    const Log r = render(melody(R"([{"op":"retro","from":0,"to":3}])"), 6);
    n = bar_notes(r, 1, 0); assert(n.size() == 4 && n[0] == 62 && n[1] == 53 && n[2] == 57 && n[3] == 50);
    /* fragment 2: the first two notes only */
    const Log f = render(melody(R"([{"op":"fragment","arg":2,"from":0,"to":3}])"), 6);
    n = bar_notes(f, 1, 0); assert(n.size() == 2 && n[0] == 50 && n[1] == 57);
    /* diminish: 8 notes in the bar (the halved motif twice) */
    const Log d = render(melody(R"([{"op":"diminish","from":0,"to":3}])"), 6);
    assert(bar_notes(d, 1, 0).size() == 8);
    /* augment: 2 notes in bar 0, the next 2 in bar 1 */
    const Log a = render(melody(R"([{"op":"augment","from":0,"to":3}])"), 12);
    assert(bar_notes(a, 1, 0).size() == 2 && bar_notes(a, 1, 1).size() == 2);
    /* a triad never silences the 9th (Review Focus 3): every motif note sounds on every bar of a 40 s walk */
    const Log t = render(melody("[]"), 40);
    int bars = 0, short_bars = 0; for (int b = 0; b < 15; b++) { bars++; if (bar_notes(t, 1, b).size() != 4) short_bars++; }
    assert(short_bars == 0);
    /* invert around the root: degrees 0 -2 -1 -4 -> A and C below, folded up into the register: D A C D */
    const Log iv = render(melody(R"([{"op":"invert","from":0,"to":3}])"), 6);
    n = bar_notes(iv, 1, 0); assert(n.size() == 4 && n[0] == 50 && n[1] == 57 && n[2] == 60 && n[3] == 50);
    /* one note per start (a mono voice's second would cut the first): diminishing starts 0 and 1 lands both on 0, the later kept */
    { Json j = Json::parse(R"({"motif":{"notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":1,"len":4}]},"chain":[{"op":"diminish","from":0,"to":0}]})");
      comp::Cfg c; comp::comp_of(&j, c); comp::Phrase ph; comp::motif_step(c, 0, ph);
      assert(ph.n == 2 && ph.note[0].start == 0 && ph.note[0].deg == 2 && ph.note[1].start == 8 && ph.note[1].deg == 2); }
    /* the phrase counts its bars from where its chain step starts (here chord 1, bar 3 of a 20 s walk), not the route's start:
       augmented, its first bar plays the motif's first two notes - G and D of G maj7 (55 62), then B and the root up (59 67) */
    const Log ao = render(melody(R"([{"op":"augment","from":1,"to":1}])"), 20);
    n = bar_notes(ao, 1, 3); std::vector<int> n4 = bar_notes(ao, 1, 4);
    std::printf("C melody augment from bar 3:"); for (int m : n) std::printf(" %d", m); std::printf(" |"); for (int m : n4) std::printf(" %d", m); std::printf("\n");
    assert(n.size() == 2 && n[0] == 55 && n[1] == 62 && n4.size() == 2 && n4[0] == 59 && n4[1] == 67);
    /* the scale basis: D minor (an m7 first chord: aeolian) degrees 0 1 2 6 -> D E F C */
    const Log sc = render(R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"},"motif":{"basis":"scale","notes":[{"deg":0,"start":0},{"deg":1,"start":4},{"deg":2,"start":8},{"deg":6,"start":12}]},"chain":[]})", 6);
    n = bar_notes(sc, 1, 0); assert(n.size() == 4 && n[0] == 50 && n[1] == 52 && n[2] == 53 && n[3] == 60);
    std::printf("C melody: repeat / seq +1 / invert / retro / fragment 2 / diminish (one note a start) / augment from its step's first bar; the scale's degrees too\n");
}
static void test_cadence() {
    /* a one-chord-step cadence on chord 0, full: the last note is the key's tonic D (50 or 62 in range), held to the bar's end */
    const Log c = render(melody(R"([{"op":"cadence","cad":"full","from":0,"to":0}])"), 6);
    std::vector<int> n = bar_notes(c, 1, 0); assert(!n.empty() && (n.back() % 12) == (50 % 12));
    const Log h = render(melody(R"([{"op":"cadence","cad":"half","from":0,"to":0}])"), 6);
    n = bar_notes(h, 1, 0); assert(!n.empty() && (n.back() % 12) == (57 % 12));          /* A, the key's 5th */
    const Log dc = render(melody(R"([{"op":"cadence","cad":"deceptive","from":0,"to":0}])"), 6);
    n = bar_notes(dc, 1, 0); assert(!n.empty() && (n.back() % 12) == (58 % 12));           /* B flat, D minor's 6th */
    /* strength scales the cadence note's velocity by 0.5 + 0.5 x strength; the other notes as they were */
    { const Log s1 = render(melody(R"([{"op":"cadence","cad":"full","others":"hold","strength":1,"from":0,"to":0}])"), 6),
                s5 = render(melody(R"([{"op":"cadence","cad":"full","others":"hold","strength":0.5,"from":0,"to":0}])"), 6);
      std::vector<double> v1, v5; for (size_t i = 0; i < s1.f.size(); i++) if (s1.role[i] == 1 && s1.t[i] < 2.5 - 1e-6) v1.push_back(s1.v[i]);
      for (size_t i = 0; i < s5.f.size(); i++) if (s5.role[i] == 1 && s5.t[i] < 2.5 - 1e-6) v5.push_back(s5.v[i]);
      assert(v1.size() == 4 && v5.size() == 4 && v1[0] < 1 && std::fabs(v5[0] - v1[0]) < 1e-9 && std::fabs(v5[3] / v1[3] - 0.75) < 1e-9); }
    /* strength 0.5: the chance is drawn once a chord - a 3-bar last chord (bars 0-2 of a 20 s walk) stops voice 2 on all its
       bars or none, and some seed of eight goes each way */
    { int stopped = 0, played = 0;
      for (int sd = 1; sd <= 8; sd++) {
          char b[2048]; std::snprintf(b, sizeof b, R"(,"comp":{"seed":%d,"voice":{"role":"melody","lo":50,"hi":81},"sect":{"style":"strum","rate":"4n"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[{"op":"cadence","cad":"full","others":"stop","strength":0.5,"from":0,"to":0}]})", sd, MOT);
          const Log l = render(b, 20); const bool e0 = bar_notes(l, 2, 0).empty(), e1 = bar_notes(l, 2, 1).empty(), e2 = bar_notes(l, 2, 2).empty();
          assert(e0 == e1 && e1 == e2); (e0 ? stopped : played)++;
      }
      assert(stopped > 0 && played > 0); }
    /* others "stop": with voice 2 strumming, voice 2 is silent on the cadence chord's bars */
    char b[2048]; std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"style":"strum","rate":"4n"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[{"op":"cadence","cad":"full","others":"stop","from":0,"to":0}]})", MOT);
    const Log s = render(b, 6);
    assert(bar_notes(s, 2, 0).empty() && !bar_notes(s, 1, 0).empty());
    /* others "thin": voice 2 arpeggiating in sixteenths keeps only its first note of the cadence chord's bar */
    std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"style":"arpeggio","rate":"16n"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[{"op":"cadence","cad":"full","others":"thin","from":0,"to":0}]})", MOT);
    const Log th = render(b, 6);
    assert(bar_notes(th, 2, 0).size() == 1);
    std::printf("C cadence: full ends on the tonic, half on the 5th, deceptive on the 6th; strength 0.5 -> x0.75 velocity, drawn once a chord; others stop / thin\n");
}
static void test_melody_change_at_bar() {
    /* Review Focus 4: the motif replaced mid-bar via fs_piece_set_route takes effect at the next bar line, no note longer than its bar */
    Log l = render(melody("[]"), 6);
    for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1) assert(l.d[i] <= 2.5 + 1e-6);
    /* the chain changed 1 s into bar 0 (repeat -> seq +2): bar 0 keeps the old phrase, bar 1 (still chord 0) plays the new -
       D minor 7's 5th, 9th (root up), 7th, and degree 6 (the 5th up): 57 62 60 69 */
    char a[8192], b[8192]; std::snprintf(a, sizeof a, ROUTE, melody("[]").c_str()); std::snprintf(b, sizeof b, ROUTE, melody(R"([{"op":"seq","arg":2,"from":0,"to":3}])").c_str());
    unsigned sd = 11; Log m;
    fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &sd, lg, &m);
    const int r = fs_piece_add_route(pc, a); const int blocks = (int)(20 * 48000 / 128);
    for (int k = 0; k < blocks; k++) { fs_piece_walk(pc, r, (double)k / blocks, 0); if (k == 375) fs_piece_set_route(pc, r, b); fs_process(pc, 128); }
    fs_destroy(pc);
    std::vector<int> n0 = bar_notes(m, 1, 0), n1 = bar_notes(m, 1, 1);
    assert(n0.size() == 4 && n0[0] == 50 && n0[1] == 57 && n0[2] == 53 && n0[3] == 62);
    assert(n1.size() == 4 && n1[0] == 57 && n1[1] == 62 && n1[2] == 60 && n1[3] == 69);
    std::printf("C melody: no note outlasts its bar; a chain changed mid-bar plays from the next bar line\n");
}

static void test_swell_to_melody() {
    /* Swells at the route, the melody on chord 1 (bars 3-4 of a 20 s walk): the melody plays with the voice's own envelope - its
       level 100 ms into bar 4's note is the level of a voice that was the melody all along */
    /* one note a bar, its tail gone before the next: the meter (~0.1 s) reads the note's own rise */
    static const char *ONE = R"("motif":{"notes":[{"deg":0,"start":0,"len":4}]})";
    { double lv[2];
      for (int run = 0; run < 2; run++) {
          char cm[2048], buf[8192];
          std::snprintf(cm, sizeof cm, run ? R"(,"comp":{"voice":{"role":"off"},"sect":{"style":"swells","rate":"1n","lo":50,"hi":81},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"over":{"chord":{"1":{"sect":{"role":"melody"}}}}})"
                                           : R"(,"comp":{"voice":{"role":"off"},"sect":{"role":"melody","lo":50,"hi":81},"v3":{"role":"off"},"bass":{"mode":"off"},%s})", ONE);
          std::snprintf(buf, sizeof buf, ROUTE, cm);
          unsigned sd = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &sd, nullptr, nullptr);
          const int r = fs_piece_add_route(pc, buf); const int blocks = (int)(20 * 48000 / 128); float rl[3 * 8];
          for (int k = 0; k < blocks; k++) { fs_piece_walk(pc, r, (double)k / blocks, 0); fs_process(pc, 128);
              if (k == (int)(10.1 * 48000 / 128)) { fs_piece_roles(pc, rl, 6); lv[run] = rl[8]; } }
          fs_destroy(pc);
      }
      std::printf("C swells to melody: the Second voice 100 ms into a note %.1f dB, the melody all along %.1f dB\n", lv[1], lv[0]);
      assert(std::fabs(lv[1] - lv[0]) < 3); }
}
static void test_chord_past_16() {
    std::string prog = "["; for (int i = 0; i < 20; i++) prog += std::string(i ? "," : "") + (i % 2 ? R"({"r":5,"q":"maj7"})" : R"({"r":0,"q":"m7"})"); prog += "]";
    char route[4096]; std::snprintf(route, sizeof route, R"({"version":17,"tempo":96,"key":50,"prog":%s,"voice":{"on":true,"synth":"fm"},"sect":{"on":false},"v3":{"on":false},"zones":{"on":false},"bed":{"on":false},"comp":{"voice":{"style":"arpeggio","rate":"16n"},"bass":{"mode":"off"},"over":{"chord":{"18":{"voice":{"style":"pedal","rate":"4n","pedal":"root"}}}}}})", prog.c_str());
    unsigned seed = 11; Log l; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, lg, &l);
    const int r = fs_piece_add_route(pc, route); const double secs = 120; const int blocks = (int)(secs * 48000 / 128);
    for (int b = 0; b < blocks; b++) { fs_piece_walk(pc, r, (double)b / blocks, 0); fs_process(pc, 128); }
    fs_destroy(pc);
    const double c0 = secs * 18 / 20 + 3, c1 = secs * 19 / 20 - 1;     /* well inside chord 18's stretch */
    std::set<int> p; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1 && l.t[i] > c0 && l.t[i] < c1) p.insert((int)std::lround(midi(l.f[i])));
    assert(p.size() == 1);
    std::printf("C chord 19 of 20 obeys its own override (NCHORD %d)\n", comp::NCHORD);
}
/* Task 4: the patch read (comp_gains runs in it; the web reads it on the audio thread, between blocks) of a 64-chord route
   with every chord overridden, on fm / formant / metal: fs_piece_set_route's worst under 1 ms, and an edit of one chord's
   style (what a setter sends) too. A cold read (a new piece: nothing worked out yet) is reported */
static void test_patch_read_cost() {
    double worst = 0, edit = 0, cold = 0;
    auto ms = [](std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    for (const char *sy : { "fm", "formant", "metal" }) {
        std::string prog = "[", over = "{"; for (int i = 0; i < 64; i++) { prog += std::string(i ? "," : "") + R"({"r":0,"q":"m7"})";
            over += std::string(i ? "," : "") + "\"" + std::to_string(i) + "\":{\"voice\":{\"style\":\"" + (i % 3 == 0 ? "strum" : i % 3 == 1 ? "swells" : "arpeggio") + "\"}}"; }
        prog += "]"; over += "}";
        std::string r = std::string(R"({"version":17,"tempo":96,"key":50,"prog":)") + prog + R"(,"voice":{"on":true,"synth":")" + sy + R"("},"sect":{"on":true,"synth":")" + sy + R"("},"v3":{"on":true,"synth":")" + sy + R"("},"zones":{"on":false},"bed":{"on":false},"comp":{"voice":{"style":"arpeggio"},"sect":{"style":"strum"},"v3":{"style":"swells"},"over":{"chord":)" + over + "}}}";
        const double w0 = worst, e0 = edit, c0 = cold; worst = edit = cold = 0;
        fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128);
        auto t0 = std::chrono::steady_clock::now(); const int id = fs_piece_add_route(pc, r.c_str()); cold = std::max(cold, ms(t0));
        for (int rep = 0; rep < 5; rep++) { t0 = std::chrono::steady_clock::now(); fs_piece_set_route(pc, id, r.c_str()); worst = std::max(worst, ms(t0)); }
        for (const char *st : { "pedal", "euclid" }) {   /* chord 7 (was swells) to another style */
            std::string e = r; const size_t at = e.find(R"("7":{"voice":{"style":"swells")"); assert(at != std::string::npos);
            e.replace(at, 30, std::string(R"("7":{"voice":{"style":")") + st + "\"");
            t0 = std::chrono::steady_clock::now(); fs_piece_set_route(pc, id, e.c_str()); edit = std::max(edit, ms(t0)); }
        fs_destroy(pc);
        std::printf("C patch read %-7s: %.2f ms, a chord edited %.2f, cold %.2f\n", sy, worst, edit, cold);
        worst = std::max(worst, w0); edit = std::max(edit, e0); cold = std::max(cold, c0);
    }
    std::printf("C patch read: worst %.2f ms, a chord's style edited %.2f ms (64 chords overridden; fm, formant, metal); cold %.2f ms\n", worst, edit, cold);
    assert(worst < 1.0 && edit < 1.0);
}
/* Task 4: the coarser solve (10 ms steps) keeps every gain within 0.3 dB of the old one's (5 ms), on all 11 synths x the
   levels test's styles and a melody voice, on each voice, at five tempos (Task 5: 180 and 200 too) */
static void test_gain_lengths() {
    double worst = 0; const char *ws = "", *wy = ""; int wq = 0;
    std::vector<std::string> st; for (auto &e : LEVEL_ST) st.push_back(e[1]);
    st.push_back(std::string(R"({"role":"melody","lo":50,"hi":81}, )") + MOT);
    for (const char *sy : LEVEL_SY) for (size_t k = 0; k < st.size(); k++) {
        const bool mel = k + 1 == st.size();
      for (int tempo : { 60, 96, 140, 180, 200 }) {
        char p[4096]; std::snprintf(p, sizeof p, R"({"version":17,"tempo":%d,"key":50,"prog":[{"r":0,"q":"m7"},{"r":5,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}],"bed":{"on":false},"zones":{"on":false},
            "voice":{"on":true,"synth":"%s","harm":%g,"index":%g},"sect":{"on":true,"synth":"%s","harm":%g,"index":%g},"v3":{"on":true,"synth":"%s","harm":%g,"index":%g},
            "comp":{"bass":{"mode":"off"},"voice":%s,"sect":%s,"v3":%s}})", tempo, sy, hd(sy, 0), hd(sy, 1), sy, hd(sy, 0), hd(sy, 1), sy, hd(sy, 0), hd(sy, 1),
            st[k].c_str(), mel ? R"({"style":"strum"})" : st[k].c_str(), mel ? R"({"style":"swells"})" : st[k].c_str());
        float ref[3], now[3]; fs_piece_test_gains(p, 16, 6, 0.005, ref); fs_piece_test_gains(p, 0, 0, 0, now);
        for (int q = 0; q < 3; q++) { const double d = 20 * std::log10(now[q] / ref[q]); if (std::fabs(d) > std::fabs(worst)) { worst = d; ws = mel ? "melody" : LEVEL_ST[k][0]; wy = sy; wq = q; } }
      }
    }
    std::printf("C gains at 10 ms steps: furthest %+.2f dB from the 5 ms solve's (%s %s, voice %d)\n", worst, wy, ws, wq + 1);
    assert(std::fabs(worst) <= 0.3);
}
/* Task 5: core/levels.hpp's measurements (node tools/levels.mjs runs this as "levels-table"). Each synth x style x rate class
   (16n, 8n, 4n, 2n+): the style's level over Free's as test_levels measures it, the levels test's styles in their classes
   (two in a class: their mean) and each style's defaults at the rate where the test has none; plus a melody voice. Prints
   what the table holds now plus that gap: the next table */
static void levels_table() {
    static const struct { int st, c; const char *j; } CELL[] = {
        { comp::ARPEGGIO, 0, LEVEL_ST[5][1] }, { comp::ARPEGGIO, 1, LEVEL_ST[0][1] }, { comp::ARPEGGIO, 2, R"({"style":"arpeggio","rate":"4n"})" }, { comp::ARPEGGIO, 3, R"({"style":"arpeggio","rate":"2n"})" },
        { comp::PEDAL, 0, R"({"style":"pedal","rate":"16n"})" }, { comp::PEDAL, 1, LEVEL_ST[1][1] }, { comp::PEDAL, 2, R"({"style":"pedal","rate":"4n"})" }, { comp::PEDAL, 3, LEVEL_ST[7][1] },
        { comp::STRUM, 0, R"({"style":"strum","rate":"16n"})" }, { comp::STRUM, 1, LEVEL_ST[2][1] }, { comp::STRUM, 2, R"({"style":"strum","rate":"4n"})" }, { comp::STRUM, 3, LEVEL_ST[6][1] },
        { comp::EUCLID, 0, LEVEL_ST[8][1] }, { comp::EUCLID, 1, LEVEL_ST[3][1] }, { comp::EUCLID, 2, R"({"style":"euclid","rate":"4n"})" }, { comp::EUCLID, 3, R"({"style":"euclid","rate":"2n"})" },
        { comp::SWELLS, 0, R"({"style":"swells","rate":"16n"})" }, { comp::SWELLS, 1, LEVEL_ST[4][1] }, { comp::SWELLS, 2, R"({"style":"swells","rate":"4n"})" }, { comp::SWELLS, 3, LEVEL_ST[9][1] }, { comp::SWELLS, 3, LEVEL_ST[10][1] } };
    const std::string mel = std::string(R"({"role":"melody","lo":50,"hi":81}, )") + MOT;
    for (int y = 0; y < 11; y++) {
        const char *sy = LEVEL_SY[y]; const bool pl = !std::strcmp(sy, "pluck");
        const double fr = level_db(sy, "", pl ? 61 : 41), secs = pl ? 31 : 9;
        double sum[comp::NSTYLE][4] = {}; int n[comp::NSTYLE][4] = {};
        for (auto &c : CELL) { sum[c.st][c.c] += level_db(sy, c.j, secs) - fr; n[c.st][c.c]++; }
        for (int s = 1; s < comp::NSTYLE; s++) for (int c = 0; c < 4; c++) std::printf("LEVEL %d %d %d %.3f %.3f\n", y, s, c, sum[s][c] / n[s][c], LEVEL_FIX[y][s][c]);
        std::printf("MELODY %d %.3f %.3f\n", y, level_db(sy, mel.c_str(), secs) - fr, LEVEL_FIX_MELODY[y]);
        std::fflush(stdout);
    }
}

/* Task 5 (Task 4's review): a memoised gain is the gain. A patch read after another, through the public path with the memo
   warm, differing in one thing at a time, has the gains a fresh solve of it has */
static void test_gain_memo() {
    auto patch = [](const char *key, const char *prog, const char *mot, const char *dens, const char *env, const char *tempo, const char *harm) {
        char b[4096]; std::snprintf(b, sizeof b, R"({"version":17,"tempo":%s,"key":%s,"prog":%s,"bed":{"on":false},"zones":{"on":false},
            "voice":{"on":true,"synth":"fm","density":%s},"sect":{"on":true,"synth":"metal","harm":4.1,"index":1.2%s},"v3":{"on":true,"synth":"formant","harm":%s,"index":0.5},
            "comp":{"bass":{"mode":"off"},"voice":{"role":"melody","lo":50,"hi":81},"sect":{"style":"arpeggio","rate":"16n"},"v3":{"style":"swells"},%s}})",
            tempo, key, prog, dens, env, harm, mot);
        return std::string(b);
    };
    const char *P0 = R"([{"r":0,"q":"m7"},{"r":5,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}])", *P1 = R"([{"r":0,"q":"m7"},{"r":3,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}])";
    const char *M1 = R"("motif":{"basis":"chord","notes":[{"deg":0,"start":0,"len":4},{"deg":3,"start":4,"len":8},{"deg":1,"start":12,"len":4}]})";
    const char *E0 = R"(,"env":[0.05,0.3,0.6,1.5])";   /* metal's own envelope set, so the variant changes its numbers, not whether it has one */
    const std::string A = patch("50", P0, MOT, "0.5", E0, "96", "0.5");
    const std::string B[] = { patch("53", P0, MOT, "0.5", E0, "96", "0.5"), patch("50", P1, MOT, "0.5", E0, "96", "0.5"), patch("50", P0, M1, "0.5", E0, "96", "0.5"),
        patch("50", P0, MOT, "0.8", E0, "96", "0.5"), patch("50", P0, MOT, "0.5", R"(,"env":[0.2,0.3,0.6,1.5])", "96", "0.5"), patch("50", P0, MOT, "0.5", E0, "120", "0.5"),
        patch("50", P0, MOT, "0.5", E0, "96", "0.9") };
    const char *what[] = { "key", "progression", "motif", "density", "envelope", "tempo", "harm" };
    fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128);
    const int r = fs_piece_add_route(pc, A.c_str()); float a[3]; fs_piece_test_route_gains(pc, r, a);
    int moved = 0;
    for (int i = 0; i < 7; i++) {
        fs_piece_set_route(pc, r, A.c_str());   /* A's gains in the memo */
        fs_piece_set_route(pc, r, B[i].c_str());
        float got[3], fresh[3]; fs_piece_test_route_gains(pc, r, got); fs_piece_test_gains(B[i].c_str(), 0, 0, 0, fresh);
        for (int q = 0; q < 3; q++) { if (got[q] != fresh[q]) std::printf("C gain memo: %s voice %d %g, fresh %g\n", what[i], q + 1, got[q], fresh[q]); assert(got[q] == fresh[q]); }
        if (got[0] != a[0] || got[1] != a[1] || got[2] != a[2]) moved++; else std::printf("C gain memo: %s left the gains\n", what[i]);
    }
    fs_destroy(pc);
    std::printf("C gain memo: 7 patches read after another (key, progression, motif, density, envelope, tempo, formant harm) have a fresh solve's gains; %d of 7 moved them\n", moved);
    assert(moved == 7);
}

/* Task 6 (Kerem 2026-10-20: voice 3's D3 sat 15 dB under its A3, even alone): the cause was the Freeverb room, not the voice.
   Its eight combs are fixed, so a held pitch met them as one gain and phase - and at some pitches the room's tail cancelled the
   dry note (a 20 dB scatter, D3 -43 dB beside -30 around it, on every voice). The comb delays now move slowly (tone.hpp).
   Checks: (1) voice 3 alone holding each of MIDI 38-74 through fx3 with its echo off (the echo is a comb of its own, +-3 dB of the same kind), am and fm, the level
   averaged over 10 s (the combs now sweep) - the pitches within 6 dB (+-3); (2) the room against the old one (its code below, as it was) on noise bursts: the wet level and the tail's decay
   within 1 dB; (3) no discontinuity in the room's output (the highs, as the onset checks measure them); (4) the room's cost. */
static double held_db(const char *syn, int m, bool delay_off) {
    char p[4096];
    std::snprintf(p, sizeof p, R"({"version":17,"tempo":96,"key":%d,"prog":[{"r":0,"q":"m7"}],"bed":{"on":false},"zones":{"on":false},"morph":{"on":false},
        %s"voice":{"on":false},"sect":{"on":false},"v3":{"on":true,"synth":"%s"},
        "comp":{"bass":{"mode":"off"},"v3":{"style":"swells","rate":"1n","tones":[0],"lo":%d,"hi":%d}}})", m, delay_off ? R"("fx3":{"delayWet":0},)" : "", syn, m - 6, m + 5);
    unsigned seed = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    const int r = fs_piece_add_route(pc, p); double s = 0; long n = 0;
    const double secs = std::getenv("HELD_SECS") ? std::atof(std::getenv("HELD_SECS")) : 10;
    for (int k = 0; k < (int)(secs * 48000 / 128); k++) { fs_piece_walk(pc, r, 0, 0); fs_process(pc, 128);
        if (k * 128 >= 0.9 * 48000) { const float *o = fs_out(pc, 0); for (int i = 0; i < 128; i++) s += (double)o[i] * o[i]; n += 128; } }
    fs_destroy(pc); return 10 * std::log10(s / n + 1e-30);
}
/* the room as it was before: whole-sample combs at fixed delays */
struct OldRoom {
    tone::LowpassComb comb[8]; tone::Biquad ap[2][4]; tone::Ctl room, wet;
    void init(double sr, double room_size, double damp) {
        static const double tune[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 }, apf[4] = { 225, 556, 441, 341 };
        for (int i = 0; i < 8; i++) comb[i].init(sr, tune[i] / 44100.0, room_size, damp);
        for (int q = 0; q < 2; q++) for (int k = 0; k < 4; k++) { ap[q][k].type = tone::ALLPASS; ap[q][k].design(apf[k], 1, sr); }
        room.init(room_size); wet.init(0);
    }
    void process(float *L, float *R, int n, double t_end) {
        room.block(t_end); wet.block(t_end); for (auto &c : comb) c.block(t_end);
        for (int i = 0; i < n; i++) {
            float l = L[i], r = R[i], sl = 0, sr_ = 0, fb = room.at(i, n);
            for (int k = 0; k < 4; k++) sl += comb[k].run(0, l, fb);
            for (int k = 4; k < 8; k++) sr_ += comb[k].run(1, r, fb);
            for (int k = 0; k < 4; k++) { sl = ap[0][k].run(0, sl); sr_ = ap[1][k].run(1, sr_); }
            float d, w; tone::xfade(wet.at(i, n), d, w);
            L[i] = l * d + sl * w; R[i] = r * d + sr_ * w;
        }
    }
};
template <class R> static std::vector<float> room_run(R &rv, const std::vector<float> &in, double wetv) {
    rv.wet.init(wetv);
    std::vector<float> out; float L[128], Rr[128];
    for (size_t b = 0; b + 128 <= in.size(); b += 128) {
        for (int i = 0; i < 128; i++) L[i] = Rr[i] = in[b + i];
        rv.process(L, Rr, 128, (double)(b + 128) / 48000); out.insert(out.end(), L, L + 128);
    }
    return out;
}
static double e_db(const std::vector<float> &x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); i++) s += (double)x[i] * x[i]; return 10 * std::log10(s / (double)(b - a) + 1e-30); }
static void test_room() {
    double hi = -1e9; const char *sy[2] = { "am", "fm" };
    const int step = std::getenv("ROOM_TABLE") ? 1 : 3;   /* every 3rd semitone (13 renders a synth); ROOM_TABLE=1: all 37, with the levels */
    if (!std::getenv("ROOM_FAST"))   /* (a dev knob: skip the long renders) */
    for (int q = 0; q < 2; q++) {
        double a = 1e9, b = -1e9, ea = 1e9, eb = -1e9;
        for (int m = 38; m <= 74; m += step) {
            const double d = held_db(sy[q], m, true), e = held_db(sy[q], m, false);   /* echo off, then fx3 as it comes */
            a = std::min(a, d); b = std::max(b, d); ea = std::min(ea, e); eb = std::max(eb, e);
            if (std::getenv("ROOM_TABLE")) std::printf("%d:%.1f/%.1f ", m, d, e);
        }
        std::printf("C room: voice 3 holding MIDI 38-74 on %s, fx3 with its echo off: %.1f .. %.1f dB (spread %.1f); default fx3, echo on: spread %.1f (not asserted: the echo is a comb of its own)\n", sy[q], a, b, b - a, eb - ea);
        hi = std::max(hi, b - a);
    }
    std::printf("C room: pitch spread %.1f dB at worst (was 19.6)\n", hi);
    assert(hi <= 6.0 || std::getenv("ROOM_FAST"));
    /* noise bursts: 0.4 s of seeded noise, then silence; wet 1 (the room alone) and the route's fx3 (0.26) */
    unsigned sd = 5; std::vector<float> in(48000 * 6, 0.0f); for (int i = 0; i < 48000 * 4 / 10; i++) in[i] = (float)(draw(&sd) * 2 - 1) * 0.5f;
    double dw = 0, dd = 0, hf = 0, hfold = 0;
    for (double decay : { 5.0, 1.8, 3.4 }) for (double wv : { 1.0, 0.26 }) {
        const double rs = std::min(0.95, std::max(0.0, 0.45 + decay * 0.062));
        OldRoom o; o.init(48000, rs, 3000); tone::Freeverb nw; nw.init(48000, rs, 3000);
        const std::vector<float> a = room_run(o, in, wv), b = room_run(nw, in, wv);
        const double wl = e_db(b, 0, 48000 * 2) - e_db(a, 0, 48000 * 2);
        const double tail = (e_db(b, 21600, 33600) - e_db(b, 33600, 45600)) - (e_db(a, 21600, 33600) - e_db(a, 33600, 45600));   /* the fall per 0.25 s, from 0.45 s in */
        dw = std::max(dw, std::fabs(wl)); dd = std::max(dd, std::fabs(tail));
        /* discontinuity: a 220 Hz sine through the room, against the old room */
        std::vector<float> sn(48000 * 3); for (size_t i = 0; i < sn.size(); i++) sn[i] = 0.3f * (float)std::sin(2 * 3.141592653589793 * 220 * i / 48000);
        OldRoom o2; o2.init(48000, rs, 3000); tone::Freeverb n2; n2.init(48000, rs, 3000);
        const std::vector<float> a2 = room_run(o2, sn, wv), b2 = room_run(n2, sn, wv);
        /* the largest second difference over the output's rms: a click is a spike of it, a smooth tone a steady ~(2 pi f / sr)^2 */
        auto high = [&](const std::vector<float> &x) { double m = 0, e = 0; for (size_t i = 48001; i < x.size(); i++) { m = std::max(m, (double)std::fabs(x[i] - 2 * x[i - 1] + x[i - 2])); e += (double)x[i] * x[i]; } return m / std::sqrt(e / (x.size() - 48001) + 1e-30); };
        hf = std::max(hf, high(b2)); hfold = std::max(hfold, high(a2));
        std::printf("C room: decay %.1f wet %.2f - level %+.2f dB, tail fall %+.2f dB per 0.25 s against the old room\n", decay, wv, wl, tail);
    }
    std::printf("C room: wet level within %.2f dB, tail decay within %.2f dB per 0.25 s; largest second difference over rms %.5f (old %.5f)\n", dw, dd, hf, hfold);
    assert(dw <= 1.0 && dd <= 1.0 && hf <= hfold * 3);
    /* cost: 128-frame blocks, 7500 of them, the best of 3 */
    auto cost = [&](auto &rv) { double best = 1e9; float L[128], R[128];
        for (int rep = 0; rep < 3; rep++) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < 7500; b++) { for (int i = 0; i < 128; i++) L[i] = R[i] = 0.2f * (float)std::sin(i * 0.3); rv.process(L, R, 128, (b + 1) * 128.0 / 48000); }
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 7500); }
        return best; };
    OldRoom oc; oc.init(48000, 0.76, 3000); oc.wet.init(0.26); tone::Freeverb nc; nc.init(48000, 0.76, 3000); nc.wet.init(0.26);
    const double co = cost(oc), cn = cost(nc);
    std::printf("C room: cost per 128-frame block, old %.4f ms, new %.4f ms (x%.2f)\n", co, cn, cn / co);
    assert(cn < 0.05);   /* a block is 2.67 ms; three rooms run per block */
}


/* composition 2, "bars": every voice's bar-0 pattern repeating (today's sound) is "repeat", bit for bit: a hash of every note's
   (role, pitch, time, length, velocity) over two routes of 30 s that run all the styles and a melody with a cadence */
static unsigned long long log_hash(const Log &l) {
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&](double x) { unsigned long long b; std::memcpy(&b, &x, 8); for (int i = 0; i < 8; i++) { h ^= (b >> (8 * i)) & 255; h *= 1099511628211ull; } };
    for (size_t i = 0; i < l.f.size(); i++) { mix(l.role[i]); mix(l.f[i]); mix(l.t[i]); mix(l.d[i]); mix(l.v[i]); }
    return h;
}
static unsigned long long repeat_hash(const char *bars, size_t *nn) {
    char a[2048], b[2048];
    std::snprintf(a, sizeof a, R"(,"comp":{"seed":3,"voice":{"style":"arpeggio","rate":"16n","lo":50,"hi":74,"variation":0.4,"grid":[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]},"sect":{"style":"strum","rate":"2n","tones":[0,1,2,3],"spread":40,"dir":"alt","lo":50,"hi":74},"v3":{"style":"euclid","rate":"16n","pulses":5,"steps":8,"rot":1,"grid":[2,4,1],"lo":55,"hi":80}%s})", bars);
    std::snprintf(b, sizeof b, R"(,"comp":{"seed":5,"voice":{"role":"melody","lo":50,"hi":81},"sect":{"style":"swells","rate":"1n","tones":[0,2,4],"len":1},"v3":{"style":"pedal","rate":"4n"},%s,"chain":[{"op":"seq","arg":1,"from":0,"to":0},{"op":"retro","from":1,"to":1},{"op":"cadence","cad":"full","from":2,"to":3}]%s})", MOT, bars);
    const Log x = render(a, 30), y = render(b, 30);
    *nn = x.f.size() + y.f.size();
    return log_hash(x) * 31 + log_hash(y);
}
static void test_repeat_hash() {
    size_t n; const unsigned long long h = repeat_hash(R"(,"bars":"repeat")", &n);
    std::printf("C bars repeat: %zu notes, hash %llu\n", n, h);
    assert(h == 7233712993534382426ull);   /* recorded on the build before "bars" existed */
}

/* composition 2, "bars": develop (the default) - a chord that lasts bars does not play its one bar again and again */
typedef std::vector<std::pair<int, int>> Seq;   /* a bar's notes as (sixteenth in the bar, pitch) */
static Seq bar_seq(const Log &l, int role, int b) {
    Seq o; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == role && l.t[i] >= b * 2.5 - 1e-6 && l.t[i] < (b + 1) * 2.5 - 1e-6)
        o.push_back({ (int)std::lround((l.t[i] - b * 2.5) / 0.15625), (int)std::lround(midi(l.f[i])) });
    return o;
}
static void test_develop() {
    /* 40 s over 4 chords: chord 0 lasts 4 bars (0..3). Melody: four distinct bars, every one with a note */
    char b[2048];
    std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[]})", MOT);
    const Log m = render(b, 40);
    int distinct = 0; Seq mb[4];
    for (int i = 0; i < 4; i++) { mb[i] = bar_seq(m, 1, i); assert(!mb[i].empty()); }
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) { assert(mb[i] != mb[j]); distinct++; }
    { const Log r = render(melody("[]"), 40); assert(bar_seq(r, 1, 0) == bar_seq(r, 1, 1) && bar_seq(r, 1, 1) == bar_seq(r, 1, 3)); }   /* repeat: as it was */
    /* arpeggio: odd bars reversed, bar 2 rotated by 2 columns */
    const Log a = render(R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n","lo":50,"hi":74,"grid":[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"}})", 40);
    const Seq a0 = bar_seq(a, 1, 0), a1 = bar_seq(a, 1, 1), a2 = bar_seq(a, 1, 2);
    assert(a0.size() == 16 && a1.size() == 16 && a0 != a1 && a2 != a0 && a2 != a1);
    for (int i = 0; i < 16; i++) assert(a1[i].second == a0[15 - i].second && a2[i].second == a0[(i + 2) % 16].second);
    /* euclid: the rotation advances a step a bar */
    const Log e = render(R"(,"comp":{"voice":{"style":"euclid","rate":"8n","pulses":3,"steps":8,"rot":0,"grid":[0],"lo":50,"hi":74},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"}})", 40);
    Seq e0 = bar_seq(e, 1, 0), e1 = bar_seq(e, 1, 1);
    assert(e0.size() == 3 && e1.size() == 3 && e0 != e1);
    /* strum: the direction alternates a bar, and a 4-tone chord drops its top tone on odd bars */
    const Log s = render(R"(,"comp":{"voice":{"role":"off"},"sect":{"role":"off"},"v3":{"style":"strum","rate":"1n","tones":[0,1,2,3],"dir":"up","spread":40,"lo":50,"hi":74},"bass":{"mode":"off"}})", 40);
    const Seq s0 = bar_seq(s, 3, 0), s1 = bar_seq(s, 3, 1), s2 = bar_seq(s, 3, 2);
    assert(s0.size() == 4 && s1.size() == 3 && s2.size() == 4 && s0[0].second < s0[1].second && s1[0].second > s1[1].second && s2[0].second < s2[1].second);
    /* swells: the voicing rotates, a tone fewer in turn */
    const Log w = render(R"(,"comp":{"voice":{"role":"off"},"sect":{"style":"swells","rate":"1n","tones":[0,2,4],"len":1},"v3":{"role":"off"},"bass":{"mode":"off"}})", 40);
    const Seq w0 = bar_seq(w, 2, 0), w1 = bar_seq(w, 2, 1), w2 = bar_seq(w, 2, 2);
    assert(w0.size() == 3 && w1.size() == 2 && w2.size() == 2 && w0 != w1 && w1 != w2);
    std::printf("C develop: melody bars 0-3 pairwise distinct (%d pairs), arpeggio bar 1 reversed and bar 2 rotated, euclid rotates, strum flips (4 -> 3 -> 4 tones), swells rotate\n", distinct);
    /* the draw is the seed's: same seed, same notes; another seed, another variant somewhere */
    auto mel_seed = [&](int sd) { char c[2048]; std::snprintf(c, sizeof c, R"(,"comp":{"seed":%d,"voice":{"role":"melody","lo":50,"hi":81},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[]})", sd, MOT); return render(c, 40); };
    const Log m1 = mel_seed(1); int differs = 0; assert(same(m1, mel_seed(1)));
    for (int sd = 2; sd <= 7; sd++) if (!same(m1, mel_seed(sd))) differs++;
    assert(differs > 0);
    /* a cadence lands on its chord's first bar as it did, and the chord's later bars do not restart the motif: they rest */
    std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[{"op":"cadence","cad":"full","from":0,"to":0}]})", MOT);
    const Log cd = render(b, 40), cr = render(melody(R"([{"op":"cadence","cad":"full","from":0,"to":0}])"), 40);
    assert(bar_seq(cd, 1, 0) == bar_seq(cr, 1, 0) && !bar_seq(cd, 1, 0).empty());
    for (int i = 1; i < 4; i++) assert(bar_seq(cd, 1, i).empty());
    std::printf("C develop: %d of 6 other seeds change the melody; the cadence lands on bar 0 and its chord's later bars rest\n", differs);
}

/* composition 3: canon and heterophony replay a voice's own notes */
static const char *MELV = R"({"role":"melody","lo":50,"hi":81})";
static const char *ARPV3 = R"({"style":"arpeggio","rate":"16n","lo":55,"hi":70,"grid":[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]})";
static std::string rel_comp(const char *voice, const char *v3, const char *motif, const char *rels, const char *sect = R"({"role":"off"})") {
    char b[4096]; std::snprintf(b, sizeof b, R"(,"comp":{"voice":%s,"sect":%s,"v3":%s,"bass":{"mode":"off"},%s,"chain":[],"bars":"repeat","relations":%s})", voice, sect, v3, motif, rels);
    return b;
}
static std::string rel_route(const char *rels, const char *v3 = ARPV3) { return rel_comp(MELV, v3, MOT, rels); }
static int tick_of(double t) { return (int)std::lround(t / 0.15625); }
/* chord 0's length in sixteenths on the 40 s walk, measured: an arpeggio on the chord's root changes pitch class with the chord */
static int span_ticks() {
    static int span = 0; if (span) return span;
    const Log p = render(R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n","lo":50,"hi":74,"grid":[0]},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"}})", 40);
    const int pc = (int)std::lround(midi(p.f[0])) % 12;
    for (size_t i = 0; i < p.f.size(); i++) if ((int)std::lround(midi(p.f[i])) % 12 != pc) { span = tick_of(p.t[i]); return span; }
    assert(false); return 0;
}
static int dm(int d, int lo, int hi) {   /* chord 0 (D m7: 50 53 57 60) degree d fitted to [lo, hi] */
    static const int T[4] = { 50, 53, 57, 60 }; int m = T[((d % 4) + 4) % 4] + 12 * (int)std::floor(d / 4.0);
    while (m < lo) m += 12; while (m > hi && m - 12 >= lo) m -= 12; return m;
}
static int chord0_deg(int m) { static const int T[4] = { 50, 53, 57, 60 }; for (int d = 0; d < 4; d++) if ((m - T[d]) % 12 == 0) return d + 4 * ((m - T[d]) / 12); assert(false); return 0; }
static int pitch(const Log &l, size_t i) { return (int)std::lround(midi(l.f[i])); }
/* the role-`r` notes of l at tick `k` (+-0 ticks) */
static bool has_note(const Log &l, int role, int k, int m = -1) {
    for (size_t j = 0; j < l.f.size(); j++) if (l.role[j] == role && tick_of(l.t[j]) == k && (m < 0 || pitch(l, j) == m)) return true;
    return false;
}
static void test_canon() {
    const int SPAN = span_ticks();
    /* chord 0 (D m7: 50 53 57 60) is the span; the leader plays root-5th-3rd-9th (50 57 53 62) = degrees 0 2 1 4 */
    const Log l = render(rel_route(R"([{"type":"canon","a":0,"b":2,"delay":8,"trans":2,"from":0,"to":0}])"), 40);
    int lead = 0, fol_in = 0, fol_after = 0;
    for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1 && tick_of(l.t[i]) < SPAN) {
        const int k = tick_of(l.t[i]), d = chord0_deg(pitch(l, i));
        if (k + 8 < SPAN) assert(has_note(l, 3, k + 8, dm(d + 2, 55, 70)));          /* two chord degrees up, exactly */
        else {                                                                      /* the tail sounds on the chord now playing (G maj7: G B D F#) */
            bool ok = false; for (size_t j = 0; j < l.f.size(); j++) if (l.role[j] == 3 && tick_of(l.t[j]) == k + 8) { const int pc = pitch(l, j) % 12; ok = pc == 7 || pc == 11 || pc == 2 || pc == 6; }
            assert(ok);
        }
        lead++;
    }
    for (size_t j = 0; j < l.f.size(); j++) if (l.role[j] == 3) {
        const int k = tick_of(l.t[j]);
        if (k < SPAN + 8) { fol_in++; bool from_lead = false;   /* every note in the span (and its delay's tail) is a replay of a leader note */
            for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1 && tick_of(l.t[i]) == k - 8 && tick_of(l.t[i]) < SPAN) from_lead = true;
            assert(from_lead); }
        else fol_after++;
    }
    assert(lead >= 12 && fol_in == lead && fol_after > 30);   /* outside the span the arpeggio plays again */
    /* the follower's level is its own style's: replays and the arpeggio within 2 dB */
    { double a = 0, b = 0; int na = 0, nb = 0;
      for (size_t j = 0; j < l.f.size(); j++) if (l.role[j] == 3) { const int k = tick_of(l.t[j]); if (k < SPAN) { a += std::log10(l.v[j]); na++; } else if (k > SPAN + 8) { b += std::log10(l.v[j]); nb++; } }
      const double db = 20 * (a / na - b / nb); std::printf("C canon level: replays %.2f dB from the follower's own arpeggio\n", db); assert(std::fabs(db) < 2); }
    /* invert: about the leader's first note of the bar (degree 0): degrees 0 -2 -1 -4 -> 62 57 60 62 in 55..70 */
    { const Log x = render(rel_route(R"([{"type":"canon","a":0,"b":2,"delay":8,"transform":"invert","from":0,"to":0}])"), 40);
      const int want[4] = { 62, 57, 60, 62 }; int n = 0;
      for (size_t j = 0; j < x.f.size(); j++) if (x.role[j] == 3 && tick_of(x.t[j]) < SPAN) { const int u = tick_of(x.t[j]) - 8; assert(u >= 0 && u % 4 == 0 && pitch(x, j) == want[(u % 16) / 4]); n++; }
      assert(n >= 8); }
    /* retro: the previous bar backwards, 8 sixteenths late: the leader's notes 50 57 53 62 fitted to 55..70 are 62 57 65 62, start s -> 12 - s */
    { const Log x = render(rel_route(R"([{"type":"canon","a":0,"b":2,"delay":8,"transform":"retro","from":0,"to":0}])"), 40);
      const int want[4] = { 62, 57, 65, 62 }; int n = 0;
      for (size_t j = 0; j < x.f.size(); j++) if (x.role[j] == 3 && tick_of(x.t[j]) < SPAN) {
          const int u = tick_of(x.t[j]) - 8; assert(u >= 16 && u % 4 == 0 && pitch(x, j) == want[(12 - u % 16) / 4]); n++; }
      assert(n >= 8);
      /* nothing from the leader's bars outside the span: a canon over chord 1 only plays nothing in its first bar */
      const Log y = render(rel_route(R"([{"type":"canon","a":0,"b":2,"delay":8,"transform":"retro","from":1,"to":1}])"), 40);
      for (size_t j = 0; j < y.f.size(); j++) if (y.role[j] == 3 && tick_of(y.t[j]) >= SPAN && tick_of(y.t[j]) < SPAN + 24) assert(false); }
    /* a ninth and an octave leap keep their contour: trans 4 on a 4-tone chord is an octave, so the follower is the leader + 12 */
    { const char *mot = R"("motif":{"basis":"chord","notes":[{"deg":0,"start":0,"len":4},{"deg":4,"start":4,"len":4},{"deg":7,"start":8,"len":4},{"deg":2,"start":12,"len":4}]})";
      const Log x = render(rel_comp(MELV, R"({"role":"off","lo":40,"hi":100})", mot, R"([{"type":"canon","a":0,"b":2,"delay":8,"trans":4,"from":0,"to":0}])"), 40); int n = 0;
      for (size_t i = 0; i < x.f.size(); i++) if (x.role[i] == 1 && tick_of(x.t[i]) + 8 < SPAN) { assert(has_note(x, 3, tick_of(x.t[i]) + 8, pitch(x, i) + 12)); n++; }
      assert(n >= 12); }
    /* a zone-limited canon is silent outside its zone: never inside, the route is the route without it; inside from 20 s, the first 20 s are unchanged */
    { const char *rz = R"([{"type":"canon","a":0,"b":2,"delay":8,"zone":"zA"}])";
      const Log none = render(rel_route("[]"), 40);
      assert(same(render(rel_route(rz), 40), none));
      const Log in = render(rel_route(rz), 40, "zA", nullptr, 0.5);
      std::vector<std::pair<double, int>> p1, p2; for (size_t i = 0; i < in.f.size(); i++) if (in.t[i] < 19.9) p1.push_back({ in.t[i], in.role[i] * 1000 + pitch(in, i) });
      for (size_t i = 0; i < none.f.size(); i++) if (none.t[i] < 19.9) p2.push_back({ none.t[i], none.role[i] * 1000 + pitch(none, i) });
      assert(p1 == p2 && !same(in, none)); }
    std::printf("C canon: %d leader notes in span %d, %d follower notes = each +8 sixteenths and two degrees up exactly (tail on the new chord); invert, retro and a ninth with an octave leap exact; zone-limited; %d arpeggio notes after\n", lead, SPAN, fol_in, fol_after);
}
static void test_canon_no_feedback() {
    const int SPAN = span_ticks();
    const Log l = render(rel_route(R"([{"type":"canon","a":0,"b":2,"delay":8,"from":0,"to":0},{"type":"canon","a":2,"b":0,"delay":8,"from":0,"to":0}])"), 40);
    std::map<std::pair<int, int>, int> cnt; int v3 = 0;
    for (size_t i = 0; i < l.f.size(); i++) if (tick_of(l.t[i]) < SPAN) { cnt[{ tick_of(l.t[i]), pitch(l, i) }]++; if (l.role[i] == 3) v3++; }
    int worst = 0; for (auto &c : cnt) worst = std::max(worst, c.second);
    assert(v3 > 0 && worst == 1);
    /* a chain a -> b, b -> c: c follows b as b plays it, a quarter later again (both register 50..81, no transposition) */
    const Log ch = render(rel_comp(MELV, R"({"role":"off","lo":50,"hi":81})", MOT, R"([{"type":"canon","a":0,"b":1,"delay":4,"from":0,"to":0},{"type":"canon","a":1,"b":2,"delay":4,"from":0,"to":0}])", R"({"role":"off","lo":50,"hi":81})"), 40);
    int n = 0; for (size_t i = 0; i < ch.f.size(); i++) if (ch.role[i] == 1 && tick_of(ch.t[i]) + 8 < SPAN) {
        assert(has_note(ch, 2, tick_of(ch.t[i]) + 4, pitch(ch, i)) && has_note(ch, 3, tick_of(ch.t[i]) + 8, pitch(ch, i))); n++; }
    assert(n >= 12);
    std::printf("C canon no feedback: a->b and b->a, %d follower notes, no (tick, pitch) more than %d time; a chain a->b->c reaches c, %d notes\n", v3, worst, n);
}
/* a strum's notes keep their roll through a canon, on a follower that is one voice; 33 sixteenths of a 4-tone strum fit the ring */
static void test_canon_strum() {
    const int SPAN = span_ticks();
    for (int delay : { 8, 32 }) {
        char r[256]; std::snprintf(r, sizeof r, R"([{"type":"canon","a":0,"b":2,"delay":%d,"from":0,"to":0}])", delay);
        const char *sv = delay == 8 ? R"({"style":"strum","rate":"1n","tones":[0,1,2,3],"dir":"up","spread":40,"lo":50,"hi":74})" : R"({"style":"strum","rate":"16n","tones":[0,1,2,3],"dir":"up","spread":5,"lo":50,"hi":74})";
        const Log l = render(rel_comp(sv, R"({"role":"off","lo":50,"hi":74})", MOT, r), 40); int n = 0;
        for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1 && tick_of(l.t[i]) + delay < SPAN) {
            bool found = false; for (size_t j = 0; j < l.f.size(); j++) if (l.role[j] == 3 && std::fabs(l.t[j] - (l.t[i] + delay * 0.15625)) < 1e-6 && pitch(l, j) == pitch(l, i)) found = true;
            assert(found); n++; }
        assert(n >= 12);
        std::printf("C canon strum: delay %d, %d strum notes each replayed at its roll offset\n", delay, n);
    }
}
static void test_hetero() {
    const int SPAN = span_ticks();
    const char *v3 = R"({"role":"off"})";
    const Log l0 = render(rel_route(R"([{"type":"hetero","a":0,"b":2,"offset":30,"ornament":0,"from":0,"to":0}])", v3), 40);
    int na = 0, nb = 0;
    for (size_t i = 0; i < l0.f.size(); i++) if (l0.role[i] == 1 && tick_of(l0.t[i]) < SPAN) {
        na++; bool found = false;
        for (size_t j = 0; j < l0.f.size(); j++) if (l0.role[j] == 3 && std::fabs(l0.t[j] - (l0.t[i] + 0.03)) <= 1.0 / 48000 + 1e-9 && l0.f[j] == l0.f[i]) found = true;
        assert(found);
    }
    for (size_t j = 0; j < l0.f.size(); j++) if (l0.role[j] == 3) nb++;
    assert(na >= 12 && nb == na);                         /* ornament 0: no grace notes */
    const std::string o1 = rel_route(R"([{"type":"hetero","a":0,"b":2,"offset":30,"ornament":1,"from":0,"to":0}])", v3);
    const Log l1 = render(o1, 40);
    int nb1 = 0, graces = 0; for (int r : l1.role) if (r == 3) nb1++;
    assert(nb1 == 2 * na);                                /* one grace per a-note */
    assert(same(l1, render(o1, 40)));
    /* the grace ends where its double starts, so a one-voice synth is not cut; the double has moved to t + grace (an offset of 30 ms is shorter) */
    for (size_t j = 0; j < l1.f.size(); j++) if (l1.role[j] == 3 && l1.d[j] < 0.1) {
        graces++; bool found = false;
        for (size_t u = 0; u < l1.f.size(); u++) if (l1.role[u] == 3 && l1.d[u] >= 0.1 && std::fabs(l1.t[u] - (l1.t[j] + l1.d[j])) < 1e-9) found = true;
        assert(found && std::fabs(l1.d[j] - 0.078125) < 1e-9);
    }
    assert(graces == na);
    const Log l5 = render(rel_route(R"([{"type":"hetero","a":0,"b":2,"offset":30,"ornament":0.5,"from":0,"to":0}])", v3), 40);
    int nb5 = 0; for (int r : l5.role) if (r == 3) nb5++;
    assert(nb5 > na && nb5 < 2 * na);
    std::printf("C hetero: %d notes doubled at +30 ms; ornament 0 -> %d, 1 -> %d (%d graces, each ending where its double starts), 0.5 -> %d notes on b\n", na, nb, nb1, graces, nb5);
}
/* a route swap clears the ring and the spans: the second route's follower replays nothing of the first's leader */
static void test_canon_swap() {
    char a[8192], b[8192];
    std::snprintf(a, sizeof a, ROUTE, rel_route(R"([{"type":"canon","a":0,"b":2,"delay":8}])").c_str());
    std::snprintf(b, sizeof b, ROUTE, rel_comp(R"({"role":"off"})", ARPV3, MOT, R"([{"type":"canon","a":0,"b":2,"delay":8}])").c_str());
    unsigned s = 11; Log l;
    fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &s, lg, &l);
    const int r1 = fs_piece_add_route(pc, a), r2 = fs_piece_add_route(pc, b);
    for (int k = 0; k < (int)(24 * 48000 / 128); k++) { fs_piece_walk(pc, k < 10 * 48000 / 128 ? r1 : r2, 0.02 + k / 90000.0, 0); fs_process(pc, 128); }
    fs_destroy(pc);
    double last_lead = 0, last_fol = 0; std::set<std::pair<double, int>> seen;
    for (size_t i = 0; i < l.f.size(); i++) { if (l.role[i] == 1) last_lead = std::max(last_lead, l.t[i]); if (l.role[i] == 3) { last_fol = std::max(last_fol, l.t[i]); assert(seen.insert({ l.t[i], pitch(l, i) }).second); } }
    std::printf("C canon swap: last leader note %.2f s, last follower note %.2f s\n", last_lead, last_fol);
    assert(last_lead > 1 && last_fol <= last_lead + 8 * 0.15625 + 1e-6);
}

/* composition 3: hocket and call & response gate a line between voices */
static const char *ARPV1 = R"({"style":"arpeggio","rate":"16n","lo":50,"hi":74,"grid":[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]})";
typedef std::pair<int, int> TP;   /* (tick, pitch) */
static std::vector<TP> notes_of(const Log &l, int role, int to) { std::vector<TP> o; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == role && tick_of(l.t[i]) < to) o.push_back({ tick_of(l.t[i]), pitch(l, i) }); return o; }
static std::string hk_route(const char *rels) { return rel_comp(ARPV1, R"({"role":"off"})", MOT, rels); }
static void test_hocket() {
    const int SPAN = span_ticks();
    const std::vector<TP> line = notes_of(render(hk_route("[]"), 40), 1, SPAN);
    const Log l = render(hk_route(R"([{"type":"hocket","a":0,"b":2,"split":"alt","from":0,"to":0}])"), 40);
    const std::vector<TP> a = notes_of(l, 1, SPAN), b = notes_of(l, 3, SPAN);
    assert(line.size() >= 40 && a.size() + b.size() == line.size());
    std::vector<TP> u = a; u.insert(u.end(), b.begin(), b.end()); std::sort(u.begin(), u.end());
    assert(u == line);                                                       /* the union is the line, no note on both */
    for (size_t i = 0; i < line.size(); i++) {                               /* in turn: even notes on a, odd on b */
        const std::vector<TP> &who = i % 2 ? b : a; assert(std::find(who.begin(), who.end(), line[i]) != who.end()); }
    /* Review Focus 3: no bar of the line that had notes goes silent, whatever the split */
    const char *splits[] = { R"("split":"alt")", R"("split":"every","every":3)", R"("split":"mask","mask":"................")", R"("split":"mask","mask":"xxxxxxxxxxxxxxxx")" };
    int bars = 0;
    for (const char *sp : splits) {
        char r[256]; std::snprintf(r, sizeof r, R"([{"type":"hocket","a":0,"b":2,%s,"from":0,"to":0}])", sp);
        const Log x = render(hk_route(r), 40);
        for (int bar = 0; bar < SPAN / 16; bar++) { int nl = 0, nx = 0;
            for (const TP &t : line) if (t.first / 16 == bar) nl++;
            for (int role : { 1, 2, 3 }) for (const TP &t : notes_of(x, role, SPAN)) if (t.first / 16 == bar) nx++;
            assert(nl > 0 && nx == nl); bars++; }
    }
    std::printf("C hocket: alt over %zu line notes, %zu on a and %zu on b in turn, union = the line; %d bars x 4 splits never silent\n", line.size(), a.size(), b.size(), bars);
}
static void test_hocket_mask() {
    const int SPAN = span_ticks();
    const std::vector<TP> line = notes_of(render(hk_route("[]"), 40), 1, SPAN);
    const Log l = render(hk_route(R"([{"type":"hocket","a":0,"b":2,"split":"mask","mask":"x..x.xx.x...xx..","from":0,"to":0}])"), 40);
    const char *m = "x..x.xx.x...xx.."; int na = 0, nb = 0;
    for (const TP &t : line) { const bool on_a = m[t.first % 16] == 'x';
        assert(has_note(l, on_a ? 1 : 3, t.first, t.second) && !has_note(l, on_a ? 3 : 1, t.first)); on_a ? na++ : nb++; }
    /* every 2 over three members: a a b b c c; the third voice (sect) is silent of its own */
    const Log e = render(rel_comp(ARPV1, R"({"role":"off"})", MOT, R"([{"type":"hocket","a":0,"b":2,"c":1,"split":"every","every":2,"from":0,"to":0}])"), 40);
    for (size_t i = 0; i < line.size(); i++) { const int role = ((int)(i / 2) % 3) == 0 ? 1 : ((int)(i / 2) % 3) == 1 ? 3 : 2; assert(has_note(e, role, line[i].first, line[i].second)); }
    std::printf("C hocket mask: x..x.xx.x...xx.. puts %d notes on a and %d on b exactly; every 2 over three voices cycles a a b b c c\n", na, nb);
}
static const char *MOT2 = R"("motif":{"basis":"chord","bars":2,"notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4},{"deg":1,"start":8,"len":4},{"deg":4,"start":12,"len":4},{"deg":4,"start":24,"len":4},{"deg":3,"start":28,"len":2}]})";
static const char *MOT1 = R"("motif":{"basis":"chord","bars":2,"notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4},{"deg":1,"start":8,"len":4},{"deg":4,"start":12,"len":4}]})";
static void test_call() {
    const int SPAN = span_ticks();
    /* own: a plays bar 1 of a 2-bar phrase; b's arpeggio fills the empty bar 2 (ticks 18..31: 2 sixteenths after a's last note ends), never over a */
    const Log l = render(rel_comp(MELV, ARPV3, MOT1, R"([{"type":"call","a":0,"b":2,"gap":2,"from":0,"to":0}])"), 40);
    int nb = 0;
    for (size_t j = 0; j < l.f.size(); j++) if (l.role[j] == 3 && tick_of(l.t[j]) < SPAN) {
        nb++; const int k = tick_of(l.t[j]); assert(k % 32 >= 18);
        for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1) assert(l.t[j] + l.d[j] <= l.t[i] + 1e-9 || l.t[i] + l.d[i] <= l.t[j] + 1e-9); }
    for (int k = 18; k < 32; k++) assert(has_note(l, 3, k));
    assert(nb == (SPAN / 32) * 14 && !notes_of(l, 1, 40).empty());
    /* half: b plays the motif's second half (degrees 4 and 3, fitted to its register), starting where the answer does (tick 18) */
    const Log h = render(rel_comp(MELV, ARPV3, MOT2, R"([{"type":"call","a":0,"b":2,"source":"half","gap":2,"from":0,"to":0}])"), 40);
    const std::vector<TP> hb = notes_of(h, 3, SPAN);
    for (int c = 0; c < 2; c++) { assert(has_note(h, 3, 32 * c + 18, dm(4, 55, 70)) && has_note(h, 3, 32 * c + 22, dm(3, 55, 70))); }
    assert((int)hb.size() == 2 * (SPAN / 32));
    /* mirror: a's phrase inverted about its first note: degrees 0 -2 -1 -4 */
    const Log m = render(rel_comp(MELV, ARPV3, MOT1, R"([{"type":"call","a":0,"b":2,"source":"mirror","gap":2,"from":0,"to":0}])"), 40);
    const int want[4] = { dm(0, 55, 70), dm(-2, 55, 70), dm(-1, 55, 70), dm(-4, 55, 70) };
    for (int c = 0; c < 2; c++) for (int i = 0; i < 4; i++) assert(has_note(m, 3, 32 * c + 18 + 4 * i, want[i]));
    assert((int)notes_of(m, 3, SPAN).size() == 4 * (SPAN / 32));
    /* trigger enter: nothing before the zone is entered (20 s = bar 8), the answer in the 2 bars after, nothing later */
    const Log z = render(rel_comp(MELV, R"({"role":"off"})", MOT2, R"([{"type":"call","a":0,"b":2,"source":"half","gap":2,"zone":"zA","trigger":"enter"}])"), 40, "zA", "zA", 0.5);
    const std::vector<TP> zb = notes_of(z, 3, 1000);
    assert(!zb.empty()); for (const TP &t : zb) assert(t.first >= 128 && t.first < 160);
    assert(has_note(z, 3, 146) && has_note(z, 3, 150));
    std::printf("C call: own fills bar 2 (%d notes, none over a), half plays degrees 4 3 at the answer's start, mirror inverts 0 -2 -1 -4, enter answers only in bars 8-9 (%zu notes)\n", nb, zb.size());
}

int main(int argc, char **argv) {
    if (argc > 1 && !std::strcmp(argv[1], "final")) { test_empty_motif(); test_melody_gain_chain(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "room")) { test_room(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "levels")) { test_levels(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "levels-table")) { levels_table(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "onsets")) { test_onsets(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "cost")) { test_cost(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "read")) { test_gain_memo(); test_gain_lengths(); test_patch_read_cost(); return 0; }
    test_repeat_hash();
    test_develop();
    test_canon();
    test_canon_no_feedback();
    test_hetero();
    test_canon_strum();
    test_canon_swap();
    test_hocket();
    test_hocket_mask();
    test_call();
    test_parse();
    test_resolve();
    test_motif_parse();
    test_relations_parse();
    test_kept();
    test_off();
    test_arpeggio();
    test_melody();
    test_cadence();
    test_melody_change_at_bar();
    test_swell_to_melody();
    test_empty_motif();
    test_styles();
    test_bass();
    test_bass_switch();
    test_model_defaults();
    test_chord_out();
    test_enter_phrase();
    test_bass_off_heard();
    test_quiet();
    test_chord_past_16();
    test_patch_read_cost();
    test_gain_memo();
    test_gain_lengths();
    test_cost();
    test_engine_zone();
    test_onsets();
    test_levels();
    test_melody_gain_chain();
    test_room();
    std::printf("composer ok\n");
    return 0;
}
