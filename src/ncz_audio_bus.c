/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_audio_bus.c - see ncz_audio_bus.h. Pure C, no GL.
 *
 * Layout:
 *   1. small xorshift RNG
 *   2. radix-2 FFT (own; no dependency)
 *   3. NczAudioAnalyzer: window, band energies, AGC, spectral flux onset
 *   4. NczBeatLimiter: photosensitive-safety slew + flash cap
 *   5. virtual beat clock (deterministic, seeded)
 *   6. NczAudioBus: ties the clock, the optional capture and the limiter
 */
#define _POSIX_C_SOURCE 200809L
#include "ncz_audio_bus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float clampf(float v, float a, float b) {
    if (!(v == v)) return a; /* NaN */
    return v < a ? a : v > b ? b : v;
}
static float clamp01(float v) { return clampf(v, 0.f, 1.f); }

/* ------------------------------------------------------------------ */
/* 1. RNG (matches the rest of the savers: xorshift32)                  */
/* ------------------------------------------------------------------ */
static uint32_t xs(uint32_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}
static float u01(uint32_t *s) { return (float)(xs(s) & 0xffffffu) / 16777216.f; }

/* ------------------------------------------------------------------ */
/* 2. FFT: iterative radix-2, in place, forward                           */
/* ------------------------------------------------------------------ */
static void fft_radix2(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                float ur = re[a], ui = im[a];
                float vr = re[b] * cr - im[b] * ci;
                float vi = re[b] * ci + im[b] * cr;
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3. Analyzer                                                           */
/* ------------------------------------------------------------------ */
#define NBINS (NCZ_AUDIO_FFT / 2)
#define FHIST 128 /* onset-envelope history for the tempo estimate */

struct NczAudioAnalyzer {
    float win[NCZ_AUDIO_FFT];
    float buf[NCZ_AUDIO_FFT];
    int   fill;
    float re[NCZ_AUDIO_FFT], im[NCZ_AUDIO_FFT];
    float prevmag[NBINS];
    int   have_prev;

    /* AGC running energy maxima (slow decay) */
    float agc_bass, agc_mid, agc_treble;
    float bass, mid, treble;

    /* adaptive onset threshold over the flux history */
    float flux;      /* current normalised flux */
    float flux_mean, flux_var;
    float onset_env;
    int   refractory;

    float level;     /* smoothed RMS, 0..1 */

    /* onset + tempo history */
    float fhist[FHIST];
    int   fh;        /* next slot */
    int   fh_n;
    unsigned onset_count;
    unsigned long onset_idx[FHIST];
    unsigned long samples; /* samples consumed since creation */

    NczAudioAnalysis out;
};

static void analyze_frame(NczAudioAnalyzer *a) {
    /* Hann window. */
    for (int i = 0; i < NCZ_AUDIO_FFT; i++) {
        a->re[i] = a->buf[i] * a->win[i];
        a->im[i] = 0.f;
    }
    /* Level (RMS of the raw window). */
    float rms = 0.f;
    for (int i = 0; i < NCZ_AUDIO_FFT; i++) rms += a->buf[i] * a->buf[i];
    rms = sqrtf(rms / (float)NCZ_AUDIO_FFT);
    a->level += (clamp01(rms * 3.f) - a->level) * 0.25f;

    fft_radix2(a->re, a->im, NCZ_AUDIO_FFT);

    float mag[NBINS];
    for (int i = 0; i < NBINS; i++) mag[i] = sqrtf(a->re[i] * a->re[i] + a->im[i] * a->im[i]);

    /* Band energies (sum of power). Bins: bass 20-250 Hz, mid 250-2000,
     * treble 2000-8000 (Nyquist is 11025). */
    float eb = 0.f, em = 0.f, et = 0.f;
    int b0 = 1, b1 = 12, m1 = 93, t1 = 372;
    for (int i = b0; i < b1 && i < NBINS; i++) eb += mag[i] * mag[i];
    for (int i = b1; i < m1 && i < NBINS; i++) em += mag[i] * mag[i];
    for (int i = m1; i < t1 && i < NBINS; i++) et += mag[i] * mag[i];

    /* AGC: track a slowly decaying maximum, normalise, then sqrt for a
     * perceptually useful 0..1. Silence stays at 0. */
    a->agc_bass = fmaxf(eb, a->agc_bass * 0.997f);
    a->agc_mid = fmaxf(em, a->agc_mid * 0.997f);
    a->agc_treble = fmaxf(et, a->agc_treble * 0.997f);
    a->bass = clamp01(sqrtf(eb / (a->agc_bass + 1e-9f)));
    a->mid = clamp01(sqrtf(em / (a->agc_mid + 1e-9f)));
    a->treble = clamp01(sqrtf(et / (a->agc_treble + 1e-9f)));

    /* Spectral flux (positive differences), normalised by the total
     * magnitude of both frames so it stays in [0,1] and a first frame of
     * silence cannot produce an astronomical spike. */
    float flux = 0.f, cursum = 0.f, prevsum = 0.f;
    for (int i = 0; i < NBINS; i++) { cursum += mag[i]; prevsum += a->prevmag[i]; }
    if (a->have_prev) {
        for (int i = 0; i < NBINS; i++) {
            float d = mag[i] - a->prevmag[i];
            if (d > 0.f) flux += d;
        }
    } else {
        a->have_prev = 1;
    }
    flux /= (cursum + prevsum + 1e-6f);
    for (int i = 0; i < NBINS; i++) a->prevmag[i] = mag[i];

    /* Adaptive threshold: a slowly adapting mean of the flux, with a floor.
     * (A mean+sigma rule was tried first; the first loud onset inflated the
     * variance and masked all later identical onsets.) */
    float thr = a->flux_mean * 2.0f + 0.06f;
    a->flux_mean += (flux - a->flux_mean) * 0.03f;

    if (a->refractory > 0) a->refractory--;
    a->out.onset = 0;
    if (a->have_prev && a->refractory == 0 && flux > thr && flux > 0.16f) {
        a->out.onset = 1;
        a->refractory = 4; /* ~46 ms */
        a->onset_idx[a->onset_count % FHIST] = a->samples;
        a->onset_count++;
        a->onset_env = 1.f;
    }
    a->flux = flux;
    /* onset envelope: fast attack, slow release. */
    a->onset_env = fmaxf(a->onset_env * expf(-(float)NCZ_AUDIO_HOP / NCZ_AUDIO_RATE / 0.12f),
                         a->out.onset ? 1.f : 0.f);
    if (a->onset_env < 0.f) a->onset_env = 0.f;

    /* History for the tempo autocorrelation. */
    a->fhist[a->fh] = clamp01(flux * 8.f);
    a->fh = (a->fh + 1) % FHIST;
    if (a->fh_n < FHIST) a->fh_n++;

    a->out.bass = a->bass;
    a->out.mid = a->mid;
    a->out.treble = a->treble;
    a->out.flux = clamp01(flux * 8.f);
    a->out.level = a->level;
}

NczAudioAnalyzer *ncz_audio_analyzer_create(void) {
    NczAudioAnalyzer *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    for (int i = 0; i < NCZ_AUDIO_FFT; i++)
        a->win[i] = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)(NCZ_AUDIO_FFT - 1));
    return a;
}
void ncz_audio_analyzer_destroy(NczAudioAnalyzer *a) { free(a); }

