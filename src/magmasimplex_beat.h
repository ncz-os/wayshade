/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_beat.h - the magmasimplex adapter over the shared audio bus.
 *
 * This file is magmasimplex-specific: it turns the generic signals of
 * ncz_audio_bus.h into the small set of bounded drives the goo shader reads,
 * with one mapping per view mode (docs/AUDIO-BUS.md has the table). The engine
 * itself (capture, FFT, onset/tempo, virtual beat, limiter) lives in
 * ncz_audio_bus.c and knows nothing about this saver.
 *
 * dynamics == 0 is special: every drive is exactly 0.0, so the shader's beat
 * terms vanish and the frame is identical to the pre-beat renderer.
 *
 * Documented drive bounds (the property test asserts these for any inputs):
 *   squeeze, breath, burst, glow, flow, bloom  in [0, 1]
 *   radius                                     in [-0.04, 0.04]
 *   beat, snare, bass, mid, phase              in [0, 1]
 */
#ifndef MAGMASIMPLEX_BEAT_H
#define MAGMASIMPLEX_BEAT_H

#include <stdint.h>
#include "ncz_audio_bus.h"

/* ------------------------------------------------------------------ */
/* Options                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float dynamics; /* 0..1; 0 = today's behaviour exactly */
    int   tempo;    /* 0 = auto (draw 96..132), else 60..180 BPM */
    int   audio;    /* 0 off, 1 system monitor, 2 microphone */
} MsBeatOpts;

/* Clamp every field to its range (dynamics 0..1, tempo 0 or 60..180,
 * audio 0..2). Safe on NaN. */
void ms_beat_opts_clamp(MsBeatOpts *o);

/* ------------------------------------------------------------------ */
/* Per-mode mapping                                                     */
/* ------------------------------------------------------------------ */
/* The lamp (lava) views, the ink light show and the Flowerchild dye show.
 * The world is orthogonal and only nudges the mapping: radial lamps take a
 * bigger squeeze, dish worlds take the bounded radius pulse. */
typedef enum {
    MS_BEAT_MODE_LAVA_INSIDE = 0,
    MS_BEAT_MODE_LAVA_GLASS = 1,
    MS_BEAT_MODE_LAVA_FIELD = 2,
    MS_BEAT_MODE_SHOW = 3,
    MS_BEAT_MODE_BUTTERFLY = 4,
    MS_BEAT_MODE_COUNT = 5
} MsBeatMode;

enum { MS_BEAT_WORLD_TANK = 0, MS_BEAT_WORLD_DISH = 1, MS_BEAT_WORLD_RADIAL = 2, MS_BEAT_WORLD_DRIFT = 3 };

/* ------------------------------------------------------------------ */
/* State the driver pushes into the shader                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float squeeze; /* 0..1 radial squeeze ripple from the dish centre */
    float breath;  /* 0..1 saturation / brightness breathing */
    float burst;   /* 0..1 droplet burst / glow pulse */
    float glow;    /* 0..1 onset glow on the emitter */
    float flow;    /* 0..1 flow-velocity burst in the dye / wax field */
    float bloom;   /* 0..1 mid-band bloom */
    float radius;  /* -0.04..0.04 bounded dish-radius pulse */
    float beat;    /* 0..1 limited kick envelope */
    float snare;   /* 0..1 limited snare envelope */
    float bass;    /* 0..1 limited low band */
    float mid;     /* 0..1 limited mid band */
    float phase;   /* 0..1 beat phase */
    float bpm;     /* the current (drifting) tempo */
    int   breakdown;  /* 1 during a half-bar drop */
    int   audio_live; /* 1 when real audio is currently driving the clock */
} MsBeatDrive;

typedef struct MsBeat MsBeat;

/* seed: the run seed (same seed -> same virtual timeline). o may be NULL. */
MsBeat *ms_beat_create(uint32_t seed, const MsBeatOpts *o);
void    ms_beat_destroy(MsBeat *b);

/* Pick the mapping. mode is MsBeatMode; world is MS_BEAT_WORLD_*. */
void    ms_beat_set_mode(MsBeat *b, int mode, int world);

/* Call once per frame with the wall-clock delta in seconds. */
void    ms_beat_update(MsBeat *b, float dt);
const MsBeatDrive *ms_beat_drive(const MsBeat *b);

float   ms_beat_bpm(const MsBeat *b);
int     ms_beat_audio_live(const MsBeat *b);
void    ms_beat_set_red_heavy(MsBeat *b, int red_heavy);
const char *ms_beat_audio_status(const MsBeat *b);

/* For diagnostics/tests: map a raw signal set through a mode without a bus. */
void ms_beat_map_signals(int mode, int world, float dynamics, const NczAudioSignals *sig, MsBeatDrive *out);

#endif
