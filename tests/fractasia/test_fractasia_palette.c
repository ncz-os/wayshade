/* SPDX-License-Identifier: Apache-2.0 */
/* test_fractasia_palette.c — palette generator for the fractasia saver.
 *
 * No GL needed. Asserts that the random palette generator:
 *  - almost never exhausts its attempts (fallback to a named palette is rare);
 *  - only returns palettes that pass the quality gate;
 *  - is deterministic for a seed and different across seeds;
 *  - produces real variety (hue spread, brightness spread);
 * and that the gate rejects washed-out pastel and grey palettes, the failure
 * that made the first O6N fractint run look like pink on white.
 * Also: every palette of the named library passes the gate for its kind
 * (and the accidental grey still fails the MONO and FILM gates); the
 * per-cycle palette draw reaches every library entry, keeps the Cygnus
 * schemes a weighted favourite (30..75%), and is deterministic.
 *
 * Original to ncz-screensavers (Apache-2.0).
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fractasia/fractasia.h"

#define N_SEEDS 600

static uint64_t seed_state(uint32_t seed) {
    return 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0xD1B54A32D192ED03ull);
}

static float hue_of(const float rgb[3]) {
    float mx = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
    float mn = fminf(rgb[0], fminf(rgb[1], rgb[2]));
    float d = mx - mn;
    if (d < 1e-4f) return 0.0f;
    float h;
    if (mx == rgb[0]) h = fmodf((rgb[1] - rgb[2]) / d, 6.0f);
    else if (mx == rgb[1]) h = (rgb[2] - rgb[0]) / d + 2.0f;
    else h = (rgb[0] - rgb[1]) / d + 4.0f;
    if (h < 0) h += 6.0f;
    return h / 6.0f;
}

int main(void) {
    int fallbacks = 0, total_attempts = 0;
    int hue_bucket[12] = {0};
    float ymean_min = 1.0f, ymean_max = 0.0f;
    int cygnus = 0; /* palettes whose strongest colour is magenta/violet or cyan */

    for (uint32_t s = 1; s <= N_SEEDS; s++) {
        uint64_t st = seed_state(s);
        FractPaletteDef p;
        int attempts = fract_palette_random(&st, &p);
        if (!attempts) { fallbacks++; continue; }
        total_attempts += attempts;
        assert(attempts >= 1 && attempts <= FRACT_PAL_MAX_ATTEMPTS);
        assert(fract_palette_acceptable(&p));

        /* Dominant hue of the palette's most saturated sample. */
        float best_chroma = -1.0f, best_hue = 0.0f, ysum = 0.0f;
        for (int i = 0; i < 64; i++) {
            float rgb[3];
            fract_palette_eval(&p, (float)i / 63.0f, rgb, NULL);
            float c = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2])) - fminf(rgb[0], fminf(rgb[1], rgb[2]));
            if (c > best_chroma) { best_chroma = c; best_hue = hue_of(rgb); }
            ysum += 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
        }
        hue_bucket[(int)(best_hue * 12.0f) % 12]++;
        if ((best_hue >= 0.70f && best_hue <= 0.95f) || (best_hue >= 0.45f && best_hue <= 0.58f)) cygnus++;
        float ym = ysum / 64.0f;
        if (ym < ymean_min) ymean_min = ym;
        if (ym > ymean_max) ymean_max = ym;
    }

    printf("seeds=%d fallbacks=%d mean_attempts=%.2f ymean=[%.2f,%.2f]\n", N_SEEDS, fallbacks,
           (double)total_attempts / (double)(N_SEEDS - fallbacks), (double)ymean_min, (double)ymean_max);
    assert(fallbacks * 100 <= N_SEEDS * 2); /* under 2% need a named fallback */

    int buckets = 0;
    for (int i = 0; i < 12; i++) {
        printf("hue bucket %2d: %d\n", i, hue_bucket[i]);
        if (hue_bucket[i] >= N_SEEDS / 60) buckets++; /* at least ~1.7% of palettes */
    }
    assert(buckets >= 10); /* every part of the colour wheel is used */
    assert(ymean_max - ymean_min >= 0.12f); /* brightness varies, not one fixed look */
    /* The Cygnus scheme (magenta / violet / cyan) leads, without owning the wheel. */
    printf("cygnus-leaning palettes: %d/%d\n", cygnus, N_SEEDS - fallbacks);
    assert(cygnus * 100 >= (N_SEEDS - fallbacks) * 30);
    assert(cygnus * 100 <= (N_SEEDS - fallbacks) * 75);

    /* Determinism, and divergence across seeds. */
    uint64_t a = seed_state(42), b = seed_state(42), c = seed_state(43);
    FractPaletteDef pa, pb, pc;
    assert(fract_palette_random(&a, &pa) && fract_palette_random(&b, &pb) && fract_palette_random(&c, &pc));
    assert(memcmp(&pa, &pb, sizeof pa) == 0);
    assert(memcmp(&pa, &pc, sizeof pa) != 0);

    /* The generator state advances, so a second draw differs (per-cycle palettes). */
    FractPaletteDef pa2;
    assert(fract_palette_random(&a, &pa2));
    assert(memcmp(&pa, &pa2, sizeof pa) != 0);

    /* The gate rejects the failure modes it exists for. */
    FractPaletteDef washed = {{0.85f, 0.80f, 0.85f}, {0.10f, 0.10f, 0.10f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.10f, 0.20f}};
    FractPaletteDef grey = {{0.5f, 0.5f, 0.5f}, {0.45f, 0.45f, 0.45f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    FractPaletteDef dark = {{0.05f, 0.05f, 0.05f}, {0.05f, 0.05f, 0.05f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.3f, 0.6f}};
    FractPaletteDef clipped = {{0.5f, 0.5f, 0.5f}, {1.4f, 1.4f, 1.4f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.3f, 0.6f}};
    assert(!fract_palette_acceptable(&washed));
    assert(!fract_palette_acceptable(&grey));
    assert(!fract_palette_acceptable(&dark));
    assert(!fract_palette_acceptable(&clipped));

    /* The old default ("stylized") is bright cream at t = 0. Its full range
     * looks fine, but the low end is what most pixels show, so it must fail. */
    FractPaletteDef stylized = {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.5f}, {0.0f, 0.10f, 0.20f}};
    assert(!fract_palette_acceptable(&stylized));

    /* Every generated palette is dark-to-mid at the low end. */
    for (uint32_t s = 1; s <= 200; s++) {
        uint64_t st = seed_state(s * 7919u);
        FractPaletteDef p;
        if (!fract_palette_random(&st, &p)) continue;
        float ysum = 0.0f;
        int n = 0;
        for (int i = 0; i <= 19; i++) { /* t in [0, 0.30] */
            float rgb[3];
            fract_palette_eval(&p, 0.30f * (float)i / 19.0f, rgb, NULL);
            ysum += 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
            n++;
        }
        assert(ysum / (float)n <= 0.46f);
    }

    /* ---- the named library and its kind gates ---- */
    for (int i = 0; i < FRACT_LIB_COUNT; i++) {
        const FractPaletteLib *l = &fractasia_palette_library[i];
        assert(l->name && fract_palette_library_find(l->name) == l);
        if (!fract_palette_acceptable_kind(&l->def, l->kind)) {
            fprintf(stderr, "library palette %s fails its %d gate\n", l->name, l->kind);
            assert(0);
        }
        for (int j = i + 1; j < FRACT_LIB_COUNT; j++) assert(strcmp(l->name, fractasia_palette_library[j].name) != 0);
    }
    assert(!fract_palette_library_find("stylized") && !fract_palette_library_find(NULL));
    /* The relaxed kinds still refuse an accidental grey and a pale wash. */
    assert(!fract_palette_acceptable_kind(&grey, FRACT_PK_MONO));
    assert(!fract_palette_acceptable_kind(&grey, FRACT_PK_FILM));
    assert(!fract_palette_acceptable_kind(&washed, FRACT_PK_PASTEL));
    assert(!fract_palette_acceptable_kind(&dark, FRACT_PK_MONO));
    /* The COLOUR gate is the original one. */
    assert(fract_palette_acceptable_kind(&stylized, FRACT_PK_COLOUR) == fract_palette_acceptable(&stylized));

    /* ---- the per-cycle draw ---- */
    {
        int lib_hits[FRACT_LIB_COUNT] = {0}, cyg = 0, gen = 0, forced = 0, lib = 0;
        for (uint32_t s = 1; s <= N_SEEDS; s++) {
            uint64_t st = seed_state(s * 104729u);
            FractPaletteDef p; const char *name = NULL;
            int leans = fract_palette_cycle(&st, &p, &name);
            assert(name);
            const FractPaletteLib *l = fract_palette_library_find(name);
            if (l) { lib++; lib_hits[l - fractasia_palette_library]++; assert(fract_palette_acceptable_kind(&p, l->kind)); }
            else { assert(fract_palette_acceptable(&p)); if (!strcmp(name, "cygnus")) forced++; else gen++; }
            /* Cygnus-leaning by the same hue measure as above, or by the draw's own flag. */
            float best_chroma = -1.0f, best_hue = 0.0f;
            for (int i = 0; i < 64; i++) {
                float rgb[3];
                fract_palette_eval(&p, (float)i / 63.0f, rgb, NULL);
                float c = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2])) - fminf(rgb[0], fminf(rgb[1], rgb[2]));
                if (c > best_chroma) { best_chroma = c; best_hue = hue_of(rgb); }
            }
            if (leans || (best_hue >= 0.70f && best_hue <= 0.95f) || (best_hue >= 0.45f && best_hue <= 0.58f)) cyg++;
        }
        printf("cycle draw: library=%d cygnus-forced=%d generated=%d cygnus-leaning=%d/%d\n", lib, forced, gen, cyg, N_SEEDS);
        for (int i = 0; i < FRACT_LIB_COUNT; i++) {
            printf("  %-10s %d\n", fractasia_palette_library[i].name, lib_hits[i]);
            assert(lib_hits[i] >= N_SEEDS / 100); /* every library entry is reachable */
        }
        assert(lib * 100 >= N_SEEDS * 30 && lib * 100 <= N_SEEDS * 50);
        assert(cyg * 100 >= N_SEEDS * 30 && cyg * 100 <= N_SEEDS * 75);
        /* Deterministic for a seed; advancing state gives the next cycle a different draw. */
        uint64_t a1 = seed_state(99), a2 = seed_state(99);
        FractPaletteDef p1, p2, p3; const char *n1, *n2, *n3;
        fract_palette_cycle(&a1, &p1, &n1); fract_palette_cycle(&a2, &p2, &n2); fract_palette_cycle(&a1, &p3, &n3);
        assert(memcmp(&p1, &p2, sizeof p1) == 0 && !strcmp(n1, n2));
        assert(memcmp(&p1, &p3, sizeof p1) != 0);
        /* The forced scheme is always Cygnus-leaning and always passes the gate. */
        for (uint32_t s = 1; s <= 100; s++) {
            uint64_t st = seed_state(s * 7u);
            FractPaletteDef p;
            assert(fract_palette_random_scheme(&st, &p, 1));
            assert(fract_palette_acceptable(&p));
        }
    }

    printf("ok\n");
    return 0;
}
