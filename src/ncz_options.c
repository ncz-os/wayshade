/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_options.c - see ncz_options.h */
#define _POSIX_C_SOURCE 200809L
#include "ncz_options.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void warn(ncz_opts *o, const char *tag, const char *fmt, const char *a, const char *b, const char *c) {
    o->warnings++;
    if (o->quiet) return;
    fprintf(stderr, "[opts] %s: ", tag ? tag : "option");
    fprintf(stderr, fmt, a ? a : "", b ? b : "", c ? c : "");
    fputc('\n', stderr);
}

static int choice_has(const char *choices, const char *tok, size_t len) {
    const char *p = choices;
    while (p && *p) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == len && strncasecmp(p, tok, len) == 0) return 1;
        p = e ? e + 1 : NULL;
    }
    return 0;
}

/* Copy canonical (lower-case) choice matching tok into out. */
static int choice_canon(const ncz_opt_def *d, const char *tok, size_t len, char *out, size_t outsz) {
    /* aliases first */
    const char *p = d->aliases;
    while (p && *p) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        const char *eq = memchr(p, '=', l);
        if (eq && (size_t)(eq - p) == len && strncasecmp(p, tok, len) == 0) {
            size_t cl = l - (size_t)(eq - p) - 1;
            if (cl + 1 > outsz) return -1;
            memcpy(out, eq + 1, cl);
            out[cl] = 0;
            return 0;
        }
        p = e ? e + 1 : NULL;
    }
    if (choice_has(d->choices, tok, len)) {
        if (len + 1 > outsz) return -1;
        for (size_t i = 0; i < len; i++) out[i] = (char)tolower((unsigned char)tok[i]);
        out[len] = 0;
        return 0;
    }
    return -1;
}

