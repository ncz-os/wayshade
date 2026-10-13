/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "ncz_harness_cfg.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NCZ_VERSION
#define NCZ_VERSION "dev"
#endif

static const ncz_opt_def GEN_OPTS[] = {
 {"render-scale", NCZ_OPT_FLOAT, "1", 0.25, 1, "", "", "NCZ_RENDER_SCALE",
  "Render scale", "Fraction of the output resolution the shader renders at; the compositor upscales it. 1 = native.", "Render"},
 {"max-render-height", NCZ_OPT_INT, "0", 0, 8640, "", "", "NCZ_MAX_RENDER_HEIGHT",
  "Max render height", "Cap the render height in pixels (aspect ratio kept); 0 = unlimited. When not set at all, the platform default applies: 1080 on Mali, and on integrated Intel above 1440 lines; unlimited elsewhere.", "Render"},
 {"render-scale-mode", NCZ_OPT_ENUM, "auto", 0, 0, "auto,fixed", "", "NCZ_RENDER_SCALE_MODE",
  "Render scale mode", "auto: step the scale down (1, 0.75, 0.5, 0.35) if the 95th percentile frame time stays above 40 ms for 3 s, never back up. fixed: keep the configured size.", "Render"},
};
#define GEN_N (sizeof GEN_OPTS / sizeof GEN_OPTS[0])

static ncz_opts g_o;
static int g_ready;
static const ncz_opt_def *g_merged;

const ncz_opts *ncz_harness_opts(void) { return g_ready ? &g_o : NULL; }
double ncz_cfg_render_scale(void) { return g_ready ? ncz_opts_get_float(&g_o, "render-scale") : 1.0; }
int ncz_cfg_max_render_height(int *explicit_set) {
    if (!g_ready) { if (explicit_set) *explicit_set = 0; return 0; }
    if (explicit_set) *explicit_set = ncz_opts_is_set(&g_o, "max-render-height");
    return (int)ncz_opts_get_int(&g_o, "max-render-height");
}
int ncz_cfg_render_scale_is_set(void) { return g_ready && ncz_opts_is_set(&g_o, "render-scale"); }
int ncz_cfg_scale_auto(void) { return g_ready ? !strcmp(ncz_opts_get(&g_o, "render-scale-mode"), "auto") : 1; }

/* ---- presets ------------------------------------------------------------
 * A preset is a key file: [preset] name/description/accuracy/sources and a
 * [<group>] section of option values. It sits between the user config file
 * and the environment in the precedence chain, so an explicit selection beats
 * the saved defaults but env and command line still win. */
static int preset_id_ok(const char *id) {
    if (!id || !*id || *id == '-' || strlen(id) > 64) return 0;
    for (const char *p = id; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return 0;
    return 1;
}

/* Directories searched for <group> presets, in order. */
static int preset_dirs(const char *group, char dirs[4][512]) {
    int n = 0;
    const char *e = getenv("NCZ_PRESET_DIR"), *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (e && *e) snprintf(dirs[n++], 512, "%s", e);
    if (x && *x) snprintf(dirs[n++], 512, "%s/ncz-screensavers/presets/%s", x, group);
    else if (h && *h) snprintf(dirs[n++], 512, "%s/.local/share/ncz-screensavers/presets/%s", h, group);
    snprintf(dirs[n++], 512, "/usr/share/ncz-screensavers/presets/%s", group);
    snprintf(dirs[n++], 512, "assets/screensaver-chooser/options/presets/%s", group);
    return n;
}

static int preset_path(const char *group, const char *id, char *out, size_t sz) {
    char dirs[4][512];
    int n = preset_dirs(group, dirs);
    for (int i = 0; i < n; i++) {
        snprintf(out, sz, "%s/%s.conf", dirs[i], id);
        FILE *f = fopen(out, "r");
        if (f) { fclose(f); return 1; }
    }
    return 0;
}

/* Value of `key` in the [preset] section of a key file (empty if absent). */
static void preset_meta(const char *path, const char *key, char *out, size_t sz) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[600];
    int in = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
        if (line[0] == '[') { in = (strcmp(line, "[preset]") == 0); continue; }
        if (!in) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (strcmp(line, key) == 0) { snprintf(out, sz, "%s", eq + 1); break; }
    }
    fclose(f);
}

