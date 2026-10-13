/* SPDX-License-Identifier: Apache-2.0 */
/* test_magmasimplex_goo.c - headless gate for the magmasimplex wax bodies.
 *
 * The operator's requirement: several distinct blobs on screen at any time,
 * persisting over minutes, never collapsing into one mass or a wash. For
 * many seeds, the body budgets (12/20 single layer; 9/12 per layer with the
 * cap mass for the sixties views) and the aspects in use, run the cycle
 * (after the same 90 s prewarm the saver uses) and check the number of
 * separate masses at 20, 60, 120 and 300 s (at least three), and sample
 * every second: never zero, and fewer than two in at most 0.05 % of the
 * sampled seconds (brief moments while two bodies fuse).
 */
#include <stdio.h>
#include "magmasimplex_goo_sim.h"

int main(void) {
    const float checks[] = {20, 60, 120, 300};
    /* Screen aspects; the dish world runs a 4:3 column and radial 1.78. */
    const float asps[] = {16.f / 9.f, 16.f / 10.f, 4.f / 3.f, 21.f / 9.f};
    /* Body budgets: the old single-layer 12/20 and the per-layer 9 (light)
     * and 12 (full) the sixties views use, with and without the cap mass. */
    const int budgets[] = {12, 20, 9, 12};
    const int caps[] = {0, 0, 1, 1};
    int fails = 0, runs = 0, worst = 99;
    long hist[16] = {0};
    for (int bi = 0; bi < 4; bi++)
        for (int ai = 0; ai < 4; ai++)
            for (unsigned seed = 1; seed <= 40; seed++) {
                MsGooSim s;
                /* Alternate single-colour and multicolour runs: the colour draw
                 * consumes random numbers, so it changes the trajectories. */
                ms_goo_sim_init_cap(&s, seed * 2654435761u, budgets[bi], (int)(seed & 1u), asps[ai], 90.f, caps[bi]);
                int ci = 0, low = 99;
                for (int f = 0; f <= 300 * 30; f++) {
                    float t = f / 30.f;
                    int v = ms_goo_sim_visible(&s);
                    if (v < 0) v = 0;
                    if (f % 30 == 0 && t >= 5.f) {
                        if (v < low) low = v;
                        hist[v > 15 ? 15 : v]++;
                    }
                    if (ci < 4 && t >= checks[ci]) {
                        if (v < 3) {
                            fails++;
                            printf("FAIL budget=%d cap=%d asp=%.2f seed=%u t=%.0f visible=%d alive=%d\n", budgets[bi],
                                   caps[bi], asps[ai], seed, t, v, ms_goo_sim_alive(&s));
                        }
                        ci++;
                    }
                    ms_goo_sim_step(&s, 1.f / 30.f);
                }
                if (low < worst) worst = low;
                if (low < 1) {
                    fails++;
                    printf("FAIL budget=%d cap=%d asp=%.2f seed=%u no separate mass at some second\n", budgets[bi],
                           caps[bi], asps[ai], seed);
                }
                runs++;
            }
    long total = 0;
    for (int i = 0; i < 16; i++) total += hist[i];
    double below2 = total ? (double)(hist[0] + hist[1]) / (double)total : 0.0;
    printf("runs=%d fails=%d worst_min_visible=%d seconds_below_two=%.4f%%\nvisible histogram (per second):", runs,
           fails, worst, below2 * 100.0);
    for (int i = 0; i < 16; i++) printf(" %d:%ld", i, hist[i]);
    printf("\n");
    if (below2 > 0.0005) {
        printf("FAIL fewer than two masses in more than 0.05%% of sampled seconds\n");
        fails++;
    }
    return fails ? 1 : 0;
}