void ncz_audio_analyzer_feed(NczAudioAnalyzer *a, const float *mono, int n) {
    if (!a || !mono) return;
    a->samples += (unsigned long)n;
    int i = 0;
    while (i < n) {
        while (i < n && a->fill < NCZ_AUDIO_FFT) a->buf[a->fill++] = mono[i++];
        if (a->fill == NCZ_AUDIO_FFT) {
            analyze_frame(a);
            /* overlap: drop one hop, keep the rest */
            memmove(a->buf, a->buf + NCZ_AUDIO_HOP, (NCZ_AUDIO_FFT - NCZ_AUDIO_HOP) * sizeof(float));
            a->fill = NCZ_AUDIO_FFT - NCZ_AUDIO_HOP;
        }
    }
    a->out.bass = a->bass;
    a->out.mid = a->mid;
    a->out.treble = a->treble;
    a->out.level = a->level;
}

NczAudioAnalysis ncz_audio_analyzer_state(const NczAudioAnalyzer *a) {
    NczAudioAnalysis z;
    memset(&z, 0, sizeof z);
    if (a) z = a->out;
    return z;
}
unsigned ncz_audio_analyzer_onsets(const NczAudioAnalyzer *a) { return a ? a->onset_count : 0; }
unsigned long ncz_audio_analyzer_onset_index(const NczAudioAnalyzer *a, unsigned i) {
    if (!a || i >= a->onset_count) return 0;
    return a->onset_idx[i % FHIST];
}
void ncz_audio_analyzer_reset(NczAudioAnalyzer *a) {
    if (!a) return;
    float win[NCZ_AUDIO_FFT];
    memcpy(win, a->win, sizeof win);
    memset(a, 0, sizeof *a);
    memcpy(a->win, win, sizeof win);
}

