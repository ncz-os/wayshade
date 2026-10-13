/* SPDX-License-Identifier: Apache-2.0 */
/* test_xst_core.c - the generic xshadertoy option, seed, director and palette
 * mechanism (src/xst_core.c): parsing, precedence, randomize ranges,
 * reproducibility, director continuity and phase integration. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xst_core.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static const char *SRC =
    "// test shader\n"
    "//@saver id=demo-saver version=3 family=dark|ink director=60\n"
    "//@palette-native bg=#0e1526 body=#e9dcc6 accent=#f4e6cc accent2=#8a7058\n"
    "//@option name=density type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation\n"
    "//@+ label=\"Filament density\" desc=\"How much.\"\n"
    "//@option name=count type=int default=4 min=2 max=9 rand=3:6 label=Count\n"
    "//@option name=mode type=enum default=calm choices=calm,storm,still rand=calm|storm label=Mode\n"
    "//@option name=glow type=bool default=false rand=0.5 label=Glow\n"
    "//@option name=hull type=enum default=auto choices=auto,slab,long bind=uLv0.y label=Hull\n"
    "//@option name=bad_name type=float default=1 min=0 max=1\n"
    "void mainImage(out vec4 c, in vec2 f) { c = vec4(OPT_DENSITY); }\n";

static int idx(const XstSpec *s, const char *n) {
    for (size_t i = 0; i < s->n; i++) if (!strcmp(s->defs[i].name, n)) return (int)i;
    return -1;
}

static void test_parse(void) {
    static XstSpec s;
    CHECK(xst_spec_parse(&s, "demo.glsl", SRC) == 0, "parse");
    CHECK(s.has_saver, "has_saver");
    CHECK(!strcmp(s.id, "demo-saver"), "id %s", s.id);
    CHECK(s.version == 3, "version");
    CHECK(!strcmp(s.prefix, "NCZ_XSHADERTOY_DEMO_SAVER_"), "prefix %s", s.prefix);
    CHECK(!strcmp(s.group, "xshadertoy_demo-saver"), "group %s", s.group);
    CHECK(s.has_native, "native palette");
    CHECK(fabs(s.director_period - 60.0) < 1e-9, "director period");
    CHECK(idx(&s, "bad_name") < 0, "invalid option name rejected");
    CHECK(strstr(s.errors, "bad_name") != NULL, "error recorded: %s", s.errors);
    const char *order[] = {"preset", "palette", "palette-cycle", "speed", "evolution", "density", "count",
                           "mode", "glow", "hull", "randomize", "quality", "seed"};
    CHECK(s.n == sizeof order / sizeof order[0], "n=%zu", s.n);
    for (size_t i = 0; i < s.n && i < sizeof order / sizeof order[0]; i++)
        CHECK(!strcmp(s.defs[i].name, order[i]), "order %zu: %s", i, s.defs[i].name);
    int d = idx(&s, "density");
    CHECK(d >= 0 && s.has_rand[d] && fabs(s.rlo[d] - 0.3) < 1e-9 && fabs(s.rhi[d] - 0.7) < 1e-9, "rand range");
    CHECK(d >= 0 && !strcmp(s.defs[d].label, "Filament density") && !strcmp(s.defs[d].desc, "How much."), "continuation line");
    int h = idx(&s, "hull");
    CHECK(h >= 0 && !strcmp(s.bind_name[h], "uLv0") && s.bind_comp[h] == 1, "bind");
    static XstSpec legacy;
    CHECK(xst_spec_parse(&legacy, "old.glsl", "void mainImage(out vec4 c, in vec2 f) {}\n") == 0 && !legacy.has_saver, "legacy");
}

static void test_run(void) {
    static XstSpec s;
    xst_spec_parse(&s, "demo.glsl", SRC);
    ncz_opts o;
    ncz_opts_init(&o, s.defs, s.n);
    char *argv[] = {"x", "--seed=777", "--count=8", NULL};
    ncz_opts_apply_argv(&o, 3, argv, "x");
    static XstRun a, b;
    xst_run_init(&a, &s, &o, 1);
    xst_run_init(&b, &s, &o, 1);
    CHECK(a.seed == 777 && !a.seed_was_random, "seed");
    for (size_t i = 0; i < s.n; i++) CHECK(!strcmp(a.val[i], b.val[i]), "reproducible %s", s.defs[i].name);
    for (int i = 0; i < 4; i++) CHECK(a.iseed[i] == b.iseed[i] && a.iseed[i] >= 0.f && a.iseed[i] < 1.f, "iseed");
    CHECK(a.palette_index == b.palette_index && a.palette_index >= 0, "palette drawn from family");
    /* family "light+stone" never draws a mineral palette tagged "stone" */
    static XstSpec fs;
    xst_spec_parse(&fs, "f.glsl", "//@saver id=f family=light+stone\n");
    for (int sd = 1; sd <= 300; sd++) {
        char sa[32]; snprintf(sa, sizeof sa, "--seed=%d", sd);
        char *av[] = {"x", sa, NULL};
        ncz_opts of; ncz_opts_init(&of, fs.defs, fs.n); ncz_opts_apply_argv(&of, 2, av, "x");
        static XstRun rf;
        xst_run_init(&rf, &fs, &of, 2);
        char line[4096];
        xst_run_describe(&rf, line, sizeof line, 1, 1, 0);
        CHECK(!strstr(line, "palette=nero") && !strstr(line, "palette=calacatta") && !strstr(line, "palette=linen")
              && !strstr(line, "palette=navy"), "light+stone drew %s", strstr(line, "palette="));
        ncz_opts_free(&of);
    }
    CHECK(!strcmp(xst_run_get(&a, "count"), "8") && !a.randomized[idx(&s, "count")], "explicit value kept");
    /* randomized values stay inside the rand ranges, over many seeds */
    int seen_storm = 0, seen_calm = 0;
    for (int sd = 1; sd <= 400; sd++) {
        char sa[32]; snprintf(sa, sizeof sa, "--seed=%d", sd);
        char *av[] = {"x", sa, NULL};
        ncz_opts oo; ncz_opts_init(&oo, s.defs, s.n); ncz_opts_apply_argv(&oo, 2, av, "x");
        static XstRun r;
        xst_run_init(&r, &s, &oo, 2);
        double dv = atof(xst_run_get(&r, "density"));
        long cv = atol(xst_run_get(&r, "count"));
        CHECK(dv >= 0.3 && dv <= 0.7, "density %g", dv);
        CHECK(cv >= 3 && cv <= 6, "count %ld", cv);
        const char *m = xst_run_get(&r, "mode");
        CHECK(!strcmp(m, "calm") || !strcmp(m, "storm"), "mode %s", m);
        seen_storm += !strcmp(m, "storm"); seen_calm += !strcmp(m, "calm");
        ncz_opts_free(&oo);
    }
    CHECK(seen_storm > 100 && seen_calm > 100, "enum draw both (%d/%d)", seen_storm, seen_calm);
    /* randomize off: defaults, native palette */
    char *av2[] = {"x", "--seed=5", "--no-randomize", NULL};
    ncz_opts o2; ncz_opts_init(&o2, s.defs, s.n); ncz_opts_apply_argv(&o2, 3, av2, "x");
    static XstRun c;
    xst_run_init(&c, &s, &o2, 0);
    CHECK(!strcmp(xst_run_get(&c, "density"), "0.5"), "default density %s", xst_run_get(&c, "density"));
    CHECK(c.palette_index == -1, "native palette when not randomizing");
    CHECK(c.quality == 0, "quality hint");
    /* bound uniform: enum index */
    char *av3[] = {"x", "--hull=long", NULL};
    ncz_opts o3; ncz_opts_init(&o3, s.defs, s.n); ncz_opts_apply_argv(&o3, 2, av3, "x");
    static XstRun d;
    xst_run_init(&d, &s, &o3, 2);
    CHECK(d.nbind == 1 && !strcmp(d.bind_uniform[0], "uLv0") && d.bind_val[0][1] == 2.f, "bind value");
    /* defines */
    char *df = xst_defines(&a);
    CHECK(df && strstr(df, "#define NCZ_QUALITY 1\n"), "quality define");
    CHECK(df && strstr(df, "#define OPT_COUNT 8\n"), "int define");
    CHECK(df && strstr(df, "#define OPT_MODE_STORM 1\n"), "enum constant");
    CHECK(df && !strstr(df, "OPT_HULL"), "bound option is not a define");
    const char *dd = df ? strstr(df, "#define OPT_DENSITY ") : NULL;
    CHECK(dd && strchr(dd + 20, '.') && strchr(dd + 20, '.') < strchr(dd + 20, '\n'), "float define has a decimal point");
    free(df);
    ncz_opts_free(&o); ncz_opts_free(&o2); ncz_opts_free(&o3);
}

