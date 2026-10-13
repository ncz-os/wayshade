/* SPDX-License-Identifier: Apache-2.0 */
/* xst_core.c - see xst_core.h and docs/XSHADERTOY-OPTIONS.md. */
#define _POSIX_C_SOURCE 200809L
#include "xst_core.h"
#include "xst_palettes.h"

#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------- hashing */

uint32_t xst_hash_u32(uint32_t x) { /* lowbias32 */
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
uint32_t xst_fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}
/* Independent stream per purpose: the same seed gives the same palette
 * whatever options are added, because each draw is keyed by name. */
uint32_t xst_stream(uint32_t seed, const char *key) {
    return xst_hash_u32(xst_hash_u32(seed * 0x9E3779B9u + 0x632BE5ABu) ^ xst_fnv1a(key));
}
static double u01(uint32_t h) { return (double)(h >> 8) / 16777216.0; }

/* ------------------------------------------------------------ string pool */

static const char *pool_dup(XstSpec *s, const char *p, size_t len) {
    if (s->pool_used + len + 1 > sizeof s->pool) {
        if (!strstr(s->errors, "pool")) {
            size_t l = strlen(s->errors);
            snprintf(s->errors + l, sizeof s->errors - l, "%sstring pool full", l ? "; " : "");
        }
        return "";
    }
    char *d = s->pool + s->pool_used;
    memcpy(d, p, len); d[len] = 0;
    s->pool_used += len + 1;
    return d;
}
static const char *pool_str(XstSpec *s, const char *p) { return pool_dup(s, p, strlen(p)); }

static void spec_err(XstSpec *s, const char *fmt, const char *a, int line) {
    char m[200];
    snprintf(m, sizeof m, fmt, a, line);
    fprintf(stderr, "[xshadertoy] spec: %s\n", m);
    size_t l = strlen(s->errors);
    if (l + strlen(m) + 2 < sizeof s->errors) { if (l) strcat(s->errors, "; "); strcat(s->errors, m); }
}

/* ---------------------------------------------------------------- parsing */

typedef struct { char key[24]; char val[600]; } Kv;

/* Split "k=v k2=\"quoted value\"" into pairs. Returns count. */
static int kv_split(const char *line, Kv *kv, int max) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *k = p;
        while (*p && *p != '=' && *p != ' ' && *p != '\t') p++;
        size_t kl = (size_t)(p - k);
        if (*p != '=' || kl == 0 || kl >= sizeof kv[n].key) { while (*p && *p != ' ') p++; continue; }
        memcpy(kv[n].key, k, kl); kv[n].key[kl] = 0;
        p++;
        size_t vl = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (vl + 1 < sizeof kv[n].val) kv[n].val[vl++] = *p; p++; }
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') { if (vl + 1 < sizeof kv[n].val) kv[n].val[vl++] = *p; p++; }
        }
        kv[n].val[vl] = 0;
        n++;
    }
    return n;
}
static const char *kv_get(const Kv *kv, int n, const char *key) {
    for (int i = 0; i < n; i++) if (!strcmp(kv[i].key, key)) return kv[i].val;
    return NULL;
}

static int parse_hex(const char *h, float out[3]) {
    if (!h) return -1;
    if (*h == '#') h++;
    if (strlen(h) != 6) return -1;
    char *end = NULL;
    unsigned long v = strtoul(h, &end, 16);
    if (!end || *end) return -1;
    out[0] = (float)((v >> 16) & 255) / 255.f;
    out[1] = (float)((v >> 8) & 255) / 255.f;
    out[2] = (float)(v & 255) / 255.f;
    return 0;
}