/* Tempo from the onset envelope: frame rate is RATE/HOP ~ 86.13 fps. */
float ncz_audio_analyzer_tempo(const NczAudioAnalyzer *a, float *bpm) {
    if (bpm) *bpm = 0.f;
    if (!a || a->fh_n < 32) return 0.f;
    const float fps = (float)NCZ_AUDIO_RATE / (float)NCZ_AUDIO_HOP;
    /* lags for 60..180 BPM: period 1.0..0.333 s -> 86..29 frames. */
    int lag_lo = 28, lag_hi = 87;
    if (lag_hi > a->fh_n - 4) lag_hi = a->fh_n - 4;
    if (lag_hi <= lag_lo) return 0.f;
    /* Mean of the history. */
    float mean = 0.f;
    for (int i = 0; i < a->fh_n; i++) mean += a->fhist[i];
    mean /= (float)a->fh_n;
    float best = -1e9f, best_lag = 0.f, sum = 0.f;
    int nb = 0;
    for (int lag = lag_lo; lag <= lag_hi; lag++) {
        float c = 0.f;
        for (int i = lag; i < a->fh_n; i++) c += (a->fhist[i] - mean) * (a->fhist[i - lag] - mean);
        c /= (float)(a->fh_n - lag);
        sum += c; nb++;
        if (c > best) { best = c; best_lag = (float)lag; }
    }
    float avg = nb ? sum / (float)nb : 0.f;
    if (best_lag <= 0.f) return 0.f;
    float b = 60.f * fps / best_lag;
    if (bpm) *bpm = b;
    /* Confidence: peak above the mean of the correlation. */
    float conf = best > 0.f ? (best - avg) / (best + 1e-9f) : 0.f;
    return clamp01(conf);
}

/* ------------------------------------------------------------------ */
/* 4. Limiter                                                           */
/* ------------------------------------------------------------------ */
void ncz_beat_limiter_init(NczBeatLimiter *l) {
    memset(l, 0, sizeof *l);
    l->prev = 0.f;
}
void ncz_beat_limiter_set_red_heavy(NczBeatLimiter *l, int r) { if (l) l->red_heavy = r ? 1 : 0; }

