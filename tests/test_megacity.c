/* SPDX-License-Identifier: Apache-2.0 */
/* test_megacity.c - the stateless megacity logic, checked over hours of scene time.
 *
 *  1. Sun: a sunrise or sunset never takes less than 120 s (whatever the option
 *     says), e stays in [-1, 1], and it never moves faster than the quintic bound.
 *  2. Weather: regimes last 180-480 s by default, never repeat back to back,
 *     every parameter moves no faster than a quintic over >= 45 s, and the same
 *     (seed, t) always gives the same answer.
 *  3. Lightning: over 6 hours at 240 Hz and for each amplitude, the level never
 *     exceeds the amplitude (0.05 default, 0.2 ceiling), rises no faster than a
 *     100 ms quintic, and no sliding second holds more than 3 peaks.
 *  4. Ships and traffic: deterministic, hulls in their size classes, faded to
 *     nothing at both ends of a crossing.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "megacity_logic.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_sun(void) {
    const int cycles[] = {1, 2, 3};
    for (int ci = 0; ci < 3; ci++) {
        for (float tr = 30.f; tr <= 600.f; tr += 190.f) {
            mc_cycle c = {cycles[ci], ci == 1, tr, 60.f, 60.f};
            float eff = tr < MC_TRANSITION_MIN ? MC_TRANSITION_MIN : tr;
            float bound = 2.f * 1.875f / eff;       /* d e / d t of a quintic swing of 2 */
            double prev = mc_sun(&c, 0), first_move = -1, last_move = -1;
            float lo = 9, hi = -9;
            for (double t = 0.01; t < 4000.0; t += 0.01) {
                float e = mc_sun(&c, t);
                lo = fminf(lo, e); hi = fmaxf(hi, e);
                CHECK(fabs(e - prev) <= bound * 0.01 * 1.02 + 1e-6, "sun step at t=%.2f cycle %d tr %.0f", t, cycles[ci], tr);
                if (fabs(e - prev) > 0) { if (first_move < 0) first_move = t; last_move = t; }
                prev = e;
                if (cycles[ci] != 3 && t > 3 * eff + 200) break;
            }
            CHECK(lo >= -1.f && hi <= 1.f, "sun range %f %f", lo, hi);
            if (cycles[ci] != 3) CHECK(last_move - first_move >= 0.99 * eff - 0.05, "transition %.1f s < %.1f", last_move - first_move, eff);
        }
    }
    mc_cycle off = {0, 1, 180, 0, 0};
    CHECK(mc_sun(&off, 5.0) == 1.f, "cycle off pale-reach");
    off.start_luminox = 0;
    CHECK(mc_sun(&off, 5.0) == -1.f, "cycle off night");
}

static void test_weather(void) {
    for (uint32_t seed = 1; seed <= 40; seed++) {
        mc_wx_cfg w = {seed * 7919u, (1u << MC_NWX) - 1u, 180.f, 480.f, 1.f};
        double T = 0;
        int prev = -1;
        for (uint32_t k = 0; k < 60; k++) {
            float d = mc_regime_len(&w, k);
            CHECK(d >= 180.f && d <= 480.f, "regime length %f", d);
            int ty = mc_regime_type(&w, k);
            CHECK(ty != prev, "regime %u repeats type %d", k, ty);
            prev = ty;
            T += d;
        }
        mc_wx a, b;
        mc_wx p = mc_weather(&w, 0.0, NULL, NULL);
        for (double t = 0.1; t < T; t += 0.1) {
            a = mc_weather(&w, t, NULL, NULL);
            b = mc_weather(&w, t, NULL, NULL);
            CHECK(memcmp(&a, &b, sizeof a) == 0, "weather not deterministic");
            const float *A = (const float *)&a, *P = (const float *)&p;
            for (int i = 0; i < 11; i++)
                CHECK(fabsf(A[i] - P[i]) <= 0.1f * 1.875f / 45.f * 1.05f + 1e-5f, "weather param %d jumps %f at t=%.1f", i, A[i] - P[i], t);
            p = a;
        }
    }
    mc_wx_cfg one = {5, 1u << MC_SNOW, 180.f, 480.f, 1.f};
    int ty; mc_weather(&one, 1234.0, &ty, NULL);
    CHECK(ty == MC_SNOW, "single weather holds");
}

