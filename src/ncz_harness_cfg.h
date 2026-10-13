/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_harness_cfg.h - command line, environment and config handling shared by
 * every GLES3 hack, resolved before any Wayland or EGL work.
 *
 * Generic options (all hacks): render-scale, max-render-height,
 * render-scale-mode. A hack may add its own table by defining the weak
 * function ncz_hack_options(); both tables are parsed together so the hack's
 * options and the generic ones share one precedence chain
 * (defaults < config file < environment < command line).
 *
 * Unknown command-line options and stray arguments are errors (exit 2), so a
 * typo can never fall through to running the full simulation.
 */
#ifndef NCZ_HARNESS_CFG_H
#define NCZ_HARNESS_CFG_H
#include "ncz_options.h"

/* Returns an exit code (>= 0) when the process should exit now (help, list,
 * version, schema dump, or a command-line error), else -1. */
int ncz_harness_cfg_init(int argc, char **argv);

/* The merged, resolved option set (generic plus the hack's own). */
const ncz_opts *ncz_harness_opts(void);

/* Effective generic render configuration. */
double ncz_cfg_render_scale(void);
int    ncz_cfg_max_render_height(int *explicit_set);
int    ncz_cfg_scale_auto(void);
int    ncz_cfg_render_scale_is_set(void);

/* Hack hook (weak): return the hack's option table. prefix is the environment
 * prefix, group the config-file group / file name. */
extern const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group)
    __attribute__((weak));

/* Hack hook (weak): return nonzero when the hack draws its sharp geometry at
 * the output resolution and scales only its own soft layers, so the weak-GPU
 * default of half resolution would only blur its edges. The user's own
 * render-scale setting still applies. */
extern int ncz_hack_native_scale(void) __attribute__((weak));
#endif