static int name_ok(const char *n) {
    if (!n || !*n || strlen(n) > 40) return 0;
    for (const char *p = n; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return 0;
    return 1;
}

static void add_def(XstSpec *s, const char *name, ncz_opt_type t, const char *def, double mn, double mx,
                    const char *choices, const char *label, const char *desc, const char *group) {
    if (s->n >= XST_MAX_OPTS) return;
    if (s->pool_used + strlen(name) + strlen(def) + strlen(choices ? choices : "") + strlen(label) +
        strlen(desc) + strlen(group) + 6 > sizeof s->pool) {
        fprintf(stderr, "[xshadertoy] spec: string pool full, option '%s' dropped\n", name);
        return;
    }
    ncz_opt_def *d = &s->defs[s->n];
    memset(d, 0, sizeof *d);
    d->name = pool_str(s, name);
    d->type = t;
    d->def = pool_str(s, def);
    d->min = mn; d->max = mx;
    d->choices = pool_str(s, choices ? choices : "");
    d->aliases = "";
    d->env_alias = NULL;
    d->label = pool_str(s, label);
    d->desc = pool_str(s, desc);
    d->group = pool_str(s, group);
    s->bind_name[s->n][0] = 0;
    s->has_rand[s->n] = 0;
    s->rchoices[s->n] = NULL;
    s->shader_opt[s->n] = 0;
    s->n++;
}

static int find_def(const XstSpec *s, const char *name) {
    for (size_t i = 0; i < s->n; i++) if (!strcmp(s->defs[i].name, name)) return (int)i;
    return -1;
}

static void add_shader_option(XstSpec *s, const Kv *kv, int nkv, int line) {
    const char *name = kv_get(kv, nkv, "name"), *type = kv_get(kv, nkv, "type");
    const char *def = kv_get(kv, nkv, "default");
    if (!name_ok(name)) { spec_err(s, "bad option name '%s' (line %d)", name ? name : "", line); return; }
    if (find_def(s, name) >= 0) { spec_err(s, "duplicate option '%s' (line %d)", name, line); return; }
    if (!type || !def) { spec_err(s, "option '%s' needs type and default (line %d)", name, line); return; }
    ncz_opt_type t;
    if (!strcmp(type, "float")) t = NCZ_OPT_FLOAT;
    else if (!strcmp(type, "int")) t = NCZ_OPT_INT;
    else if (!strcmp(type, "bool")) t = NCZ_OPT_BOOL;
    else if (!strcmp(type, "enum")) t = NCZ_OPT_ENUM;
    else { spec_err(s, "option '%s': unknown type (line %d)", name, line); return; }
    double mn = 0, mx = 0;
    const char *smin = kv_get(kv, nkv, "min"), *smax = kv_get(kv, nkv, "max");
    if (t == NCZ_OPT_FLOAT || t == NCZ_OPT_INT) {
        if (!smin || !smax) { spec_err(s, "option '%s' needs min and max (line %d)", name, line); return; }
        mn = atof(smin); mx = atof(smax);
        if (!(mn <= mx)) { spec_err(s, "option '%s': min > max (line %d)", name, line); return; }
    }
    const char *choices = kv_get(kv, nkv, "choices");
    if (t == NCZ_OPT_ENUM && (!choices || !*choices)) { spec_err(s, "enum '%s' needs choices (line %d)", name, line); return; }
    const char *label = kv_get(kv, nkv, "label");
    const char *desc = kv_get(kv, nkv, "desc");
    const char *group = kv_get(kv, nkv, "group");
    add_def(s, name, t, def, mn, mx, t == NCZ_OPT_ENUM ? choices : "", label ? label : name,
            desc ? desc : "", group ? group : "Simulation");
    int i = (int)s->n - 1;
    s->shader_opt[i] = 1;
    char why[128], canon[NCZ_OPT_VALMAX];
    if (ncz_opt_validate(&s->defs[i], def, canon, sizeof canon, why, sizeof why) != 0) {
        spec_err(s, "option '%s': default invalid (line %d)", name, line);
        s->n--;
        return;
    }
    const char *r = kv_get(kv, nkv, "rand");
    if (r && *r) {
        if (t == NCZ_OPT_FLOAT || t == NCZ_OPT_INT) {
            const char *c = strchr(r, ':');
            if (c) {
                s->rlo[i] = atof(r); s->rhi[i] = atof(c + 1);
                if (s->rlo[i] < mn) s->rlo[i] = mn;
                if (s->rhi[i] > mx) s->rhi[i] = mx;
                s->has_rand[i] = s->rlo[i] <= s->rhi[i];
            }
        } else if (t == NCZ_OPT_ENUM) {
            s->rchoices[i] = pool_str(s, r);
            s->has_rand[i] = 1;
        } else if (t == NCZ_OPT_BOOL) {
            s->rlo[i] = atof(r); /* probability of true */
            s->has_rand[i] = 1;
        }
    }
    const char *b = kv_get(kv, nkv, "bind");
    if (b && *b) {
        const char *dot = strrchr(b, '.');
        int comp = -1;
        if (dot && strlen(dot) == 2) comp = dot[1] == 'x' ? 0 : dot[1] == 'y' ? 1 : dot[1] == 'z' ? 2 : dot[1] == 'w' ? 3 : -1;
        size_t nl = dot ? (size_t)(dot - b) : 0;
        if (comp < 0 || nl == 0 || nl >= sizeof s->bind_name[i]) {
            spec_err(s, "option '%s': bind must be <uniform>.<x|y|z|w> (line %d)", name, line);
        } else {
            memcpy(s->bind_name[i], b, nl); s->bind_name[i][nl] = 0;
            s->bind_comp[i] = comp;
        }
    }
}

static void palette_choices(char *out, size_t sz) {
    snprintf(out, sz, "auto,native");
    for (int i = 0; i < XST_NPALETTES; i++) {
        size_t l = strlen(out);
        snprintf(out + l, sz - l, ",%s", XST_PALETTES[i].name);
    }
}

int xst_spec_parse(XstSpec *s, const char *basename, const char *src) {
    memset(s, 0, sizeof *s);
    s->director_period = 90.0;
    /* collect directives, joining //@+ continuations */
    char cur[2048] = "";
    int cur_line = 0, lineno = 0;
    enum { D_NONE, D_SAVER, D_OPTION, D_NATIVE } kind = D_NONE;
    /* two passes: first //@saver (common options first), then the rest */
    Kv kv[24];
    const char *p = src;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        p = src; lineno = 0; kind = D_NONE; cur[0] = 0;
        for (;;) {
            const char *e = p ? strchr(p, '\n') : NULL;
            size_t len = e ? (size_t)(e - p) : (p ? strlen(p) : 0);
            int at_end = !p || !*p;
            char line[1024];
            if (!at_end) {
                size_t l = len < sizeof line - 1 ? len : sizeof line - 1;
                memcpy(line, p, l); line[l] = 0;
                if (l && line[l - 1] == '\r') line[l - 1] = 0;
                lineno++;
            } else line[0] = 0;
            const char *t = line;
            while (*t == ' ' || *t == '\t') t++;
            int is_cont = !strncmp(t, "//@+", 4);
            int is_dir = !is_cont && !strncmp(t, "//@", 3);
            if ((is_dir || at_end || !is_cont) && kind != D_NONE) {
                /* flush the pending directive */
                int n = kv_split(cur, kv, 24);
                if (pass == 0 && kind == D_SAVER) {
                    s->has_saver = 1;
                    const char *id = kv_get(kv, n, "id");
                    if (id && name_ok(id)) snprintf(s->id, sizeof s->id, "%s", id);
                    const char *v = kv_get(kv, n, "version");
                    s->version = v ? atoi(v) : 1;
                    const char *f = kv_get(kv, n, "family");
                    snprintf(s->family, sizeof s->family, "%s", f ? f : "");
                    const char *d = kv_get(kv, n, "director");
                    if (d && atof(d) >= 5.0) s->director_period = atof(d);
                } else if (pass == 1 && kind == D_NATIVE) {
                    static const char *roles[4] = {"bg", "body", "accent", "accent2"};
                    int ok = 1;
                    for (int r = 0; r < 4; r++) if (parse_hex(kv_get(kv, n, roles[r]), s->native[r]) != 0) ok = 0;
                    if (!ok) spec_err(s, "palette-native needs bg, body, accent, accent2 as #rrggbb%s (line %d)", "", cur_line);
                    s->has_native = ok;
                } else if (pass == 1 && kind == D_OPTION) {
                    add_shader_option(s, kv, n, cur_line);
                }
                kind = D_NONE; cur[0] = 0;
            }
            if (at_end) break;
            if (is_dir) {
                const char *rest = NULL;
                if (!strncmp(t, "//@saver", 8)) { kind = D_SAVER; rest = t + 8; }
                else if (!strncmp(t, "//@option", 9)) { kind = D_OPTION; rest = t + 9; }
                else if (!strncmp(t, "//@palette-native", 17)) { kind = D_NATIVE; rest = t + 17; }
                else kind = D_NONE;
                if (rest) { snprintf(cur, sizeof cur, "%s", rest); cur_line = lineno; }
            } else if (is_cont && kind != D_NONE) {
                size_t l = strlen(cur);
                snprintf(cur + l, sizeof cur - l, " %s", t + 4);
            }
            p = e ? e + 1 : p + len;
        }
        if (pass == 0) {
            if (!s->has_saver) return 0;
            if (!s->id[0]) {
                const char *b = strrchr(basename, '/');
                b = b ? b + 1 : basename;
                size_t l = strcspn(b, ".");
                if (l >= sizeof s->id) l = sizeof s->id - 1;
                memcpy(s->id, b, l); s->id[l] = 0;
            }
            /* common options, in front of the shader's own */
            char pc[2048];
            palette_choices(pc, sizeof pc);
            add_def(s, "preset", NCZ_OPT_STRING, "", 0, 0, "", "Preset",
                    "Named scene (see --list-presets): applied above the saved config file and below environment and command line. Empty = none.", "Look");
            add_def(s, "palette", NCZ_OPT_ENUM, "auto", 0, 0, pc, "Palette",
                    "Color roles (background, body, accent, accent2). auto: drawn from this saver's family with the run seed. native: the saver's own colors. Any name from the shared role table.", "Look");
            add_def(s, "palette-cycle", NCZ_OPT_FLOAT, "0", 0, 60, "", "Palette cycle (min)",
                    "Minutes between slow cross-fades to another palette of the same family (interpolated in Oklab: lightness moves steadily from one palette to the other, never through grey). 0 = keep one palette.", "Look");
            add_def(s, "speed", NCZ_OPT_FLOAT, "1", 0.25, 3, "", "Speed",
                    "Time scale of every motion.", "Motion");
            add_def(s, "evolution", NCZ_OPT_FLOAT, "1", 0, 3, "", "Evolution",
                    "Pace of the slow director: seeded keyframes about every minute that change structure over minutes. 0 holds the opening structure.", "Motion");
        }
    }
    if (!s->has_saver) return 0;
    add_def(s, "randomize", NCZ_OPT_BOOL, "true", 0, 0, "", "Randomize",
            "Draw every option you have not set (and the palette) from the run seed, inside ranges chosen to stay coherent. Off: defaults.", "System");
    add_def(s, "quality", NCZ_OPT_ENUM, "auto", 0, 0, "auto,low,medium,high", "Quality",
            "Detail tier. auto follows the GPU class of this machine (NCZ_SHADER_QUALITY still overrides it).", "System");
    add_def(s, "seed", NCZ_OPT_INT, "0", 0, 999999, "", "Random seed",
            "Fixes the run's randomness so it can be replayed exactly (same options). 0 = a new seed (1 to 999999) every run; the seed in use is logged.", "System");
    /* environment prefix and config group */
    snprintf(s->prefix, sizeof s->prefix, "NCZ_XSHADERTOY_");
    size_t pl = strlen(s->prefix);
    for (const char *q = s->id; *q && pl + 2 < sizeof s->prefix; q++)
        s->prefix[pl++] = *q == '-' ? '_' : (char)toupper((unsigned char)*q);
    s->prefix[pl++] = '_'; s->prefix[pl] = 0;
    snprintf(s->group, sizeof s->group, "xshadertoy_%s", s->id);
    return 0;
}