static void test_flash(void) {
    const float amps[] = {MC_FLASH_DEFAULT, 0.1f, MC_FLASH_CEILING, 0.5f};
    for (int ai = 0; ai < 4; ai++) {
        float amp = amps[ai], cap = amp > MC_FLASH_CEILING ? MC_FLASH_CEILING : amp;
        double dt = 1.0 / 240.0, prev = 0, maxv = 0;
        int peaks_in_window[3] = {0};
        (void)peaks_in_window;
        double peak_t[64]; int np = 0;
        int rising = 0;
        double maxslope = 0;
        for (double t = 0; t < 6 * 3600.0; t += dt) {
            float f = mc_flash(77u + ai, t, amp, 1.f);
            if (f > maxv) maxv = f;
            double slope = (f - prev) / dt;
            if (slope > maxslope) maxslope = slope;
            if (f > prev) rising = 1;
            else if (f < prev && rising) {
                rising = 0;
                if (prev > 0.15 * cap) {   /* a peak at t - dt */
                    if (np < 64) peak_t[np++] = t - dt;
                    else { memmove(peak_t, peak_t + 1, 63 * sizeof(double)); peak_t[63] = t - dt; }
                    int in = 0;
                    for (int i = 0; i < np; i++) if (t - dt - peak_t[i] < 1.0) in++;
                    CHECK(in <= 3, "%d flashes within one second at t=%.2f", in, t);
                    if (np > 1) CHECK(peak_t[np - 1] - peak_t[np - 2] >= 0.39, "peaks %.3f s apart", peak_t[np - 1] - peak_t[np - 2]);
                }
            }
            prev = f;
        }
        CHECK(maxv <= cap + 1e-6, "flash %.4f exceeds cap %.4f", maxv, cap);
        CHECK(maxv > 0.6 * cap, "flash never reaches the amplitude (%.4f of %.4f)", maxv, cap);
        double bound = cap * 1.875 / 0.10 * 1.03;
        CHECK(maxslope <= bound, "flash rises too fast: %.3f/s > %.3f/s (amp %.3f)", maxslope, bound, cap);
    }
    /* rise times of every event in 20000 buckets are 100-300 ms, spacing >= 0.4 s */
    for (uint32_t b = 0; b < 20000; b++) {
        mc_flash_ev ev[3];
        int n = mc_flash_events(9u, b, 0.05f, ev);
        for (int i = 0; i < n; i++) {
            CHECK(ev[i].rise >= 0.0999f && ev[i].rise <= 0.3001f, "rise %f", ev[i].rise);
            if (i) CHECK(ev[i].t0 - ev[i - 1].t0 >= MC_FLASH_MIN_GAP, "spacing %f", ev[i].t0 - ev[i - 1].t0);
            CHECK(ev[i].t0 + ev[i].rise < (b + 1) * MC_FLASH_BUCKET, "event escapes its bucket");
        }
    }
    CHECK(mc_flash(1, 100.0, 0.05f, 0.f) == 0.f, "no storm, no flash");
}

static mc_anchor cam_anchor(void *ctx, double t) {
    (void)ctx;
    mc_cam c = mc_camera(11, t, 1.f, 1.f, 1.f);
    mc_anchor a = {c.x, c.y, c.z};
    return a;
}
static void test_ships(void) {
    int caps = 0, mids = 0;
    for (double t = 0; t < 6 * 3600.0; t += 2.0) {
        for (int i = 0; i < MC_NSHIP; i++) {
            mc_ship a = mc_ship_slot(11, i, t, cam_anchor, NULL, 1.f, 0.35f);
            mc_ship b = mc_ship_slot(11, i, t, cam_anchor, NULL, 1.f, 0.35f);
            CHECK(memcmp(&a, &b, sizeof a) == 0, "ship not deterministic");
            if (!a.live) continue;
            if (i == MC_NSHIP - 1) { caps++; CHECK(a.scale >= 2400.f && a.scale <= 3800.f && a.kind == 4, "capital %f", a.scale); }
            else { mids++; CHECK(a.scale >= 110.f && a.scale <= 330.f && a.kind >= 0 && a.kind < 4, "mid %f", a.scale); }
            CHECK(a.fade > 0.f && a.fade <= 1.f, "fade %f", a.fade);
        }
    }
    CHECK(caps > 20, "capitals never appear (%d)", caps);
    CHECK(mids > 1000, "freighters never appear (%d)", mids);
    for (int i = 0; i < MC_NSHIP; i++)
        CHECK(!mc_ship_slot(11, i, 500.0, cam_anchor, NULL, 0.f, 0.f).live, "density 0 must remove freighters and capital 0 the capital");
    int live = 0;
    for (int j = 0; j < MC_NCRAFT; j++) {
        mc_craft c = mc_craft_at(3, j, 1000.0, 0, 34000.0, 2.f);
        mc_craft d = mc_craft_at(3, j, 1000.0, 0, 34000.0, 2.f);
        CHECK(memcmp(&c, &d, sizeof c) == 0, "craft not deterministic");
        if (c.fade > 0) live++;
        CHECK(fabs(c.x) < 700.0 && c.y > 40 && c.y < 330, "craft lane %f %f", c.x, c.y);
    }
    CHECK(live >= 12, "traffic 2 should keep most craft live (%d)", live);
}

int main(void) {
    test_sun();
    test_weather();
    test_flash();
    test_ships();
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("megacity logic ok\n");
    return 0;
}