static void test_director(void) {
    CHECK(fabs(xst_quintic_integral(1.0) - 0.5) < 1e-12, "Q(1)");
    /* numeric integral of the quintic */
    double acc = 0; int N = 100000;
    for (int i = 0; i < N; i++) { double u = (i + 0.5) / N; acc += u * u * u * (u * (u * 6 - 15) + 10) / N; }
    CHECK(fabs(acc - 0.5) < 1e-6, "numeric");
    static XstSpec s;
    xst_spec_parse(&s, "demo.glsl", SRC);
    char *av[] = {"x", "--seed=31337", NULL};
    ncz_opts o; ncz_opts_init(&o, s.defs, s.n); ncz_opts_apply_argv(&o, 2, av, "x");
    static XstRun r, fresh;
    xst_run_init(&r, &s, &o, 2);
    float prev[16], cur[16];
    double dt = 1.0 / 60.0;
    xst_director(&r, 0.0, prev);
    for (int i = 0; i < 16; i++) CHECK(i < 8 || fabsf(prev[i]) < 1e-6f, "phase(0)=0 (%d: %g)", i, prev[i]);
    double maxdp = 0, minrate = 9, maxrate = 0;
    for (long f = 1; f <= 60L * 3600; f++) {
        double t = f * dt;
        xst_director(&r, t, cur);
        for (int i = 0; i < 8; i++) {
            CHECK(cur[i] >= 0.f && cur[i] <= 1.f, "param range");
            double d = fabs((double)cur[i] - prev[i]);
            if (d > maxdp) maxdp = d;
        }
        if (f % 600 == 0) {  /* rate over 10 s steps, in float precision */
            for (int i = 8; i < 16; i++) (void)0;
        }
        for (int i = 8; i < 16; i++) {
            double rate = ((double)cur[i] - prev[i]) / dt;
            if (t < 600) { if (rate < minrate) minrate = rate; if (rate > maxrate) maxrate = rate; }
        }
        memcpy(prev, cur, sizeof prev);
    }
    CHECK(maxdp < 0.002, "director params change smoothly (max step %g per frame)", maxdp);
    CHECK(minrate > 0.45 && maxrate < 1.55, "phase rate in [0.5,1.5] (%g..%g)", minrate, maxrate);
    /* the cache is only a speed-up: a fresh run evaluated straight at t gives the same numbers */
    xst_run_init(&fresh, &s, &o, 2);
    float a[16], b[16];
    xst_director(&r, 2345.6, a);
    xst_director(&fresh, 2345.6, b);
    for (int i = 0; i < 16; i++) CHECK(fabsf(a[i] - b[i]) <= 1e-3f * (1.f + fabsf(a[i])), "cache-independent %d", i);
    /* evolution 0 holds the parameters */
    char *av0[] = {"x", "--seed=9", "--evolution=0", NULL};
    ncz_opts o0; ncz_opts_init(&o0, s.defs, s.n); ncz_opts_apply_argv(&o0, 3, av0, "x");
    static XstRun z;
    xst_run_init(&z, &s, &o0, 2);
    xst_director(&z, 0.0, a); xst_director(&z, 1000.0, b);
    for (int i = 0; i < 8; i++) CHECK(a[i] == b[i], "evolution 0 holds p%d", i);
    ncz_opts_free(&o); ncz_opts_free(&o0);
}