/* -------------------------------------------------------------------- run */

const char *xst_run_get(const XstRun *run, const char *name) {
    int i = find_def(run->spec, name);
    return i < 0 ? "" : run->val[i];
}

static float srgb_to_lin(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
static void hex_lin(unsigned h, float o[3]) {
    o[0] = srgb_to_lin((float)((h >> 16) & 255) / 255.f);
    o[1] = srgb_to_lin((float)((h >> 8) & 255) / 255.f);
    o[2] = srgb_to_lin((float)(h & 255) / 255.f);
}
static void palette_lin(const XstRun *run, int idx, float out[4][3]) {
    if (idx < 0 || idx >= XST_NPALETTES) {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++)
            out[r][c] = run->spec->has_native ? srgb_to_lin(run->spec->native[r][c]) : (r ? 1.f : 0.f);
        return;
    }
    const XstPalette *p = &XST_PALETTES[idx];
    hex_lin(p->bg, out[0]); hex_lin(p->body, out[1]); hex_lin(p->accent, out[2]); hex_lin(p->accent2, out[3]);
}

/* family: alternatives separated by '|', each a set of tags joined by '+'
 * that must all be present ("light+stone|mineral"). Empty matches all. */
static int has_tag(const char *tags, const char *tok, size_t l) {
    for (const char *t = tags; t && *t;) {
        const char *e = strchr(t, '|');
        size_t tl = e ? (size_t)(e - t) : strlen(t);
        if (tl == l && !strncmp(t, tok, l)) return 1;
        t = e ? e + 1 : NULL;
    }
    return 0;
}
static int tag_match(const char *family, const char *tags) {
    if (!family || !*family) return 1;
    for (const char *alt = family; alt && *alt;) {
        const char *ae = strchr(alt, '|');
        size_t al = ae ? (size_t)(ae - alt) : strlen(alt);
        int all = 1;
        for (const char *t = alt; t < alt + al;) {
            const char *pe = memchr(t, '+', (size_t)(alt + al - t));
            size_t tl = pe ? (size_t)(pe - t) : (size_t)(alt + al - t);
            if (tl && !has_tag(tags, t, tl)) all = 0;
            t += tl + 1;
        }
        if (all && al) return 1;
        alt = ae ? ae + 1 : NULL;
    }
    return 0;
}
/* One pick (by hash h) among the family's palettes, never repeating `avoid`. */
static int family_pick(const XstSpec *s, uint32_t h, int avoid) {
    int cand[64], n = 0;
    for (int i = 0; i < XST_NPALETTES && n < 64; i++)
        if (tag_match(s->family, XST_PALETTES[i].tags) && i != avoid) cand[n++] = i;
    if (!n) return avoid >= 0 ? avoid : 0;
    return cand[h % (uint32_t)n];
}
static int palette_by_name(const char *n) {
    for (int i = 0; i < XST_NPALETTES; i++) if (!strcmp(XST_PALETTES[i].name, n)) return i;
    return -1;
}