float ncz_beat_limiter_apply(NczBeatLimiter *l, float raw, double now) {
    if (!l) return clamp01(raw);
    raw = clamp01(raw);
    if (!l->started) {
        l->started = 1;
        l->prev = raw;
        l->last = now;
        l->win_start = now;
        l->flashes = 0;
        return raw;
    }
    double dt = now - l->last;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;
    l->last = now;

    /* Allow at most ~25 % of the previous level per beat; at 180 BPM one beat
     * is 1/3 s, so the slew rate is 3 * 0.25 = 0.75 per second, scaled by the
     * level (with a floor so a rise from 0 is still possible). */
    float cap = 0.25f * (0.20f + 0.80f * l->prev);
    float allowed = cap * (float)(dt * 3.0);
    float out = l->prev + clampf(raw - l->prev, -allowed, allowed);

    /* Flash cap: a rising edge of at least 0.10 counts as a flash; at most 3
     * per one-second window. */
    if (now - l->win_start >= 1.0) {
        l->win_start = now;
        l->flashes = 0;
    }
    float rise = out - l->prev;
    if (rise > 0.10f) {
        if (l->flashes >= 3) {
            out = l->prev; /* hold: no more flashes this second */
        } else {
            l->flashes++;
        }
    }
    /* Red-heavy saturated flashes are damped to 60 % of the intended rise. */
    if (l->red_heavy && out > l->prev) out = l->prev + (out - l->prev) * 0.6f;

    l->prev = clamp01(out);
    return l->prev;
}

/* ------------------------------------------------------------------ */
/* 5. Virtual beat clock                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t rng;
    float bpm0, drift_amp, drift_period, drift_phase;
    float swing;
    float phase;         /* 0..1 within the beat */
    int   sub;           /* 0..3 within the beat (16ths) */
    int   step16;        /* 0..15 within the bar */
    int   bar;           /* bar index */
    int   beat_in_bar;   /* 0..3 */
    int   bar_len;       /* bars per breakdown cycle (16..31) */
    int   pattern;       /* 0..2 pattern variation */
    float kick_amp, snare_amp;
    int   breakdown;
    double vt;           /* virtual time, seconds */
} VBeat;

static void vbeat_init(VBeat *v, uint32_t seed) {
    memset(v, 0, sizeof *v);
    v->rng = seed ? seed * 2654435761u + 12345u : 0x9E3779B9u;
    v->bpm0 = 96.f + u01(&v->rng) * 36.f;               /* 96..132 */
    v->drift_amp = 1.0f + u01(&v->rng) * 1.5f;          /* <= 2.5 BPM */
    v->drift_period = 40.f + u01(&v->rng) * 80.f;
    v->drift_phase = u01(&v->rng) * 6.2831853f;
    v->swing = 0.05f + u01(&v->rng) * 0.10f;
    v->bar_len = 16 + (int)(u01(&v->rng) * 16.f);       /* 16..31 */
    v->pattern = (int)(u01(&v->rng) * 3.f);             /* 0..2 */
}

/* Pattern tables: kick / snare per 16th of the bar. Variations differ. */
static void pattern_hits(const VBeat *v, int pat, int step, int *kick, int *snare) {
    (void)v;
    *kick = 0; *snare = 0;
    switch (pat) {
    case 0: /* four-on-the floor with a snare backbeat */
        if (step == 0 || step == 4 || step == 8 || step == 12) *kick = 1;
        if (step == 4 || step == 12) *snare = 1;
        break;
    case 1: /* kick 1 and 3, snare 2 and 4, a pushed kick */
        if (step == 0 || step == 8 || step == 10) *kick = 1;
        if (step == 4 || step == 12) *snare = 1;
        break;
    default: /* sparse kick, syncopated snare */
        if (step == 0 || step == 6 || step == 11) *kick = 1;
        if (step == 4 || step == 12 || step == 14) *snare = 1;
        break;
    }
}

static float vbeat_bpm(const VBeat *v) {
    float b = v->bpm0 + v->drift_amp * sinf((float)(2.0 * M_PI * v->vt / v->drift_period) + v->drift_phase);
    return clampf(b, 93.f, 135.f);
}

