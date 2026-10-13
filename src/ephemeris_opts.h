/* SPDX-License-Identifier: Apache-2.0 */
/* ephemeris_opts.h - the option table of ephemeris_gles3.
 *
 * Single source of truth for the parser, --help/--list-options and the schema
 * dump (--dump-schema, compared with assets/screensaver-chooser/options/
 * ephemeris.tsv by a meson test).
 */
#ifndef EPHEMERIS_OPTS_H
#define EPHEMERIS_OPTS_H
#include "ncz_options.h"

#define EPH_MAX_WORKERS 48

static const ncz_opt_def EPH_OPTS[] = {
 /* name, type, default, min, max, choices, aliases, env_alias, label, description, group */
 {"composition", NCZ_OPT_ENUM, "dramatic", 0, 0, "dramatic,quiet,masses,drawing", "auto=quiet", NULL,
  "Composition", "dramatic: few workers with very different hands, long contour-following workers, a light ground and dark charcoal, and rare events: a mass lifted to a dull smear that takes marks badly for a long time, and droughts in which nothing is accepted. quiet: the first version, many similar workers (masses or drawing, chosen per run). masses and drawing: the quiet version with that layout.", "Look"},
 {"preset", NCZ_OPT_STRING, "", 0, 0, "", "", NULL,
  "Scene", "A named set of the options below (see --list-presets); options you set yourself still win.", "Look"},
 {"randomize", NCZ_OPT_ENUM, "auto", 0, 0, "auto,off", "", NULL,
  "Randomize", "auto: in the dramatic composition every option you or the scene leave unset is drawn at launch (workers, paper tone, tide, event rates, tools, iron oxide, the apparent past), so no two runs share a hand. off: the defaults.", "Look"},
 {"tide", NCZ_OPT_FLOAT, "330", 120, 600, "", "", NULL,
  "Tide period", "Dramatic: seconds for one slow rise and fall of how much the workers are allowed to lay down.", "Look"},
 {"lift-every", NCZ_OPT_FLOAT, "300", 0, 1800, "", "", NULL,
  "Lift every", "Dramatic: average seconds between lifts (a mass of work smeared away); 0 = none. The first is never before two minutes.", "Events"},
 {"drought-every", NCZ_OPT_FLOAT, "360", 0, 1800, "", "", NULL,
  "Drought every", "Dramatic: average seconds between droughts (stretches in which nothing is accepted); 0 = none.", "Events"},
 {"tools", NCZ_OPT_ENUM, "mix", 0, 0, "mix,points,sticks,washes", "", NULL,
  "Tools", "Dramatic: the hands' tools. mix: hard points, sticks, the side of the stick and washes; the others lean on one of them.", "Look"},
 {"oxide", NCZ_OPT_FLOAT, "0.04", 0, 0.3, "", "", NULL,
  "Iron oxide", "Dramatic: share of the hands that work in iron oxide rather than charcoal.", "Look"},
 {"past", NCZ_OPT_FLOAT, "0.35", 0, 0.6, "", "", NULL,
  "Apparent past", "Dramatic: how dark the marks already on the plate at the start may be (0 = a clean plate).", "Look"},
 {"tone", NCZ_OPT_FLOAT, "0.5", 0.25, 0.85, "", "", NULL,
  "Paper tone", "Lightness of the limestone ground. Lower is easier on the eyes in a dark room. The dramatic composition uses 0.70 unless this is set.", "Look"},
 {"workers", NCZ_OPT_INT, "32", 8, EPH_MAX_WORKERS, "", "", NULL,
  "Workers", "How many worker slots there are. The dramatic composition uses 10 unless this is set, and keeps it between 4 and 24.", "Look"},
 {"pace", NCZ_OPT_FLOAT, "1", 0.25, 3, "", "", NULL,
  "Pace", "Multiplier on how fast workers move and how fast the surface forgets.", "Look"},
 {"record", NCZ_OPT_BOOL, "true", 0, 0, "", "", NULL,
  "Emit the run's table", "On exit, write a plain numeric table describing the run that just ended to standard error (the session journal). It cannot be loaded back; the next run starts fresh either way.", "System"},
 {"seed", NCZ_OPT_INT, "0", 0, 4294967295.0, "", "", "NCZ_EPHEMERIS_FIXED_SEED",
  "Random seed", "Testing only: fixes the per-run randomness so a capture can be repeated. 0 = a new run every time.", "System"},
};
#define EPH_NOPTS (sizeof EPH_OPTS / sizeof EPH_OPTS[0])
#endif
