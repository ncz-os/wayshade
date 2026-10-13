/* SPDX-License-Identifier: Apache-2.0 */
/* leviathan_opts.h - the option table of leviathan_gles3.
 *
 * Single source of truth for the parser, --help/--list-options and the schema
 * dump (--dump-schema, compared with assets/screensaver-chooser/options/
 * leviathan.tsv by a meson test; leviathan.json is generated from the same
 * dump). Every value maps onto the shader's uLv0..uLv2 uniforms (see the top
 * of vendor/xshadertoy/glsl/leviathan.glsl). "auto" means: drawn per chapter
 * from the run seed. Whatever is chosen, the luminance and flash caps live in
 * the shader and cannot be exceeded from here.
 */
#ifndef LEVIATHAN_OPTS_H
#define LEVIATHAN_OPTS_H
#include "ncz_options.h"

#define LV_HULLS     "auto,slab,tiered,long"
#define LV_LANDMARKS "auto,ring,fins"
#define LV_PALETTES  "auto,warm,cool,sodium,white,harmonic"
#define LV_SIDES     "auto,left,right"
#define LV_PLANETS   "auto,ocean,desert,ice,giant,none"
#define LV_INTERIORS "auto,hangar,foundry"
#define LV_RANDOMIZE "auto,fixed"
#define LV_QUALITY   "auto,weak,mid,strong"

static const ncz_opt_def LV_OPTS[] = {
 /* name, type, default, min, max, choices, aliases, env_alias, label, description, group */
 {"preset", NCZ_OPT_STRING, "", 0, 0, "", "", NULL,
  "Preset", "Named bundle of options (see --list-presets): applied above the saved config file and below environment and command line. Empty = none.", "Look"},
 {"hull", NCZ_OPT_ENUM, "auto", 0, 0, LV_HULLS, "", NULL,
  "Hull", "Proportions of the vessel. slab: broad and flat. tiered: a tall superstructure over a deep drop. long: lean and very long. auto: drawn per chapter.", "Vessel"},
 {"landmark", NCZ_OPT_ENUM, "auto", 0, 0, LV_LANDMARKS, "", NULL,
  "Landmark", "The one structure each vessel is remembered by: a ring the runway passes under, or three tall fins on the superstructure. auto: drawn per chapter.", "Vessel"},
 {"palette", NCZ_OPT_ENUM, "auto", 0, 0, LV_PALETTES, "", NULL,
  "Window palette", "Colour family of the windows and lights. harmonic: a colourway generated per chapter (windows at one hue and near its complement; the gate follows it). auto: drawn per chapter among all of them.", "Look"},
 {"aperture-side", NCZ_OPT_ENUM, "auto", 0, 0, LV_SIDES, "", NULL,
  "Vessel side", "Which side of the frame the vessel appears on, and so which side the runway and aperture are on. auto: drawn per chapter.", "Vessel"},
 {"planet", NCZ_OPT_ENUM, "auto", 0, 0, LV_PLANETS, "", NULL,
  "Planet", "The world below the vessel, or none (stars only). auto: drawn per chapter.", "Look"},
 {"interior", NCZ_OPT_ENUM, "auto", 0, 0, LV_INTERIORS, "", NULL,
  "Interior", "Light inside the hall: hangar (cool daylight shafts) or foundry (amber shafts). auto: drawn per chapter.", "Look"},
 {"chapter-length", NCZ_OPT_INT, "0", 0, 900, "", "", NULL,
  "Chapter length (arc s)", "Length of one chapter in arc seconds, 380 to 900; the extra time goes to the quiet room. 0 = automatic (390 to 420, from the seed).", "Timing"},
 {"arc-scale", NCZ_OPT_FLOAT, "1", 0.7, 2, "", "", NULL,
  "Arc scale", "Wall seconds per arc second: 2 plays the whole arc (and every motion in it) at half speed, 0.7 at about 1.4 times the speed. The lower limit keeps every blinking light at or below 1 Hz.", "Timing"},
 {"gate-hue", NCZ_OPT_INT, "-1", -1, 359, "", "", NULL,
  "Gate hue (degrees)", "Starting hue of the gate's colour path, 0 to 359; it still drifts slowly (<= 0.03 Hz). -1 = automatic.", "Look"},
 {"gate-intensity", NCZ_OPT_FLOAT, "1", 0.3, 1, "", "", NULL,
  "Gate intensity", "Contrast and saturation of the gate, 0.3 to 1. The gate never exceeds a display value of 0.85 or a mean near 0.45 at any setting.", "Look"},
 {"star-density", NCZ_OPT_FLOAT, "1", 0.25, 2, "", "", NULL,
  "Star density", "Multiplier on the number of visible stars.", "Look"},
 {"randomize", NCZ_OPT_ENUM, "auto", 0, 0, LV_RANDOMIZE, "", NULL,
  "Randomize", "auto: every chapter re-seeds at the end of the quiet room (new vessel, palette, planet, side). fixed: every chapter repeats the first one.", "System"},
 {"quality", NCZ_OPT_ENUM, "auto", 0, 0, LV_QUALITY, "", NULL,
  "Quality", "Detail tier. auto follows the GPU class of this machine (NCZ_SHADER_QUALITY still overrides it).", "System"},
 {"start-time", NCZ_OPT_FLOAT, "0", 0, 900, "", "", NULL,
  "Start at (arc s)", "Testing and captures: begin the run this many arc seconds into the first chapter (for example 45 for the hull pass, 200 for the hall, 300 for the gate). 0 = from the start.", "System"},
 {"seed", NCZ_OPT_INT, "0", 0, 999999, "", "", "NCZ_LEVIATHAN_FIXED_SEED",
  "Random seed", "Fixes the run's randomness so a run can be repeated exactly (with the same preset and options). 0 = a new seed (1 to 999999) every run; the seed in use is logged to standard error, so any run can be replayed with --seed.", "System"},
};
#define LV_NOPTS (sizeof LV_OPTS / sizeof LV_OPTS[0])
#endif
