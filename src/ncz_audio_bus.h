/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_audio_bus.h - the shared, saver-agnostic audio/beat engine.
 *
 * One small C API any screensaver can adopt (docs/AUDIO-BUS.md): a running
 * structural clock that needs no audio at all (the "virtual beat") plus an
 * optional PipeWire capture with a 1024-point FFT, band energies, spectral-
 * flux onset detection, automatic gain, a tempo estimate that keeps the
 * virtual clock phase-locked, and a always-on photosensitive-safety limiter.
 *
 * Nothing here is magmasimplex-specific and nothing here touches GL: the whole
 * engine is pure C. The magmasimplex adapter (magmasimplex_beat.c) only reads
 * the `NczAudioSignals` struct this module produces. Tests drive the analyzer
 * and the limiter directly (tests/test_ncz_audio_bus.c).
 *
 * Two rhythm sources:
 *   1. the deterministic virtual beat, seeded by the run seed: BPM drawn in
 *      96..132 with slow drift and swing, 4/4 with pattern variations and an
 *      occasional half-bar breakdown every 16..32 bars, producing kick/snare
 *      impulses with attack/decay envelopes. Same seed -> same timeline.
 *   2. optional real audio (opts.audio 1 = system output monitor, 2 = mic).
 *      Capture is build-gated (NCZ_AUDIO_CAPTURE); without it, or when capture
 *      fails or goes silent, the engine falls back to the virtual beat quietly
 *      with one log line.
 *
 * dynamics == 0 is special: the engine still runs, but every drive the adapter
 * reads is exactly 0, so a consumer's beat terms vanish and its frame is
 * byte-identical to the pre-beat renderer.
 */
#ifndef NCZ_AUDIO_BUS_H
#define NCZ_AUDIO_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Options                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float dynamics; /* 0..1; 0 = no drives at all */
    int   tempo;    /* 0 = auto (draw 96..132), else 60..180 BPM */
    int   audio;    /* 0 off, 1 system output monitor, 2 microphone */
} NczAudioOpts;

/* Clamp every field to its range. Safe on NaN. */
void ncz_audio_opts_clamp(NczAudioOpts *o);

/* ------------------------------------------------------------------ */
/* Signals a consumer reads once per frame                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float bass, mid, treble; /* 0..1 AGC-normalised band energy */
    float onset_env;         /* 0..1 spectral-flux onset envelope */
    float beat_phase;        /* 0..1 phase within the current beat */
    float bpm;               /* current (drifting) tempo */
    float kick;              /* 0..1 limited envelope of the last kick */
    float snare;             /* 0..1 limited envelope of the last snare */
    float level;             /* 0..1 limited smoothed loudness */
    float drive;             /* 0..1 limited overall drive (breathing) */
    int   onset;             /* 1 on the frame an onset was detected */
    int   breakdown;         /* 1 during a half-bar drop */
    int   active;            /* 1 when the bus is producing drives */
    int   audio_live;        /* 1 when real audio is currently driving it */
} NczAudioSignals;

/* ------------------------------------------------------------------ */
/* Audio analysis (always compiled; pure, deterministic, testable)      */
/* ------------------------------------------------------------------ */
#define NCZ_AUDIO_FFT  1024   /* analysis window, samples */
#define NCZ_AUDIO_HOP  256    /* onset resolution, samples (~11.6 ms) */
#define NCZ_AUDIO_RATE 22050  /* mono sample rate */

typedef struct {
    float bass, mid, treble; /* AGC-normalised band energy, 0..1 */
    float flux;              /* AGC-normalised spectral flux, 0..1 */
    float level;             /* smoothed RMS level, 0..1 */
    int   onset;             /* 1 on the frame an onset was detected */
} NczAudioAnalysis;

typedef struct NczAudioAnalyzer NczAudioAnalyzer;

NczAudioAnalyzer *ncz_audio_analyzer_create(void);
void              ncz_audio_analyzer_destroy(NczAudioAnalyzer *a);
/* Feed n mono samples in roughly [-1,1]. Frames are processed when a full
 * window is available; leftover samples are buffered. */
void              ncz_audio_analyzer_feed(NczAudioAnalyzer *a, const float *mono, int n);
NczAudioAnalysis  ncz_audio_analyzer_state(const NczAudioAnalyzer *a);
/* Monotonic count of onsets detected since creation (for tests). */
unsigned          ncz_audio_analyzer_onsets(const NczAudioAnalyzer *a);
/* Sample index (since creation) of the i-th onset, 0-based; 0 if out of range. */
unsigned long     ncz_audio_analyzer_onset_index(const NczAudioAnalyzer *a, unsigned i);
void              ncz_audio_analyzer_reset(NczAudioAnalyzer *a);

/* Tempo estimate from the onset envelope: 0..1 confidence, *bpm filled. */
float ncz_audio_analyzer_tempo(const NczAudioAnalyzer *a, float *bpm);

/* ------------------------------------------------------------------ */
/* Photosensitive-safety limiter (always compiled; pure, testable)      */
/* ------------------------------------------------------------------ */
typedef struct {
    float  prev;       /* previous limited value */
    double last;       /* time of the previous call */
    double win_start;  /* start of the current one-second flash window */
    int    flashes;    /* flashes counted in the current window */
    int    red_heavy;  /* damp red-heavy saturated flashes harder */
    int    started;
} NczBeatLimiter;

void  ncz_beat_limiter_init(NczBeatLimiter *l);
/* Limit a raw drive in [0,1]. `now` is monotonic seconds. The returned value
 * cannot change by more than about 25 % of its previous level per beat, at
 * most 3 flashes are counted per second, and a red-heavy source is damped. */
float ncz_beat_limiter_apply(NczBeatLimiter *l, float raw, double now);
void  ncz_beat_limiter_set_red_heavy(NczBeatLimiter *l, int red_heavy);

/* ------------------------------------------------------------------ */
/* Beat engine                                                          */
/* ------------------------------------------------------------------ */
typedef struct NczAudioBus NczAudioBus;

/* seed: the run seed (same seed -> same virtual timeline). o may be NULL. */
NczAudioBus *ncz_audio_bus_create(uint32_t seed, const NczAudioOpts *o);
void         ncz_audio_bus_destroy(NczAudioBus *b);

/* Call once per frame with the wall-clock delta in seconds. Clamped to
 * [0, 0.25] so a stalled frame never jumps the clock. Safe with no audio:
 * the virtual clock always runs. */
void                   ncz_audio_bus_update(NczAudioBus *b, float dt);
const NczAudioSignals *ncz_audio_bus_signals(const NczAudioBus *b);

/* The tempo the virtual clock is running at, for tests/diagnostics. */
float ncz_audio_bus_bpm(const NczAudioBus *b);
/* 1 if a real audio capture is currently feeding the engine. */
int   ncz_audio_bus_audio_live(const NczAudioBus *b);
/* Mark the consumer's palette as red-heavy so the limiter damps its flashes. */
void  ncz_audio_bus_set_red_heavy(NczAudioBus *b, int red_heavy);
/* Capture status for logging, e.g. "system via pw-record" or "off". */
const char *ncz_audio_bus_status(const NczAudioBus *b);

#ifdef __cplusplus
}
#endif
#endif