static void list_presets(const char *group) {
    char dirs[4][512], seen[64][64];
    int nseen = 0, n = preset_dirs(group, dirs);
    for (int i = 0; i < n; i++) {
        DIR *d = opendir(dirs[i]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 6 || strcmp(e->d_name + l - 5, ".conf") != 0) continue;
            char id[64];
            if (l - 5 >= sizeof id) continue;
            memcpy(id, e->d_name, l - 5); id[l - 5] = 0;
            if (!preset_id_ok(id)) continue;
            int dup = 0;
            for (int k = 0; k < nseen; k++) if (!strcmp(seen[k], id)) dup = 1;
            if (dup || nseen >= 64) continue;
            snprintf(seen[nseen++], 64, "%s", id);
            char path[1024], name[200], acc[40], desc[400];
            snprintf(path, sizeof path, "%s/%s", dirs[i], e->d_name);
            preset_meta(path, "name", name, sizeof name);
            preset_meta(path, "accuracy", acc, sizeof acc);
            preset_meta(path, "description", desc, sizeof desc);
            printf("%s\t%s\t%s\t%s\n", id, name, acc, desc);
        }
        closedir(d);
    }
}

static void dump_schema(const ncz_opt_def *defs, size_t n, const char *prefix) {
    for (size_t k = 0; k < n; k++) {
        const ncz_opt_def *d = &defs[k];
        const char *t = d->type == NCZ_OPT_BOOL ? "bool" : d->type == NCZ_OPT_INT ? "int" :
                        d->type == NCZ_OPT_FLOAT ? "float" : d->type == NCZ_OPT_ENUM ? "enum" : "string";
        char mn[32] = "", mx[32] = "", en[128], *q = en;
        if (d->type == NCZ_OPT_INT || d->type == NCZ_OPT_FLOAT) {
            snprintf(mn, sizeof mn, "%g", d->min);
            snprintf(mx, sizeof mx, "%g", d->max);
        }
        for (const char *p = prefix; *p && q < en + 60; p++) *q++ = *p;
        for (const char *p = d->name; *p && q < en + 120; p++)
            *q++ = *p == '-' ? '_' : (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        *q = 0;
        printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", d->name, t, d->def, mn, mx,
               (d->type == NCZ_OPT_ENUM || d->type == NCZ_OPT_LIST) ? d->choices : "",
               d->label, d->desc, d->group, en);
    }
}

/* JSON string, escaped: the first n bytes of v, or all of it for n < 0. */
static void json_strn(const char *v, long n) {
    putchar('"');
    const unsigned char *p = (const unsigned char *)(v ? v : "");
    for (long i = 0; *p && (n < 0 || i < n); p++, i++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 0x20) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}
static void json_str(const char *v) { json_strn(v, -1); }
/* A number, or 0 for a value JSON cannot hold (inf, nan). */
static void json_num(double v) { printf("%.10g", isfinite(v) ? v : 0.0); }

/* The schema as docs/OPTIONS-SCHEMA.md's options/<id>.json: one object per
 * option, typed defaults, ranges and choices. A LIST option is "list" with
 * its choices; its default is a comma string. */