static uint32_t fresh_seed(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t x = (uint32_t)ts.tv_nsec ^ ((uint32_t)ts.tv_sec * 2654435761u) ^ ((uint32_t)getpid() << 16);
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { uint32_t r; if (fread(&r, sizeof r, 1, f) == 1) x ^= r; fclose(f); }
    return 1u + xst_hash_u32(x) % 999999u;
}

static void fmt_float(char *out, size_t sz, double v) { snprintf(out, sz, "%.9g", v); }

void xst_run_init(XstRun *run, const XstSpec *spec, const ncz_opts *o, int quality_hint) {
    memset(run, 0, sizeof *run);
    run->spec = spec;
    for (size_t i = 0; i < spec->n; i++) {
        const char *v = o ? ncz_opts_get(o, spec->defs[i].name) : NULL;
        char canon[NCZ_OPT_VALMAX], why[96];
        if (!v || ncz_opt_validate(&spec->defs[i], v, canon, sizeof canon, why, sizeof why) != 0)
            ncz_opt_validate(&spec->defs[i], spec->defs[i].def, canon, sizeof canon, why, sizeof why);
        snprintf(run->val[i], NCZ_OPT_VALMAX, "%s", canon);
    }
    if (!spec->has_saver) {
        run->quality = quality_hint; run->speed = 1.f; run->evolution = 1.f; run->palette_index = -1;
        uint32_t sd = fresh_seed();
        for (int i = 0; i < 4; i++) run->iseed[i] = (float)u01(xst_stream(sd, i == 0 ? "geometry0" : i == 1 ? "geometry1" : i == 2 ? "geometry2" : "geometry3"));
        run->seed = sd; run->seed_was_random = 1;
        return;
    }
    long sd = strtol(xst_run_get(run, "seed"), NULL, 10);
    if (sd <= 0) { run->seed = fresh_seed(); run->seed_was_random = 1; }
    else run->seed = (uint32_t)sd;
    int randomize = !strcmp(xst_run_get(run, "randomize"), "true");
    /* randomize: every shader option with a rand range that nobody set */
    for (size_t i = 0; i < spec->n; i++) {
        if (!spec->shader_opt[i] || !spec->has_rand[i] || !randomize) continue;
        if (o && ncz_opts_is_set(o, spec->defs[i].name)) continue;
        char key[80];
        snprintf(key, sizeof key, "opt:%s", spec->defs[i].name);
        double r = u01(xst_stream(run->seed, key));
        const ncz_opt_def *d = &spec->defs[i];
        char out[NCZ_OPT_VALMAX];
        if (d->type == NCZ_OPT_FLOAT) fmt_float(out, sizeof out, spec->rlo[i] + r * (spec->rhi[i] - spec->rlo[i]));
        else if (d->type == NCZ_OPT_INT) {
            long lo = (long)ceil(spec->rlo[i]), hi = (long)floor(spec->rhi[i]);
            snprintf(out, sizeof out, "%ld", lo + (long)(r * (double)(hi - lo + 1)) > hi ? hi : lo + (long)(r * (double)(hi - lo + 1)));
        } else if (d->type == NCZ_OPT_BOOL) snprintf(out, sizeof out, "%s", r < spec->rlo[i] ? "true" : "false");
        else {
            /* enum: pick among the '|' list */
            const char *lst = spec->rchoices[i];
            int cnt = 1;
            for (const char *q = lst; *q; q++) if (*q == '|') cnt++;
            int k = (int)(r * cnt); if (k >= cnt) k = cnt - 1;
            const char *q = lst;
            for (int j = 0; j < k; j++) q = strchr(q, '|') + 1;
            size_t l = strcspn(q, "|");
            if (l >= sizeof out) l = sizeof out - 1;
            memcpy(out, q, l); out[l] = 0;
        }
        char canon[NCZ_OPT_VALMAX], why[96];
        if (ncz_opt_validate(d, out, canon, sizeof canon, why, sizeof why) == 0) {
            snprintf(run->val[i], NCZ_OPT_VALMAX, "%s", canon);
            run->randomized[i] = 1;
        }
    }
    /* quality */
    const char *q = xst_run_get(run, "quality");
    run->quality = !strcmp(q, "low") ? 0 : !strcmp(q, "medium") ? 1 : !strcmp(q, "high") ? 2 : quality_hint;
    run->speed = (float)atof(xst_run_get(run, "speed"));
    run->evolution = (float)atof(xst_run_get(run, "evolution"));
    /* palette */
    const char *pn = xst_run_get(run, "palette");
    if (!strcmp(pn, "native")) run->palette_index = spec->has_native ? -1 : family_pick(spec, xst_stream(run->seed, "palette"), -1);
    else if (!strcmp(pn, "auto")) {
        /* randomize off: native look when the saver has one */
        if (!randomize && spec->has_native) run->palette_index = -1;
        else run->palette_index = family_pick(spec, xst_stream(run->seed, "palette"), -1);
    } else run->palette_index = palette_by_name(pn);
    run->palette_cycle_s = atof(xst_run_get(run, "palette-cycle")) * 60.0;
    palette_lin(run, run->palette_index, run->pal_lin);
    /* geometry stream */
    for (int i = 0; i < 4; i++) {
        char key[16]; snprintf(key, sizeof key, "geometry%d", i);
        run->iseed[i] = (float)u01(xst_stream(run->seed, key));
    }
    /* bound uniforms */
    for (size_t i = 0; i < spec->n; i++) {
        if (!spec->bind_name[i][0]) continue;
        int b;
        for (b = 0; b < run->nbind; b++) if (!strcmp(run->bind_uniform[b], spec->bind_name[i])) break;
        if (b == run->nbind) {
            if (run->nbind >= XST_MAX_BINDS) continue;
            snprintf(run->bind_uniform[b], sizeof run->bind_uniform[b], "%s", spec->bind_name[i]);
            run->nbind++;
        }
        const ncz_opt_def *d = &spec->defs[i];
        float v;
        if (d->type == NCZ_OPT_BOOL) v = !strcmp(run->val[i], "true") ? 1.f : 0.f;
        else if (d->type == NCZ_OPT_ENUM) {
            int k = 0; const char *c = d->choices; size_t l = strlen(run->val[i]);
            v = 0.f;
            while (c && *c) {
                const char *e = strchr(c, ',');
                size_t cl = e ? (size_t)(e - c) : strlen(c);
                if (cl == l && !strncasecmp(c, run->val[i], l)) { v = (float)k; break; }
                k++; c = e ? e + 1 : NULL;
            }
        } else v = (float)atof(run->val[i]);
        run->bind_val[b][spec->bind_comp[i]] = v;
    }
    for (int k = 0; k < XST_DIR_PHASES; k++) run->dir_cache_seg[k] = -1;
}

