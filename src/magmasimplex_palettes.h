/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_palettes.h - every liquid/wax pairing magmasimplex knows, with
 * provenance, plus a generator for coherent new pairs. Pure C (no GL), so the
 * randomize test can check them headless. See docs/MAGMASIMPLEX-60S.md. */
#ifndef MAGMASIMPLEX_PALETTES_H
#define MAGMASIMPLEX_PALETTES_H
#include <stdint.h>

enum {
    MS_PAL_CLEAR = 1,        /* untinted liquid: lit by the bulb, the wax carries the colour */
    MS_PAL_MIST = 2,         /* hazy liquid ("Champagne Mist", "Blue Mist") */
    MS_PAL_GLITTER_SILVER = 4,
    MS_PAL_GLITTER_GOLD = 8,
    MS_PAL_GLITTER_RAINBOW = 16,
    MS_PAL_SRGB = 32,        /* colours below are sRGB 0..255 as sampled, converted at lookup */
    MS_PAL_DARK = 64,        /* keep the liquid dark: not normalised to full intensity (dye fields) */
};

typedef struct {
    const char *id;      /* option value, e.g. "code-06", "way-07", "yellow-on-pink" */
    const char *label;   /* "clear/coral-red" (liquid/wax) */
    const char *era;     /* 1960s, 1970s, 1990s, modern */
    const char *source;  /* codes-1992-2004, era-pairings, magmasimplex-ways, photos, footage */
    float liquid[3], wax[3], secondary[3]; /* secondary all zero: derived from wax */
    unsigned flags;
} MsPaletteDef;

/* A palette ready for the renderer: linear colours, glitter kind, flags. */
typedef struct {
    float liquid[3], wax[3], secondary[3], bulb[3];
    int clear, mist, glitter; /* glitter: 0 none, 1 silver, 2 gold, 3 rainbow */
    char id[32];
} MsPalette;

int ms_palette_count(void);
const MsPaletteDef *ms_palette_def(int i);
int ms_palette_find(const char *id);   /* -1 if unknown */
/* Linear, renderer-ready palette for table entry i. */
void ms_palette_get(int i, MsPalette *out);
/* A generated pair. family: 0 vintage (warm, 1960s-leaning), 1 modern neon.
 * Always passes ms_palette_readable. */
void ms_palette_generate(uint32_t *rng, int family, MsPalette *out);
/* Readability rule: wax and liquid differ enough in luminance or hue, the
 * liquid is saturated enough to glow, and neither is a muddy mid-tone.
 * Clear liquids pass when the wax is not near-white on its own. */
int ms_palette_readable(const MsPalette *p);
/* Comma-separated list of every id (for the option's choices), "auto" and
 * the generators first. */
extern const char ms_palette_choices_str[];
const char *ms_palette_choices(void);

#endif
