// The core's self-check. Build and run on any platform:
//   c++ -std=c++17 -O2 core/core.cpp core/mix.cpp core/place.cpp core/sections.cpp core/webm.cpp core/devices/*.cpp core/test.cpp -o fs_test && ./fs_test
#include "fieldscape.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

int main() {
    const float SR = 48000;
    const int B = 128;

    assert(fs_create("nope") == nullptr);

    /* passthrough is bit-exact */
    fs_device *p = fs_create("passthrough");
    fs_prepare(p, SR, B);
    for (int i = 0; i < B; i++) { fs_in(p, 0)[i] = std::sin(i * 0.1f); fs_in(p, 1)[i] = -0.5f; }
    fs_process(p, B);
    for (int i = 0; i < B; i++) { assert(fs_out(p, 0)[i] == fs_in(p, 0)[i]); assert(fs_out(p, 1)[i] == -0.5f); }
    fs_destroy(p);

    /* sine: 1 s at 440 Hz, level 0.2 -> RMS 0.2/sqrt(2), 880 zero crossings */
    fs_device *s = fs_create("sine");
    fs_prepare(s, SR, B);
    assert(fs_param_count(s) == 2);
    fs_set_param(s, 1, 5.0f);                        /* clamped to 1 */
    fs_set_param(s, 1, 0.2f);
    double sum = 0; int crossings = 0; float prev = 0;
    for (int b = 0; b < SR / B; b++) {
        fs_process(s, B);
        for (int i = 0; i < B; i++) {
            float v = fs_out(s, 0)[i];
            sum += v * v;
            if ((prev < 0) != (v < 0)) crossings++;
            prev = v;
        }
    }
    double rms = std::sqrt(sum / (SR / B * B));
    std::printf("sine rms %.4f (want %.4f), crossings %d (want ~880)\n", rms, 0.2 / std::sqrt(2.0), crossings);
    assert(std::fabs(rms - 0.2 / std::sqrt(2.0)) < 0.002);
    assert(crossings >= 876 && crossings <= 884);

    /* A-2: a level jump 0.2 -> 1.0 must ramp, not step. The largest sample-to-sample change stays
       near the sine's own slope (2*pi*440/48000 ~ 0.058 at full level). */
    fs_set_param(s, 1, 1.0f);
    float maxstep = 0; prev = fs_out(s, 0)[B - 1];
    for (int b = 0; b < 20; b++) {
        fs_process(s, B);
        for (int i = 0; i < B; i++) { float v = fs_out(s, 0)[i]; maxstep = std::fmax(maxstep, std::fabs(v - prev)); prev = v; }
    }
    std::printf("level jump: max step %.4f (limit 0.07)\n", maxstep);
    assert(maxstep < 0.07f);
    fs_destroy(s);

    /* mix: quiet material passes bit-exact, only delayed by the 5 ms lookahead */
    {
        fs_device *a = fs_create("sine"), *ref = fs_create("sine");
        fs_prepare(a, SR, B); fs_prepare(ref, SR, B);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 4);
        assert(fs_mix_add(m, a, SR, 1.0f) == 0);
        const int L = (int)(0.005f * SR);
        std::vector<float> want, got;
        for (int b = 0; b < 40; b++) {
            fs_mix_process(m, B); fs_process(ref, B);
            for (int i = 0; i < B; i++) { got.push_back(fs_mix_out(m, 0)[i]); want.push_back(fs_out(ref, 0)[i]); }
        }
        for (size_t i = L; i < got.size(); i++) assert(got[i] == want[i - L]);
        float gr; long long over; fs_mix_stats(m, &gr, &over);
        assert(gr == 0.0f && over == 0);
        fs_mix_destroy(m); fs_destroy(a); fs_destroy(ref);
    }

    /* mix: two full-level sines (peaks to 2.0) never pass -1 dBFS, and a slot's gain ramps */
    {
        fs_device *a = fs_create("sine"), *b2 = fs_create("sine");
        fs_prepare(a, SR, B); fs_prepare(b2, SR, B);
        fs_set_param(a, 1, 1.0f); fs_set_param(b2, 1, 1.0f); fs_set_param(b2, 0, 443.0f);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 4);
        fs_mix_add(m, a, SR, 1.0f);
        int s2 = fs_mix_add(m, b2, SR, 0.0f);
        fs_mix_set_gain(m, s2, 1.0f);                      /* fade the second in: must ramp */
        float peak = 0, maxstep = 0, prev = 0;
        for (int b = 0; b < 375 * 3; b++) {
            fs_mix_process(m, B);
            for (int i = 0; i < B; i++) {
                float v = fs_mix_out(m, 0)[i];
                peak = std::fmax(peak, std::fabs(v));
                maxstep = std::fmax(maxstep, std::fabs(v - prev)); prev = v;
            }
        }
        float gr; long long over; fs_mix_stats(m, &gr, &over);
        std::printf("mix: 2 sines peak %.4f (ceiling 0.8913), reduction %.1f dB, over %lld, max step %.4f\n", peak, gr, over, maxstep);
        assert(peak <= 0.8913f && over == 0 && gr < -5.0f);
        assert(maxstep < 0.13f);                           /* two sines' own slope ~0.116: no step from the fade or the limiter */
        fs_mix_destroy(m); fs_destroy(a); fs_destroy(b2);
    }

    /* mix: a burst from silence to 2.0 is caught before it arrives */
    {
        fs_device *p2 = fs_create("passthrough");
        fs_prepare(p2, SR, B);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 2);
        fs_mix_add(m, p2, SR, 1.0f);
        unsigned r = 1; float peak = 0;
        for (int b = 0; b < 200; b++) {
            for (int i = 0; i < B; i++) {
                r = r * 1664525u + 1013904223u;
                float v = b < 50 ? 0.0f : 2.0f * ((r >> 9) / 8388608.0f - 1.0f);
                fs_in(p2, 0)[i] = v; fs_in(p2, 1)[i] = -v;
            }
            fs_mix_process(m, B);
            for (int i = 0; i < B; i++) peak = std::fmax(peak, std::fabs(fs_mix_out(m, 0)[i]));
        }
        float gr; long long over; fs_mix_stats(m, &gr, &over);
        std::printf("mix: burst 0 -> 2.0 peak %.4f, reduction %.1f dB, over %lld\n", peak, gr, over);
        assert(peak <= 0.8913f && over == 0);
        fs_mix_destroy(m); fs_destroy(p2);
    }

    /* stretch: a 16-bit source plays bit-identically to the float source holding the same samples */
    {
        const int n = 48000 * 3;
        std::vector<short> q(n); std::vector<float> fq(n);
        unsigned r = 5;
        for (int i = 0; i < n; i++) { r = r * 1664525u + 1013904223u; q[i] = (short)((int)(r >> 16) - 32768); fq[i] = q[i] * (1.0f / 32768.0f); }
        const short *qs[1] = { q.data() }; const float *fs[1] = { fq.data() };
        fs_device *a = fs_create("stretch"), *b = fs_create("stretch");
        fs_prepare(a, SR, B); fs_prepare(b, SR, B);
        fs_set_source_i16(a, 1, n, qs); fs_set_source(b, 1, n, fs);
        fs_set_param(a, 0, 0.3f); fs_set_param(b, 0, 0.3f);
        long diff = 0; double energy = 0;
        for (int k = 0; k < 48000 * 4 / B; k++) {
            fs_process(a, B); fs_process(b, B);
            for (int c = 0; c < 2; c++) for (int i = 0; i < B; i++) { diff += fs_out(a, c)[i] != fs_out(b, c)[i]; energy += fs_out(a, c)[i] * fs_out(a, c)[i]; }
        }
        std::printf("stretch int16 vs float source: %ld differing samples (signal energy %.1f)\n", diff, energy);
        assert(diff == 0 && energy > 1);
        fs_destroy(a); fs_destroy(b);
    }

    /* mix: grit 1 settles on tanh(4 x crush(x, 3 bits)), the web's buildGrit at full amount */
    {
        fs_device *p2 = fs_create("passthrough");
        fs_prepare(p2, SR, B);
        fs_mix *m = fs_mix_create();
        fs_mix_prepare(m, SR, B, 1);
        fs_mix_add(m, p2, SR, 1.0f);
        fs_mix_set_grit(m, 0, 1.0f);
        double err = 0; long n = 0; const int L = (int)(0.005f * SR);   /* the limiter's delay */
        std::vector<float> hist;
        for (int b = 0; b < 400; b++) {
            for (int i = 0; i < B; i++) {
                float v = 0.1f * std::sin(0.01f * (b * B + i));
                fs_in(p2, 0)[i] = fs_in(p2, 1)[i] = v; hist.push_back(v);
            }
            fs_mix_process(m, B);
            if (b < 200) continue;            /* after the 0.2 s glide */
            for (int i = 0; i < B; i++) {
                float x = hist[b * B + i - L], step = 0.25f;               /* 3 bits */
                float want = std::tanh(4 * step * std::floor(x / step + 0.5f));
                err = std::fmax(err, (double)std::fabs(fs_mix_out(m, 0)[i] - want));   /* peak tanh(1) < the ceiling */
                n++;
            }
        }
        std::printf("mix: grit 1 vs tanh(4 crush3(x)): max err %.2g over %ld samples\n", err, n);
        assert(err < 1e-3);
        fs_mix_destroy(m); fs_destroy(p2);
    }

    {   /* the route card's patch: the default progression (16 chords, D dorian), key D, 72 bpm */
        fs_device *pc = fs_create("piece");
        assert(fs_piece_add_route(pc, "{}") == 0);
        char buf[4096];
        assert(fs_piece_route_info(pc, 0, buf, sizeof buf) > 0);
        std::string j = buf;
        int chords = 1; for (size_t k = 0; (k = j.find("],[", k)) != std::string::npos; k++) chords++;
        assert(chords == 16);
        assert(j.find("{\"tempo\":72,\"key\":\"D\",\"key2\":\"G\",\"sections\":7,\"voices\":4,\"prog\":[[") == 0);
        assert(j.find("],\"sectors\":[") != std::string::npos && j.back() == '}');
        assert(fs_piece_route_info(pc, 1, buf, sizeof buf) == 0);
        std::printf("route info: %s\n", j.c_str());
        fs_destroy(pc);
    }
    {   /* play: 1x as made, holds when paused, seeks without a step, stops at the end */
        const int n = 48000;
        std::vector<short> q(n); for (int i = 0; i < n; i++) q[i] = (short)(10000 * std::sin(i * 0.01));
        const short *qs[1] = { q.data() };
        fs_device *pl = fs_create("play");
        fs_prepare(pl, SR, B);
        fs_set_source_i16(pl, 1, n, qs);
        fs_stats_t st;
        fs_process(pl, B); fs_stats(pl, &st); assert(st.frames == 0);          /* not playing: holds */
        fs_set_param(pl, 0, 1);
        float maxstep = 0, prev = 0; double err = 0;
        for (int b = 0; b < 100; b++) {
            fs_process(pl, B);
            for (int i = 0; i < B; i++) {
                float v = fs_out(pl, 0)[i];
                maxstep = std::fmax(maxstep, std::fabs(v - prev)); prev = v;
                if (b > 10) { fs_stats(pl, &st); }
            }
            if (b == 40) fs_set_param(pl, 1, 0.5f);                            /* seek mid-way */
        }
        fs_stats(pl, &st);
        std::printf("play: position %lld, max step %.4f\n", st.frames, maxstep);
        assert(st.frames > n / 2 && st.frames < n / 2 + 60 * B);
        assert(maxstep < 0.02f);                                               /* the sine's own slope ~0.003; a hard jump would be ~0.6 */
        for (int b = 0; b < 400; b++) fs_process(pl, B);                       /* past the end */
        fs_stats(pl, &st); assert(st.frames == n);
        fs_process(pl, B); assert(fs_out(pl, 0)[B - 1] == 0.0f);
        fs_destroy(pl);
    }
    {   /* solo on a rhythm point: the route's synths fall silent, and come back when it is let go */
        fs_device *pc = fs_create("piece");
        fs_prepare(pc, SR, B);
        fs_piece_add_route(pc, "{}");
        int h = fs_piece_rhythm_add(pc, "{}", 0);
        assert(h >= 0);
        auto energy = [&](double secs) {
            double e = 0; int blocks = (int)(secs * SR / B);
            for (int b = 0; b < blocks; b++) { fs_piece_walk(pc, 0, 0.3, 0); fs_process(pc, B); for (int i = 0; i < B; i++) e += fs_out(pc, 0)[i] * fs_out(pc, 0)[i]; }
            return e / (blocks * B);
        };
        energy(6); double open = energy(2);
        fs_piece_solo(pc, h); energy(1); double soloed = energy(1);
        fs_piece_solo(pc, -1); energy(1); double back = energy(2);
        std::printf("piece solo: route %.2e, soloed %.2e, let go %.2e\n", open, soloed, back);
        assert(open > 1e-7 && soloed < open * 1e-6 && back > open * 0.05);
        fs_destroy(pc);
    }
    {   /* the morph cells: the playing route's morphs at the piece's clock, by the web's morphValue */
        fs_device *pc = fs_create("piece");
        fs_prepare(pc, SR, B);
        fs_piece_add_route(pc, "{}");
        fs_piece_add_route(pc, "{\"version\":12,\"prog\":[{\"r\":0,\"q\":\"m7\"}],\"morph\":{\"on\":true,\"cells\":false,\"list\":["
            "{\"id\":\"a\",\"on\":true,\"shape\":\"ramp\",\"period\":10,\"depth\":1,\"bias\":0,\"phase\":0.25,\"dest\":\"v3.cutoff\"},"
            "{\"id\":\"b\",\"on\":false,\"shape\":\"drift\",\"dest\":\"voice.cutoff\"}]}}");
        const double TAU = 6.283185307179586;
        double o[7 * 24], clock = 0; int root = -1, shown = -1;
        assert(fs_piece_morphs(pc, o, 24, &clock, &root, &shown) == -1);        /* nothing playing yet */
        for (int b = 0; b < (int)(3.0 * SR / B); b++) { fs_piece_walk(pc, 0, 0.3, 0); fs_process(pc, B); }
        int n = fs_piece_morphs(pc, o, 24, &clock, &root, &shown);
        double u = clock / 660, tide = 0.08 + 0.85 * (0.5 + (std::sin(TAU * u) + std::sin(TAU * u * 1.6180339887)) / 4);
        std::printf("morph cells: %d, clock %.2f s, root %d, m1 %.4f (web %.4f)\n", n, clock, root, o[4], tide);
        assert(n == 6 && shown == 1 && clock > 2.5 && root >= 0 && root < 12);
        assert(o[0] == 4 && o[1] == 0 && std::fabs(o[4] - tide) < 1e-9);       /* m1: tide on voice.cutoff */
        assert(o[7 * 3 + 1] == 1 && o[7 * 4 + 1] == 1);                        /* m4, m5 drive the second voice */
        for (int i = 0; i < n; i++) assert(o[7 * i + 4] >= 0 && o[7 * i + 4] <= 1);
        for (int b = 0; b < (int)(3.0 * SR / B); b++) { fs_piece_walk(pc, 1, 0.3, 0); fs_process(pc, B); }
        n = fs_piece_morphs(pc, o, 24, &clock, &root, &shown);
        double ramp = clock / 10 + 0.25; ramp -= std::floor(ramp);
        std::printf("morph cells, own patch: %d, shown %d, ramp %.4f (web %.4f)\n", n, shown, o[4], ramp);
        assert(n == 1 && shown == 0 && o[0] == 3 && o[1] == 2 && std::fabs(o[4] - ramp) < 1e-9);
        fs_destroy(pc);
    }
    {   /* the engine's live reads: morphs, chord and route of the piece it drives */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
            "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{}}}]}");
        double o[7 * 24], clock = 0; int root = -1, shown = -1, count = 0; char label[32];
        assert(fs_engine_route(e) == -1 && fs_engine_chord(e, &count, label, 32) == -1);
        for (int b = 0; b < (int)(4.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); }
        int n = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        int ch = fs_engine_chord(e, &count, label, 32);
        std::printf("engine live: route %d, chord %d of %d %s, %d morphs at %.2f s\n", fs_engine_route(e), ch, count, label, n, clock);
        assert(fs_engine_route(e) == 0 && ch >= 0 && count == 16 && label[0] && n == 6 && clock > 3);
        for (int b = 0; b < (int)(2.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.1, 41.1); fs_engine_process(e, B); }
        assert(fs_engine_route(e) == -1 && fs_engine_chord(e, &count, label, 32) == -1);
        assert(fs_engine_morphs(e, o, 24, &clock, &root, &shown) == -1);   /* 8 km away: nothing playing */
        fs_engine_destroy(e);
    }
    {   /* Listen (fs_engine_solo): one point alone at its full level from any distance */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.02,41.0]},"
            "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.03,41.0]},"
            "\"properties\":{\"id\":\"g1\",\"kind\":\"point\",\"name\":\"G\",\"has_audio\":true,\"audio_mode\":\"grains\",\"storage_path\":\"g1.webm\"}}]}");
        std::string need = fs_engine_step(e, 29.0, 41.0), st = fs_engine_state(e);
        assert(need.empty() && st.find("\"s1\"") == std::string::npos);            /* 1.7 km: out of reach */
        fs_engine_solo(e, "s1");
        need = fs_engine_step(e, 29.0, 41.0); st = fs_engine_state(e);
        std::printf("solo s1: %s | %s\n", need.c_str(), st.c_str());
        assert(need.find(" s1 s1.webm") != std::string::npos && st.find("\"id\":\"s1\",\"name\":\"S\",\"level\":1.0000") != std::string::npos);
        fs_engine_solo(e, "g1");
        need = fs_engine_step(e, 29.0, 41.0); st = fs_engine_state(e);
        std::printf("solo g1: %s | %s\n", need.c_str(), st.c_str());
        assert(need.find("R ") == 0 && need.find(" g1 g1.webm") != std::string::npos);
        assert(st.find("{\"id\":\"g1\",\"level\":1.0000") != std::string::npos && st.find("\"rows\":[]") != std::string::npos);
        fs_engine_solo(e, "");
        fs_engine_step(e, 29.0, 41.0); st = fs_engine_state(e);
        assert(st.find("\"rows\":[]") != std::string::npos && st.find("\"beats\":[]") != std::string::npos);
        fs_engine_destroy(e);
    }
    {   /* live edits (fs_engine_upsert / fs_engine_remove): a setter's change reaches the sound without a reload */
        fs_engine *e = fs_engine_create(SR, B);
        const char *route = "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
                            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":%s}}";
        char buf[1024];
        std::snprintf(buf, sizeof buf, route, "{}");
        fs_engine_upsert(e, buf);
        fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.02,41.0]},"
            "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\",\"sound\":{\"radius\":140}}}");
        double o[7 * 24], clock = 0; int root = 0, shown = 0;
        auto walk = [&](double secs) { std::string need; for (int b = 0; b < (int)(secs * SR / B); b++) { if (b % 40 == 0) need += fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); } return need; };
        std::string need = walk(3);
        int n0 = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        assert(n0 == 6 && need.find(" s1 ") == std::string::npos);                 /* default morphs; s1 1.7 km away */
        std::snprintf(buf, sizeof buf, route, "{\"version\":12,\"prog\":[{\"r\":0,\"q\":\"m7\"}],\"morph\":{\"on\":false,\"list\":[]}}");
        fs_engine_upsert(e, buf);                                                   /* the setter switches Morphs off */
        walk(0.5);
        int n1 = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[29.02,41.0]},"
            "\"properties\":{\"id\":\"s1\",\"kind\":\"point\",\"name\":\"S\",\"has_audio\":true,\"storage_path\":\"s1.webm\",\"sound\":{\"radius\":3000}}}");
        need = walk(0.5);                                                           /* ...and gives s1 a 3 km reach */
        std::printf("live edits: morphs %d -> %d, s1 asked for after its radius grew: %s\n", n0, n1, need.find(" s1 s1.webm") != std::string::npos ? "yes" : "no");
        assert(n1 == 0 && need.find(" s1 s1.webm") != std::string::npos);
        fs_engine_remove(e, "s1"); walk(0.5);
        assert(std::string(fs_engine_state(e)).find("\"s1\"") == std::string::npos);  /* deleted: gone from what plays */
        fs_engine_upsert(e, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.1,41.1],[29.102,41.1]]},"
            "\"properties\":{\"id\":\"r2\",\"kind\":\"route\",\"name\":\"R2\",\"patch\":{}}}");
        fs_engine_remove(e, "r1"); walk(0.5);
        assert(fs_engine_route(e) == -1);                                           /* r1 deleted: standing on its line plays nothing */
        for (int b = 0; b < (int)(2.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.101, 41.1); fs_engine_process(e, B); }
        assert(fs_engine_route(e) == 1);                                            /* r2, added live, plays (piece index 1) */
        fs_engine_destroy(e);
    }
    {   /* Fieldscape's own stretch shaping (docs/superpowers/specs/2026-09-27-own-stretch-shaping-design.md) */
        auto pidx = [](fs_device *d, const char *id) {
            for (int i = 0; i < fs_param_count(d); i++) if (!std::strcmp(fs_param_info(d, i)->id, id)) return i;
            return -1;
        };
        const double TAU = 6.283185307179586;
        /* Goertzel power at f over x */
        auto power = [&](const std::vector<float> &x, double f) {
            double w = TAU * f / SR, c = 2 * std::cos(w), s1 = 0, s2 = 0;
            for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
            return (s1 * s1 + s2 * s2 - c * s1 * s2) / (double)x.size();
        };
        auto rmsdb = [](const std::vector<float> &x) { double s = 0; for (float v : x) s += (double)v * v; return 10 * std::log10(s / x.size() + 1e-20); };
        /* play a device for `secs`, keep the left channel of the last `keep` seconds */
        auto run = [&](fs_device *d, double secs, double keep, std::vector<float> *all = nullptr) {
            std::vector<float> o; long total = (long)(secs * SR / B), from = total - (long)(keep * SR / B);
            for (long k = 0; k < total; k++) {
                fs_process(d, B);
                if (all) all->insert(all->end(), fs_out(d, 0), fs_out(d, 0) + B);
                if (k >= from) o.insert(o.end(), fs_out(d, 0), fs_out(d, 0) + B);
            }
            return o;
        };
        auto make = [&](const std::vector<float> &src) {
            fs_device *d = fs_create("stretch"); fs_prepare(d, SR, B);
            const float *s[1] = { src.data() }; fs_set_source(d, 1, (int)src.size(), s);
            return d;
        };
        const int n = 48000 * 3;

        /* 1: every new control at zero - the stretch exactly as it was (checksum captured before the stage existed) */
        {
            std::vector<float> l(n), r(n); unsigned s = 11;
            for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; l[i] = 0.3f * std::sin(0.0576f * i) + ((int)(s >> 16) - 32768) / 327680.0f; r[i] = l[(i * 7) % n]; }
            const float *src[2] = { l.data(), r.data() };
            uint64_t h = 1469598103934665603ULL;
            for (float stretch : { 0.0f, 0.4f }) {
                fs_device *d = fs_create("stretch"); fs_prepare(d, SR, B); fs_set_source(d, 2, n, src);
                fs_set_param(d, 0, stretch); fs_set_param(d, 3, stretch > 0 ? 0.3f : 0.0f);
                for (int k = 0; k < 48000 * 5 / B; k++) { fs_process(d, B); for (int c = 0; c < 2; c++) { const float *o = fs_out(d, c); for (int i = 0; i < B; i++) { uint32_t u; std::memcpy(&u, &o[i], 4); h = (h ^ u) * 1099511628211ULL; } } }
                fs_destroy(d);
            }
            std::printf("shape 1: all zero, checksum %llu (the stretch before the stage: 9279134860882250983)\n", (unsigned long long)h);
            assert(h == 9279134860882250983ULL);
        }
        std::vector<float> sine(n), noise(n);
        { unsigned s = 3; double b0 = 0, b1 = 0, b2 = 0;
          for (int i = 0; i < n; i++) {
              sine[i] = 0.3f * (float)std::sin(TAU * 440 * i / SR);
              s = s * 1664525u + 1013904223u; double w = ((int)(s >> 16) - 32768) / 32768.0;
              b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
              noise[i] = (float)(0.1 * (b0 + b1 + b2 + w * 0.1848));             /* pink (Kellet) */
          } }

        /* 2: transpose +12 moves 440 Hz to 880 Hz */
        {
            fs_device *d = make(sine);
            assert(pidx(d, "transpose") >= 0);
            fs_set_param(d, pidx(d, "transpose"), 12);
            std::vector<float> o = run(d, 4, 2);
            double p440 = power(o, 440), p880 = power(o, 880);
            std::printf("shape 2: transpose +12: 880 Hz %.2e vs 440 Hz %.2e\n", p880, p440);
            assert(p880 > 20 * p440);
            fs_destroy(d);
        }

        /* the chord's partials across the octaves, and the points half a semitone off them */
        auto on_off = [&](const std::vector<float> &o, std::initializer_list<int> pcs) {
            double on = 0, off = 0;
            for (int pc : pcs) for (int oct = 3; oct <= 6; oct++) {
                double f = 440 * std::pow(2.0, (pc + 12 * (oct + 1) - 69) / 12.0);
                on += power(o, f); off += power(o, f * std::pow(2.0, 0.5 / 12));
            }
            return on / (off + 1e-30);
        };
        auto set_chord = [&](fs_device *d, std::initializer_list<int> midi) {
            int i = 0; for (int m : midi) fs_set_param(d, pidx(d, (std::string("chord") + char('0' + i++)).c_str()), (float)m);
            for (; i < 5; i++) fs_set_param(d, pidx(d, (std::string("chord") + char('0' + i)).c_str()), -1);
            fs_set_param(d, pidx(d, "root"), (float)*midi.begin());
        };

        /* 3: tune pulls pink noise onto the chord (D F A) */
        double dry_ratio, wet_ratio, dry_db, wet_db;
        {
            fs_device *d = make(noise); std::vector<float> o = run(d, 4, 2);
            dry_ratio = on_off(o, { 2, 5, 9 }); dry_db = rmsdb(o); fs_destroy(d);
        }
        {
            fs_device *d = make(noise);
            assert(pidx(d, "tune") >= 0 && pidx(d, "chord0") >= 0);
            set_chord(d, { 62, 65, 69 });
            fs_set_param(d, pidx(d, "tune"), 1); fs_set_param(d, pidx(d, "focus"), 0.1f);
            fs_set_param(d, pidx(d, "partials"), 6);     /* the 11th overtone of D sits 51 cents above G: the measure's own off point */
            std::vector<float> o = run(d, 4, 2);
            double before = 0; for (size_t i = 1; i < o.size(); i++) before = std::fmax(before, std::fabs(o[i] - o[i - 1]));
            wet_ratio = on_off(o, { 2, 5, 9 }); wet_db = rmsdb(o);
            std::printf("shape 3: tune 1 on pink noise: on/off the chord %.1f (dry %.1f), level %.1f dB (dry %.1f)\n", wet_ratio, dry_ratio, wet_db, dry_db);
            assert(wet_ratio > 20 && dry_ratio < 3);
            assert(std::fabs(wet_db - dry_db) < 1.0);                                    /* 6: A-18, a timbre not a level */
            /* 4: a chord change glides, with no click */
            set_chord(d, { 67, 71, 74 }); fs_set_param(d, pidx(d, "glide"), 1);
            std::vector<float> all; std::vector<float> o2 = run(d, 4, 1.5, &all);
            double jump = 0; for (size_t i = 1; i < all.size(); i++) jump = std::fmax(jump, std::fabs(all[i] - all[i - 1]));
            double g_ratio = on_off(o2, { 7, 11, 2 });
            std::printf("shape 4: to G B D over a 1 s glide: on/off the new chord %.1f, largest step %.3f (the noise's own before: %.3f)\n", g_ratio, jump, before);
            assert(g_ratio > 20 && jump < 1.5 * before);                             /* no click beyond what the sound itself does */
            fs_destroy(d);
        }

        /* 5: layers add the chord's intervals above the recording (root D: F +3, A +7) */
        {
            fs_device *d = make(sine);
            set_chord(d, { 62, 65, 69 });
            fs_set_param(d, pidx(d, "layers"), 2); fs_set_param(d, pidx(d, "harmony"), 1);
            std::vector<float> o = run(d, 4, 2);
            double p0 = power(o, 440), p3 = power(o, 440 * std::pow(2.0, 3 / 12.0)), p7 = power(o, 440 * std::pow(2.0, 7 / 12.0)), px = power(o, 440 * std::pow(2.0, 5 / 12.0));
            std::printf("shape 5: layers 2: +3 st %.2e, +7 st %.2e, off (+5) %.2e, the recording %.2e\n", p3, p7, px, p0);
            assert(p3 > 20 * px && p7 > 20 * px);
            fs_destroy(d);
        }

        /* 6b: tune, layers and blur all at 1 keep the level */
        {
            fs_device *d = make(noise);
            set_chord(d, { 62, 65, 69 });
            for (const char *k : { "tune", "blur" }) fs_set_param(d, pidx(d, k), 1);
            fs_set_param(d, pidx(d, "layers"), 4); fs_set_param(d, pidx(d, "harmony"), 1);
            double db = rmsdb(run(d, 4, 2));
            std::printf("shape 6: tune + layers 4 + blur, level %.1f dB (dry %.1f)\n", db, dry_db);
            assert(std::fabs(db - dry_db) < 1.0);
            fs_destroy(d);
        }

        /* 7: a region plays only its part (300 Hz, then 900 Hz; region = the first half) */
        {
            std::vector<float> two(n);
            for (int i = 0; i < n; i++) two[i] = 0.3f * (float)std::sin(TAU * (i < n / 2 ? 300 : 900) * i / SR);
            fs_device *d = make(two);
            assert(pidx(d, "end") >= 0);
            fs_set_param(d, pidx(d, "start"), 0); fs_set_param(d, pidx(d, "end"), 0.5f);
            std::vector<float> o = run(d, 8, 5);
            double p3 = power(o, 300), p9 = power(o, 900);
            std::printf("shape 7: region 0-0.5: 300 Hz %.2e, 900 Hz %.2e\n", p3, p9);
            assert(p3 > 50 * p9);
            fs_destroy(d);
        }

        /* 8: drift changes the sound, not its level */
        {
            fs_device *a = make(noise), *b = make(noise);
            fs_set_param(b, pidx(b, "drift"), 1);
            std::vector<float> oa = run(a, 4, 2), ob = run(b, 4, 2);
            double diff = 0; for (size_t i = 0; i < oa.size(); i++) diff += std::fabs(oa[i] - ob[i]);
            std::printf("shape 8: drift 1: differs by %.1f, level %.1f dB vs %.1f\n", diff, rmsdb(ob), rmsdb(oa));
            assert(diff > 1 && std::fabs(rmsdb(ob) - rmsdb(oa)) < 1.0);
            fs_destroy(a); fs_destroy(b);
        }

        /* 9: everything on at the longest window, a chord change mid-way: still inside one 128-sample
           callback's budget (2.67 ms at 48 kHz; the stretch's own check holds 1.33 ms) */
        {
            fs_device *d = make(noise);
            fs_set_param(d, 1, 2.0f);                                          /* window 2 s */
            set_chord(d, { 62, 65, 69, 72, 76 });
            for (const char *k : { "tune", "blur", "drift" }) fs_set_param(d, pidx(d, k), 1);
            fs_set_param(d, pidx(d, "layers"), 4); fs_set_param(d, pidx(d, "harmony"), 1);
            fs_set_param(d, pidx(d, "partials"), 24); fs_set_param(d, pidx(d, "transpose"), 5);
            std::vector<double> ms;
            for (int k = 0; k < (int)(12.0 * SR / B); k++) {
                if (k == (int)(6.0 * SR / B)) set_chord(d, { 67, 71, 74, 77 });
                auto t0 = std::chrono::steady_clock::now();
                fs_process(d, B);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            fs_stats_t st{}; fs_stats(d, &st);
            std::sort(ms.begin(), ms.end());
            const double p999 = ms[(size_t)(ms.size() * 0.999)], worst = ms.back();
            /* a frame finished late is an audible failure; one slow callback on a desktop is the OS */
            std::printf("shape 9: everything on, window 2 s, a chord change: late frames %d, 99.9%% of callbacks within %.3f ms (budget 1.33), worst %.3f ms (a callback is 2.67)\n", st.late_frames, p999, worst);
            assert(st.late_frames == 0 && p999 < 1.33 && worst < 2.67);
            fs_destroy(d);
        }
    }
    std::printf("core ok\n");
    return 0;
}