void xst_run_describe(const XstRun *run, char *out, size_t sz, int w, int h, uint32_t src_hash) {
    const XstSpec *s = run->spec;
    const char *pal = run->palette_index >= 0 ? XST_PALETTES[run->palette_index].name : "native";
    int n = snprintf(out, sz, "[xshadertoy] saver=%s version=%d src=%08x seed=%u%s preset=%s palette=%s quality=%d res=%dx%d options:",
                     s->id, s->version, src_hash, run->seed, run->seed_was_random ? "(random)" : "",
                     *xst_run_get(run, "preset") ? xst_run_get(run, "preset") : "-", pal, run->quality, w, h);
    for (size_t i = 0; i < s->n && n > 0 && (size_t)n < sz; i++) {
        if (!strcmp(s->defs[i].name, "seed") || !strcmp(s->defs[i].name, "preset")) continue;
        n += snprintf(out + n, sz - (size_t)n, " %s=%s%s", s->defs[i].name, run->val[i], run->randomized[i] ? "*" : "");
    }
}

/* ---------------------------------------------------------------- defines */

static void upper_ident(const char *in, char *out, size_t sz) {
    size_t k = 0;
    for (; *in && k + 1 < sz; in++) out[k++] = *in == '-' ? '_' : (char)toupper((unsigned char)*in);
    out[k] = 0;
}