static void trim(char *s) {
    size_t n = strlen(s), i = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

int ncz_opt_validate(const ncz_opt_def *d, const char *in, char *out, size_t outsz,
                     char *why, size_t whysz) {
    char buf[NCZ_OPT_VALMAX];
    if (!in) { if (why) snprintf(why, whysz, "no value"); return -1; }
    if (strlen(in) >= sizeof buf) { if (why) snprintf(why, whysz, "value too long"); return -1; }
    strcpy(buf, in);
    trim(buf);
    switch (d->type) {
    case NCZ_OPT_BOOL:
        if (!strcasecmp(buf, "true") || !strcmp(buf, "1") || !strcasecmp(buf, "yes") || !strcasecmp(buf, "on"))
            { snprintf(out, outsz, "true"); return 0; }
        if (!strcasecmp(buf, "false") || !strcmp(buf, "0") || !strcasecmp(buf, "no") || !strcasecmp(buf, "off"))
            { snprintf(out, outsz, "false"); return 0; }
        if (why) snprintf(why, whysz, "expected true|false|1|0");
        return -1;
    case NCZ_OPT_INT:
    case NCZ_OPT_FLOAT: {
        if (!*buf) { if (why) snprintf(why, whysz, "empty number"); return -1; }
        char *end = NULL;
        errno = 0;
        double v = strtod(buf, &end);
        if (errno || end == buf || *end || !isfinite(v)) { if (why) snprintf(why, whysz, "not a number"); return -1; }
        if (d->type == NCZ_OPT_INT && v != floor(v)) { if (why) snprintf(why, whysz, "expected an integer"); return -1; }
        if (v < d->min || v > d->max) {
            if (why) snprintf(why, whysz, "out of range %g..%g", d->min, d->max);
            return -1;
        }
        if (d->type == NCZ_OPT_INT) snprintf(out, outsz, "%.0f", v);
        else snprintf(out, outsz, "%.9g", v);
        return 0;
    }
    case NCZ_OPT_ENUM:
        if (choice_canon(d, buf, strlen(buf), out, outsz) == 0) return 0;
        if (why) snprintf(why, whysz, "expected one of %s", d->choices);
        return -1;
    case NCZ_OPT_LIST: {
        out[0] = 0;
        size_t used = 0;
        const char *p = buf;
        while (*p) {
            const char *e = strchr(p, ',');
            size_t l = e ? (size_t)(e - p) : strlen(p);
            char tok[64];
            if (l >= sizeof tok) { if (why) snprintf(why, whysz, "list element too long"); return -1; }
            memcpy(tok, p, l); tok[l] = 0;
            trim(tok);
            if (*tok) {
                char c[64];
                if (choice_canon(d, tok, strlen(tok), c, sizeof c) != 0) {
                    if (why) snprintf(why, whysz, "'%s' is not one of %s", tok, d->choices);
                    return -1;
                }
                size_t cl = strlen(c);
                if (used + cl + 2 > outsz) { if (why) snprintf(why, whysz, "list too long"); return -1; }
                if (used) out[used++] = ',';
                memcpy(out + used, c, cl); used += cl; out[used] = 0;
            }
            p = e ? e + 1 : p + l;
        }
        return 0;
    }
    case NCZ_OPT_STRING:
    default:
        if (strlen(buf) >= outsz) { if (why) snprintf(why, whysz, "too long"); return -1; }
        strcpy(out, buf);
        return 0;
    }
}

int ncz_opts_init(ncz_opts *o, const ncz_opt_def *defs, size_t n) {
    memset(o, 0, sizeof *o);
    o->defs = defs;
    o->n = n;
    o->val = calloc(n ? n : 1, sizeof *o->val);
    o->set = calloc(n ? n : 1, 1);
    o->src = calloc(n ? n : 1, 1);
    if (!o->val || !o->set || !o->src) return -1;
    for (size_t i = 0; i < n; i++) {
        char why[96];
        if (ncz_opt_validate(&defs[i], defs[i].def, o->val[i], NCZ_OPT_VALMAX, why, sizeof why) != 0)
            snprintf(o->val[i], NCZ_OPT_VALMAX, "%s", defs[i].def); /* table bug: keep raw */
    }
    return 0;
}

void ncz_opts_free(ncz_opts *o) {
    free(o->val); free(o->set); free(o->src);
    o->val = NULL; o->set = NULL; o->src = NULL; o->n = 0;
}


static int find(const ncz_opts *o, const char *name, size_t len) {
    for (size_t i = 0; i < o->n; i++) {
        const char *dn = o->defs[i].name;
        size_t k = 0;
        /* match with '-' and '_' interchangeable, case-insensitive */
        while (k < len && dn[k]) {
            char a = (char)tolower((unsigned char)dn[k]), b = (char)tolower((unsigned char)name[k]);
            if (a == '_') a = '-';
            if (b == '_') b = '-';
            if (a != b) break;
            k++;
        }
        if (k == len && !dn[k]) return (int)i;
    }
    return -1;
}

static void set_one(ncz_opts *o, int idx, const char *value, const char *tag, int src) {
    char out[NCZ_OPT_VALMAX], why[128];
    if (ncz_opt_validate(&o->defs[idx], value, out, sizeof out, why, sizeof why) != 0) {
        char m[420];
        snprintf(m, sizeof m, "invalid value '%.60s' for '%.40s' (%.120s); ignored", value ? value : "", o->defs[idx].name, why);
        warn(o, tag, "%s%s%s", m, NULL, NULL);
        return;
    }
    snprintf(o->val[idx], NCZ_OPT_VALMAX, "%s", out);
    o->set[idx] = 1;
    o->src[idx] = (unsigned char)src;
}

void ncz_opts_apply_keyfile(ncz_opts *o, const char *path, const char *group, const char *tag) {
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return;
    int src = (tag && strstr(tag, "preset")) ? NCZ_SRC_PRESET : NCZ_SRC_CONFIG;
    char line[512], cur[64] = "";
    int in_group = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        if (!strchr(line, '\n') && !feof(f)) { /* over-long line: skip the rest */
            int ch; while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            char m[64]; snprintf(m, sizeof m, "line %d too long", lineno);
            warn(o, tag, "%s%s%s", m, NULL, NULL);
            continue;
        }
        trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (*line == '[') {
            char *e = strchr(line, ']');
            if (!e) continue;
            *e = 0;
            snprintf(cur, sizeof cur, "%s", line + 1);
            in_group = (strcmp(cur, group) == 0);
            continue;
        }
        if (!in_group) continue;
        char *eq = strchr(line, '=');
        if (!eq) { char m[64]; snprintf(m, sizeof m, "line %d: no '='", lineno); warn(o, tag, "%s%s%s", m, NULL, NULL); continue; }
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key); trim(val);
        int idx = find(o, key, strlen(key));
        if (idx < 0) { char m[128]; snprintf(m, sizeof m, "unknown option '%.60s'; ignored", key); warn(o, tag, "%s%s%s", m, NULL, NULL); continue; }
        set_one(o, idx, val, tag, src);
    }
    fclose(f);
}

