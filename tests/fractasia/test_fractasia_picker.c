/* SPDX-License-Identifier: Apache-2.0 */
/* test_fractasia_picker.c — deterministic-picker unit test for the
 *                         fractasia screensaver.
 *
 * No GL context needed: this test exercises the algorithm-name
 * dispatch table (src/fractasia/fractasia_data.c) and the
 * xorshift RNG that the picker relies on. It asserts:
 *
 *  - Every family id maps to a non-NULL .frag basename.
 *  - The Wolfram rules list contains eight distinct, non-zero entries.
 *  - xorshift with a fixed seed reproduces the same sequence on two
 *    independent runs.
 *  - xorshift with two different seeds diverges at some finite
 *    position.
 *  - The view picker for the escape-time families: the gate rejects a flat
 *    far-field view and a screen of interior and accepts a known boundary
 *    view; every picked view passes the gate; picks are deterministic per
 *    seed, differ across seeds, and the fallback is rare and cheap.
 *
 * Original to ncz-screensavers (Apache-2.0).
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include "fractasia/fractasia.h"

static uint64_t xs_next(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return (x * 0x2545F4914F6CDD1Dull) >> 32;
}
static void xs_seed(uint64_t *s, uint32_t seed) {
    uint64_t v = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0xD1B54A32D192ED03ull);
    if (!v) v = 1;
    *s = v;
}

int main(void) {
    /* Rotation membership: the eleven families that draw well are in, the six weak
     * ones are out, and the set can never be empty. */
    {
        int in = 0;
        for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++) in += fract_family_in_rotation(fid);
        assert(in == 11);
        assert(!fract_family_in_rotation(FRACT_IFS));
        assert(!fract_family_in_rotation(FRACT_NEWTON));
        assert(!fract_family_in_rotation(FRACT_QUATERNION));
        assert(!fract_family_in_rotation(FRACT_LSYSTEM));
        assert(!fract_family_in_rotation(FRACT_LYAPUNOV));
        assert(!fract_family_in_rotation(FRACT_ATTRACTOR));
        assert(fract_family_in_rotation(FRACT_CELLULAR) && fract_family_in_rotation(FRACT_MOBIUS));
        assert(fract_family_in_rotation(FRACT_SHIP_GLOW));
        assert(!fract_family_in_rotation(-1) && !fract_family_in_rotation(FRACT_NUM_FAMILIES));
    }

    /* Every family id maps to a non-NULL filename. */
    for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++) {
        const char *fn = fract_family_filename(fid);
        if (!fn) {
            fprintf(stderr, "FAIL family %d has NULL filename\n", fid);
            return 1;
        }
        if (strlen(fn) < 4 || !strstr(fn, ".frag")) {
            fprintf(stderr, "FAIL family %d filename %s missing .frag\n",
                    fid, fn);
            return 1;
        }
        /* No Fractint-source path in any filename. */
        if (strstr(fn, "fractint.c") || strstr(fn, "frm")) {
            fprintf(stderr, "FAIL family %d filename %s contains Fractint source ref\n",
                    fid, fn);
            return 1;
        }
    }

    /* Wolfram rules: 8 entries, all non-zero, no duplicates. */
    if (sizeof fractasia_wolfram_rules / sizeof fractasia_wolfram_rules[0] != 8) {
        fprintf(stderr, "FAIL wolfram rules array size != 8\n");
        return 1;
    }
    for (int i = 0; i < 8; i++) {
        if (fractasia_wolfram_rules[i] == 0) {
            fprintf(stderr, "FAIL wolfram rules[%d] == 0\n", i);
            return 1;
        }
        for (int j = i + 1; j < 8; j++) {
            if (fractasia_wolfram_rules[i] == fractasia_wolfram_rules[j]) {
                fprintf(stderr, "FAIL wolfram rules[%d] duplicates [%d]\n", i, j);
                return 1;
            }
        }
    }

    /* xorshift with a fixed seed reproduces the same sequence. */
    uint64_t s1, s2;
    xs_seed(&s1, 12345);
    xs_seed(&s2, 12345);
    for (int i = 0; i < 1000; i++) {
        uint32_t a = xs_next(&s1);
        uint32_t b = xs_next(&s2);
        if (a != b) {
            fprintf(stderr, "FAIL xorshift diverged at step %d: %u vs %u\n",
                    i, a, b);
            return 1;
        }
    }

    /* xorshift with two different seeds diverges within 100 steps. */
    xs_seed(&s1, 1);
    xs_seed(&s2, 2);
    int diverged = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t a = xs_next(&s1);
        uint32_t b = xs_next(&s2);
        if (a != b) { diverged = 1; break; }
    }
    if (!diverged) {
        fprintf(stderr, "FAIL xorshift never diverged with seed 1 vs 2\n");
        return 1;
    }

    /* xorshift seed 0 is coerced to 1 (the implementation refuses 0). */
    uint64_t s;
    xs_seed(&s, 0);
    if (s == 0) {
        fprintf(stderr, "FAIL xorshift seed 0 produced state 0\n");
        return 1;
    }

    /* The first 10 xorshift draws from seed 42 are stable. */
    uint32_t expected[10] = {
        0x5a3fa999, 0xef2a482c, 0x62b4dff5, 0x12d541d8,
        0x5bac6a94, 0x375cbf75, 0xd3257ca7, 0x92e12934,
        0xb41a9d00, 0xb8a05f39,
    };
    xs_seed(&s, 42);
    for (int i = 0; i < 10; i++) {
        uint32_t got = xs_next(&s);
        if (got != expected[i]) {
            fprintf(stderr,
                    "FAIL xorshift seed 42 step %d got %08x expected %08x\n",
                    i, got, expected[i]);
            return 1;
        }
    }

    /* ---- view picker ---- */
    {
        FractView v; FractViewStats st;
        /* A window far from the set: all far field. */
        memset(&v, 0, sizeof v); v.n = 2; v.zoom = 1.0f; v.cx = 5.0f; v.cy = 5.0f;
        fract_view_probe(FRACT_ESCAPE_TIME, &v, FRACT_PROBE_ITER, &st);
        assert(st.far > 0.9f && !fract_view_acceptable(&st));
        /* Deep inside the main cardioid: all interior. */
        v.cx = -0.2f; v.cy = 0.0f; v.zoom = 60.0f;
        fract_view_probe(FRACT_ESCAPE_TIME, &v, FRACT_PROBE_ITER, &st);
        assert(st.interior > 0.9f && !fract_view_acceptable(&st));
        /* The filaments around the Misiurewicz point near -0.1011 + 0.9563i:
         * boundary everywhere, almost no interior.  (Deep in seahorse valley
         * most points need more than the probe's 160 iterations, and such a
         * view correctly reads as interior, which is also how the weak tier's
         * iteration budget would draw it.) */
        v.cx = -0.1011f; v.cy = 0.9563f; v.zoom = 8.0f;
        fract_view_probe(FRACT_ESCAPE_TIME, &v, FRACT_PROBE_ITER, &st);
        printf("misiurewicz: boundary=%.3f central=%.3f interior=%.3f far=%.3f spread=%.3f\n",
               (double)st.boundary, (double)st.central, (double)st.interior, (double)st.far, (double)st.spread);
        assert(fract_view_acceptable(&st));
        /* The gate on its own terms: a frame with its structure all in one
         * corner (plenty of boundary overall, none in the middle) is out;
         * the same frame with a central share is in. */
        {
            FractViewStats corner = { 0.20f, 0.10f, 0.15f, 0.02f, 1.0f };
            assert(!fract_view_acceptable(&corner));
            corner.central = 0.12f;
            assert(fract_view_acceptable(&corner));
        }
        /* The whole default Mandelbrot window (the old fixed view) is the
         * dull case the picker exists for: a sea of far field. */
        v.cx = 0.0f; v.cy = 0.0f; v.zoom = 1.0f;
        fract_view_probe(FRACT_ESCAPE_TIME, &v, FRACT_PROBE_ITER, &st);
        printf("old default window: boundary=%.3f interior=%.3f far=%.3f spread=%.3f\n",
               (double)st.boundary, (double)st.interior, (double)st.far, (double)st.spread);
        assert(!fract_view_acceptable(&st));

        /* The iteration mirrors the shader: the origin is interior, 2+0i escapes at once. */
        memset(&v, 0, sizeof v); v.n = 2;
        assert(fract_view_iterate(FRACT_ESCAPE_TIME, &v, 0.0f, 0.0f, 100) == 100);
        assert(fract_view_iterate(FRACT_ESCAPE_TIME, &v, 2.0f, 0.0f, 100) == 0);
        v.n = 3; v.cre = 0.0f; v.cim = 0.0f;   /* z^3 with c = 0: the origin is fixed, 2 escapes */
        assert(fract_view_iterate(FRACT_MULTIBROT, &v, 0.0f, 0.0f, 100) == 100);
        assert(fract_view_iterate(FRACT_MULTIBROT, &v, 2.0f, 0.0f, 100) == 0);
        v.cre = -0.745f; v.cim = 0.113f;
        assert(fract_view_iterate(FRACT_JULIA, &v, 3.0f, 0.0f, 100) == 0);

        /* Every pick passes the gate at the budget it was judged with (the
         * weak tier's 48 for half the seeds, the probe ceiling for the rest);
         * determinism; variety; cost. */
        const int fams[5] = { FRACT_ESCAPE_TIME, FRACT_MULTIBROT, FRACT_BURNING_SHIP, FRACT_TRICORN, FRACT_JULIA };
        int fallbacks = 0, total_attempts = 0, picks = 0, deep = 0;
        double worst_ms = 0.0, total_ms = 0.0;
        for (int fi = 0; fi < 5; fi++) {
            int fam_fallbacks = 0;
            for (uint32_t seed = 1; seed <= 60; seed++) {
                uint64_t st1, st2;
                int budget = (seed & 1) ? 48 : FRACT_PROBE_ITER;
                xs_seed(&st1, seed * 31u + (uint32_t)fi);
                st2 = st1;
                struct timespec t0, t1;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                int a = fract_view_pick(&st1, fams[fi], 3 + (int)(seed % 6), budget, &v);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
                if (ms > worst_ms) worst_ms = ms;
                total_ms += ms;
                picks++;
                if (!a) { fallbacks++; fam_fallbacks++; continue; }
                total_attempts += a;
                assert(a >= 1 && a <= FRACT_VIEW_MAX_ATTEMPTS);
                fract_view_probe(fams[fi], &v, budget, &st);
                assert(fract_view_acceptable(&st));
                assert(v.zoom >= 1.0f && v.zoom <= 64.0f);
                assert(v.zoom_rate > 0.0f && v.zoom_rate <= 0.05f);
                if (v.zoom >= 4.0f) deep++;
                if (fams[fi] == FRACT_MULTIBROT) assert(v.n >= 3 && v.n <= 8);
                /* Same seed, same view. */
                FractView w;
                int b = fract_view_pick(&st2, fams[fi], 3 + (int)(seed % 6), budget, &w);
                assert(b == a && memcmp(&v, &w, sizeof v) == 0);
            }
            printf("family %d: fallbacks=%d/60\n", fams[fi], fam_fallbacks);
        }
        printf("views: picks=%d fallbacks=%d mean_attempts=%.2f deep=%d mean_ms=%.1f worst_ms=%.1f\n",
               picks, fallbacks, (double)total_attempts / (double)(picks - fallbacks), deep,
               total_ms / picks, worst_ms);
        assert(fallbacks * 100 <= picks * 5);      /* under 5% fall back to the curated list */
        assert(deep * 4 >= picks);                 /* at least a quarter start zoomed in */
        /* The pick runs on the render thread at cycle start: a brief hitch at
         * most, never a stall, even in an unoptimised test build. */
        assert(total_ms / picks < 60.0);
        assert(worst_ms < 400.0);

        /* Different seeds give different views. */
        uint64_t sa, sb; FractView va, vb;
        xs_seed(&sa, 7); xs_seed(&sb, 8);
        fract_view_pick(&sa, FRACT_ESCAPE_TIME, 2, 48, &va);
        fract_view_pick(&sb, FRACT_ESCAPE_TIME, 2, 48, &vb);
        assert(memcmp(&va, &vb, sizeof va) != 0);
        /* The budget is part of the judgement: a view judged at 100 need not
         * pass at 48, but one judged at 48 must still pass when probed at 48. */
        fract_view_probe(FRACT_ESCAPE_TIME, &va, 48, &st);
        assert(fract_view_acceptable(&st));

        /* Julia's c lands near the Mandelbrot boundary: both an escaping and
         * a bound point within 0.01 of it. */
        int near = 0;
        for (uint32_t seed = 1; seed <= 40; seed++) {
            xs_seed(&sa, seed);
            if (!fract_view_pick(&sa, FRACT_JULIA, 2, 48, &va)) continue;
            FractView m; memset(&m, 0, sizeof m); m.n = 2;
            int in = 0, out = 0;
            for (int k = 0; k < 9; k++) {
                float dx = 0.01f * (float)(k % 3 - 1), dy = 0.01f * (float)(k / 3 - 1);
                if (fract_view_iterate(FRACT_ESCAPE_TIME, &m, va.cre + dx, va.cim + dy, 200) >= 200) in++; else out++;
            }
            near += (in > 0 && out > 0);
        }
        printf("julia c near the boundary: %d/40\n", near);
        assert(near >= 36);

        /* The zoom schedule is clamped at the float-precision limit. */
        memset(&v, 0, sizeof v); v.zoom = 10.0f; v.zoom_rate = 0.05f;
        assert(fabsf(fract_view_zoom_at(&v, 0.0) - 10.0f) < 1e-4f);
        assert(fract_view_zoom_at(&v, 20.0) > 19.9f && fract_view_zoom_at(&v, 20.0) < 20.1f);
        assert(fract_view_zoom_at(&v, 1000.0) == FRACT_VIEW_MAX_ZOOM);
        /* Only the five escape-time families take a view. */
        int nview = 0;
        for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++) nview += fract_view_family(fid);
        assert(nview == 5 && !fract_view_family(FRACT_JULIA_GLOW) && !fract_view_family(FRACT_PLASMA));
    }

    /* ---- effect recipes and palette mapping ---- */
    {
        int weak = 0, medium = 0, high = 0;
        for (int i = 0; i < FRACT_RECIPE_COUNT; i++) {
            const FractRecipe *r = &fractasia_recipes[i];
            assert(r->name && fract_recipe_find(r->name) == r);
            assert(r->min_quality >= 0 && r->min_quality <= 2);
            assert((r->bits & ~((1 << 12) - 1)) == 0);
            /* The distance estimate doubles the loop's arithmetic: never on the weak class. */
            if (r->bits & (FRACT_FX_DE | FRACT_FX_GLOW)) assert(r->min_quality >= 1);
            for (int j = i + 1; j < FRACT_RECIPE_COUNT; j++) assert(strcmp(r->name, fractasia_recipes[j].name) != 0);
            if (r->min_quality == 0) weak++; else if (r->min_quality == 1) medium++; else high++;
        }
        assert(weak >= 6 && medium >= 2 && high >= 1);
        assert(!strcmp(fractasia_recipes[0].name, "plain") && fractasia_recipes[0].bits == 0);
        assert(!fract_recipe_find("no-such-recipe") && !fract_recipe_find(NULL));

        /* Draws respect the tier; every allowed recipe is reachable; deterministic. */
        int seen_weak[FRACT_RECIPE_COUNT] = {0}, seen_all[FRACT_RECIPE_COUNT] = {0};
        for (uint32_t seed = 1; seed <= 400; seed++) {
            uint64_t s1, s2;
            xs_seed(&s1, seed); s2 = s1;
            int a = fract_recipe_draw(&s1, 0, 0), b = fract_recipe_draw(&s2, 0, 0);
            assert(a == b && a >= 0 && a < FRACT_RECIPE_COUNT && fractasia_recipes[a].min_quality == 0);
            seen_weak[a]++;
            xs_seed(&s1, seed);
            int c = fract_recipe_draw(&s1, 2, 0);
            assert(c >= 0 && c < FRACT_RECIPE_COUNT);
            seen_all[c]++;
            xs_seed(&s1, seed);
            int d = fract_recipe_draw(&s1, 0, 1);  /* style=enhanced: everything, even on weak */
            assert(d >= 0 && d < FRACT_RECIPE_COUNT);
            xs_seed(&s1, seed);
            int e = fract_recipe_draw(&s1, 1, 0);
            assert(fractasia_recipes[e].min_quality <= 1);
        }
        for (int i = 0; i < FRACT_RECIPE_COUNT; i++) {
            if (fractasia_recipes[i].min_quality == 0) assert(seen_weak[i] > 0); else assert(seen_weak[i] == 0);
            assert(seen_all[i] > 0);
        }

        /* Mapping draws stay in their documented ranges and vary. */
        int poster = 0, reversed = 0, cycling = 0;
        float scale_min = 9.0f, scale_max = 0.0f;
        for (uint32_t seed = 1; seed <= 400; seed++) {
            uint64_t s1; xs_seed(&s1, seed * 3u);
            FractMapping m; fract_mapping_draw(&s1, &m);
            assert(m.band_scale >= 0.6f && m.band_scale <= 1.7f);
            assert(m.posterise == 0 || (m.posterise >= 4 && m.posterise <= 9));
            assert(m.direction == 1.0f || m.direction == -1.0f);
            assert(m.cycle == 0.0f || (m.cycle >= 0.010f && m.cycle <= 0.040f));
            assert(m.interior_t >= 0.0f && m.interior_t < 1.0f);
            poster += m.posterise > 0; reversed += m.direction < 0.0f; cycling += m.cycle > 0.0f;
            if (m.band_scale < scale_min) scale_min = m.band_scale;
            if (m.band_scale > scale_max) scale_max = m.band_scale;
            float fx[4]; fract_fx_params_draw(&s1, fx);
            assert(fx[0] >= 3.0f && fx[0] <= 8.0f && fx[1] > 0.0f && fx[2] > 0.0f && fx[3] > 0.0f);
        }
        printf("mapping: posterised=%d reversed=%d cycling=%d scale=[%.1f,%.1f]\n", poster, reversed, cycling,
               (double)scale_min, (double)scale_max);
        assert(poster > 60 && poster < 180 && reversed > 120 && reversed < 280 && cycling > 160 && cycling < 320);
        assert(scale_min < 0.7f && scale_max > 1.6f);
    }

    printf("PASS fractasia-picker-determinism\n");
    return 0;
}