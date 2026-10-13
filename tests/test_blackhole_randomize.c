/* SPDX-License-Identifier: Apache-2.0 */
/* test_blackhole_randomize.c - deterministic, in-envelope, class-gated rolls.
 *
 * Fail-before/pass-after: before this feature the symbols did not exist. The
 * test pins the safety envelope from docs/BLACKHOLE-RANDOMIZE.md and the
 * class gating rule (a scene flagged above the GPU class is never picked
 * unless the heavy pool was opted into).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "blackhole_randomize.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

/* The documented envelope: one row per rolled option, [weak],[mid],[strong].
 * Every value must also sit inside the option's schema min/max (checked by
 * blackhole-schema-in-sync plus eye here). */
typedef struct { const char *name; double wlo, whi, mlo, mhi, slo, shi; } Row;
static const Row ENV[] = {
    {"exposure",            0.60, 1.40, 0.60, 1.60, 0.60, 1.80},
    {"disk-brightness",     0.70, 1.40, 0.70, 1.50, 0.70, 1.60},
    {"bloom",               0.00, 0.00, 0.00, 0.90, 0.00, 1.80},
    {"fringe",              0.00, 0.35, 0.00, 0.35, 0.00, 0.35},
    {"beaming",             0.50, 1.60, 0.50, 1.60, 0.50, 1.60},
    {"star-density",        0.40, 1.00, 0.50, 1.40, 0.50, 1.80},
    {"nebula",              0.00, 0.60, 0.20, 1.20, 0.20, 1.50},
    {"jet",                 0.40, 1.50, 0.40, 1.50, 0.40, 1.50},
    {"hot-sector",          0.05, 0.45, 0.05, 0.45, 0.05, 0.45},
    {"temperature",         0.70, 1.30, 0.70, 1.30, 0.70, 1.30},
    {"rotation-speed",      0.40, 1.60, 0.40, 1.60, 0.40, 1.60},
    {"precession",          0.30, 1.80, 0.30, 1.80, 0.30, 1.80},
    {"disk-outer",          8.00, 30.0, 8.00, 30.0, 8.00, 30.0},
    {"jet-length",          10.0, 60.0, 10.0, 60.0, 10.0, 60.0},
    {"spin",                0.00, 0.90, 0.00, 0.90, 0.00, 0.90},
    {"ripples",             0.40, 1.50, 0.40, 1.50, 0.40, 1.50},
    {"speed",               0.60, 1.60, 0.60, 1.60, 0.60, 1.60},
    {"palette-transition",  3.00, 15.0, 3.00, 15.0, 3.00, 15.0},
};
#define NENV (sizeof ENV / sizeof ENV[0])

#define GET(r, n) (!strcmp(n, "exposure") ? (r).exposure : !strcmp(n, "disk-brightness") ? (r).disk_brightness : \
                   !strcmp(n, "bloom") ? (r).bloom : !strcmp(n, "fringe") ? (r).fringe : !strcmp(n, "beaming") ? (r).beaming : \
                   !strcmp(n, "star-density") ? (r).star_density : !strcmp(n, "nebula") ? (r).nebula : !strcmp(n, "jet") ? (r).jet : \
                   !strcmp(n, "hot-sector") ? (r).hot_sector : !strcmp(n, "temperature") ? (r).temperature : \
                   !strcmp(n, "rotation-speed") ? (r).rotation_speed : !strcmp(n, "precession") ? (r).precession : \
                   !strcmp(n, "disk-outer") ? (r).disk_outer : !strcmp(n, "jet-length") ? (r).jet_length : \
                   !strcmp(n, "spin") ? (r).spin : !strcmp(n, "ripples") ? (r).ripples : \
                   !strcmp(n, "speed") ? (r).speed : (r).palette_transition)

static const char *CLASSES[3] = {"weak", "mid", "strong"};

static const BhRandomPreset CAT[] = {
    {"alpha-weak", "weak"},
    {"beta-mid", "mid"},
    {"gamma-strong", "strong"},
    {"delta-weak", "weak"},
};
#define NCAT (sizeof CAT / sizeof CAT[0])

static int palette_ok(const char *p) {
    static const char *P[10] = {"stylized","kipthorne","faithful","singularity","slingshot",
                                "whitehole","eht","psychedelic","synthwave","cygnus"};
    for (int i = 0; i < 10; i++) if (!strcmp(p, P[i])) return 1;
    return 0;
}

static int rank_of(const char *c) { return !strcmp(c,"strong")?2:!strcmp(c,"mid")?1:0; }

static void test_determinism(void) {
    BhRandomRoll a, b;
    CHECK(bh_randomize_roll(123456u, BH_RAND_EVERYTHING, "mid", 0, CAT, NCAT, NULL, &a) == 0, "roll failed");
    CHECK(bh_randomize_roll(123456u, BH_RAND_EVERYTHING, "mid", 0, CAT, NCAT, NULL, &b) == 0, "roll failed");
    CHECK(memcmp(&a, &b, sizeof a) == 0, "same seed is not deterministic");
    BhRandomRoll c;
    bh_randomize_roll(123457u, BH_RAND_EVERYTHING, "mid", 0, CAT, NCAT, NULL, &c);
    CHECK(memcmp(&a, &c, sizeof a) != 0, "different seeds produced the same roll");
    /* A look roll has no preset. */
    BhRandomRoll l;
    CHECK(bh_randomize_roll(9u, BH_RAND_LOOK, "weak", 0, NULL, 0, NULL, &l) == 0, "look roll needs no catalogue");
    CHECK(l.preset[0] == 0, "look roll picked a preset");
    CHECK(palette_ok(l.palette), "look roll palette %s", l.palette);
}