static void vbeat_advance(VBeat *v, float dt) {
    v->vt += dt;
    float bpm = vbeat_bpm(v);
    float period = 60.f / bpm;
    v->phase += dt / period;
    while (v->phase >= 1.f) { v->phase -= 1.f; v->beat_in_bar = (v->beat_in_bar + 1) & 3; if (v->beat_in_bar == 0) v->bar++; }
    /* swing: delay the off-16ths by a fraction of a 16th. */
    float ph = v->phase;
    int sub = (int)(ph * 4.f);
    if (sub >= 4) sub = 3;
    if ((sub & 1) && v->swing > 0.f) {
        float shifted = ph - v->swing * 0.25f;
        int s2 = (int)(shifted * 4.f);
        if (s2 >= 4) s2 = 3;
        if (s2 >= 0) sub = s2;
    }
    if (sub != v->sub) {
        v->sub = sub;
        v->step16 = (v->beat_in_bar * 4 + sub) & 15;
        /* breakdown: one half-bar drop every bar_len bars (the last bar). */
        v->breakdown = ((v->bar + 1) % v->bar_len == 0);
        int k = 0, s = 0;
        int bk = 0;
        if (v->breakdown) {
            /* first half bar: almost silent, one soft kick; second half: build. */
            if (v->beat_in_bar == 0 && sub == 0) { k = 1; bk = 1; }
            if (v->beat_in_bar == 2 && (sub == 0 || sub == 2)) s = 1;
            if (v->beat_in_bar == 3) { s = 1; if (sub == 2) k = 1; }
        } else {
            pattern_hits(v, v->pattern, v->step16, &k, &s);
        }
        if (k) v->kick_amp = bk ? 0.5f : 1.0f;
        if (s) v->snare_amp = 0.85f;
    }
    /* envelopes: exponential decay (attack is supplied by the limiter slew). */
    v->kick_amp *= expf(-dt / 0.20f);
    v->snare_amp *= expf(-dt / 0.13f);
    if (v->kick_amp < 1e-4f) v->kick_amp = 0.f;
    if (v->snare_amp < 1e-4f) v->snare_amp = 0.f;
}

/* ------------------------------------------------------------------ */
/* Optional capture (build-gated; no link dependency, uses a child       */
/* process so the saver builds and runs without PipeWire).              */
/* ------------------------------------------------------------------ */
typedef struct {
    FILE *pipe;
    int   live;
    int   failed;
    int   logged;
    float last_data;
    char  status[64];
} Capture;

#ifdef NCZ_AUDIO_CAPTURE
#include <fcntl.h>
#include <unistd.h>

static FILE *capture_open(const NczAudioOpts *o, char *status, size_t sn) {
    const char *sys_cmd = "parec --format=s16le --rate=22050 --channels=1 --device=@DEFAULT_MONITOR@ - 2>/dev/null";
    const char *fallback = "pw-record --rate 22050 --channels 1 --format s16 - 2>/dev/null";
    const char *cmd;
    if (o->audio == 1) {
        cmd = sys_cmd;
        snprintf(status, sn, "system via parec");
    } else {
        cmd = fallback;
        snprintf(status, sn, "mic via pw-record");
    }
    FILE *p = popen(cmd, "r");
    /* If parec is missing popen still succeeds and the shell errors; the read
     * will return EOF and we fall back. */
    return p;
}
#endif

static void capture_poll(Capture *c, NczAudioAnalyzer *a, float dt) {
    if (!c->pipe || c->failed) return;
#ifdef NCZ_AUDIO_CAPTURE
    int fd = fileno(c->pipe);
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0 && !(fl & O_NONBLOCK)) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int16_t buf[2048];
    int got = 0;
    for (;;) {
        ssize_t r = read(fd, buf + got, (sizeof buf) - (size_t)got * 2);
        if (r <= 0) break;
        got += (int)(r / 2);
        if (got >= (int)(sizeof buf / 2)) break;
    }
    if (got > 0) {
        float mono[2048];
        for (int i = 0; i < got; i++) mono[i] = (float)buf[i] * (1.f / 32768.f);
        ncz_audio_analyzer_feed(a, mono, got);
        c->last_data += dt;
        if (!c->live) { c->live = 1; }
    } else {
        c->last_data += dt;
    }
    if (feof(c->pipe) || c->last_data > 4.f) {
        c->failed = 1;
        c->live = 0;
    }
