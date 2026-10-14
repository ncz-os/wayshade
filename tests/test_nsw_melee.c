/* SPDX-License-Identifier: Apache-2.0 */
/* test_nsw_melee.c - headless checks of the Melee simulation and camera.
 *
 * 1. Every ordered race pair fights to a resolution with no NaN; the 1 v 1
 *    win-rate matrix keeps every race between 20 and 80%; battles average
 *    30-80 s.
 * 2. 1000 battles picked by the director all resolve, with no back-to-back
 *    repeat of the same set of races.
 * 3. Determinism: 200 seeds replayed twice, once at a steady 60 Hz at full
 *    quality and once with jittery frame times at low quality, must decide
 *    their first two battles in exactly the same state (nsw_state_hash).
 * 4. Camera, flown over the steady replays at 1920x1080: every hull at least
 *    6% of the frame height, no frame-to-frame zoom jump, and a ship out of
 *    frame in under 0.5% of fight frames and never for more than 1.5 s (the
 *    6% floor can make that unavoidable when two ships sit on opposite rims).
 *
 * 5. 300 random configurations (race subsets, matchup kinds, planet mass and
 *    size, boundary rate, time limit): two battles each resolve, with no NaN,
 *    only allowed races and the asked-for kind of battle.
 *
 * Argument: battles per ordered race pair (default 24, about 1000 in all).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "nsw_melee.h"
#include "nsw_melee_camera.h"

static int run_until_outro(NswWorld *w, float max_s) {
    int b0 = w->battle;
    for (float t = 0; t < max_s; t += 1.f / 60.f) {
        nsw_step(w, 1.f / 60.f);
        if (w->phase == PH_OUTRO || w->battle != b0) return 1;
    }
    return 0;
}

static int matrix(int reps) {
    static NswWorld w;
    int fails = 0;
    int wins[NSW_RACES][NSW_RACES] = {{0}}, games[NSW_RACES][NSW_RACES] = {{0}};
    double dur_sum = 0, cause[4] = {0};
    int dur_n = 0, short_n = 0, timeouts = 0, contracted = 0;
    static float ttk[NSW_RACES * NSW_RACES * 64];
    int nttk = 0, stalemates = 0, mutual = 0, comebacks = 0, decided = 0, planet_kills = 0;
    double specials[NSW_RACES] = {0}, sgames[NSW_RACES] = {0}, dsum = 0, dsq = 0;
    long dn = 0;
    for (int a = 0; a < NSW_RACES; a++)
        for (int b = 0; b < NSW_RACES; b++) {
            if (a == b) continue;
            for (int k = 0; k < reps; k++) {
                nsw_init(&w, 1000003u * (uint32_t)(a * 31 + b * 7 + k + 1), 0);
                int races[2] = {a, b}, teams[2] = {0, 1};
                nsw_start_battle(&w, races, teams, 2);
                unsigned long to0 = w.timeouts, c0 = w.contractions;
                if (!run_until_outro(&w, NSW_BATTLE_MAX + 20)) { printf("FAIL no resolution %d v %d rep %d\n", a, b, k); fails++; continue; }
                if (w.nan_seen) { printf("FAIL NaN %d v %d\n", a, b); fails++; }
                games[a][b]++;
                if (w.winner_team == 0) wins[a][b]++;
                dur_sum += w.last_duration; dur_n++;
                for (int c = 0; c < 4; c++) cause[c] += w.dmg_by[c];
                if (w.last_duration < 20.f) short_n++;
                int to = (int)(w.timeouts - to0);
                timeouts += to;
                contracted += w.contractions != c0;
                if (!to && nttk < (int)(sizeof ttk / sizeof ttk[0])) ttk[nttk++] = w.last_duration;
                stalemates += w.bs.longest_calm >= 20.f;
                mutual += w.winner_team < 0;
                planet_kills += w.bs.planet_kills;
                if (w.winner_team >= 0) { decided++; comebacks += w.bs.deficit[w.winner_team] >= 0.2f; }
                for (int s2 = 0; s2 < 2; s2++) { specials[w.ship[s2].race] += w.bs.specials[s2]; sgames[w.ship[s2].race] += 1; }
                dsum += w.bs.dist_sum; dsq += w.bs.dist_sq; dn += w.bs.dist_n;
            }
        }
    printf("1v1 win rate of row race against column race (%d battles per ordered pair):\n", reps);
    int bad_race = 0;
    for (int a = 0; a < NSW_RACES; a++) {
        int tw = 0, tg = 0;
        for (int b = 0; b < NSW_RACES; b++) {
            if (a == b) { printf("   -- "); continue; }
            printf("  %.2f", games[a][b] ? (double)wins[a][b] / games[a][b] : 0.0);
            tw += wins[a][b] + (games[b][a] - wins[b][a]);
            tg += games[a][b] + games[b][a];
        }
        double wr = tg ? (double)tw / tg : 0;
        printf("   overall %.2f %s\n", wr, NSW_RACE[a].name);
        if (wr < 0.2 || wr > 0.8) bad_race++;
    }
    printf("\ndamage by shots %.0f, rams %.0f, planet %.0f, chain %.0f\n", cause[0], cause[1], cause[2], cause[3]);
    printf("mean duration %.1f s, under 20 s: %d of %d, timeouts %d, boundary closed in %d\n",
           dur_sum / dur_n, short_n, dur_n, timeouts, contracted);
    /* Balance metrics beyond win rate (Fable's list). */
    for (int i = 1; i < nttk; i++)
        for (int j = i; j > 0 && ttk[j - 1] > ttk[j]; j--) { float t = ttk[j]; ttk[j] = ttk[j - 1]; ttk[j - 1] = t; }
    float p10 = nttk ? ttk[nttk / 10] : 0, p50 = nttk ? ttk[nttk / 2] : 0, p90 = nttk ? ttk[nttk * 9 / 10] : 0;
    double dm = dn ? dsum / dn : 0, dsd = dn ? sqrt(fmax(0.0, dsq / dn - dm * dm)) : 0;
    printf("time to kill p10 %.1f p50 %.1f p90 %.1f s; stalemates (20 s with no hit) %.1f%%; mutual kills %.1f%%; "
           "comebacks %.1f%% of decided; planet kills %.3f per battle; engagement distance %.2f +- %.2f\n",
           p10, p50, p90, 100.0 * stalemates / dur_n, 100.0 * mutual / dur_n, decided ? 100.0 * comebacks / decided : 0,
           (double)planet_kills / dur_n, dm, dsd);
    printf("specials per battle:");
    for (int r = 0; r < NSW_RACES; r++) printf(" %s %.1f", NSW_RACE[r].name, sgames[r] ? specials[r] / sgames[r] : 0);
    printf("\n");
    for (int r = 0; r < NSW_RACES; r++)
        if (sgames[r] && specials[r] / sgames[r] < 1.0) { printf("FAIL %s uses its special under once a battle\n", NSW_RACE[r].name); fails++; }
    double mean = dur_sum / dur_n;
    if (mean < 30.0 || mean > 80.0) { printf("FAIL mean battle %.1f s outside 30-80 s\n", mean); fails++; }
    if (bad_race) { printf("FAIL %d race(s) outside the 0.20-0.80 overall win band\n", bad_race); fails++; }
    return fails;
}