static const char *sys_getenv(const char *n) { return getenv(n); }

static void envname(const char *prefix, const char *opt, char *out, size_t sz) {
    size_t k = 0;
    for (const char *p = prefix; *p && k + 1 < sz; p++) out[k++] = *p;
    for (const char *p = opt; *p && k + 1 < sz; p++) out[k++] = *p == '-' ? '_' : (char)toupper((unsigned char)*p);
    out[k] = 0;
}

void ncz_opts_apply_env(ncz_opts *o, const char *prefix, const char *(*getenv_fn)(const char *)) {
    if (!getenv_fn) getenv_fn = sys_getenv;
    for (size_t i = 0; i < o->n; i++) {
        char nm[128];
        envname(prefix, o->defs[i].name, nm, sizeof nm);
        const char *v = getenv_fn(nm);
        if (v && *v) set_one(o, (int)i, v, nm, NCZ_SRC_ENV);
        /* legacy aliases (only used if the canonical name is unset) */
        else if (o->defs[i].env_alias) {
            const char *p = o->defs[i].env_alias;
            while (p && *p) {
                const char *e = strchr(p, ',');
                char a[96];
                size_t l = e ? (size_t)(e - p) : strlen(p);
                if (l >= sizeof a) break;
                memcpy(a, p, l); a[l] = 0;
                const char *av = getenv_fn(a);
                if (av && *av) { set_one(o, (int)i, av, a, NCZ_SRC_ENV); break; }
                p = e ? e + 1 : NULL;
            }
        }
    }
}

int ncz_opts_apply_argv(ncz_opts *o, int argc, char **argv, const char *prog) {
    int flags = 0;
    (void)prog;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!a) continue;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { flags |= 1; continue; }
        if (!strcmp(a, "--list-options")) { flags |= 2; continue; }
        if (!strcmp(a, "--print-config")) { flags |= 16; continue; }
        if (strncmp(a, "--", 2) != 0) { warn(o, "command line", "unexpected argument '%.60s'%s%s", a, "", ""); flags |= 8; continue; }
        const char *name = a + 2, *eq = strchr(name, '=');
        size_t nl = eq ? (size_t)(eq - name) : strlen(name);
        int idx = find(o, name, nl);
        int neg = 0;
        if (idx < 0 && !eq && strncmp(name, "no-", 3) == 0) {
            idx = find(o, name + 3, nl - 3);
            if (idx >= 0 && o->defs[idx].type == NCZ_OPT_BOOL) neg = 1; else idx = -1;
        }
        if (idx < 0) { warn(o, "command line", "unknown option '%.60s'%s%s", a, "", ""); flags |= 4; continue; }
        if (eq) set_one(o, idx, eq + 1, "command line", NCZ_SRC_ARGV);
        else if (o->defs[idx].type == NCZ_OPT_BOOL) set_one(o, idx, neg ? "false" : "true", "command line", NCZ_SRC_ARGV);
        else { char m[128]; snprintf(m, sizeof m, "option '%.40s' needs a value (--%.40s=VALUE); ignored", o->defs[idx].name, o->defs[idx].name); warn(o, "command line", "%s%s%s", m, NULL, NULL); }
    }
    return flags;
}