/* printf into a growing buffer; 0 on success */
static int sb_printf(char **b, size_t *cap, size_t *len, const char *fmt, ...) {
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(*b + *len, *cap - *len, fmt, ap);
        va_end(ap);
        if (n < 0) return -1;
        if ((size_t)n < *cap - *len) { *len += (size_t)n; return 0; }
        size_t nc = *cap * 2 + (size_t)n;
        char *nb = realloc(*b, nc);
        if (!nb) return -1;
        *b = nb; *cap = nc;
    }
}

char *xst_defines(const XstRun *run) {
    const XstSpec *s = run->spec;
    size_t cap = 8192, len = 0;
    char *b = malloc(cap);
    if (!b) return NULL;
#define APPEND(...) do { if (sb_printf(&b, &cap, &len, __VA_ARGS__) != 0) { free(b); return NULL; } } while (0)
    APPEND("#define NCZ_QUALITY %d\n", run->quality);
    if (!s->has_saver) return b;
    APPEND("#define XST 1\n#define XST_VERSION %d\n", s->version);
    APPEND("precision highp float;\nprecision highp int;\nuniform vec3 uPal[4];\nuniform vec4 uDir[4];\n");
    for (size_t i = 0; i < s->n; i++) {
        if (!s->shader_opt[i] || s->bind_name[i][0]) continue;
        const ncz_opt_def *d = &s->defs[i];
        char id[64];
        upper_ident(d->name, id, sizeof id);
        if (d->type == NCZ_OPT_FLOAT) {
            char v[64];
            snprintf(v, sizeof v, "%.9g", atof(run->val[i]));
            if (!strpbrk(v, ".eEn")) strcat(v, ".0");
            APPEND("#define OPT_%s %s\n", id, v);
        } else if (d->type == NCZ_OPT_INT) APPEND("#define OPT_%s %s\n", id, run->val[i]);
        else if (d->type == NCZ_OPT_BOOL) APPEND("#define OPT_%s %d\n", id, !strcmp(run->val[i], "true"));
        else if (d->type == NCZ_OPT_ENUM) {
            int k = 0, sel = 0;
            const char *c = d->choices;
            while (c && *c) {
                const char *e = strchr(c, ',');
                size_t cl = e ? (size_t)(e - c) : strlen(c);
                char ch[64], cu[64];
                if (cl >= sizeof ch) cl = sizeof ch - 1;
                memcpy(ch, c, cl); ch[cl] = 0;
                upper_ident(ch, cu, sizeof cu);
                APPEND("#define OPT_%s_%s %d\n", id, cu, k);
                if (!strcasecmp(ch, run->val[i])) sel = k;
                k++; c = e ? e + 1 : NULL;
            }
            APPEND("#define OPT_%s %d\n", id, sel);
        }
    }
    return b;
#undef APPEND
}

/* --------------------------------------------------------------- includes */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

char *xst_splice_includes(const char *body, const char *const *dirs) {
    size_t cap = strlen(body) + 1, len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;
    const char *p = body;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p + 1) : strlen(p);
        const char *t = p;
        while (t < p + l && (*t == ' ' || *t == '\t')) t++;
        const char *ins = NULL; char *loaded = NULL;
        if (!strncmp(t, "#include \"", 10)) {
            const char *q = t + 10, *qe = memchr(q, '"', (size_t)(p + l - q));
            if (qe) {
                char name[128];
                size_t nl = (size_t)(qe - q);
                if (nl < sizeof name) {
                    memcpy(name, q, nl); name[nl] = 0;
                    /* a plain file name only: no directories, no "..", no hidden files */
                    if (!nl || name[0] == '.' || strchr(name, '/') || strchr(name, '\\')) {
                        fprintf(stderr, "[xshadertoy] include \"%s\" rejected (plain file name only)\n", name);
                        free(out);
                        return NULL;
                    }
                    for (int i = 0; dirs && dirs[i] && !loaded; i++) {
                        char path[1024];
                        snprintf(path, sizeof path, "%s/%s", dirs[i], name);
                        loaded = read_file(path);
                    }
                    if (!loaded) {
                        fprintf(stderr, "[xshadertoy] include \"%s\" not found\n", name);
                        free(out);
                        return NULL;
                    }
                    ins = loaded;
                }
            }
        }
        const char *src = ins ? ins : p;
        size_t sl = ins ? strlen(ins) : l;
        if (len + sl + 2 > cap) {
            cap = (len + sl + 2) * 2;
            char *nb = realloc(out, cap);
            if (!nb) { free(out); free(loaded); return NULL; }
            out = nb;
        }
        memcpy(out + len, src, sl); len += sl;
        if (ins && (sl == 0 || ins[sl - 1] != '\n')) out[len++] = '\n';
        free(loaded);
        p += l;
    }
    out[len] = 0;
    return out;
}

/* --------------------------------------------------------------- director */

static double quintic(double u) { return u * u * u * (u * (u * 6.0 - 15.0) + 10.0); }
double xst_quintic_integral(double u) { /* u^6 - 3u^5 + 2.5u^4 */
    double u2 = u * u, u4 = u2 * u2;
    return u4 * (u2 - 3.0 * u + 2.5);
}

/* keyframe k of channel c, in [0,1] */
static double key(uint32_t seed, int c, long k) {
    char name[48];
    snprintf(name, sizeof name, "dir:%d:%ld", c, k);
    return u01(xst_stream(seed, name));
}

/* Each channel has its own period (0.8..1.25 of the base) and offset so the
 * channels never change together. */
