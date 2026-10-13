/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_palettes.c - the palette table and generator (see the header).
 *
 * Provenance, in table order (sources and method in
 * docs/research/magmasimplex-palette-sources.md, not packaged):
 *   photos        the operator's four reference photographs plus five
 *                 sixties pairings (linear, set by eye);
 *   footage       medians of masked pixels in frames of reference videos
 *                 (analysis only);
 *   era-pairings  pairings documented in words for lamps of the 1960s-1980s
 *                 (colours chosen to match the words);
 *   codes-1992-2004  lamp colour codes of 1992-2004, liquid/wax, colours
 *                 SAMPLED from a reference chart (median of the liquid and wax
 *                 clusters of each swatch; ids keep the code numbers). The
 *                 chart itself is not reproduced anywhere;
 *   magmasimplex-ways  the original 30 colourways of this saver (the same
 *                 values as ways[] in gles3_magmasimplex.c, same indices, so
 *                 way-N matches colourway=N of the fluid engine).
 */
#include "magmasimplex_palettes.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define S MS_PAL_SRGB
#define C MS_PAL_CLEAR
#define M MS_PAL_MIST
static const MsPaletteDef PALS[] = {
    /* photos (linear) */
    {"yellow-on-pink", "pink/yellow-orange", "modern", "photos", {1.00f, 0.04f, 0.28f}, {1.00f, 0.62f, 0.05f}, {1.00f, 0.82f, 0.18f}, 0},
    {"orange-on-cyan", "pale-cyan/orange", "modern", "photos", {0.30f, 0.72f, 0.95f}, {1.00f, 0.28f, 0.02f}, {0.95f, 0.12f, 0.02f}, 0},
    {"lime-on-green", "green/pale-lime", "modern", "photos", {0.04f, 0.80f, 0.16f}, {0.78f, 1.00f, 0.28f}, {0.95f, 1.00f, 0.45f}, 0},
    {"red-on-amber", "amber/red-pink", "modern", "photos", {1.00f, 0.42f, 0.06f}, {0.95f, 0.05f, 0.22f}, {0.80f, 0.02f, 0.10f}, 0},
    {"pink-on-violet", "violet/hot-pink", "1960s", "photos", {0.35f, 0.04f, 0.80f}, {1.00f, 0.22f, 0.62f}, {1.00f, 0.55f, 0.15f}, 0},
    {"violet-on-green", "deep-green/blue-violet", "1960s", "photos", {0.02f, 0.50f, 0.22f}, {0.50f, 0.25f, 1.00f}, {0.90f, 0.25f, 0.75f}, 0},
    {"lime-on-magenta", "magenta/lime", "1960s", "photos", {0.85f, 0.03f, 0.55f}, {0.65f, 1.00f, 0.10f}, {1.00f, 0.85f, 0.10f}, 0},
    {"cream-on-blue", "electric-blue/cream", "1960s", "photos", {0.05f, 0.25f, 1.00f}, {1.00f, 0.90f, 0.70f}, {1.00f, 0.50f, 0.12f}, 0},
    {"orange-on-yellow", "yellow/orange", "1960s", "photos", {1.00f, 0.72f, 0.04f}, {1.00f, 0.28f, 0.03f}, {0.90f, 0.08f, 0.05f}, 0},
    /* footage (sRGB medians of masked pixels) */
    {"footage-red-in-amber", "amber/red-orange", "modern", "footage", {253, 236, 89}, {248, 66, 5}, {0, 0, 0}, S},
    {"footage-cyan-in-blue", "blue/cyan-white", "modern", "footage", {0, 84, 178}, {193, 210, 233}, {0, 0, 0}, S},
    {"footage-violet-in-teal", "teal/violet", "modern", "footage", {0, 110, 169}, {71, 121, 243}, {0, 0, 0}, S},
    {"footage-red-in-clear", "clear/red", "modern", "footage", {200, 200, 200}, {232, 9, 32}, {255, 161, 16}, S | C},
    {"footage-blue-in-clear", "clear/violet-blue", "modern", "footage", {200, 200, 205}, {55, 80, 230}, {170, 200, 255}, S | C},
    /* era-pairings (documented in words) */
    {"era-60s-yellow-orange", "yellow/orange", "1960s", "era-pairings", {250, 215, 40}, {240, 90, 20}, {0, 0, 0}, S},
    {"era-60s-blue-green", "blue/green", "1960s", "era-pairings", {20, 90, 200}, {60, 200, 70}, {0, 0, 0}, S},
    {"era-60s-clear-red", "clear/red", "1960s", "era-pairings", {200, 200, 200}, {215, 45, 30}, {0, 0, 0}, S | C},
    {"era-60s-champagne-mist", "hazy-amber/cream", "1960s", "era-pairings", {225, 170, 90}, {245, 230, 195}, {0, 0, 0}, S | M},
    {"era-60s-fluorescent-green-red", "fluorescent-green/red", "1960s", "era-pairings", {90, 240, 60}, {220, 30, 25}, {0, 0, 0}, S},
    {"era-60s-blue-green-yellow", "blue-green/yellow", "1960s", "era-pairings", {20, 160, 150}, {250, 215, 40}, {0, 0, 0}, S},
    {"era-60s-lavender-cream", "lavender/cream", "1960s", "era-pairings", {150, 100, 200}, {245, 230, 200}, {0, 0, 0}, S},
    {"era-60s-lavender-blue", "lavender/blue", "1960s", "era-pairings", {150, 100, 200}, {40, 70, 210}, {0, 0, 0}, S},
    {"era-60s-blue-mist-blue", "blue-mist/blue", "1960s", "era-pairings", {120, 160, 220}, {30, 60, 190}, {0, 0, 0}, S | M},
    {"era-60s-orange-mist-yellow", "orange-mist/yellow", "1960s", "era-pairings", {240, 150, 70}, {250, 220, 60}, {0, 0, 0}, S | M},
    {"era-60s-green-mist-lime", "green-mist/lime", "1960s", "era-pairings", {90, 170, 110}, {190, 240, 60}, {0, 0, 0}, S | M},
    {"era-60s-ruby-in-rose", "rose/ruby-red", "1960s", "era-pairings", {235, 120, 140}, {160, 10, 30}, {0, 0, 0}, S},
    {"era-60s-topaz-in-blue-green", "blue-green/topaz", "1960s", "era-pairings", {30, 150, 140}, {235, 175, 60}, {0, 0, 0}, S},
    {"era-70s-blue-white", "blue/white", "1970s", "era-pairings", {20, 90, 200}, {240, 240, 235}, {0, 0, 0}, S},
    /* further pre-1990 pairings (estimated from colour names, nothing sampled) */
    {"era-60s-mint-green-mist", "green-mist/mint", "1960s", "era-pairings", {100, 190, 110}, {165, 230, 140}, {0, 0, 0}, S | M},
    {"era-60s-red-green-mist", "green-mist/red", "1960s", "era-pairings", {90, 175, 105}, {205, 40, 45}, {0, 0, 0}, S | M},
    {"era-60s-red-pale-blue", "pale-blue/red (prototype)", "1960s", "era-pairings", {170, 205, 240}, {210, 40, 45}, {0, 0, 0}, S},
    {"era-60s-amber-yellow", "yellow/amber (prototype)", "1960s", "era-pairings", {250, 215, 90}, {225, 140, 30}, {0, 0, 0}, S},
    {"era-60s-glitter-emerald", "emerald/gold-glitter", "1960s", "era-pairings", {50, 150, 100}, {215, 175, 60}, {0, 0, 0}, S | MS_PAL_GLITTER_GOLD},
    {"era-60s-glitter-gold", "gold/gold-glitter", "1960s", "era-pairings", {215, 175, 60}, {245, 235, 200}, {0, 0, 0}, S | MS_PAL_GLITTER_GOLD},
    {"era-60s-glitter-silver", "silver/silver-glitter", "1960s", "era-pairings", {200, 205, 210}, {60, 70, 90}, {0, 0, 0}, S | C | MS_PAL_GLITTER_SILVER},
    {"era-80s-red-yellow", "yellow/red", "1980s", "era-pairings", {250, 215, 60}, {200, 30, 25}, {0, 0, 0}, S},
    {"era-80s-gold-clear", "clear/gold", "1980s", "era-pairings", {200, 200, 200}, {225, 170, 50}, {0, 0, 0}, S | C},
    /* codes-1992-2004 (sampled from the chart; see the header note) */
    {"code-01", "clear/white", "1990s", "codes-1992-2004", {227, 224, 226}, {238, 236, 232}, {0, 0, 0}, S | C},
    {"code-02", "yellow/red", "1990s", "codes-1992-2004", {229, 218, 118}, {161, 53, 36}, {0, 0, 0}, S},
    {"code-03", "blue/white", "1990s", "codes-1992-2004", {19, 93, 171}, {238, 238, 236}, {0, 0, 0}, S},
    {"code-04", "pink/white-pink", "1990s", "codes-1992-2004", {223, 16, 119}, {240, 96, 163}, {0, 0, 0}, S},
    {"code-05", "orange/white", "1990s", "codes-1992-2004", {218, 113, 40}, {245, 236, 225}, {0, 0, 0}, S},
    {"code-06", "clear/coral-red", "1990s", "codes-1992-2004", {203, 204, 204}, {226, 65, 51}, {0, 0, 0}, S | C},
    {"code-07", "clear/peach", "1990s", "codes-1992-2004", {202, 206, 207}, {249, 169, 98}, {0, 0, 0}, S | C},
    {"code-08", "clear/yellow", "1990s", "codes-1992-2004", {199, 207, 209}, {244, 186, 24}, {0, 0, 0}, S | C},
    {"code-09", "clear/purple", "1990s", "codes-1992-2004", {189, 202, 207}, {65, 42, 116}, {0, 0, 0}, S | C},
    {"code-10", "clear/red", "1990s", "codes-1992-2004", {200, 205, 207}, {156, 53, 36}, {0, 0, 0}, S | C},
    {"code-11", "clear/neon-yellow", "1990s", "codes-1992-2004", {197, 208, 211}, {154, 204, 105}, {0, 0, 0}, S | C},
    {"code-12", "clear/green", "1990s", "codes-1992-2004", {198, 210, 215}, {71, 131, 62}, {0, 0, 0}, S | C},
    {"code-13", "clear/blue", "1990s", "codes-1992-2004", {184, 203, 209}, {44, 78, 160}, {0, 0, 0}, S | C},
    {"code-14", "clear/pink", "1990s", "codes-1992-2004", {181, 203, 211}, {223, 18, 123}, {0, 0, 0}, S | C},
    {"code-15", "clear/orange", "1990s", "codes-1992-2004", {184, 208, 217}, {228, 117, 39}, {0, 0, 0}, S | C},
    {"code-16", "clear/black", "1990s", "codes-1992-2004", {182, 206, 216}, {60, 53, 47}, {0, 0, 0}, S | C},
    {"code-16a", "clear/brown", "1990s", "codes-1992-2004", {180, 210, 224}, {60, 33, 16}, {0, 0, 0}, S | C},
    {"code-17", "clear/teal", "1990s", "codes-1992-2004", {171, 208, 220}, {20, 146, 193}, {0, 0, 0}, S | C},
    {"code-18", "clear/cranberry", "1990s", "codes-1992-2004", {175, 207, 219}, {136, 30, 44}, {0, 0, 0}, S | C},
    {"code-19", "clear/lime-green", "1990s", "codes-1992-2004", {171, 206, 220}, {75, 176, 73}, {0, 0, 0}, S | C},
    {"code-20", "lavender/white", "1990s", "codes-1992-2004", {119, 76, 153}, {241, 131, 181}, {0, 0, 0}, S},
    {"code-21", "purple/royal-red", "1990s", "codes-1992-2004", {119, 76, 153}, {218, 18, 120}, {0, 0, 0}, S},
    {"code-22", "blue/purple", "1990s", "codes-1992-2004", {19, 93, 171}, {75, 45, 127}, {0, 0, 0}, S},
    {"code-23", "blue/green", "1990s", "codes-1992-2004", {19, 93, 171}, {17, 144, 70}, {0, 0, 0}, S},
    {"code-24", "blue/yellow", "1990s", "codes-1992-2004", {19, 93, 171}, {131, 196, 78}, {0, 0, 0}, S},
    {"code-25", "purple/yellow", "1990s", "codes-1992-2004", {122, 78, 158}, {244, 145, 60}, {0, 0, 0}, S},
    {"code-26", "red/white", "1990s", "codes-1992-2004", {165, 29, 33}, {245, 225, 220}, {0, 0, 0}, S},
    {"code-27", "blue/red", "1990s", "codes-1992-2004", {19, 93, 171}, {133, 42, 28}, {0, 0, 0}, S},
    {"code-28", "green/white", "1990s", "codes-1992-2004", {53, 103, 60}, {103, 190, 116}, {0, 0, 0}, S},
    {"code-29", "yellow/lime-green", "1990s", "codes-1992-2004", {229, 218, 118}, {83, 182, 72}, {0, 0, 0}, S},
    {"code-30w", "clear/silver-glitter", "1990s", "codes-1992-2004", {148, 151, 151}, {238, 236, 232}, {0, 0, 0}, S | C | MS_PAL_GLITTER_SILVER},
    {"code-30wg", "clear/rainbow-glitter", "1990s", "codes-1992-2004", {142, 130, 119}, {238, 236, 232}, {0, 0, 0}, S | C | MS_PAL_GLITTER_RAINBOW},
    {"code-31w", "blue/silver-glitter", "1990s", "codes-1992-2004", {30, 85, 139}, {238, 238, 236}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    {"code-31wg", "blue/rainbow-glitter", "1990s", "codes-1992-2004", {38, 51, 115}, {238, 238, 236}, {0, 0, 0}, S | MS_PAL_GLITTER_RAINBOW},
    {"code-32w", "red/silver-glitter", "1990s", "codes-1992-2004", {164, 28, 33}, {245, 225, 220}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    {"code-32wg", "red/rainbow-glitter", "1990s", "codes-1992-2004", {134, 27, 38}, {245, 225, 220}, {0, 0, 0}, S | MS_PAL_GLITTER_RAINBOW},
    {"code-33w", "purple/silver-glitter", "1990s", "codes-1992-2004", {79, 50, 122}, {245, 230, 240}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    {"code-33wg", "purple/rainbow-glitter", "1990s", "codes-1992-2004", {83, 46, 110}, {245, 230, 240}, {0, 0, 0}, S | MS_PAL_GLITTER_RAINBOW},
    {"code-34w", "pink/silver-glitter", "1990s", "codes-1992-2004", {158, 28, 97}, {245, 225, 235}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    {"code-35w", "teal/silver-glitter", "1990s", "codes-1992-2004", {20, 114, 131}, {235, 245, 245}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    {"code-36w", "green/gold-glitter", "1990s", "codes-1992-2004", {36, 132, 63}, {240, 240, 220}, {0, 0, 0}, S | MS_PAL_GLITTER_GOLD},
    {"code-37w", "yellow/silver-glitter", "1990s", "codes-1992-2004", {197, 149, 45}, {245, 240, 225}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    {"code-38w", "orange/silver-glitter", "1990s", "codes-1992-2004", {196, 90, 43}, {245, 235, 225}, {0, 0, 0}, S | MS_PAL_GLITTER_SILVER},
    /* light-show: dye sets for the Flowerchild view (view=butterfly), taken from the operator's
     * reference footage of a 1960s overhead-projector liquid light show
     * (2026-10-05; the footage itself is not in the repository): liquid is
     * the dye field, wax and secondary the two inks dropped into it. */
    {"show-cyan-red", "cyan/red+orange", "1960s", "light-show", {0, 170, 190}, {235, 40, 20}, {255, 140, 30}, S},
    {"show-royal-white", "royal-blue/white+cyan", "1960s", "light-show", {10, 50, 210}, {240, 245, 250}, {60, 200, 230}, S},
    {"show-green-blue-yellow", "green/yellow+blue", "1960s", "light-show", {20, 140, 80}, {250, 220, 40}, {30, 80, 220}, S},
    {"show-yellow-red", "yellow/dark-red+red", "1960s", "light-show", {250, 220, 30}, {150, 10, 10}, {240, 60, 30}, S},
    {"show-holo-pink-teal", "teal/pink+purple", "modern", "light-show", {20, 170, 170}, {255, 70, 170}, {120, 50, 220}, S},
    /* radial vein network: teal, yellow and pink interdigitating (view=butterfly, dendrites=veins) */
    {"show-veins", "teal/yellow+pink", "1960s", "light-show", {0, 150, 165}, {245, 220, 40}, {250, 60, 170}, S},
    {"show-ember-gold", "dark-amber/gold+orange", "modern", "light-show", {60, 25, 5}, {255, 180, 40}, {240, 90, 10}, S | MS_PAL_DARK},
    {"show-neon-pulse", "electric-cyan/magenta+yellow", "modern", "light-show", {0, 220, 255}, {255, 0, 160}, {240, 255, 40}, S},
    /* the 1969 Fillmore East footage: dye splats on a white-hot dish; deep red macro */
    {"show-fillmore-splat", "white-hot/red+magenta", "1960s", "light-show", {250, 242, 225}, {225, 20, 30}, {250, 60, 150}, S},
    {"show-fillmore-red", "deep-red/pink+red", "1960s", "light-show", {130, 8, 18}, {255, 70, 110}, {190, 15, 35}, S | MS_PAL_DARK},
    /* a cooler, electronic set: electric blue field, silver and teal inks */
    {"show-silver-apple", "electric-blue/silver+teal", "modern", "light-show", {20, 70, 220}, {210, 215, 225}, {0, 175, 175}, S},
    /* the "Swinging London" set (moods named by the operator, 2026-10-05) */
    {"show-technicolor", "green/magenta+orange", "1960s", "light-show", {30, 190, 90}, {230, 30, 170}, {255, 140, 20}, S},
    {"show-ufo", "dark-violet/white+lavender", "1960s", "light-show", {40, 10, 70}, {245, 245, 255}, {170, 140, 230}, S | MS_PAL_DARK},
    {"show-middle-earth", "deep-green/teal+moss", "1960s", "light-show", {10, 60, 35}, {0, 170, 150}, {110, 160, 40}, S | MS_PAL_DARK},
    {"show-roundhouse", "ice-blue/white+violet", "1960s", "light-show", {120, 190, 255}, {250, 250, 255}, {140, 90, 230}, S},
    {"show-hapshash", "violet/orange+magenta", "1960s", "light-show", {90, 30, 170}, {255, 120, 0}, {240, 20, 140}, S},
    {"show-kaleido", "deep-blue/yellow+magenta", "1960s", "light-show", {20, 40, 190}, {255, 220, 30}, {230, 30, 150}, S},
    {"show-piper", "pale-peach/lavender+primrose", "1960s", "light-show", {255, 205, 185}, {200, 180, 240}, {255, 240, 170}, S},
    {"show-itchycoo", "park-green/gold+cream", "1960s", "light-show", {40, 150, 60}, {240, 190, 40}, {250, 240, 200}, S},
    {"show-carnaby", "hot-pink/orange+white", "1960s", "light-show", {255, 30, 140}, {255, 130, 0}, {255, 250, 245}, S},
    /* magmasimplex-ways: ways[0..29] (linear), names from colourway_names[] */
    {"way-00", "ember", "modern", "magmasimplex-ways", {1, 1, 1}, {1.00f, 0.10f, 0.10f}, {1.00f, 0.55f, 0.20f}, C},
    {"way-01", "orchid", "modern", "magmasimplex-ways", {1, 1, 1}, {1.00f, 0.55f, 0.20f}, {0.95f, 0.15f, 0.55f}, C},
    {"way-02", "ultraviolet", "modern", "magmasimplex-ways", {1, 1, 1}, {1.00f, 0.85f, 0.10f}, {0.85f, 0.10f, 0.55f}, C},
    {"way-03", "sunflower", "modern", "magmasimplex-ways", {1, 1, 1}, {0.70f, 0.10f, 0.95f}, {0.10f, 0.55f, 1.00f}, C},
    {"way-04", "neon", "modern", "magmasimplex-ways", {1, 1, 1}, {0.20f, 0.95f, 0.30f}, {0.10f, 0.55f, 1.00f}, C},
    {"way-05", "ember", "modern", "magmasimplex-ways", {1, 1, 1}, {1.00f, 0.30f, 0.75f}, {1.00f, 0.10f, 0.45f}, C},
    {"way-06", "orchid", "modern", "magmasimplex-ways", {0.10f, 0.25f, 0.95f}, {1.00f, 0.10f, 0.20f}, {1.00f, 0.85f, 0.10f}, 0},
    {"way-07", "ultraviolet", "modern", "magmasimplex-ways", {0.10f, 0.25f, 0.95f}, {1.00f, 1.00f, 0.20f}, {0.95f, 0.10f, 0.10f}, 0},
    {"way-08", "sunflower", "modern", "magmasimplex-ways", {0.10f, 0.25f, 0.95f}, {0.95f, 0.95f, 0.95f}, {0.70f, 0.10f, 0.95f}, 0},
    {"way-09", "neon", "modern", "magmasimplex-ways", {0.10f, 0.25f, 0.95f}, {0.20f, 0.95f, 0.30f}, {0.70f, 0.10f, 0.95f}, 0},
    {"way-10", "ember", "modern", "magmasimplex-ways", {0.10f, 0.25f, 0.95f}, {0.70f, 0.10f, 0.95f}, {1.00f, 0.10f, 0.20f}, 0},
    {"way-11", "orchid", "modern", "magmasimplex-ways", {0.95f, 0.20f, 0.55f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-12", "ultraviolet", "modern", "magmasimplex-ways", {0.95f, 0.10f, 0.10f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.85f, 0.10f}, 0},
    {"way-13", "sunflower", "modern", "magmasimplex-ways", {0.95f, 0.10f, 0.10f}, {1.00f, 0.85f, 0.10f}, {0.70f, 0.10f, 0.95f}, 0},
    {"way-14", "neon", "modern", "magmasimplex-ways", {0.95f, 0.10f, 0.10f}, {0.20f, 0.95f, 0.40f}, {1.00f, 0.30f, 0.85f}, 0},
    {"way-15", "ember", "modern", "magmasimplex-ways", {0.70f, 0.10f, 0.95f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-16", "orchid", "modern", "magmasimplex-ways", {0.70f, 0.10f, 0.95f}, {1.00f, 0.20f, 0.10f}, {1.00f, 0.85f, 0.10f}, 0},
    {"way-17", "ultraviolet", "modern", "magmasimplex-ways", {0.70f, 0.10f, 0.95f}, {1.00f, 0.85f, 0.10f}, {1.00f, 0.10f, 0.20f}, 0},
    {"way-18", "sunflower", "modern", "magmasimplex-ways", {1.00f, 0.50f, 0.10f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-19", "neon", "modern", "magmasimplex-ways", {1.00f, 0.50f, 0.10f}, {0.70f, 0.10f, 0.95f}, {0.20f, 0.95f, 0.30f}, 0},
    {"way-20", "ember", "modern", "magmasimplex-ways", {1.00f, 0.50f, 0.10f}, {0.05f, 0.05f, 0.05f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-21", "orchid", "modern", "magmasimplex-ways", {0.20f, 0.95f, 0.30f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-22", "ultraviolet", "modern", "magmasimplex-ways", {0.20f, 0.95f, 0.30f}, {0.10f, 0.25f, 0.95f}, {1.00f, 0.85f, 0.10f}, 0},
    {"way-23", "sunflower", "modern", "magmasimplex-ways", {0.20f, 0.95f, 0.30f}, {1.00f, 0.85f, 0.10f}, {1.00f, 0.10f, 0.20f}, 0},
    {"way-24", "neon", "modern", "magmasimplex-ways", {0.05f, 0.05f, 0.25f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-25", "ember", "modern", "magmasimplex-ways", {0.25f, 0.05f, 0.45f}, {1.00f, 0.30f, 0.85f}, {1.00f, 0.85f, 0.10f}, 0},
    {"way-26", "orchid", "modern", "magmasimplex-ways", {0.05f, 0.40f, 0.15f}, {0.95f, 0.95f, 0.95f}, {1.00f, 0.85f, 0.10f}, 0},
    {"way-27", "ultraviolet", "modern", "magmasimplex-ways", {0.30f, 0.10f, 0.05f}, {1.00f, 0.85f, 0.10f}, {1.00f, 0.10f, 0.10f}, 0},
    {"way-28", "sunflower", "modern", "magmasimplex-ways", {0.05f, 0.10f, 0.45f}, {1.00f, 0.85f, 0.20f}, {0.70f, 0.10f, 0.95f}, 0},
    {"way-29", "neon", "modern", "magmasimplex-ways", {0.95f, 0.05f, 0.20f}, {0.20f, 1.00f, 0.50f}, {1.00f, 0.85f, 0.20f}, 0},
};
#undef S
#undef C
#undef M
#define NPAL ((int)(sizeof PALS / sizeof PALS[0]))

/* Every id, for the option's choices (kept in step with PALS by the
 * randomize test, which resolves each one). */
const char ms_palette_choices_str[] =
    "auto,generated-vintage,generated-neon,"
    "yellow-on-pink,orange-on-cyan,lime-on-green,red-on-amber,pink-on-violet,violet-on-green,lime-on-magenta,"
    "cream-on-blue,orange-on-yellow,"
    "footage-red-in-amber,footage-cyan-in-blue,footage-violet-in-teal,footage-red-in-clear,footage-blue-in-clear,"
    "era-60s-yellow-orange,era-60s-blue-green,era-60s-clear-red,era-60s-champagne-mist,era-60s-fluorescent-green-red,"
    "era-60s-blue-green-yellow,era-60s-lavender-cream,era-60s-lavender-blue,era-60s-blue-mist-blue,era-60s-orange-mist-yellow,"
    "era-60s-green-mist-lime,era-60s-ruby-in-rose,era-60s-topaz-in-blue-green,era-70s-blue-white,"
    "era-60s-mint-green-mist,era-60s-red-green-mist,era-60s-red-pale-blue,era-60s-amber-yellow,era-60s-glitter-emerald,"
    "era-60s-glitter-gold,era-60s-glitter-silver,era-80s-red-yellow,era-80s-gold-clear,"
    "code-01,code-02,code-03,code-04,code-05,code-06,code-07,code-08,code-09,"
    "code-10,code-11,code-12,code-13,code-14,code-15,code-16,code-16a,code-17,"
    "code-18,code-19,code-20,code-21,code-22,code-23,code-24,code-25,code-26,"
    "code-27,code-28,code-29,code-30w,code-30wg,code-31w,code-31wg,code-32w,"
    "code-32wg,code-33w,code-33wg,code-34w,code-35w,code-36w,code-37w,code-38w,"
    "show-cyan-red,show-royal-white,show-green-blue-yellow,show-yellow-red,show-holo-pink-teal,show-veins,show-ember-gold,"
    "show-neon-pulse,show-fillmore-splat,show-fillmore-red,show-silver-apple,"
    "show-technicolor,show-ufo,show-middle-earth,show-roundhouse,show-hapshash,show-kaleido,show-piper,show-itchycoo,"
    "show-carnaby,"
    "way-00,way-01,way-02,way-03,way-04,way-05,way-06,way-07,way-08,way-09,way-10,way-11,way-12,way-13,way-14,"
    "way-15,way-16,way-17,way-18,way-19,way-20,way-21,way-22,way-23,way-24,way-25,way-26,way-27,way-28,way-29";
const char *ms_palette_choices(void) { return ms_palette_choices_str; }

int ms_palette_count(void) { return NPAL; }
const MsPaletteDef *ms_palette_def(int i) { return (i >= 0 && i < NPAL) ? &PALS[i] : NULL; }
int ms_palette_find(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < NPAL; i++)
        if (!strcmp(PALS[i].id, id)) return i;
    return -1;
}

static float srgb_to_lin(float v) {
    v /= 255.f;
    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}
static float maxc(const float *c) { return fmaxf(c[0], fmaxf(c[1], c[2])); }
static float lum(const float *c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }

/* Warm incandescent bulb; clear liquids show it directly. */
static const float BULB[3] = {1.00f, 0.84f, 0.55f};

static void finish(MsPalette *o) {
    /* The liquid is a glowing carrier: its brightness comes from the bulb,
     * so normalise it to full intensity (mists stay a little paler). */
    if (o->clear) {
        /* Clear liquid is transparent: the dark room shows through, lit warm
         * only near the bulb (v2 made it an opaque beige field). */
        /* Darker than v2's 0.42/0.32/0.20: in the reference footage the wax
         * glows against a dim room, and an opaque beige field read as a
         * tinted liquid (operator photo IMG_8240). */
        o->liquid[0] = 0.26f; o->liquid[1] = 0.19f; o->liquid[2] = 0.11f;
    } else {
        float m = maxc(o->liquid);
        if (m > 1e-4f)
            for (int k = 0; k < 3; k++) o->liquid[k] /= m;
    }
    if (o->mist)
        for (int k = 0; k < 3; k++) o->liquid[k] = 0.75f * o->liquid[k] + 0.25f * BULB[k];
    /* Wax is an albedo: keep dark wax dark (clear/black is a real lamp), but
     * lift mid-dark sampled colours so the body can glow. */
    float wm = maxc(o->wax);
    if (wm > 0.12f && wm < 0.85f)
        for (int k = 0; k < 3; k++) o->wax[k] *= 0.85f / wm;
    if (maxc(o->secondary) <= 0.f)
        for (int k = 0; k < 3; k++) o->secondary[k] = fminf(1.f, 0.7f * o->wax[k] + 0.3f * BULB[k]);
    memcpy(o->bulb, BULB, sizeof BULB);
}

void ms_palette_get(int i, MsPalette *o) {
    memset(o, 0, sizeof *o);
    if (i < 0 || i >= NPAL) i = 0;
    const MsPaletteDef *d = &PALS[i];
    for (int k = 0; k < 3; k++) {
        if (d->flags & MS_PAL_SRGB) {
            o->liquid[k] = srgb_to_lin(d->liquid[k]);
            o->wax[k] = srgb_to_lin(d->wax[k]);
            o->secondary[k] = srgb_to_lin(d->secondary[k]);
        } else {
            o->liquid[k] = d->liquid[k];
            o->wax[k] = d->wax[k];
            o->secondary[k] = d->secondary[k];
        }
    }
    o->clear = (d->flags & MS_PAL_CLEAR) != 0;
    o->mist = (d->flags & MS_PAL_MIST) != 0;
    o->glitter = (d->flags & MS_PAL_GLITTER_SILVER) ? 1 : (d->flags & MS_PAL_GLITTER_GOLD) ? 2 : (d->flags & MS_PAL_GLITTER_RAINBOW) ? 3 : 0;
    snprintf(o->id, sizeof o->id, "%s", d->id);
    float dark[3] = {o->liquid[0], o->liquid[1], o->liquid[2]};
    finish(o);
    /* A dark dye field stays dark (the ember light show): no normalisation. */
    if (d->flags & MS_PAL_DARK) memcpy(o->liquid, dark, sizeof dark);
}

static void hsv(float h, float s, float v, float *out) {
    h = h - floorf(h);
    float r = fabsf(h * 6.f - 3.f) - 1.f, g = 2.f - fabsf(h * 6.f - 2.f), b = 2.f - fabsf(h * 6.f - 4.f);
    float c[3] = {fminf(fmaxf(r, 0.f), 1.f), fminf(fmaxf(g, 0.f), 1.f), fminf(fmaxf(b, 0.f), 1.f)};
    for (int k = 0; k < 3; k++) out[k] = v * (1.f - s + s * c[k]);
    for (int k = 0; k < 3; k++) out[k] = out[k] * out[k] * 1.0f; /* rough sRGB -> linear */
}
static float rnd(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}
static void rgb_hs(const float *c, float *h, float *s) {
    float mx = maxc(c), mn = fminf(c[0], fminf(c[1], c[2])), d = mx - mn;
    *s = mx > 1e-5f ? d / mx : 0.f;
    if (d < 1e-5f) { *h = 0.f; return; }
    float hh;
    if (mx == c[0]) hh = fmodf((c[1] - c[2]) / d, 6.f);
    else if (mx == c[1]) hh = (c[2] - c[0]) / d + 2.f;
    else hh = (c[0] - c[1]) / d + 4.f;
    *h = hh / 6.f;
    if (*h < 0) *h += 1.f;
}

int ms_palette_readable(const MsPalette *p) {
    if (p->clear) return 1;
    float hl, sl, hw, sw;
    rgb_hs(p->liquid, &hl, &sl);
    rgb_hs(p->wax, &hw, &sw);
    float dh = fabsf(hl - hw);
    if (dh > 0.5f) dh = 1.f - dh;
    float dl = fabsf(lum(p->liquid) - lum(p->wax));
    if (sl < 0.45f && !p->mist) return 0;          /* the liquid must glow in colour */
    /* muddy: a desaturated wax at mid luminance */
    if (sw < 0.25f && lum(p->wax) > 0.08f && lum(p->wax) < 0.35f) return 0;
    return dh > 0.22f || dl > 0.22f;
}

void ms_palette_generate(uint32_t *rng, int family, MsPalette *o) {
    for (int tries = 0; tries < 64; tries++) {
        memset(o, 0, sizeof *o);
        float hl, hw;
        if (family == 0) {
            /* Vintage: warm or deep liquids; wax a complement or split
             * complement, often cream/white or orange/red, as in the
             * sixties lineup. */
            static const float warm[] = {0.13f, 0.08f, 0.95f, 0.58f, 0.47f, 0.33f, 0.78f};
            hl = warm[(int)(rnd(rng) * 7) % 7] + (rnd(rng) - 0.5f) * 0.04f;
            float r = rnd(rng);
            hw = r < 0.5f ? hl + 0.5f : (r < 0.8f ? hl + 0.5f + (rnd(rng) < 0.5f ? -0.08f : 0.08f) : 0.06f);
            hsv(hl, 0.8f + 0.15f * rnd(rng), 0.95f, o->liquid);
            if (rnd(rng) < 0.25f) hsv(0.1f, 0.12f, 1.f, o->wax); /* cream */
            else hsv(hw, 0.75f + 0.2f * rnd(rng), 1.f, o->wax);
        } else {
            /* Modern neon: electric complements, analogous pairs only with a
             * strong luminance step. */
            hl = rnd(rng);
            float r = rnd(rng);
            hw = r < 0.6f ? hl + 0.5f : hl + (rnd(rng) < 0.5f ? 0.1f : -0.1f);
            hsv(hl, 0.95f, 1.f, o->liquid);
            hsv(hw, 0.9f, r < 0.6f ? 1.f : 0.55f, o->wax);
        }
        for (int k = 0; k < 3; k++) o->secondary[k] = 0.f;
        o->mist = family == 0 && rnd(rng) < 0.1f;
        snprintf(o->id, sizeof o->id, "generated-%s", family ? "neon" : "vintage");
        finish(o);
        if (ms_palette_readable(o)) return;
    }
    /* No readable draw in 64 tries (not seen in 1000 test draws): fall back
     * to the classic yellow/orange pairing rather than an unreadable one. */
    int i = ms_palette_find("era-60s-yellow-orange");
    ms_palette_get(i >= 0 ? i : 0, o);
    snprintf(o->id, sizeof o->id, "generated-%s", family ? "neon" : "vintage");
}