static int director(void) {
    static NswWorld w;
    int fails = 0, done = 0;
    unsigned prev_sig = 0;
    nsw_init(&w, 777u, 1);
    for (int b = 0; b < 1000; b++) {
        unsigned sig = 0; /* the set of races, whatever the order, sides or count */
        for (int i = 0; i < w.nships; i++) sig |= 1u << w.matchup[i];
        if (sig == prev_sig) { printf("FAIL repeated matchup at battle %d\n", b); fails++; }
        prev_sig = sig;
        int b0 = w.battle, ok = 0;
        for (float t = 0; t < NSW_BATTLE_MAX + 30; t += 1.f / 30.f) {
            nsw_step(&w, 1.f / 30.f);
            if (w.battle != b0) { ok = 1; break; }
        }
        if (!ok) { printf("FAIL director battle %d did not end\n", b); fails++; break; }
        done++;
        if (w.nan_seen) { printf("FAIL NaN in director battles\n"); fails++; break; }
    }
    printf("director battles completed: %d, timeouts %lu\n", done, w.timeouts);
    return fails;
}

typedef struct {
    long frames, small, lost, lost_floor, lost_punch, lost_n[NSW_MAX_SHIPS + 1];
    int lost_run, lost_run_max;
    double min_frac, max_jump;
} CamStats;

