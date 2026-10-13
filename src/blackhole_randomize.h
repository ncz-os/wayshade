/* SPDX-License-Identifier: Apache-2.0 */
/* blackhole_randomize.h - the "Randomize everything" re-roll of blackhole_gles3.
 *
 * The option `randomize` (off | look | everything) re-rolls the look on every
 * screensaver start. `look` re-rolls the palette and the look/tuning options
 * inside the safe envelope (docs/BLACKHOLE-RANDOMIZE.md); `everything` also
 * picks the scene type at random from the presets this GPU class may run.
 *
 * The roll itself (bh_randomize_roll) is pure: no file I/O, no GL, no globals,
 * so the unit test can drive thousands of seeded rolls. The preset catalogue
 * (bh_randomize_catalogue) is the only part that touches the filesystem.
 */
#ifndef BLACKHOLE_RANDOMIZE_H
#define BLACKHOLE_RANDOMIZE_H
#include <stddef.h>
#include <stdint.h>

/* Randomize modes, matching the `randomize` option choices. */
enum { BH_RAND_OFF = 0, BH_RAND_LOOK = 1, BH_RAND_EVERYTHING = 2 };

/* A preset the launcher may run, with its declared minimum GPU class. */
typedef struct {
    char id[64];
    char min_class[8]; /* weak | mid | strong */
} BhRandomPreset;

/* One rolled configuration, all values already inside the envelope.
 * `rolled` has the BH_RF_* bit of each option that was actually drawn; an
 * option named in pinned_csv keeps its bit clear, so the caller must not write
 * it. Strings point at the arrays below and stay valid. */
enum {
    BH_RF_EXPOSURE = 1u << 0,  BH_RF_DISK_BRIGHTNESS = 1u << 1,
    BH_RF_BLOOM = 1u << 2,     BH_RF_FRINGE = 1u << 3,
    BH_RF_BEAMING = 1u << 4,   BH_RF_STAR_DENSITY = 1u << 5,
    BH_RF_NEBULA = 1u << 6,    BH_RF_JET = 1u << 7,
    BH_RF_HOT_SECTOR = 1u << 8, BH_RF_TEMPERATURE = 1u << 9,
    BH_RF_ROTATION_SPEED = 1u << 10, BH_RF_PRECESSION = 1u << 11,
    BH_RF_DISK_OUTER = 1u << 12, BH_RF_JET_LENGTH = 1u << 13,
    BH_RF_SPIN = 1u << 14,     BH_RF_RIPPLES = 1u << 15,
    BH_RF_SPEED = 1u << 16,    BH_RF_PALETTE_TRANSITION = 1u << 17,
    BH_RF_PALETTE = 1u << 18,  BH_RF_PRESET = 1u << 19,
};

typedef struct {
    uint32_t seed;
    uint32_t rolled;       /* BH_RF_* bits of the options that were drawn */
    int mode;              /* BH_RAND_LOOK or BH_RAND_EVERYTHING */
    char preset[64];       /* chosen preset id, empty for mode==LOOK or pinned */
    char palette[24];      /* chosen palette name, empty when pinned */
    char mode_name[16];    /* "look" or "everything" */
    /* Look/tuning options (see docs/BLACKHOLE-RANDOMIZE.md for the ranges). */
    double exposure, disk_brightness, bloom, fringe, beaming;
    double star_density, nebula, jet, hot_sector, temperature;
    double rotation_speed, precession, disk_outer, jet_length, spin;
    double ripples, speed, palette_transition;
    char style[16];        /* classic | enhanced */
} BhRandomRoll;

/* Roll one configuration from `seed`.
 *
 * gpu_class is "weak", "mid" or "strong" (the class of the GPU that will
 * render it). heavy_pool is nonzero when the user opted into the heavy pool
 * (launcher pool-gpu-class=all/strong): only then may a scene whose
 * min-gpu-class is above gpu_class be picked.
 *
 * mode must be BH_RAND_LOOK or BH_RAND_EVERYTHING. For BH_RAND_EVERYTHING the
 * preset catalogue must hold at least one allowed preset. `pinned_csv` is a
 * comma-separated list of option names the user (or the image default) set
 * explicitly: those are never drawn (precedence env/CLI > pinned stored >
 * rolled > engine default). May be NULL or empty. Returns 0 on success and -1
 * when mode is not a roll mode or no preset is allowed. */
int bh_randomize_roll(uint32_t seed, int mode, const char *gpu_class,
                      int heavy_pool, const BhRandomPreset *presets, size_t npresets,
                      const char *pinned_csv, BhRandomRoll *out);

/* Nonzero when `name` is named in the comma-separated `csv` (NULL -> 0). */
int bh_randomize_pinned(const char *csv, const char *name);

/* Rank of a class name (weak=0, mid=1, strong=2); unknown -> weak. */
int bh_class_rank(const char *name);

/* Scan the preset directories for `group` ("blackhole") and fill up to `cap`
 * catalogue entries (sorted by id). Returns the number written, or 0. */
size_t bh_randomize_catalogue(const char *group, BhRandomPreset *out, size_t cap);

#endif /* BLACKHOLE_RANDOMIZE_H */