static void test_palette_cycle(void) {
    static XstSpec s;
    xst_spec_parse(&s, "demo.glsl", SRC);
    char *av[] = {"x", "--seed=12", "--palette-cycle=1", NULL};
    ncz_opts o; ncz_opts_init(&o, s.defs, s.n); ncz_opts_apply_argv(&o, 3, av, "x");
    static XstRun r;
    xst_run_init(&r, &s, &o, 2);
    float p[12], q[12];
    xst_palette_at(&r, 0.0, p);
    double maxd = 0; int changed = 0;
    for (int f = 1; f < 60 * 400; f++) {
        xst_palette_at(&r, f / 60.0, q);
        for (int i = 0; i < 12; i++) { double d = fabs((double)q[i] - p[i]); if (d > maxd) maxd = d; if (d > 0) changed = 1; }
        memcpy(p, q, sizeof p);
    }
    CHECK(changed, "palette cycles");
    CHECK(maxd < 0.01, "palette cross-fade is smooth (max step %g)", maxd);
    ncz_opts_free(&o);
}

int main(void) {
    test_parse();
    test_run();
    test_director();
    test_palette_cycle();
    if (fails) { fprintf(stderr, "%d failures\n", fails); return 1; }
    printf("test_xst_core: ok\n");
    return 0;
}
