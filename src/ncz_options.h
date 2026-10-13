/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_options.h - small declarative option parser shared by screensaver hacks.
 *
 * One option table, three input channels, one validator:
 *   defaults < key-file config < environment < command line
 * Environment: <PREFIX><OPTION> with '-' written as '_' and upper-cased
 * (prefix "NCZ_BLACKHOLE_" turns "disk-brightness" into
 * NCZ_BLACKHOLE_DISK_BRIGHTNESS). Command line: --option=value, plus bare
 * --flag / --no-flag for booleans. Key file: "[group]" then "option=value".
 *
 * An unknown or invalid value is reported on stderr (once per occurrence in a
 * source) and ignored: the next lower source (finally the default) keeps applying.
 */
#ifndef NCZ_OPTIONS_H
#define NCZ_OPTIONS_H

#include <stdio.h>
#include <stddef.h>

/* Bits returned by ncz_opts_apply_argv / ncz_opts_resolve. */
#define NCZ_ARG_HELP            1
#define NCZ_ARG_LIST            2
#define NCZ_ARG_UNKNOWN_OPTION  4
#define NCZ_ARG_STRAY_ARGUMENT  8
#define NCZ_ARG_PRINT_CONFIG    16

typedef enum {
    NCZ_OPT_BOOL,
    NCZ_OPT_INT,
    NCZ_OPT_FLOAT,
    NCZ_OPT_ENUM,
    NCZ_OPT_STRING, /* free text */
    NCZ_OPT_LIST    /* comma separated list; every element must be one of choices */
} ncz_opt_type;

typedef struct {
    const char  *name;     /* e.g. "disk-brightness" */
    ncz_opt_type type;
    const char  *def;      /* default, canonical string */
    double       min, max; /* INT / FLOAT range, inclusive */
    const char  *choices;  /* ENUM / LIST: comma separated */
    const char  *aliases;  /* ENUM: "alias=canonical,alias=canonical" (optional) */
    const char  *env_alias;/* extra legacy environment names, comma separated (optional) */
    const char  *label;
    const char  *desc;
    const char  *group;
} ncz_opt_def;

#define NCZ_OPT_VALMAX 256

/* Where an option's value came from (ncz_opts.src). Higher wins in the
 * precedence chain; a caller can tell a scene's value from the run's. */
enum {
    NCZ_SRC_DEFAULT = 0,
    NCZ_SRC_CONFIG  = 1,
    NCZ_SRC_PRESET  = 2,
    NCZ_SRC_ENV     = 3,
    NCZ_SRC_ARGV    = 4,
};

typedef struct {
    const ncz_opt_def *defs;
    size_t             n;
    char             (*val)[NCZ_OPT_VALMAX];
    unsigned char     *set;      /* 1 when the value came from a source, not the default */
    unsigned char     *src;      /* NCZ_SRC_* of the winning source */
    int                warnings; /* number of warnings issued */
    int                quiet;    /* when set, warnings are counted but not printed */
} ncz_opts;

/* Validate `in` against `d`. On success writes the canonical form to out and
 * returns 0; otherwise returns -1 and writes a short reason to why (may be NULL). */
int ncz_opt_validate(const ncz_opt_def *d, const char *in, char *out, size_t outsz,
                     char *why, size_t whysz);

/* Allocate storage and load defaults. Returns 0 on success. */
int  ncz_opts_init(ncz_opts *o, const ncz_opt_def *defs, size_t n);
void ncz_opts_free(ncz_opts *o);

/* Apply one source. `tag` names the source in warnings. */
void ncz_opts_apply_keyfile(ncz_opts *o, const char *path, const char *group, const char *tag);
void ncz_opts_apply_env(ncz_opts *o, const char *prefix, const char *(*getenv_fn)(const char *));
/* Returns bit 1 for --help, 2 for --list-options, 4 for an unknown option,
 * 8 for a stray non-option argument. */
int  ncz_opts_apply_argv(ncz_opts *o, int argc, char **argv, const char *prog);

/* Full resolution in precedence order. conf_path may be NULL (then the default
 * $XDG_CONFIG_HOME/ncz-screensavers/<group>.conf, or ~/.config/..., is used;
 * NCZ_<GROUP>_CONFIG overrides). Returns the bit set from apply_argv. */
int  ncz_opts_resolve(ncz_opts *o, const char *prefix, const char *group,
                      const char *conf_path, int argc, char **argv, const char *prog,
                      const char *(*getenv_fn)(const char *));

int         ncz_opts_is_set(const ncz_opts *o, const char *name);
/* NCZ_SRC_* of the source that supplied `name`; NCZ_SRC_DEFAULT if unset. */
int         ncz_opts_source(const ncz_opts *o, const char *name);
/* Like ncz_opts_resolve, with an optional preset key file applied after the
 * user config file and before the environment (defaults < config < preset <
 * environment < command line). */
int  ncz_opts_resolve_preset(ncz_opts *o, const char *prefix, const char *group,
                             const char *conf_path, const char *preset_path,
                             const char *preset_tag, int argc, char **argv,
                             const char *prog, const char *(*getenv_fn)(const char *));

const char *ncz_opts_get(const ncz_opts *o, const char *name);
double      ncz_opts_get_float(const ncz_opts *o, const char *name);
long        ncz_opts_get_int(const ncz_opts *o, const char *name);
int         ncz_opts_get_bool(const ncz_opts *o, const char *name);

void ncz_opts_print_list(const ncz_opts *o, FILE *f, const char *prefix);
void ncz_opts_print_help(const ncz_opts *o, FILE *f, const char *prefix, const char *prog);

#endif
