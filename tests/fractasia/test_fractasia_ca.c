/* SPDX-License-Identifier: Apache-2.0 */
/* test_fractasia_ca.c — CPU stepping of the fractasia cellular automaton.
 *
 * No GL needed. Asserts the rule application against hand-computed rows, the
 * conservation property of rule 184, wrap-around, determinism, and that every
 * shipped rule keeps a live, changing pattern over thousands of rows. The last
 * is the regression for the old shader, which degenerated into vertical bars.
 *
 * Original to ncz-screensavers (Apache-2.0).
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fractasia/fractasia.h"

static int live(const uint8_t *r) {
    int n = 0;
    for (int i = 0; i < FRACT_CA_W; i++) n += r[i];
    return n;
}

int main(void) {
    uint8_t a[FRACT_CA_W], b[FRACT_CA_W];
    const int c0 = FRACT_CA_W / 2; /* the single live cell of a fresh row */

    /* Rule 90 from one cell is Sierpinski: row 1 has cells at +-1, row 2 at +-2 (the middle is left xor right = 0). */
    fract_ca_init_row(90, 1, a);
    assert(live(a) == 1 && a[FRACT_CA_W / 2] == 1);
    fract_ca_step(90, a, b);
    assert(live(b) == 2 && b[c0 - 1] == 1 && b[c0 + 1] == 1 && b[c0] == 0);
    fract_ca_step(90, b, a);
    assert(live(a) == 2 && a[c0 - 2] == 1 && a[c0 + 2] == 1 && a[c0] == 0);

    /* Rule 30 first rows: 1 -> 3 cells (111) -> 3 cells (11 .. 1: cells -2, -1, +2). */
    fract_ca_init_row(30, 1, a);
    fract_ca_step(30, a, b);
    assert(live(b) == 3 && b[c0 - 1] && b[c0] && b[c0 + 1]);
    fract_ca_step(30, b, a);
    assert(live(a) == 3 && a[c0 - 2] && a[c0 - 1] && !a[c0] && !a[c0 + 1] && a[c0 + 2]);

    /* Wrap-around: a cell at the right edge feeds the left edge. */
    memset(a, 0, sizeof a);
    a[FRACT_CA_W - 1] = 1;
    fract_ca_step(90, a, b);
    assert(b[0] == 1 && b[FRACT_CA_W - 2] == 1);

    /* The ring width must not be a power of two: rule 90 is nilpotent there. */
    assert((FRACT_CA_W & (FRACT_CA_W - 1)) != 0);

    /* Rule 184 conserves the number of live cells. */
    fract_ca_init_row(184, 7, a);
    int n0 = live(a);
    assert(n0 > FRACT_CA_W / 4 && n0 < 3 * FRACT_CA_W / 4); /* a random row, not a lone cell */
    for (int i = 0; i < 500; i++) {
        fract_ca_step(184, a, b);
        memcpy(a, b, sizeof a);
        assert(live(a) == n0);
    }

    /* Determinism: same seed, same row; different seed, different row. */
    uint8_t c[FRACT_CA_W], d[FRACT_CA_W];
    fract_ca_init_row(110, 5, c);
    fract_ca_init_row(110, 5, d);
    assert(memcmp(c, d, sizeof c) == 0);
    fract_ca_init_row(110, 6, d);
    assert(memcmp(c, d, sizeof c) != 0);

    /* Every shipped rule stays alive AND keeps changing for thousands of rows. */
    const int rules[8] = {30, 90, 110, 184, 54, 60, 102, 150};
    for (int k = 0; k < 8; k++) {
        fract_ca_init_row(rules[k], 11, a);
        int changed_rows = 0, empty_rows = 0, full_rows = 0;
        for (int row = 0; row < 4000; row++) {
            fract_ca_step(rules[k], a, b);
            if (memcmp(a, b, sizeof a) != 0) changed_rows++;
            int n = live(b);
            if (n == 0) empty_rows++;
            if (n == FRACT_CA_W) full_rows++;
            memcpy(a, b, sizeof a);
        }
        printf("rule %3d: changed=%d empty=%d full=%d\n", rules[k], changed_rows, empty_rows, full_rows);
        /* Not all rules can stay busy forever (184 is a conserved traffic flow
         * that settles into a steady shift), but none may die out. */
        assert(empty_rows == 0 && full_rows == 0);
        assert(changed_rows > 3000);
    }

    printf("ok\n");
    return 0;
}