/* One camera frame: measure what a viewer at 1920x1080 would see. */
static void cam_measure(const NswCam *c, const NswWorld *w, float prev_zoom, CamStats *st) {
    const int W = 1920, H = 1080;
    if (w->phase != PH_FIGHT) { st->lost_run = 0; return; }
    st->frames++;
    int any_lost = 0;
    for (int i = 0; i < w->nships; i++) {
        const NswShip *s = &w->ship[i];
        if (!s->alive) continue;
        double frac = 2.0 * NSW_RACE[s->race].scale * c->zoom / H;
        if (frac < st->min_frac) st->min_frac = frac;
        if (frac < NSW_CAM_MIN_FRAC * 0.999) st->small++;
        float px, py;
        nsw_cam_to_px(c, W, H, s->p.x, s->p.y, &px, &py);
        if (px < 0 || px > W || py < 0 || py > H) any_lost = 1;
    }
    if (prev_zoom > 0) {
        double j = fabs(log(c->zoom / prev_zoom));
        if (j > st->max_jump) st->max_jump = j;
    }
    if (any_lost) {
        st->lost_floor += c->floor_limited;
        st->lost_punch += c->punch > 0;
        st->lost_n[w->nships]++;
    }
    if (any_lost && getenv("NSW_CAM_DEBUG") && (st->lost_run == 1 || st->lost_run == 60 || st->lost_run == 150)) {
        printf("DBG run %d t %.1f zoom %.1f rot %.2f cx %.2f %.2f fl %d:", st->lost_run, w->battle_t, c->zoom, c->rot, c->cx, c->cy, c->floor_limited);
        for (int i = 0; i < w->nships; i++) {
            float px, py;
            nsw_cam_to_px(c, W, H, w->ship[i].p.x, w->ship[i].p.y, &px, &py);
            printf(" [%s %d p %.2f,%.2f px %.0f,%.0f]", NSW_RACE[w->ship[i].race].name, w->ship[i].alive, w->ship[i].p.x, w->ship[i].p.y, px, py);
        }
        printf("\n");
    }
    if (any_lost) { st->lost++; st->lost_run++; if (st->lost_run > st->lost_run_max) st->lost_run_max = st->lost_run; }
    else st->lost_run = 0;
}

