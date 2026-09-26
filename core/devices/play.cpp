/* play: a recording as it was made, at 1x - the point card's player (iOS cannot open WebM, so the
   apps decode it as they do for the stretch voices and play it here). Starts and stops with a 10 ms
   fade, a seek fades out, jumps and fades back in (A-2); stops at the end. fs_stats' frames is the
   read position, for the card's progress. */
#include "../device.hpp"
#include <atomic>

static const fs_param PLAY_PARAMS[] = {
    { "play", "Play", "", 0.0f, 1.0f, 0.0f },
    { "seek", "Position", "", 0.0f, 1.0f, 0.0f },     /* setting it jumps there */
};

struct Play : Device {
    const int16_t *src[FS_CHANNELS] = {};
    int nch = 0; long long frames = 0;
    std::atomic<long long> pos{0};
    std::atomic<long long> seek_to{-1};
    Smoothed level;
    float playing = 0;
    int fade = 480;                   /* 10 ms at 48 kHz */
    float env = 0;                    /* the seek's own dip */
    bool dipping = false;

    void prepare(float sr, int) override { level.setup(sr, 3, 0); fade = (int)(sr * 0.01f); }
    const fs_param *params(int &n) override { n = 2; return PLAY_PARAMS; }
    void set_param(int i, float v) override {
        if (i == 0) { playing = v > 0.5f ? 1.0f : 0.0f; level.target = playing; }
        else if (frames > 0) seek_to = (long long)(std::fmin(std::fmax(v, 0.0f), 1.0f) * (frames - 1));
    }
    void set_source_i16(int channels, int n, const int16_t *const *s) override {
        nch = s && n > 0 ? (channels < FS_CHANNELS ? channels : FS_CHANNELS) : 0;
        for (int c = 0; c < nch; c++) src[c] = s[c];
        frames = nch ? n : 0; pos = 0; seek_to = -1;
    }
    void stats(fs_stats_t &st) override { st.frames = pos; }
    void process(int n) override {
        long long p = pos;
        for (int i = 0; i < n; i++) {
            if (seek_to >= 0 && !dipping) dipping = true;
            if (dipping) {                                   /* out, jump, back in */
                env -= 1.0f / fade;
                if (env <= 0) { env = 0; long long t = seek_to.exchange(-1); if (t >= 0) p = t; dipping = false; }
            } else if (env < 1) env = std::fmin(1.0f, env + 1.0f / fade);
            float g = level.next() * env * (frames - p < fade ? (float)(frames - p) / fade : 1.0f);   /* the end fades too */
            float l = 0, r = 0;
            if (p < frames && g > 0) {
                l = src[0][p] * (1.0f / 32768.0f);
                r = nch > 1 ? src[1][p] * (1.0f / 32768.0f) : l;
                l *= g; r *= g;
            }
            if (p < frames && level.value > 1e-4f) p++;
            out[0][i] = l; out[1][i] = r;
        }
        pos = p;
    }
};

Device *make_play() { return new Play(); }
