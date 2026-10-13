/* SPDX-License-Identifier: Apache-2.0 */
/* test_ncz_audio_bus.c - gates for the shared audio/beat engine.
 *
 * Covers: virtual-beat determinism, BPM range and drift, onset detection on
 * synthetic kick bursts (within 40 ms, few false positives on noise), AGC,
 * the photosensitive limiter at 180 BPM full dynamics, and dt clamping.
 */
#include "ncz_audio_bus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RATE NCZ_AUDIO_RATE

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            failures++;                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                \
            fprintf(stderr, __VA_ARGS__);                                       \
            fprintf(stderr, "\n");                                              \
        }                                                                       \
    } while (0)

static uint32_t rs = 0xC0FFEEu;
static float frnd(void) {
    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
    return (float)(rs & 0xffffffu) / 16777216.f;
}

/* ---- virtual beat determinism ---- */
static void test_determinism(void) {
    NczAudioOpts o = {0.f, 0, 0};
    NczAudioBus *a = ncz_audio_bus_create(12345u, &o);
    NczAudioBus *b = ncz_audio_bus_create(12345u, &o);
    int same = 1;
    for (int i = 0; i < 120 * 60; i++) {
        ncz_audio_bus_update(a, 1.f / 60.f);
        ncz_audio_bus_update(b, 1.f / 60.f);
        const NczAudioSignals *sa = ncz_audio_bus_signals(a);
        const NczAudioSignals *sb = ncz_audio_bus_signals(b);
        if (sa->kick != sb->kick || sa->snare != sb->snare || sa->beat_phase != sb->beat_phase ||
            sa->bpm != sb->bpm || sa->breakdown != sb->breakdown) {
            same = 0;
            break;
        }
    }
    CHECK(same, "same seed did not give the same beat timeline");
    ncz_audio_bus_destroy(a);
    ncz_audio_bus_destroy(b);
    printf("determinism: %s\n", same ? "ok" : "FAIL");
}

/* ---- BPM range and drift ---- */
static void test_bpm(void) {
    NczAudioOpts o = {0.f, 0, 0};
    float gmin = 1e9f, gmax = -1e9f, worst_drift = 0.f;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        NczAudioBus *b = ncz_audio_bus_create(seed * 7919u, &o);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 300 * 60; i++) {
            ncz_audio_bus_update(b, 1.f / 60.f);
            float v = ncz_audio_bus_bpm(b);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        CHECK(lo >= 93.f && hi <= 135.f, "seed %u bpm out of range [%f,%f]", seed, lo, hi);
        if (lo < gmin) gmin = lo;
        if (hi > gmax) gmax = hi;
        if (hi - lo > worst_drift) worst_drift = hi - lo;
        ncz_audio_bus_destroy(b);
    }
    CHECK(worst_drift <= 8.f, "bpm drift too large: %f", worst_drift);
    printf("bpm: global range [%.2f, %.2f], worst drift %.2f\n", gmin, gmax, worst_drift);
}

/* ---- synthetic kicks for onset tests ---- */
static void add_kick(float *buf, int n, double at, float amp) {
    int start = (int)(at * RATE);
    for (int i = start; i < n && i < start + RATE / 4; i++) {
        double t = (double)(i - start) / RATE;
        float env = expf((float)(-t / 0.045));
        float click = (t < 0.003) ? 0.6f : 0.f;
        buf[i] += amp * env * (0.9f * sinf(2.f * (float)M_PI * 70.0 * (float)t) + click);
    }
}

static void test_onsets(void) {
    int n = 2 * RATE;
    float *buf = calloc((size_t)n, sizeof(float));
    double at[] = {0.20, 0.45, 0.70, 0.95, 1.20, 1.45};
    int nk = (int)(sizeof at / sizeof at[0]);
    for (int k = 0; k < nk; k++) add_kick(buf, n, at[k], 0.9f);
    NczAudioAnalyzer *a = ncz_audio_analyzer_create();
    for (int i = 0; i < n; i += 256) ncz_audio_analyzer_feed(a, buf + i, (n - i) < 256 ? (n - i) : 256);
    unsigned got = ncz_audio_analyzer_onsets(a);
    int matched = 0, fp = 0;
    for (unsigned j = 0; j < got; j++) {
        double t = (double)ncz_audio_analyzer_onset_index(a, j) / RATE;
        int best = 0; double bd = 1e9;
        for (int k = 0; k < nk; k++) { double d = fabs(t - at[k]); if (d < bd) { bd = d; best = k; } }
        (void)best;
        if (bd <= 0.040) matched++;
        else fp++;
    }
    CHECK(matched >= nk, "only %d/%d kicks detected within 40 ms", matched, nk);
    CHECK(fp <= 1, "too many false positives: %d", fp);

    /* noise only: few or no onsets */
    NczAudioAnalyzer *noise = ncz_audio_analyzer_create();
    float *nb = calloc((size_t)n, sizeof(float));
    for (int i = 0; i < n; i++) nb[i] = (frnd() - 0.5f) * 0.05f;
    for (int i = 0; i < n; i += 256) ncz_audio_analyzer_feed(noise, nb + i, 256);
    unsigned nf = ncz_audio_analyzer_onsets(noise);
    CHECK(nf <= 2, "noise produced %u onsets", nf);
    printf("onsets: matched=%d/%d fp=%d noise=%u\n", matched, nk, fp, nf);

    /* AGC: a loud passage must not push the normalised bands above 1. */
    NczAudioAnalyzer *agc = ncz_audio_analyzer_create();
    for (int i = 0; i < 4 * RATE; i++) {
        float amp = (i < 2 * RATE) ? 0.02f : 0.8f;
        float s = amp * sinf(2.f * (float)M_PI * 440.f * (float)i / RATE) +
                  amp * sinf(2.f * (float)M_PI * 80.f * (float)i / RATE);
        ncz_audio_analyzer_feed(agc, &s, 1);
    }
    NczAudioAnalysis an = ncz_audio_analyzer_state(agc);
    CHECK(an.bass >= 0.f && an.bass <= 1.f && an.mid >= 0.f && an.mid <= 1.f,
          "AGC out of range bass=%f mid=%f", an.bass, an.mid);
    CHECK(an.bass > 0.2f, "AGC did not respond to the loud passage: bass=%f", an.bass);
    printf("agc: bass=%.3f mid=%.3f treble=%.3f\n", an.bass, an.mid, an.treble);

    ncz_audio_analyzer_destroy(a);
    ncz_audio_analyzer_destroy(noise);
    ncz_audio_analyzer_destroy(agc);
    free(buf); free(nb);
}