#else
    (void)a; (void)dt; (void)c;
#endif
}

/* ------------------------------------------------------------------ */
/* 6. Bus                                                               */
/* ------------------------------------------------------------------ */
struct NczAudioBus {
    NczAudioOpts   opts;
    NczAudioSignals sig;
    VBeat          vb;
    NczAudioAnalyzer *an;
    Capture        cap;
    NczBeatLimiter lim_kick, lim_snare, lim_drive;
    int   red_heavy;
    float bass, mid, treble, onset_env, level;
    float kick, snare, drive;
    float est_bpm, est_conf;
    int   live;
    int   fallback_logged;
    float silent;
};

void ncz_audio_opts_clamp(NczAudioOpts *o) {
    if (!o) return;
    o->dynamics = clamp01(o->dynamics);
    if (!(o->tempo == 0)) o->tempo = (int)clampf((float)o->tempo, 60.f, 180.f);
    o->audio = (int)clampf((float)o->audio, 0.f, 2.f);
}

NczAudioBus *ncz_audio_bus_create(uint32_t seed, const NczAudioOpts *o) {
    NczAudioBus *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (o) b->opts = *o;
    ncz_audio_opts_clamp(&b->opts);
    vbeat_init(&b->vb, seed);
    if (b->opts.tempo) b->vb.bpm0 = (float)b->opts.tempo;
    b->an = ncz_audio_analyzer_create();
    ncz_beat_limiter_init(&b->lim_kick);
    ncz_beat_limiter_init(&b->lim_snare);
    ncz_beat_limiter_init(&b->lim_drive);
    snprintf(b->cap.status, sizeof b->cap.status, "off");
    b->sig.bpm = vbeat_bpm(&b->vb);
    b->sig.active = 1;
    if (b->opts.audio != 0) {
#ifdef NCZ_AUDIO_CAPTURE
        b->cap.pipe = capture_open(&b->opts, b->cap.status, sizeof b->cap.status);
        if (b->cap.pipe) {
            fprintf(stderr, "[diag] ncz-audio: capture started (%s)\n", b->cap.status);
        } else {
            snprintf(b->cap.status, sizeof b->cap.status, "%s (unavailable)", b->opts.audio == 1 ? "system" : "mic");
            fprintf(stderr, "[diag] ncz-audio: capture unavailable; using the virtual beat\n");
        }
#else
        snprintf(b->cap.status, sizeof b->cap.status, "%s (not built)", b->opts.audio == 1 ? "system" : "mic");
        fprintf(stderr, "[diag] ncz-audio: audio not built in; using the virtual beat\n");
#endif
    }
    return b;
}

void ncz_audio_bus_destroy(NczAudioBus *b) {
    if (!b) return;
    if (b->cap.pipe) pclose(b->cap.pipe);
    ncz_audio_analyzer_destroy(b->an);
    free(b);
}

const char *ncz_audio_bus_status(const NczAudioBus *b) { return b ? b->cap.status : "off"; }

void ncz_audio_bus_set_red_heavy(NczAudioBus *b, int r) {
    if (!b) return;
    b->red_heavy = r ? 1 : 0;
    ncz_beat_limiter_set_red_heavy(&b->lim_kick, r);
    ncz_beat_limiter_set_red_heavy(&b->lim_snare, r);
    ncz_beat_limiter_set_red_heavy(&b->lim_drive, r);
}

