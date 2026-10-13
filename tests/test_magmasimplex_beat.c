/* SPDX-License-Identifier: Apache-2.0 */
/* test_magmasimplex_beat.c - gates for the magmasimplex adapter.
 *
 * Covers: the per-mode mapping is bounded for any input (property test);
 * dynamics=0 makes every drive exactly 0; the adapter is deterministic; and
 * options clamp.
 */
#include "magmasimplex_beat.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

static uint32_t rs = 0xBEEF1234u;
static float frnd(void) {
    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
    return (float)(rs & 0xffffffu) / 16777216.f;
}

static int drive_in_bounds(const MsBeatDrive *d) {
    const float one = 1.0f + 1e-5f;
    if (d->squeeze < -1e-6f || d->squeeze > one) return 0;
    if (d->breath < -1e-6f || d->breath > one) return 0;
    if (d->burst < -1e-6f || d->burst > one) return 0;
    if (d->glow < -1e-6f || d->glow > one) return 0;
    if (d->flow < -1e-6f || d->flow > one) return 0;
    if (d->bloom < -1e-6f || d->bloom > one) return 0;
    if (d->radius < -0.04001f || d->radius > 0.04001f) return 0;
    if (d->beat < -1e-6f || d->beat > one) return 0;
    if (d->snare < -1e-6f || d->snare > one) return 0;
    if (d->bass < -1e-6f || d->bass > one) return 0;
    if (d->mid < -1e-6f || d->mid > one) return 0;
    if (d->phase < -1e-6f || d->phase > one) return 0;
    return 1;
}

static void test_property_bounds(void) {
    float dyns[] = {0.f, 0.1f, 0.25f, 0.5f, 0.7f, 0.85f, 1.f};
    int bad = 0;
    for (int it = 0; it < 20000; it++) {
        NczAudioSignals s;
        /* in-range and out-of-range draws, to prove clamping */
        s.bass = frnd() * 1.6f - 0.3f;
        s.mid = frnd() * 1.6f - 0.3f;
        s.treble = frnd() * 1.6f - 0.3f;
        s.onset_env = frnd() * 1.6f - 0.3f;
        s.beat_phase = frnd() * 1.6f - 0.3f;
        s.bpm = 60.f + frnd() * 120.f;
        s.kick = frnd() * 1.6f - 0.3f;
        s.snare = frnd() * 1.6f - 0.3f;
        s.level = frnd();
        s.drive = frnd();
        s.onset = (int)(frnd() * 2.f);
        s.breakdown = (int)(frnd() * 2.f);
        s.active = 1;
        s.audio_live = (int)(frnd() * 2.f);
        int mode = (int)(frnd() * (float)MS_BEAT_MODE_COUNT);
        if (mode >= MS_BEAT_MODE_COUNT) mode = MS_BEAT_MODE_COUNT - 1;
        int world = (int)(frnd() * 4.f);
        if (world > 3) world = 3;
        float dyn = dyns[(int)(frnd() * 7.f) % 7];
        MsBeatDrive d;
        ms_beat_map_signals(mode, world, dyn, &s, &d);
        if (!drive_in_bounds(&d)) {
            bad++;
            if (bad <= 5)
                fprintf(stderr, "  out of bounds mode=%d world=%d dyn=%.2f sq=%.3f br=%.3f bu=%.3f gl=%.3f fl=%.3f bl=%.3f r=%.4f\n",
                        mode, world, dyn, d.squeeze, d.breath, d.burst, d.glow, d.flow, d.bloom, d.radius);
        }
    }
    CHECK(bad == 0, "%d/20000 mapping results out of bounds", bad);
    printf("mapping bounds: %s\n", bad ? "FAIL" : "ok (20000 draws)");
}

static void test_dynamics_zero(void) {
    int bad = 0;
    for (int it = 0; it < 2000; it++) {
        NczAudioSignals s;
        s.bass = frnd(); s.mid = frnd(); s.treble = frnd();
        s.onset_env = frnd(); s.beat_phase = frnd();
        s.bpm = 96.f + frnd() * 36.f;
        s.kick = frnd(); s.snare = frnd(); s.level = frnd(); s.drive = frnd();
        s.onset = (int)(frnd() * 2.f); s.breakdown = 0; s.active = 1; s.audio_live = 0;
        for (int mode = 0; mode < MS_BEAT_MODE_COUNT; mode++) {
            MsBeatDrive d;
            ms_beat_map_signals(mode, it % 4, 0.f, &s, &d);
            if (d.squeeze != 0.f || d.breath != 0.f || d.burst != 0.f || d.glow != 0.f ||
                d.flow != 0.f || d.bloom != 0.f || d.radius != 0.f || d.beat != 0.f ||
                d.snare != 0.f || d.bass != 0.f || d.mid != 0.f)
                bad++;
        }
    }
    CHECK(bad == 0, "dynamics=0 left %d non-zero drives", bad);
    printf("dynamics=0: %s\n", bad ? "FAIL" : "ok (all drives exactly 0)");
}

static void test_adapter_determinism(void) {
    MsBeatOpts o = {0.7f, 0, 0};
    MsBeat *a = ms_beat_create(4242u, &o);
    MsBeat *b = ms_beat_create(4242u, &o);
    int same = 1;
    ms_beat_set_mode(a, MS_BEAT_MODE_BUTTERFLY, MS_BEAT_WORLD_DISH);
    ms_beat_set_mode(b, MS_BEAT_MODE_BUTTERFLY, MS_BEAT_WORLD_DISH);
    for (int i = 0; i < 60 * 60; i++) {
        ms_beat_update(a, 1.f / 60.f);
        ms_beat_update(b, 1.f / 60.f);
        const MsBeatDrive *da = ms_beat_drive(a), *db = ms_beat_drive(b);
        if (memcmp(da, db, sizeof *da) != 0) { same = 0; break; }
    }
    CHECK(same, "adapter not deterministic for the same seed");
    ms_beat_destroy(a);
    ms_beat_destroy(b);
    printf("adapter determinism: %s\n", same ? "ok" : "FAIL");
}

static void test_opts(void) {
    MsBeatOpts o = {9.f, 999, 8};
    ms_beat_opts_clamp(&o);
    CHECK(o.dynamics == 1.f && o.tempo == 180 && o.audio == 2, "adapter opts clamp: %f %d %d", o.dynamics, o.tempo, o.audio);
    MsBeatOpts n = {NAN, -3, -9};
    ms_beat_opts_clamp(&n);
    CHECK(n.dynamics == 0.f && n.tempo == 60 && n.audio == 0, "adapter opts NaN clamp: %f %d %d", n.dynamics, n.tempo, n.audio);
    printf("adapter opts clamp: ok\n");
}

int main(void) {
    test_property_bounds();
    test_dynamics_zero();
    test_adapter_determinism();
    test_opts();
    if (failures) {
        printf("magmasimplex_beat: %d FAILURES\n", failures);
        return 1;
    }
    printf("magmasimplex_beat: all gates passed\n");
    return 0;
}
