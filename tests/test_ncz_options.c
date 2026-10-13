/* SPDX-License-Identifier: Apache-2.0 */
/* Unit test for the option parser and the blackhole option table.
 * Covers: defaults, precedence (defaults < config file < env < CLI), invalid
 * and unknown input (warn once, fall back), ranges, legacy env aliases, bool
 * forms, --help/--list-options flags, and table consistency. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ncz_options.h"
#include "blackhole_opts.h"

static const char *g_env[16][2];
static int g_nenv;
static const char *fake_getenv(const char *n) {
    for (int i = 0; i < g_nenv; i++) if (!strcmp(g_env[i][0], n)) return g_env[i][1];
    return NULL;
}
static void setenv_fake(const char *n, const char *v) { g_env[g_nenv][0] = n; g_env[g_nenv][1] = v; g_nenv++; }

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char *write_conf(const char *body) {
    static char path[] = "/tmp/ncz_opts_test_XXXXXX";
    static int made;
    if (!made) { int fd = mkstemp(path); close(fd); made = 1; }
    FILE *f = fopen(path, "w"); fputs(body, f); fclose(f);
    return path;
}

int main(void) {
    ncz_opts o;
    /* silence expected warnings on stderr */
    FILE *keep = stderr; stderr = fopen("/dev/null", "w");

    /* table consistency */
    for (size_t i = 0; i < BH_NOPTS; i++) {
        char out[NCZ_OPT_VALMAX];
        CHECK(ncz_opt_validate(&BH_OPTS[i], BH_OPTS[i].def, out, sizeof out, NULL, 0) == 0);
        if (BH_OPTS[i].type == NCZ_OPT_INT || BH_OPTS[i].type == NCZ_OPT_FLOAT) {
            double d = atof(BH_OPTS[i].def);
            CHECK(BH_OPTS[i].min <= d && d <= BH_OPTS[i].max);
        }
    }

    /* defaults */
    CHECK(ncz_opts_init(&o, BH_OPTS, BH_NOPTS) == 0);
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "stylized"));
    CHECK(ncz_opts_get_float(&o, "spin") == 0.0);
    CHECK(ncz_opts_get_bool(&o, "lensing") == 1);
    CHECK(ncz_opts_get_int(&o, "seed") == 0);
    ncz_opts_free(&o);

    /* precedence: file < env < CLI */
    char *conf = write_conf("[blackhole]\npalette=kipthorne\nspin = 0.5\nexposure=1.5\n[other]\nspin=0.9\n");
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0; setenv_fake("NCZ_BLACKHOLE_SPIN", "0.7"); setenv_fake("NCZ_BLACKHOLE_EXPOSURE", "2");
    char *argv1[] = {"bh", "--exposure=3", NULL};
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", conf, 2, argv1, "bh", fake_getenv);
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "kipthorne")); /* file only */
    CHECK(ncz_opts_get_float(&o, "spin") == 0.7);             /* env beats file; [other] ignored */
    CHECK(ncz_opts_get_float(&o, "exposure") == 3.0);         /* CLI beats env and file */
    CHECK(o.warnings == 0);
    ncz_opts_free(&o);

    /* invalid values warn once and fall back to the next lower source */
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0; setenv_fake("NCZ_BLACKHOLE_SPIN", "0.4");
    char *argv2[] = {"bh", "--spin=1.5", "--palette=magenta", "--seed=1.5", "--lensing=maybe",
                     "--flyby=sideways", "--cycle-palettes=stylized,nope", "--bogus=1", "stray", NULL};
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent/none.conf", 9, argv2, "bh", fake_getenv);
    CHECK(ncz_opts_get_float(&o, "spin") == 0.4);              /* CLI 1.5 out of range: env 0.4 stays */
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "stylized"));   /* default */
    CHECK(ncz_opts_get_int(&o, "seed") == 0);
    CHECK(ncz_opts_get_bool(&o, "lensing") == 1);
    CHECK(!strcmp(ncz_opts_get(&o, "flyby"), "auto"));
    CHECK(!strcmp(ncz_opts_get(&o, "cycle-palettes"), ""));
    CHECK(o.warnings == 8);                                     /* one warning per bad item */
    ncz_opts_free(&o);

    /* aliases and canonicalization */
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0; setenv_fake("NCZ_BLACKHOLE_COLORS", "Stylised"); setenv_fake("NCZ_BLACKHOLE_FIXED_SEED", "77");
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent", 1, (char *[]){"bh", NULL}, "bh", fake_getenv);
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "stylized"));
    CHECK(ncz_opts_get_int(&o, "seed") == 77);
    ncz_opts_free(&o);
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0; setenv_fake("NCZ_BLACKHOLE_COLORS", "faithful"); setenv_fake("NCZ_BLACKHOLE_PALETTE", "slingshot");
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent", 1, (char *[]){"bh", NULL}, "bh", fake_getenv);
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "slingshot"));   /* canonical name beats legacy alias */
    ncz_opts_free(&o);

    /* bool forms, list canonicalization, flags */
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0;
    char *argv3[] = {"bh", "--no-lensing", "--cycle-palettes=WhiteHole, slingshot", "--help", "--list-options", NULL};
    int fl = ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent", 5, argv3, "bh", fake_getenv);
    CHECK(fl == 3);
    CHECK(ncz_opts_get_bool(&o, "lensing") == 0);
    CHECK(!strcmp(ncz_opts_get(&o, "cycle-palettes"), "whitehole,slingshot"));
    ncz_opts_free(&o);
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    char *argv4[] = {"bh", "--lensing", "--seed=4294967295", "--inclination=-1", NULL};
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent", 4, argv4, "bh", fake_getenv);
    CHECK(ncz_opts_get_bool(&o, "lensing") == 1);
    CHECK(ncz_opts_get_int(&o, "seed") == 4294967295L);
    CHECK(ncz_opts_get_float(&o, "inclination") == -1.0);
    CHECK(o.warnings == 0);
    ncz_opts_free(&o);

    /* palettes: cygnus parses cleanly; an unknown palette is rejected.
     * Ported from dc19663 (the Cygnus palette commit); the palette itself and
     * its shader branches already live on the integration line. */
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0;
    char *argv5[] = {"bh", "--palette=cygnus", NULL};
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent", 2, argv5, "bh", fake_getenv);
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "cygnus"));     /* cygnus is a valid palette */
    CHECK(o.warnings == 0);
    ncz_opts_free(&o);
    ncz_opts_init(&o, BH_OPTS, BH_NOPTS);
    g_nenv = 0;
    char *argv6[] = {"bh", "--palette=bogus", NULL};
    ncz_opts_resolve(&o, "NCZ_BLACKHOLE_", "blackhole", "/nonexistent", 2, argv6, "bh", fake_getenv);
    CHECK(!strcmp(ncz_opts_get(&o, "palette"), "stylized"));   /* bogus is rejected: falls back to the default */
    CHECK(o.warnings == 1);
    ncz_opts_free(&o);

    unlink(conf);
    stderr = keep;
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    printf("ncz_options: all checks passed\n");
    return 0;
}