static void test_envelope(void) {
    long checked = 0;
    for (int ci = 0; ci < 3; ci++) {
        for (uint32_t s = 1; s <= 10000u; s++) {
            BhRandomRoll r;
            if (bh_randomize_roll(s, BH_RAND_EVERYTHING, CLASSES[ci], 0, CAT, NCAT, NULL, &r) != 0) { CHECK(0, "roll failed"); continue; }
            for (size_t k = 0; k < NENV; k++) {
                double v = GET(r, ENV[k].name);
                double lo = ci == 0 ? ENV[k].wlo : ci == 1 ? ENV[k].mlo : ENV[k].slo;
                double hi = ci == 0 ? ENV[k].whi : ci == 1 ? ENV[k].mhi : ENV[k].shi;
                if (!(v >= lo - 1e-9 && v <= hi + 1e-9)) {
                    CHECK(0, "class %s %s=%.6f outside [%.3f,%.3f] seed=%u", CLASSES[ci], ENV[k].name, v, lo, hi, s);
                    k = NENV; /* one report per seed */
                }
            }
            /* weak must never bloom (the black-floor audit's rule) */
            if (ci == 0) CHECK(r.bloom == 0.0, "weak rolled bloom=%.3f", r.bloom);
            CHECK(palette_ok(r.palette), "palette %s", r.palette);
            checked++;
        }
    }
    fprintf(stderr, "envelope: %ld seeded rolls checked, all inside the table\n", checked);
}

static void test_class_gating(void) {
    for (int ci = 0; ci < 3; ci++) {
        for (int heavy = 0; heavy <= 1; heavy++) {
            int want = heavy ? 2 : ci;
            for (uint32_t s = 1; s <= 10000u; s++) {
                BhRandomRoll r;
                if (bh_randomize_roll(s, BH_RAND_EVERYTHING, CLASSES[ci], heavy, CAT, NCAT, NULL, &r) != 0) { CHECK(0, "roll failed"); continue; }
                /* find the picked preset and check its declared class */
                int got = -1;
                for (size_t k = 0; k < NCAT; k++) if (!strcmp(CAT[k].id, r.preset)) got = rank_of(CAT[k].min_class);
                CHECK(got >= 0, "picked unknown preset '%s'", r.preset);
                CHECK(got <= want, "class %s heavy=%d picked '%s' (rank %d > %d)", CLASSES[ci], heavy, r.preset, got, want);
                if (ci == 0 && !heavy) CHECK(got == 0, "weak picked a heavier scene '%s'", r.preset);
            }
        }
    }
    /* No allowed preset -> the roll refuses rather than overreach. */
    BhRandomRoll r;
    CHECK(bh_randomize_roll(1u, BH_RAND_EVERYTHING, "weak", 0, NULL, 0, NULL, &r) == -1, "empty catalogue should fail");
    CHECK(bh_randomize_roll(1u, BH_RAND_OFF, "weak", 0, CAT, NCAT, NULL, &r) == -1, "off is not a roll mode");
}

static void test_pinning(void) {
    /* Pin palette: 10 000 rolled configurations must never carry a palette. */
    for (uint32_t s = 1; s <= 10000u; s++) {
        BhRandomRoll r;
        CHECK(bh_randomize_roll(s, BH_RAND_EVERYTHING, "strong", 1, CAT, NCAT, "palette", &r) == 0, "roll failed");
        CHECK((r.rolled & BH_RF_PALETTE) == 0, "pinned palette was rolled seed=%u", s);
        CHECK(r.palette[0] == 0, "pinned palette changed seed=%u -> %s", s, r.palette);
        CHECK((r.rolled & BH_RF_PRESET) != 0, "preset should still roll when only palette is pinned");
    }
    /* Pin preset and palette: the scene never changes, the look still rolls. */
    for (uint32_t s = 1; s <= 10000u; s++) {
        BhRandomRoll r;
        CHECK(bh_randomize_roll(s, BH_RAND_EVERYTHING, "strong", 1, CAT, NCAT, "preset,palette", &r) == 0, "roll failed");
        CHECK((r.rolled & BH_RF_PRESET) == 0, "pinned preset was rolled seed=%u", s);
        CHECK(r.preset[0] == 0, "pinned preset changed seed=%u -> %s", s, r.preset);
        CHECK((r.rolled & BH_RF_EXPOSURE) != 0, "an unpinned look option must still roll");
    }
    BhRandomRoll r;
    CHECK(bh_randomize_roll(7u, BH_RAND_LOOK, "mid", 0, NULL, 0, "bloom,exposure", &r) == 0, "roll failed");
    CHECK((r.rolled & (BH_RF_BLOOM | BH_RF_EXPOSURE)) == 0, "pinned look options rolled");
    CHECK((r.rolled & BH_RF_SPEED) != 0, "unpinned option did not roll");
    CHECK(bh_randomize_pinned("a,bc,palette", "palette"), "pinned helper missed palette");
    CHECK(!bh_randomize_pinned("a,bc,palette", "pal"), "pinned helper prefix-matched");
    CHECK(!bh_randomize_pinned(NULL, "palette"), "NULL pin list");
}

int main(void) {
    test_determinism();
    test_envelope();
    test_class_gating();
    test_pinning();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("blackhole randomize: ok\n");
    return 0;
}
