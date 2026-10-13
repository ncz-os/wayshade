/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_goo_cfg.h - every setting of the goo engine and the seeded
 * randomizer that fills the ones the user (or a scene) did not set. Pure C,
 * no GL: tests/test_magmasimplex_randomize.c draws thousands of these.
 *
 * Reproducibility: the same (scene values, explicit options, seed) always
 * resolves to the same configuration; the saver prints it in one [diag] line.
 */
#ifndef MAGMASIMPLEX_GOO_CFG_H
#define MAGMASIMPLEX_GOO_CFG_H
#include <stdint.h>
#include <stddef.h>
#include "magmasimplex_palettes.h"

#define MS_UNSET_I (-99)
/* float settings use NAN for unset */

enum { MS_VIEW_AUTO = -1, MS_VIEW_INSIDE = 0, MS_VIEW_GLASS = 1, MS_VIEW_SHOW = 2, MS_VIEW_FIELD = 3, MS_VIEW_BUTTERFLY = 4 };
enum { MS_WORLD_TANK = 0, MS_WORLD_DISH = 1, MS_WORLD_RADIAL = 2, MS_WORLD_DRIFT = 3 };
enum { MS_MAT_MILKY = 0, MS_MAT_TRANSLUCENT = 1, MS_MAT_JELLY = 2 };
/* projector-mask (light shows): fill the screen; the soft round throw with a
 * dark surround; a small bright dish on black (the 1969 Fillmore footage). */
enum { MS_MASK_OFF = 0, MS_MASK_SOFT = 1, MS_MASK_DISH = 2, MS_MASK_DISC = 3 };
/* overlay (Flowerchild): extra projector layers over the dye. */
enum { MS_OVERLAY_OFF = 0, MS_OVERLAY_STREAKS = 1, MS_OVERLAY_NEON = 2, MS_OVERLAY_BOTH = 3 };
/* dendrites (Flowerchild): a branching ink tree or a fibrous vein network
 * drawn over the dye field. Off unless a scene asks for it, so no existing
 * preset changes. */
enum { MS_DEND_OFF = 0, MS_DEND_INK = 1, MS_DEND_VEINS = 2 };
/* projector-mask-size: 0 = the style's default (now a large disc that fills
 * most of the screen); otherwise the dish/disc radius as a fraction of the
 * viewport's half short side, clamped to [0.30, 1.00]. The legacy small dish
 * (the 1969 footage, the operator's 0.47 reading) is the `fillmore-small`
 * scene. */
#define MS_MASK_SIZE_DEFAULT 0.92f
#define MS_MASK_SIZE_MIN 0.30f
#define MS_MASK_SIZE_MAX 1.00f
#define MS_MASK_SIZE_LEGACY_DISH 0.47f
#define MS_MASK_SIZE_LEGACY_DISC 0.80f
enum { MS_ERA_ANY = 0, MS_ERA_1960S, MS_ERA_1970S, MS_ERA_1980S, MS_ERA_1990S, MS_ERA_MODERN };
/* palette: table index >= 0, or one of these */
enum { MS_PAL_GEN_VINTAGE = -2, MS_PAL_GEN_NEON = -3 };
/* audio: the shared beat engine's capture source (ncz_audio_bus.h). */
enum { MS_AUDIO_OFF = 0, MS_AUDIO_SYSTEM = 1, MS_AUDIO_MIC = 2 };

typedef struct {
    int randomize;      /* 1: draw unset settings from the seed; 0: defaults */
    int view;           /* MS_VIEW_*; AUTO = chapters */
    int world;
    int palette;
    int era;            /* narrows palette draws */
    int material;
    int glitter;        /* -1 as the palette says, 0 none, 1 silver, 2 gold, 3 rainbow */
    int startup;        /* 1: the lamp warms up from cold */
    int cap;            /* 1: cooled wax collects under the top */
    int kaleidoscope;   /* light show: 0 off, else sectors */
    float texture, viscosity, wax, heat, speed, motes, glass, depth, drift, chapter, glitter_amount;
    int quality;        /* 0 light, 1 full (not drawn: the harness decides) */
    int projector_mask; /* light shows: MS_MASK_* */
    float projector_mask_size; /* dish/disc radius fraction; 0 = style default */
    int overlay;        /* Flowerchild projector layers: MS_OVERLAY_* */
    int poster;         /* Flowerchild: flat colour bands with hard edges */
    /* The beat/audio layer (all magmasimplex modes; docs/AUDIO-BUS.md). */
    float dynamics;      /* 0..1; 0 = the pre-beat look exactly (never drawn) */
    int   tempo;         /* 0 = auto (96..132 BPM), else 60..180 */
    int   audio;         /* MS_AUDIO_*; off in every preset unless selected */
    int dendrites;      /* Flowerchild: MS_DEND_* (off unless a scene asks) */
    float dendrite_strength; /* Flowerchild: 0..1 */
} MsGooConfig;

/* All settings unset (randomize on). */
void ms_goo_cfg_unset(MsGooConfig *c);
/* Fill every unset setting: drawn from seed when c->randomize, else the
 * default. Palette coherence: drawn palettes pass ms_palette_readable. */
void ms_goo_cfg_resolve(MsGooConfig *c, uint32_t seed);
/* The resolved palette (table entry or generated from the seed). */
void ms_goo_cfg_palette(const MsGooConfig *c, uint32_t seed, MsPalette *out);
/* One line, "key=value ..." with every setting, for the [diag] log. */
void ms_goo_cfg_describe(const MsGooConfig *c, char *out, size_t n);

const char *ms_goo_view_name(int v);
const char *ms_goo_world_name(int w);
const char *ms_goo_material_name(int m);
const char *ms_goo_era_name(int e);
const char *ms_goo_mask_name(int m);
const char *ms_goo_overlay_name(int m);
const char *ms_goo_audio_name(int a);
const char *ms_goo_dendrites_name(int m);
/* Effective dish/disc radius (p units, half short side = 1) for a mask style
 * and a projector-mask-size setting: 0 = the style's default (0.92, large);
 * a positive value is clamped to [0.30, 1.00]; mask off/soft has no radius. */
float ms_goo_mask_radius(int mask, float size);
/* Time scale: simulation seconds per wall second at speed 1, calibrated so
 * bodies rise and sink at the rates measured on real lamps (docs table). */
float ms_goo_time_scale(void);

#endif
