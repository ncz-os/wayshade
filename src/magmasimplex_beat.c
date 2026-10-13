/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_beat.c - see magmasimplex_beat.h. The adapter only reads the
 * generic NczAudioSignals and maps them, per view mode, into the bounded
 * drives the goo shader consumes. No GL, no sim, no capture here. */
#include "magmasimplex_beat.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float v, float a, float b) {
    if (!(v == v)) return a;
    return v < a ? a : v > b ? b : v;
}
static float clamp01(float v) { return clampf(v, 0.f, 1.f); }
static float fmaxf2(float a, float b) { return a > b ? a : b; }

void ms_beat_opts_clamp(MsBeatOpts *o) {
    if (!o) return;
    o->dynamics = clamp01(o->dynamics);
    if (o->tempo != 0) o->tempo = (int)clampf((float)o->tempo, 60.f, 180.f);
    o->audio = (int)clampf((float)o->audio, 0.f, 2.f);
}

/* ------------------------------------------------------------------ */
/* The mapping table. Every drive is multiplied by dynamics and clamped  */
/* to its documented bound; dynamics == 0 yields exactly 0.             */
/* ------------------------------------------------------------------ */
void ms_beat_map_signals(int mode, int world, float dynamics, const NczAudioSignals *sig, MsBeatDrive *out) {
    memset(out, 0, sizeof *out);
    float dyn = clamp01(dynamics);
    if (!sig) return;
    out->breakdown = sig->breakdown;
    out->audio_live = sig->audio_live;
    out->bpm = sig->bpm;
    out->phase = clamp01(sig->beat_phase);
    if (dyn <= 0.f) return;

    float beat = clamp01(sig->kick);
    float snare = clamp01(sig->snare);
    float bass = clamp01(sig->bass);
    float mid = clamp01(sig->mid);
    float onset = clamp01(sig->onset_env);

    float squeeze = 0.f, breath = 0.f, burst = 0.f, glow = 0.f, flow = 0.f, bloom = 0.f, radius = 0.f;
    switch (mode) {
    case MS_BEAT_MODE_LAVA_GLASS:
        /* the wax is pressed on the glass: a gentler squeeze, a stronger glow */
        squeeze = 0.35f * beat + 0.20f * onset + 0.10f * bass;
        breath = 0.50f + 0.35f * bass;
        burst = 0.35f * snare + 0.25f * onset;
        glow = 0.70f * beat + 0.30f * bass;
        flow = 0.30f * beat + 0.20f * onset;
        bloom = 0.70f * mid;
        radius = 2.f * beat - 1.f;
        break;
    case MS_BEAT_MODE_SHOW:
        /* the ink light show: the projector pulse and dye movement */
        squeeze = beat;
        breath = 0.40f + 0.50f * bass + 0.10f * mid;
        burst = fmaxf2(snare, onset);
        glow = 0.50f * beat;
        flow = 0.30f * beat;
        bloom = mid;
        radius = 2.f * beat - 1.f;
        break;
    case MS_BEAT_MODE_BUTTERFLY:
        /* Flowerchild: the dye breathes and the droplets burst on the beat */
        squeeze = 0.60f * beat + 0.40f * onset;
        breath = 0.35f + 0.45f * bass + 0.20f * mid;
        burst = fmaxf2(0.7f * snare + 0.3f * onset, 0.5f * beat);
        glow = 0.55f * beat + 0.20f * bass;
        flow = 0.40f * beat + 0.20f * onset;
        bloom = 0.8f * mid;
        radius = 2.f * beat - 1.f;
        break;
    case MS_BEAT_MODE_LAVA_INSIDE:
    case MS_BEAT_MODE_LAVA_FIELD:
    default:
        /* the classic lamp: heat/buoyancy rises with bass, onsets pulse the
         * emitter, mids modulate the bloom */
        squeeze = 0.60f * beat + 0.30f * onset + 0.10f * bass;
        breath = 0.45f + 0.45f * bass + 0.10f * mid;
        burst = 0.50f * snare + 0.30f * onset;
        glow = 0.60f * beat + 0.40f * bass;
        flow = 0.60f * beat + 0.40f * onset;
        bloom = mid;
        radius = 2.f * beat - 1.f;
        break;
    }

    /* World nudges, always kept inside the same bounds. */
    if (world == MS_BEAT_WORLD_RADIAL) squeeze *= 1.20f;
    if (world == MS_BEAT_WORLD_DISH) flow *= 0.70f;
    if (world == MS_BEAT_WORLD_DRIFT) flow *= 0.90f;

    out->squeeze = dyn * clamp01(squeeze);
    out->breath = dyn * clamp01(breath);
    out->burst = dyn * clamp01(burst);
    out->glow = dyn * clamp01(glow);
    out->flow = dyn * clamp01(flow);
    out->bloom = dyn * clamp01(bloom);
    out->radius = dyn * 0.04f * clampf(radius, -1.f, 1.f);
    out->beat = dyn * beat;
    out->snare = dyn * snare;
    out->bass = dyn * bass;
    out->mid = dyn * mid;
}

/* ------------------------------------------------------------------ */
/* MsBeat wraps a bus and a mode                                        */
/* ------------------------------------------------------------------ */
struct MsBeat {
    MsBeatOpts        opts;
    NczAudioBus      *bus;
    int               mode;
    int               world;
    MsBeatDrive       drive;
};

MsBeat *ms_beat_create(uint32_t seed, const MsBeatOpts *o) {
    MsBeat *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (o) b->opts = *o;
    ms_beat_opts_clamp(&b->opts);
    NczAudioOpts ao;
    ao.dynamics = b->opts.dynamics;
    ao.tempo = b->opts.tempo;
    ao.audio = b->opts.audio;
    b->bus = ncz_audio_bus_create(seed, &ao);
    b->mode = MS_BEAT_MODE_LAVA_INSIDE;
    b->world = MS_BEAT_WORLD_TANK;
    if (!b->bus) { free(b); return NULL; }
    return b;
}

void ms_beat_destroy(MsBeat *b) {
    if (!b) return;
    ncz_audio_bus_destroy(b->bus);
    free(b);
}

void ms_beat_set_mode(MsBeat *b, int mode, int world) {
    if (!b) return;
    if (mode < 0 || mode >= MS_BEAT_MODE_COUNT) mode = MS_BEAT_MODE_LAVA_INSIDE;
    if (world < 0 || world > MS_BEAT_WORLD_DRIFT) world = MS_BEAT_WORLD_TANK;
    b->mode = mode;
    b->world = world;
}

void ms_beat_update(MsBeat *b, float dt) {
    if (!b) return;
    ncz_audio_bus_update(b->bus, dt);
    ms_beat_map_signals(b->mode, b->world, b->opts.dynamics, ncz_audio_bus_signals(b->bus), &b->drive);
}

const MsBeatDrive *ms_beat_drive(const MsBeat *b) {
    static const MsBeatDrive zero;
    return b ? &b->drive : &zero;
}
float ms_beat_bpm(const MsBeat *b) { return b ? b->drive.bpm : 0.f; }
int   ms_beat_audio_live(const MsBeat *b) { return b ? ncz_audio_bus_audio_live(b->bus) : 0; }
void  ms_beat_set_red_heavy(MsBeat *b, int r) { if (b) ncz_audio_bus_set_red_heavy(b->bus, r); }
const char *ms_beat_audio_status(const MsBeat *b) { return b ? ncz_audio_bus_status(b->bus) : "off"; }