int ncz_opts_resolve_preset(ncz_opts *o, const char *prefix, const char *group,
                            const char *conf_path, const char *preset_path,
                            const char *preset_tag, int argc, char **argv,
                            const char *prog, const char *(*getenv_fn)(const char *)) {
    if (!getenv_fn) getenv_fn = sys_getenv;
    char path[512] = "";
    if (conf_path) snprintf(path, sizeof path, "%s", conf_path);
    else {
        char en[96];
        envname("NCZ_", group, en, sizeof en);
        strncat(en, "_CONFIG", sizeof en - strlen(en) - 1);
        const char *ov = getenv_fn(en);
        if (ov && *ov) snprintf(path, sizeof path, "%s", ov);
        else {
            const char *x = getenv_fn("XDG_CONFIG_HOME"), *h = getenv_fn("HOME");
            if (x && *x) snprintf(path, sizeof path, "%s/ncz-screensavers/%s.conf", x, group);
            else if (h && *h) snprintf(path, sizeof path, "%s/.config/ncz-screensavers/%s.conf", h, group);
        }
    }
    if (*path) ncz_opts_apply_keyfile(o, path, group, "config file");
    if (preset_path) ncz_opts_apply_keyfile(o, preset_path, group, preset_tag ? preset_tag : "preset");
    ncz_opts_apply_env(o, prefix, getenv_fn);
    return ncz_opts_apply_argv(o, argc, argv, prog);
}

int ncz_opts_resolve(ncz_opts *o, const char *prefix, const char *group,
                     const char *conf_path, int argc, char **argv, const char *prog,
                     const char *(*getenv_fn)(const char *)) {
    return ncz_opts_resolve_preset(o, prefix, group, conf_path, NULL, NULL, argc, argv, prog, getenv_fn);
}

int ncz_opts_is_set(const ncz_opts *o, const char *name) {
    int i = find(o, name, strlen(name));
    return i >= 0 && o->set[i];
}

int ncz_opts_source(const ncz_opts *o, const char *name) {
    int i = find(o, name, strlen(name));
    return (i >= 0 && o->src) ? (int)o->src[i] : NCZ_SRC_DEFAULT;
}

const char *ncz_opts_get(const ncz_opts *o, const char *name) {
    int i = find(o, name, strlen(name));
    return i < 0 ? "" : o->val[i];
}
double ncz_opts_get_float(const ncz_opts *o, const char *name) { return atof(ncz_opts_get(o, name)); }
long   ncz_opts_get_int(const ncz_opts *o, const char *name) { return strtol(ncz_opts_get(o, name), NULL, 10); }
int    ncz_opts_get_bool(const ncz_opts *o, const char *name) { return strcmp(ncz_opts_get(o, name), "true") == 0; }

static const char *type_name(ncz_opt_type t) {
    switch (t) { case NCZ_OPT_BOOL: return "bool"; case NCZ_OPT_INT: return "int"; case NCZ_OPT_FLOAT: return "float";
    case NCZ_OPT_ENUM: return "enum"; case NCZ_OPT_LIST: return "list"; default: return "string"; }
}

void ncz_opts_print_list(const ncz_opts *o, FILE *f, const char *prefix) {
    for (size_t i = 0; i < o->n; i++) {
        const ncz_opt_def *d = &o->defs[i];
        char nm[128], range[160] = "";
        envname(prefix, d->name, nm, sizeof nm);
        if (d->type == NCZ_OPT_INT || d->type == NCZ_OPT_FLOAT) snprintf(range, sizeof range, "%g..%g", d->min, d->max);
        else if (d->type == NCZ_OPT_ENUM || d->type == NCZ_OPT_LIST) snprintf(range, sizeof range, "%s", d->choices);
        fprintf(f, "--%-18s %-6s default=%-12s range=%-40s env=%s\n", d->name, type_name(d->type),
                d->def[0] ? d->def : "\"\"", range[0] ? range : "-", nm);
    }
}

void ncz_opts_print_help(const ncz_opts *o, FILE *f, const char *prefix, const char *prog) {
    fprintf(f, "Usage: %s [--option=value ...]\n\n", prog);
    fprintf(f, "Precedence: command line > environment (%s<OPTION>) > config file > defaults.\n", prefix);
    fprintf(f, "Invalid values are reported once and ignored.\n\n");
    const char *group = "";
    for (size_t i = 0; i < o->n; i++) {
        const ncz_opt_def *d = &o->defs[i];
        if (strcmp(group, d->group) != 0) { group = d->group; fprintf(f, "%s:\n", group); }
        fprintf(f, "  --%s=%s  (%s)\n      %s\n", d->name,
                d->type == NCZ_OPT_ENUM || d->type == NCZ_OPT_LIST ? d->choices : type_name(d->type),
                d->def[0] ? d->def : "empty", d->desc);
    }
    fprintf(f, "\n--list-options prints every option with type, range and default.\n");
}
