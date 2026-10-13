/* SPDX-License-Identifier: Apache-2.0 */
/* megacity_opts.h - the option table of megacity_gles3.
 *
 * Single source of truth for the parser, --help/--list-options, the schema dump
 * (--dump-schema, compared with assets/screensaver-chooser/options/megacity.tsv
 * by a meson test) and the unit test.
 */
#ifndef MEGACITY_OPTS_H
#define MEGACITY_OPTS_H
#include "ncz_options.h"

#define MC_WX_CHOICES "clear,rain,storm,snow,sleet,fog,smog,dust,ash"

static const ncz_opt_def MC_OPTS[] = {
 /* name, type, default, min, max, choices, aliases, env_alias, label, description, group */
 {"preset", NCZ_OPT_STRING, "", 0, 0, "", "", NULL,
  "Preset", "Named bundle of options (see --list-presets): applied above the saved config file and below environment and command line. Empty = none.", "Look"},
 /* Canonical modes are Night and Pale Reach (the brief's current naming). The
  * brief's earlier oscura/luminox names still parse as legacy aliases, so old
  * configs and saved launch files keep working. */
 {"mode", NCZ_OPT_ENUM, "night", 0, 0, "night,pale-reach", "oscura=night,luminox=pale-reach,dark=night,bright=pale-reach,day=pale-reach", NULL,
  "Light", "night: dark, rain-soaked, neon. pale-reach: bright, high-key, pale haze, the opposite lighting. With a cycle this is the state the run begins in. (legacy aliases oscura, luminox, dark, bright, day also accepted.)", "Look"},
 {"cycle", NCZ_OPT_ENUM, "off", 0, 0, "off,dawn,dusk,day", "sunrise=dawn,sunset=dusk", NULL,
  "Day cycle", "off: hold the mode. dawn: begin in night and let the sun rise into pale-reach, then stay. dusk: begin in pale-reach and let the sun set into night, then stay. day: hold, rise, hold, set, and repeat. The light is one continuous sun elevation, never a cut.", "Look"},
 {"transition", NCZ_OPT_FLOAT, "180", 120, 1800, "", "", NULL,
  "Sunrise / sunset length (s)", "Seconds a sunrise or sunset takes. Never less than 120: the light changes slowly enough that nothing reads as a flash.", "Look"},
 {"night-hold", NCZ_OPT_FLOAT, "120", 0, 3600, "", "", NULL,
  "Night hold (s)", "Seconds held in night before a sunrise (cycle dawn and day).", "Look"},
 {"day-hold", NCZ_OPT_FLOAT, "120", 0, 3600, "", "", NULL,
  "Day hold (s)", "Seconds held in pale-reach before a sunset (cycle dusk and day).", "Look"},
 {"palette", NCZ_OPT_ENUM, "neon", 0, 0, "neon,violet,refinery,mono", "", NULL,
  "Neon palette", "neon: magenta, violet, ice blue and a little acid green. violet: cool purples and blues with pink accents. refinery: sodium orange with teal haze and flare tips (opt in: orange and teal only ever appear together here). mono: white and blue-white light only.", "Look"},
 {"exposure", NCZ_OPT_FLOAT, "1", 0.5, 1.6, "", "", NULL,
  "Exposure", "Overall brightness multiplier applied before tone mapping.", "Look"},
 {"haze", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Haze", "Multiplier on the aerial haze that separates the far towers. 0 is crisp, 2 is heavy.", "Look"},
 {"wet", NCZ_OPT_FLOAT, "1", 0, 1, "", "", NULL,
  "Wet streets", "How strongly wet streets and glass reflect the neon when it has rained. 0 turns the reflections off.", "Look"},
 {"height", NCZ_OPT_FLOAT, "1", 0.6, 1.8, "", "", NULL,
  "Building height", "Multiplier on the height of every building, mega-structures included.", "Look"},
 {"mega", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Mega-structures", "How many blocks are mega-structures (arcology slabs, terraced masses, spire clusters with sky bridges). 0 is an ordinary skyscraper city.", "Look"},
 {"weather", NCZ_OPT_ENUM, "auto", 0, 0, "auto," MC_WX_CHOICES, "", NULL,
  "Weather", "auto: seeded regimes of 3 to 8 minutes drawn from the weather set, each blended into the next along a quintic curve. Any other value holds that weather.", "Weather"},
 {"weather-set", NCZ_OPT_LIST, "clear,rain,storm,snow,sleet,fog,smog,dust,ash", 0, 0, MC_WX_CHOICES, "", NULL,
  "Weather set", "Comma separated weather types the automatic regimes may use.", "Weather"},
 {"weather-intensity", NCZ_OPT_FLOAT, "1", 0, 1.5, "", "", NULL,
  "Weather intensity", "Multiplier on precipitation and fog density.", "Weather"},
 {"regime-min", NCZ_OPT_FLOAT, "180", 60, 1200, "", "", NULL,
  "Shortest regime (s)", "Shortest automatic weather regime, seconds.", "Weather"},
 {"regime-max", NCZ_OPT_FLOAT, "480", 60, 1800, "", "", NULL,
  "Longest regime (s)", "Longest automatic weather regime, seconds.", "Weather"},
 {"lightning", NCZ_OPT_BOOL, "true", 0, 0, "", "", NULL,
  "Lightning", "Lightning in storm weather. At most 3 flashes per second, each with a soft 100 to 300 ms rise. Turn off to have storms without any flash.", "Weather"},
 {"lightning-intensity", NCZ_OPT_FLOAT, "0.05", 0, 0.2, "", "", NULL,
  "Lightning strength", "Peak luminance delta of a flash as a fraction of full scale. 0.05 by default, never more than 0.2.", "Weather"},
 {"ships", NCZ_OPT_BOOL, "true", 0, 0, "", "", NULL,
  "Ships", "Freighters and the occasional colossal capital ship crossing the sky.", "Ships"},
 {"ship-density", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Freighter density", "How many mid-size freighters are in the sky at once (1 at 0.34, 3 at 1, all 4 above 1.34).", "Ships"},
 {"capital-rate", NCZ_OPT_FLOAT, "0.35", 0, 1, "", "", NULL,
  "Capital ships", "Chance that a colossal capital ship crosses in each 7-minute slot. 0 for none.", "Ships"},
 {"traffic", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Lane traffic", "Density of the small craft on the boulevard lanes, the scale cue. 0 removes them.", "Ships"},
 {"ship-style", NCZ_OPT_ENUM, "paperback", 0, 0, "paperback,clean", "", NULL,
  "Ship rendering", "paperback: 1970s-80s paperback cover art, airbrushed gouache with bold color blocks and fixed grain. clean: plain smooth shading.", "Ships"},
 {"ship-saturation", NCZ_OPT_FLOAT, "1", 0, 1.5, "", "", NULL,
  "Hull saturation", "Saturation of the hull color blocks.", "Ships"},
 {"speed", NCZ_OPT_FLOAT, "1", 0.2, 3, "", "", NULL,
  "Flight speed", "Time scale of the whole scene: camera, weather, ships and the day cycle.", "Camera"},
 {"altitude", NCZ_OPT_FLOAT, "1", 0.5, 2, "", "", NULL,
  "Altitude", "Multiplier on the camera height (about 100 to 320 metres).", "Camera"},
 {"sway", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Sway", "Multiplier on the sideways drift of the camera along the boulevard.", "Camera"},
 {"frame-x", NCZ_OPT_FLOAT, "0", -1, 1, "", "", NULL,
  "Frame shift X", "Shifts the composition sideways with a lens shift (verticals stay vertical). Positive moves the scene right.", "Camera"},
 {"frame-y", NCZ_OPT_FLOAT, "0", -1, 1, "", "", NULL,
  "Frame shift Y", "Shifts the composition vertically with a lens shift. Positive moves the horizon up the screen, showing more street; negative shows more sky.", "Camera"},
 {"seed", NCZ_OPT_INT, "0", 0, 4294967295.0, "", "", "NCZ_MEGACITY_FIXED_SEED",
  "Random seed", "Fixes the city, weather, ships and camera so a run can be repeated. 0 = a new city every launch.", "System"},
 {"time-offset", NCZ_OPT_FLOAT, "0", 0, 864000, "", "", NULL,
  "Start time (s)", "Testing: begin this many scene seconds into the run (everything is a function of time, so this lands exactly there).", "System"},
 {"flash-hold", NCZ_OPT_BOOL, "false", 0, 0, "", "", NULL,
  "Hold flash at peak", "Testing only: hold a lightning flash at its peak amplitude to measure the luminance delta.", "System"},
};
#define MC_NOPTS (sizeof MC_OPTS / sizeof MC_OPTS[0])
#endif