static void dump_schema_json(const ncz_opt_def *defs, size_t n, const char *prefix) {
    printf("{\n  \"options\": [\n");
    for (size_t k = 0; k < n; k++) {
        const ncz_opt_def *d = &defs[k];
        const char *t = d->type == NCZ_OPT_BOOL ? "bool" : d->type == NCZ_OPT_INT ? "int" :
                        d->type == NCZ_OPT_FLOAT ? "float" : d->type == NCZ_OPT_ENUM ? "enum" :
                        d->type == NCZ_OPT_LIST ? "list" : "string";
        printf("    { \"id\": "); json_str(d->name);
        printf(", \"type\": \"%s\", \"label\": ", t); json_str(d->label);
        printf(", \"default\": ");
        const char *def = d->def ? d->def : "";
        if (d->type == NCZ_OPT_BOOL) printf("%s", (!strcmp(def, "1") || !strcmp(def, "true") || !strcmp(def, "on")) ? "true" : "false");
        else if (d->type == NCZ_OPT_INT || d->type == NCZ_OPT_FLOAT) json_num(*def ? strtod(def, NULL) : 0.0);
        else json_str(def);
        if (d->type == NCZ_OPT_INT || d->type == NCZ_OPT_FLOAT) {
            printf(", \"range\": [");
            json_num(d->min);
            printf(", ");
            json_num(d->max);
            printf("]");
        }
        if (d->type == NCZ_OPT_ENUM || d->type == NCZ_OPT_LIST) {
            printf(", \"choices\": [");
            const char *p = d->choices;
            for (int first = 1; p && *p; first = 0) {
                const char *e = strchr(p, ',');
                size_t len = e ? (size_t)(e - p) : strlen(p);
                printf("%s", first ? "" : ", ");
                json_strn(p, (long)len);
                p = e ? e + 1 : NULL;
            }
            printf("]");
        }
        printf(",\n      \"group\": "); json_str(d->group);
        char env[128], *q = env;
        for (const char *p = prefix; *p && q < env + 60; p++) *q++ = *p;
        for (const char *p = d->name; *p && q < env + 120; p++)
            *q++ = *p == '-' ? '_' : (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        *q = 0;
        printf(", \"env\": "); json_str(env);
        printf(",\n      \"description\": "); json_str(d->desc);
        printf(" }%s\n", k + 1 < n ? "," : "");
    }
    printf("  ]\n}\n");
}

int ncz_harness_cfg_init(int argc, char **argv) {
    const ncz_opt_def *hack = NULL;
    size_t hn = 0;
    const char *prefix = "NCZ_", *group = "render";
    if (ncz_hack_options) {
        const char *hp = NULL, *hg = NULL;
        hack = ncz_hack_options(&hn, &hp, &hg);
        if (hp) prefix = hp;
        if (hg) group = hg;
    }
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : "hack";
    int flags = 0;
    /* schema dumps first: --dump-schema (hack options) / --dump-schema-common */
    for (int i = 1; i < argc; i++) {
        if (argv[i] && !strcmp(argv[i], "--dump-schema")) { if (hack) dump_schema(hack, hn, prefix); return 0; }
        if (argv[i] && !strcmp(argv[i], "--dump-schema-json")) { if (hack) dump_schema_json(hack, hn, prefix); return 0; }
        if (argv[i] && !strcmp(argv[i], "--dump-schema-common")) { dump_schema(GEN_OPTS, GEN_N, "NCZ_"); return 0; }
        if (argv[i] && !strcmp(argv[i], "--list-presets")) { if (hack) list_presets(group); return 0; }
        if (argv[i] && !strcmp(argv[i], "--version")) { printf("%s (ncz-screensavers %s)\n", prog, NCZ_VERSION); return 0; }
    }
    ncz_opt_def *m = calloc(GEN_N + hn, sizeof *m);
    if (!m) return -1;
    memcpy(m, GEN_OPTS, sizeof GEN_OPTS);
    if (hack) memcpy(m + GEN_N, hack, hn * sizeof *m);
    g_merged = m;
    /* Pass 1 (quiet) learns which preset was asked for from any source; pass 2
     * re-resolves with the preset key file inserted below env and CLI. */
    char ppath[600] = "";
    for (int pass = 0; pass < 2; pass++) {
        if (ncz_opts_init(&g_o, m, GEN_N + hn) != 0) { free(m); g_merged = NULL; return -1; }
        g_o.quiet = (pass == 0);
        {
            const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
            char path[512] = "";
            if (x && *x) snprintf(path, sizeof path, "%s/ncz-screensavers/render.conf", x);
            else if (h && *h) snprintf(path, sizeof path, "%s/.config/ncz-screensavers/render.conf", h);
            if (*path) ncz_opts_apply_keyfile(&g_o, path, "render", "render config file");
        }
        char tag[96] = "";
        const char *pp = NULL;
        if (pass == 1 && *ppath) {
            const char *base = strrchr(ppath, '/');
            snprintf(tag, sizeof tag, "preset %s", base ? base + 1 : ppath);
            char *dot = strstr(tag, ".conf");
            if (dot) *dot = 0;
            pp = ppath;
        }
        flags = ncz_opts_resolve_preset(&g_o, prefix, group, NULL, pp, tag, argc, argv, prog, NULL);
        if (pass == 0) {
            const char *pid = hack ? ncz_opts_get(&g_o, "preset") : "";
            if (pid && *pid) {
                if (!preset_id_ok(pid) || !preset_path(group, pid, ppath, sizeof ppath)) {
                    fprintf(stderr, "[opts] preset: unknown preset '%.60s' (see --list-presets); ignored\n", pid);
                    ppath[0] = 0;
                }
            }
            if (!*ppath) { ncz_opts_free(&g_o); continue; }
            ncz_opts_free(&g_o);
        }
    }
    g_ready = 1;
    if (flags & (NCZ_ARG_UNKNOWN_OPTION | NCZ_ARG_STRAY_ARGUMENT)) {
        fprintf(stderr, "%s: unknown option or argument (see --help)\n", prog);
        return 2;
    }
    if (flags & NCZ_ARG_PRINT_CONFIG) {
        for (size_t k = 0; k < GEN_N + hn; k++)
            printf("%s=%s\n", m[k].name, ncz_opts_get(&g_o, m[k].name));
        return 0;
    }
    if (flags & 1) { ncz_opts_print_help(&g_o, stdout, prefix, prog); return 0; }
    if (flags & 2) { ncz_opts_print_list(&g_o, stdout, prefix); return 0; }
    return -1;
}