/* Replays: the steady run also flies the camera. Returns failures. */
static int replays(int nseeds) {
    static NswWorld a, b;
    NswCam cam;
    CamStats st = {0};
    st.min_frac = 1e9;
    int fails = 0, mismatches = 0;
    for (int seed = 1; seed <= nseeds; seed++) {
        uint32_t ha[2] = {0}, hb[2] = {0};
        nsw_init(&a, 0x51ED0000u + (uint32_t)seed, 0);
        nsw_cam_init(&cam, 0, 0.28f, (float)seed);
        for (int f = 0; a.battles_done < 2 && f < 60 * 400; f++) {
            unsigned long d0 = a.battles_done;
            float z0 = cam.ready ? cam.zoom : 0;
            nsw_step(&a, 1.f / 60.f);
            nsw_cam_update(&cam, &a, 1920, 1080, 1.f / 60.f);
            cam_measure(&cam, &a, z0, &st);
            if (a.battles_done != d0) ha[d0] = a.last_hash;
        }
        nsw_init(&b, 0x51ED0000u + (uint32_t)seed, 1);
        uint32_t j = 0x1234567u * (uint32_t)seed | 1u;
        for (int f = 0; b.battles_done < 2 && f < 60 * 800; f++) {
            unsigned long d0 = b.battles_done;
            j ^= j << 13; j ^= j >> 17; j ^= j << 5;
            nsw_step(&b, 0.004f + 0.046f * (float)(j & 0xffff) / 65535.f);
            if (b.battles_done != d0) hb[d0] = b.last_hash;
        }
        if (a.battles_done < 2 || b.battles_done < 2 || ha[0] != hb[0] || ha[1] != hb[1]) {
            if (mismatches++ < 5)
                printf("FAIL replay seed %d: %08x %08x vs %08x %08x\n", seed, ha[0], ha[1], hb[0], hb[1]);
        }
    }
    printf("replays: %d seeds, %d mismatched\n", nseeds, mismatches);
    if (mismatches) fails++;
    printf("camera: %ld fight frames, smallest hull %.3f of the height (%ld frames under 6%%), "
           "largest zoom step %.4f/frame, ship out of frame %ld frames (%.2f%%), longest %d frames\n",
           st.frames, st.min_frac, st.small, st.max_jump, st.lost, 100.0 * st.lost / (st.frames ? st.frames : 1), st.lost_run_max);
    printf("  lost frames: %ld at the 6%% floor, %ld in a kill punch-in; by ships in battle: 2:%ld 3:%ld 4:%ld\n",
           st.lost_floor, st.lost_punch, st.lost_n[2], st.lost_n[3], st.lost_n[4]);
    if (st.small) { printf("FAIL hull under 6%% of the frame height\n"); fails++; }
    if (st.max_jump > 0.03) { printf("FAIL camera zoom jumps (%.4f per frame)\n", st.max_jump); fails++; }
    if (st.lost > st.frames / 200 || st.lost_run_max > 90) { printf("FAIL camera loses ships\n"); fails++; }
    return fails;
}

static int configs(int n) {
    static NswWorld w;
    int fails = 0;
    uint32_t z = 0xC0FFEEu;
#define RND() (z ^= z << 13, z ^= z >> 17, z ^= z << 5, (float)(z & 0xffffffu) / 16777216.f)
    for (int k = 0; k < n; k++) {
        NswConfig c;
        nsw_config_default(&c);
        int want = 2 + (int)(RND() * (NSW_RACES - 1));   /* 2..7 races */
        c.race_mask = 0;
        while (__builtin_popcount(c.race_mask) < want) c.race_mask |= 1u << (int)(RND() * NSW_RACES);
        c.matchups = (int)(RND() * 4);
        c.gravity = 3.f * RND();
        c.planet = 0.5f + 1.5f * RND();
        c.contract = 3.f * RND();
        c.battle_max = 30.f + 150.f * RND();
        nsw_init_cfg(&w, 7919u * (uint32_t)(k + 1), k & 1, &c);
        int b0 = w.battle, bad = 0;
        for (float t = 0; w.battles_done < 2 && t < 2 * (c.battle_max + 30); t += 1.f / 30.f) {
            nsw_step(&w, 1.f / 30.f);
            if (w.battle != b0) {
                b0 = w.battle;
                for (int i = 0; i < w.nships; i++)
                    if (!(c.race_mask >> w.matchup[i] & 1u)) bad = 1;
                int kind_n = c.matchups == NSW_MATCH_DUEL ? 2 : c.matchups == NSW_MATCH_TEAMS ? 4 :
                             c.matchups == NSW_MATCH_FFA ? (want >= 3 ? 3 : 2) : w.nships;
                if (w.nships != kind_n) bad = 2;
            }
        }
        if (w.battles_done < 2 || w.nan_seen || bad) {
            if (fails < 5) printf("FAIL config %d: done %lu nan %d bad %d\n", k, w.battles_done, w.nan_seen, bad);
            fails++;
        }
    }
#undef RND
    printf("configurations: %d random, %d failed\n", n, fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 24;
    int seeds = argc > 2 ? atoi(argv[2]) : 200;
    int fails = matrix(reps);
    fails += director();
    fails += replays(seeds);
    fails += configs(300);
    printf(fails ? "RESULT FAIL (%d)\n" : "RESULT OK\n", fails);
    return fails ? 1 : 0;
}