static void chan_timing(const XstRun *run, int c, int phase_chan, double *period, double *offset) {
    char n1[24], n2[24];
    snprintf(n1, sizeof n1, "dirT:%d:%d", phase_chan, c);
    snprintf(n2, sizeof n2, "dirO:%d:%d", phase_chan, c);
    *period = run->spec->director_period * (0.8 + 0.45 * u01(xst_stream(run->seed, n1)));
    *offset = u01(xst_stream(run->seed, n2));
}

void xst_director(XstRun *run, double t, float out[16]) {
    uint32_t sd = xst_stream(run->seed, "director");
    double ev = run->evolution;
    for (int c = 0; c < XST_DIR_PARAMS; c++) {
        double T, off;
        chan_timing(run, c, 0, &T, &off);
        double x = t * ev / T + off;
        long k = (long)floor(x);
        double u = x - (double)k;
        double a = key(sd, c, k), b = key(sd, c, k + 1);
        out[c] = (float)(a + (b - a) * quintic(u));
    }
    /* phases: rate r(s) in [0.5, 1.5] keyed like the parameters (director
     * time s = t * ev / T); phase = integral of r over motion time t. With
     * evolution 0 the rate stays at its first key and phase = r0 * t. */
    for (int c = 0; c < XST_DIR_PHASES; c++) {
        double T, off;
        chan_timing(run, c, 1, &T, &off);
        int cc = 100 + c;
        if (ev <= 1e-9) {
            /* frozen at director time `off`, like the parameters above */
            long k0 = (long)floor(off);
            double a = key(sd, cc, k0), b = key(sd, cc, k0 + 1);
            out[8 + c] = (float)((0.5 + a + (b - a) * quintic(off - (double)k0)) * t);
            continue;
        }
        double Te = T / ev;                 /* seconds of motion time per segment */
        double x = t / Te + off;
        long k = (long)floor(x);
        long k0 = (long)floor(off);
        double u = x - (double)k;
        /* phase measured from t = 0: whole segments k0..k-1, minus the part of
         * segment k0 before t=0, plus the part of segment k up to u. */
        double sum;
        if (run->dir_cache_seg[c] == k) sum = run->dir_cache_sum[c];
        else {
            long from = k0; double acc = 0.0;
            if (run->dir_cache_seg[c] >= k0 && run->dir_cache_seg[c] < k) { from = run->dir_cache_seg[c]; acc = run->dir_cache_sum[c]; }
            for (long j = from; j < k; j++) acc += 0.5 * ((0.5 + key(sd, cc, j)) + (0.5 + key(sd, cc, j + 1))) * Te;
            if (k >= k0) { run->dir_cache_seg[c] = k; run->dir_cache_sum[c] = acc; }
            sum = acc;
        }
        double a0 = 0.5 + key(sd, cc, k0), b0 = 0.5 + key(sd, cc, k0 + 1);
        double u0 = off - (double)k0;
        double before = (a0 * u0 + (b0 - a0) * xst_quintic_integral(u0)) * Te;
        double a = 0.5 + key(sd, cc, k), b = 0.5 + key(sd, cc, k + 1);
        double part = (a * u + (b - a) * xst_quintic_integral(u)) * Te;
        out[8 + c] = (float)(sum - before + part);
    }
}

/* ---------------------------------------------------------------- palette */