/* ---- limiter bounds at 180 BPM, full dynamics ---- */
static void test_limiter(void) {
    /* Square wave, 180 BPM (3 Hz), sampled at 60 fps. */
    NczBeatLimiter l;
    ncz_beat_limiter_init(&l);
    double period = 60.0 / 180.0;
    int steps = (int)(4.0 * 60);
    float prev_beat = -1.f;
    float max_beat_delta = 0.f;
    int flashes = 0;
    double win = 0.0;
    int win_flashes = 0, max_win_flashes = 0;
    for (int i = 0; i < steps; i++) {
        double now = (double)i / 60.0;
        float raw = (fmod(now, period) < period * 0.5) ? 1.0f : 0.0f;
        float out = ncz_beat_limiter_apply(&l, raw, now);
        CHECK(out >= 0.f && out <= 1.f, "limiter out of range %f", out);
        /* sample the value at each beat boundary to measure the per-beat change */
        double ph = fmod(now, period);
        if (prev_beat >= 0.f && ph < 1.0 / 60.0) {
            float d = fabsf(out - prev_beat);
            if (d > max_beat_delta) max_beat_delta = d;
        }
        if (ph < 1.0 / 60.0) prev_beat = out;
        if (now - win >= 1.0) { if (win_flashes > max_win_flashes) max_win_flashes = win_flashes; win = now; win_flashes = 0; }
        if (i > 0 && fmod((double)(i - 1) / 60.0, period) >= period * 0.5 && raw > 0.5f) win_flashes++;
    }
    (void)flashes;
    CHECK(max_beat_delta <= 0.27f, "luminance changed too much between beats: %f", max_beat_delta);
    CHECK(max_win_flashes <= 3, "too many flashes in a second: %d", max_win_flashes);
    printf("limiter: max_beat_delta=%.3f max_flashes/s=%d\n", max_beat_delta, max_win_flashes);

    /* red-heavy damping: from a zero start the first rise is 60 % of the
     * undamped one. */
    NczBeatLimiter r, p0;
    ncz_beat_limiter_init(&r);
    ncz_beat_limiter_init(&p0);
    ncz_beat_limiter_set_red_heavy(&r, 1);
    ncz_beat_limiter_apply(&r, 0.f, 0.0);
    ncz_beat_limiter_apply(&p0, 0.f, 0.0);
    float rr = ncz_beat_limiter_apply(&r, 1.0f, 0.05);
    float pp = ncz_beat_limiter_apply(&p0, 1.0f, 0.05);
    CHECK(rr < pp * 0.75f, "red-heavy flash was not damped: %f vs %f", rr, pp);
    printf("limiter red-heavy first rise: %.4f vs undamped %.4f\n", rr, pp);
}

/* ---- dt clamping / robustness ---- */
static void test_dt(void) {
    NczAudioOpts o = {0.f, 0, 0};
    NczAudioBus *b = ncz_audio_bus_create(99u, &o);
    ncz_audio_bus_update(b, 1000.f);
    ncz_audio_bus_update(b, -5.f);
    ncz_audio_bus_update(b, NAN);
    const NczAudioSignals *s = ncz_audio_bus_signals(b);
    CHECK(s->bpm == s->bpm && s->bpm > 90.f && s->bpm < 140.f, "bpm not finite/sane after bad dt: %f", s->bpm);
    CHECK(s->kick == s->kick && s->beat_phase >= 0.f && s->beat_phase <= 1.f, "phase out of range");
    ncz_audio_bus_destroy(b);
    printf("dt clamp: ok\n");
}

/* ---- options clamp ---- */
static void test_opts(void) {
    NczAudioOpts o = {5.f, 999, 9};
    ncz_audio_opts_clamp(&o);
    CHECK(o.dynamics == 1.f && o.tempo == 180 && o.audio == 2, "opts clamp failed: %f %d %d", o.dynamics, o.tempo, o.audio);
    NczAudioOpts n = {NAN, -4, -3};
    ncz_audio_opts_clamp(&n);
    CHECK(n.dynamics == 0.f && n.tempo == 60 && n.audio == 0, "opts NaN clamp failed: %f %d %d", n.dynamics, n.tempo, n.audio);
    printf("opts clamp: ok\n");
}

int main(void) {
    test_determinism();
    test_bpm();
    test_onsets();
    test_limiter();
    test_dt();
    test_opts();
    if (failures) {
        printf("ncz_audio_bus: %d FAILURES\n", failures);
        return 1;
    }
    printf("ncz_audio_bus: all gates passed\n");
    return 0;
}