void ncz_audio_bus_update(NczAudioBus *b, float dt) {
    if (!b) return;
    if (!(dt == dt)) dt = 0.f;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.25f) dt = 0.25f;

    if (b->cap.pipe && !b->cap.failed) capture_poll(&b->cap, b->an, dt);
    b->live = b->cap.live;

    if (b->live) {
        NczAudioAnalysis an = ncz_audio_analyzer_state(b->an);
        b->bass = an.bass; b->mid = an.mid; b->treble = an.treble;
        b->onset_env = b->an ? clamp01(an.flux) : 0.f;
        b->level = an.level;
        b->est_conf = ncz_audio_analyzer_tempo(b->an, &b->est_bpm);
        /* Phase-lock: a detected onset nudges the beat phase toward 0. */
        if (an.onset) b->vb.phase *= 0.55f;
    } else {
        /* virtual only: synthesise slowly moving bands from the clock */
        b->est_bpm = 0.f; b->est_conf = 0.f;
        b->level += (0.f - b->level) * 0.05f;
        b->bass = 0.35f + 0.25f * sinf((float)b->vb.vt * 0.7f);
        b->mid = 0.30f + 0.20f * sinf((float)b->vb.vt * 0.9f + 1.7f);
        b->treble = 0.25f + 0.15f * sinf((float)b->vb.vt * 1.3f + 3.1f);
        b->onset_env = clamp01(b->onset_env * 0.9f + b->vb.kick_amp * 0.2f);
    }
    b->bass = clamp01(b->bass);
    b->mid = clamp01(b->mid);
    b->treble = clamp01(b->treble);

    vbeat_advance(&b->vb, dt);

    /* Tempo estimate keeps the virtual clock close when audio is live. */
    if (b->live && b->est_conf > 0.2f && b->est_bpm >= 60.f && b->est_bpm <= 180.f)
        b->vb.bpm0 += (b->est_bpm - b->vb.bpm0) * 0.02f;

    double now = b->vb.vt;
    float kick_raw = b->vb.kick_amp;
    float snare_raw = b->vb.snare_amp;
    float breath = 0.25f + 0.35f * b->bass + 0.20f * b->mid;
    float drive_raw = clamp01(0.45f * kick_raw + 0.35f * snare_raw + 0.20f * breath);

    b->kick = ncz_beat_limiter_apply(&b->lim_kick, kick_raw, now);
    b->snare = ncz_beat_limiter_apply(&b->lim_snare, snare_raw, now);
    b->drive = ncz_beat_limiter_apply(&b->lim_drive, drive_raw, now);

    b->sig.bass = b->bass;
    b->sig.mid = b->mid;
    b->sig.treble = b->treble;
    b->sig.onset_env = b->onset_env;
    b->sig.beat_phase = b->vb.phase;
    b->sig.bpm = vbeat_bpm(&b->vb);
    b->sig.kick = b->kick;
    b->sig.snare = b->snare;
    b->sig.level = b->level;
    b->sig.drive = b->drive;
    b->sig.onset = b->live && b->an ? ncz_audio_analyzer_state(b->an).onset : 0;
    b->sig.breakdown = b->vb.breakdown;
    b->sig.active = 1;
    b->sig.audio_live = b->live;

    /* One quiet log line on capture failure / silence. */
    if (b->cap.failed && !b->fallback_logged) {
        fprintf(stderr, "[diag] ncz-audio: capture stopped; using the virtual beat\n");
        b->fallback_logged = 1;
    }
}

const NczAudioSignals *ncz_audio_bus_signals(const NczAudioBus *b) {
    static const NczAudioSignals zero;
    return b ? &b->sig : &zero;
}
float ncz_audio_bus_bpm(const NczAudioBus *b) { return b ? b->sig.bpm : 0.f; }
int   ncz_audio_bus_audio_live(const NczAudioBus *b) { return b ? b->live : 0; }