static void lin_to_oklab(const float c[3], double o[3]) {
    double l = 0.4122214708 * c[0] + 0.5363325363 * c[1] + 0.0514459929 * c[2];
    double m = 0.2119034982 * c[0] + 0.6806995451 * c[1] + 0.1073969566 * c[2];
    double s = 0.0883024619 * c[0] + 0.2817188376 * c[1] + 0.6299787005 * c[2];
    l = cbrt(l); m = cbrt(m); s = cbrt(s);
    o[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    o[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    o[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}
static void oklab_to_lin(const double o[3], float c[3]) {
    double l = o[0] + 0.3963377774 * o[1] + 0.2158037573 * o[2];
    double m = o[0] - 0.1055613458 * o[1] - 0.0638541728 * o[2];
    double s = o[0] - 0.0894841775 * o[1] - 1.2914855480 * o[2];
    l = l * l * l; m = m * m * m; s = s * s * s;
    double r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    c[0] = (float)(r < 0 ? 0 : r); c[1] = (float)(g < 0 ? 0 : g); c[2] = (float)(b < 0 ? 0 : b);
}

void xst_palette_at(const XstRun *run, double t, float out[12]) {
    if (run->palette_cycle_s <= 0.0 || run->palette_index < 0) {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) out[r * 3 + c] = run->pal_lin[r][c];
        return;
    }
    /* palette k holds for 80% of a cycle and cross-fades for 20% */
    double x = t / run->palette_cycle_s;
    long k = (long)floor(x);
    double u = x - (double)k;
    int cur = run->palette_index, nxt;
    for (long j = 1; j <= k; j++) {
        char key[32]; snprintf(key, sizeof key, "palette:%ld", j);
        cur = family_pick(run->spec, xst_stream(run->seed, key), cur);
    }
    { char key[32]; snprintf(key, sizeof key, "palette:%ld", k + 1);
      nxt = family_pick(run->spec, xst_stream(run->seed, key), cur); }
    float A[4][3], B[4][3];
    palette_lin(run, cur, A);
    palette_lin(run, nxt, B);
    double f = u < 0.8 ? 0.0 : quintic((u - 0.8) / 0.2);
    for (int r = 0; r < 4; r++) {
        double oa[3], ob[3], om[3];
        lin_to_oklab(A[r], oa); lin_to_oklab(B[r], ob);
        for (int i = 0; i < 3; i++) om[i] = oa[i] + (ob[i] - oa[i]) * f;
        oklab_to_lin(om, &out[r * 3]);
    }
}

/* ------------------------------------------------------ source assembly */

/* Shadertoy uniform set (adapted from upstream xscreensaver 6.16
 * xshadertoy.c fragment_shader_head, MIT-style license, see
 * src/gles3_xshadertoy.c). #version and the defines are emitted first. */
static const char *XST_PREAMBLE =
    "precision highp float;\n"
    "precision highp int;\n"
    "out vec4 frag_color;\n"
    "uniform vec3  iResolution;\n"
    "uniform float iTime;\n"
    "uniform float iTimeDelta;\n"
    "uniform float iFrameRate;\n"
    "uniform int   iFrame;\n"
    "uniform vec4  iDate;\n"
    "uniform vec4  iMouse;\n"
    "uniform vec4  iSeed;\n"
    "uniform vec3  iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n";

static const char *XST_TAIL =
    "\nvoid main() {\n"
    "  vec4 col = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  mainImage(col, gl_FragCoord.xy);\n"
    /* Shadertoy semantics: mainImage's output is opaque.  Several vendored
     * ports leave .a at 0 (they initialize o = vec4(0,0,0,0), write only
     * .rgb, or multiply the output by a vec4 whose .a is 0).  On a
     * layer-shell surface alpha 0 composites to nothing, so the saver
     * looked blank even though its RGB was correct.  Force 1.0 here, in
     * the one shared tail, rather than editing each shader. */
    "  frag_color = vec4(col.rgb, 1.0);\n"
    "}\n";

/* Skip the leading block of #version/#extension/#pragma/precision lines,
 * comments and blank lines: the preamble already issues them. */
static const char *skip_leading_directives(const char *src) {
    const char *p = src;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (*p == '#') {
            static const char *k[] = {"version", "extension", "pragma", NULL};
            int m = 0;
            for (int i = 0; k[i]; i++) {
                size_t L = strlen(k[i]);
                if (!strncmp(p + 1, k[i], L) && (p[L + 1] == ' ' || p[L + 1] == '\t' || p[L + 1] == '\n')) m = 1;
            }
            if (!m) return p;
            while (*p && *p != '\n') p++;
            continue;
        }
        if (!strncmp(p, "precision ", 10)) { while (*p && *p != '\n') p++; continue; }
        return p;
    }
    return p;
}

char *xst_fragment_source(const XstRun *run, const char *body) {
    char *defs = xst_defines(run);
    if (!defs) return NULL;
    const char *b = skip_leading_directives(body);
    size_t n = 32 + strlen(defs) + strlen(XST_PREAMBLE) + strlen(b) + strlen(XST_TAIL);
    char *out = malloc(n);
    if (out) snprintf(out, n, "#version 300 es\n%s%s\n%s%s", defs, XST_PREAMBLE, b, XST_TAIL);
    free(defs);
    return out;
}

const char *const *xst_search_dirs(void) {
    static const char *dirs[] = {
        "vendor/jurassicaquarium/glsl",
        "../vendor/jurassicaquarium/glsl",
        "../../vendor/jurassicaquarium/glsl",
        "vendor/wave2-materia/glsl",
        "../vendor/wave2-materia/glsl",
        "../../vendor/wave2-materia/glsl",
        "vendor/xshadertoy/glsl",
        "../vendor/xshadertoy/glsl",
        "../../vendor/xshadertoy/glsl",
        "/usr/share/ncz-screensavers/shaders",
        NULL,
    };
    return dirs;
}

char *xst_load_shader(const char *name, char *used, size_t cap) {
    const char *ov = getenv("NCZ_SHADER_DIR");
    char path[1024];
    if (ov && *ov) {
        snprintf(path, sizeof path, "%s/%s", ov, name);
        char *b = read_file(path);
        if (b) { if (used) snprintf(used, cap, "%s", path); return b; }
        fprintf(stderr, "xshadertoy: NCZ_SHADER_DIR=%s set but %s not found\n", ov, path);
    }
    const char *const *d = xst_search_dirs();
    for (int i = 0; d[i]; i++) {
        snprintf(path, sizeof path, "%s/%s", d[i], name);
        char *b = read_file(path);
        if (b) { if (used) snprintf(used, cap, "%s", path); return b; }
    }
    /* a path given directly (the headless tool) */
    char *b = read_file(name);
    if (b && used) snprintf(used, cap, "%s", name);
    if (!b) fprintf(stderr, "xshadertoy: cannot locate %s in any known location\n", name);
    return b;
}

char *xst_splice_near(const char *body, const char *used_path) {
    char dir[1024] = ".";
    const char *sl = used_path ? strrchr(used_path, '/') : NULL;
    if (sl) { size_t l = (size_t)(sl - used_path); if (l < sizeof dir) { memcpy(dir, used_path, l); dir[l] = 0; } }
    const char *ov = getenv("NCZ_SHADER_DIR");
    const char *const *d = xst_search_dirs();
    const char *all[16];
    int n = 0;
    all[n++] = dir;
    if (ov && *ov) all[n++] = ov;
    for (int i = 0; d[i] && n < 15; i++) all[n++] = d[i];
    all[n] = NULL;
    return xst_splice_includes(body, all);
}
