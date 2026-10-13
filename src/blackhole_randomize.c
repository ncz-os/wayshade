/* SPDX-License-Identifier: Apache-2.0 */
/* blackhole_randomize.c - see blackhole_randomize.h.
 *
 * The named-stream hashing is the same algorithm xst_core.c uses for
 * xshadertoy's iSeed (xst_hash_u32 / xst_fnv1a / xst_stream): each draw is
 * keyed by name, so adding a new rolled option never shifts the others and a
 * given seed reproduces exactly. The engine's existing `seed` option carries
 * the roll seed, so NCZ_BLACKHOLE_SEED / --seed reproduces the render too.
 */
#define _POSIX_C_SOURCE 200809L
#include "blackhole_randomize.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- hashing */

static uint32_t bh_hash_u32(uint32_t x) { /* lowbias32, as xst_core.c */
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static uint32_t bh_fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}
static uint32_t bh_stream(uint32_t seed, const char *key) {
    return bh_hash_u32(bh_hash_u32(seed * 0x9E3779B9u + 0x632BE5ABu) ^ bh_fnv1a(key));
}
static double bh_u01(uint32_t h) { return (double)(h >> 8) / 16777216.0; }
static double bh_rnd(uint32_t seed, const char *key, double lo, double hi) {
    return lo + (hi - lo) * bh_u01(bh_stream(seed, key));
}

int bh_class_rank(const char *name) {
    if (name && !strcmp(name, "strong")) return 2;
    if (name && !strcmp(name, "mid")) return 1;
    return 0; /* weak, empty or unknown */
}

int bh_randomize_pinned(const char *csv, const char *name) {
    if (!csv || !*csv || !name) return 0;
    size_t len = strlen(name);
    for (const char *p = csv; p && *p; ) {
        const char *c = strchr(p, ',');
        size_t l = c ? (size_t)(c - p) : strlen(p);
        if (l == len && !strncmp(p, name, len)) return 1;
        p = c ? c + 1 : NULL;
    }
    return 0;
}

static const char *const BH_PALETTES[10] = {
    "stylized", "kipthorne", "faithful", "singularity", "slingshot",
    "whitehole", "eht", "psychedelic", "synthwave", "cygnus",
};

/* ---- the safe envelope -------------------------------------------------
 * Every range is inside the option's schema [min,max] AND inside the safety
 * envelope from docs/BLACKHOLE-RANDOMIZE.md and
 * docs/audit/blackhole-black-floor-evidence/ROOTCAUSE.md. Weak keeps bloom
 * off and star/nebula density low; the render scale stays the launcher's
 * weak default (0.5, not rolled here). Returns 0 when the option is not
 * part of the roll. */
static int look_range(const char *name, int cls, double *lo, double *hi) {
    /* cls: 0 weak, 1 mid, 2 strong. Values below the schema minimum are
     * impossible; the schema maxima are only reached on strong where the
     * audit found no floor. */
    if (!strcmp(name, "exposure"))          { *lo = 0.60; *hi = cls == 0 ? 1.40 : cls == 1 ? 1.60 : 1.80; return 1; }
    if (!strcmp(name, "disk-brightness"))   { *lo = 0.70; *hi = cls == 0 ? 1.40 : cls == 1 ? 1.50 : 1.60; return 1; }
    if (!strcmp(name, "bloom"))             { *lo = 0.00; *hi = cls == 0 ? 0.00 : cls == 1 ? 0.90 : 1.80; return 1; }
    if (!strcmp(name, "fringe"))            { *lo = 0.00; *hi = 0.35; return 1; }
    if (!strcmp(name, "beaming"))           { *lo = 0.50; *hi = 1.60; return 1; }
    if (!strcmp(name, "star-density"))      { *lo = cls == 0 ? 0.40 : 0.50; *hi = cls == 0 ? 1.00 : cls == 1 ? 1.40 : 1.80; return 1; }
    if (!strcmp(name, "nebula"))            { *lo = cls == 0 ? 0.00 : 0.20; *hi = cls == 0 ? 0.60 : cls == 1 ? 1.20 : 1.50; return 1; }
    if (!strcmp(name, "jet"))               { *lo = 0.40; *hi = 1.50; return 1; }
    if (!strcmp(name, "hot-sector"))        { *lo = 0.05; *hi = 0.45; return 1; }
    if (!strcmp(name, "temperature"))       { *lo = 0.70; *hi = 1.30; return 1; }
    if (!strcmp(name, "rotation-speed"))    { *lo = 0.40; *hi = 1.60; return 1; }
    if (!strcmp(name, "precession"))        { *lo = 0.30; *hi = 1.80; return 1; }
    if (!strcmp(name, "disk-outer"))        { *lo = 8.00; *hi = 30.0; return 1; }
    if (!strcmp(name, "jet-length"))        { *lo = 10.0; *hi = 60.0; return 1; }
    if (!strcmp(name, "spin"))              { *lo = 0.00; *hi = 0.90; return 1; }
    if (!strcmp(name, "ripples"))           { *lo = 0.40; *hi = 1.50; return 1; }
    if (!strcmp(name, "speed"))             { *lo = 0.60; *hi = 1.60; return 1; }
    if (!strcmp(name, "palette-transition")){ *lo = 3.00; *hi = 15.0; return 1; }
    return 0;
}

int bh_randomize_roll(uint32_t seed, int mode, const char *gpu_class, int heavy_pool,
                      const BhRandomPreset *presets, size_t npresets,
                      const char *pinned_csv, BhRandomRoll *out) {
    if (!out || (mode != BH_RAND_LOOK && mode != BH_RAND_EVERYTHING)) return -1;
    memset(out, 0, sizeof *out);
    int cls = bh_class_rank(gpu_class);
    out->seed = seed;
    out->mode = mode;
    snprintf(out->mode_name, sizeof out->mode_name, "%s", mode == BH_RAND_EVERYTHING ? "everything" : "look");

    /* Read the rest of the roll. Each draw is its own named stream so the
     * order and the addition of options never change the others. An option
     * named in pinned_csv is left at 0 with its bit clear. */
#define ROLL(field, name, bit) do { \
        if (!bh_randomize_pinned(pinned_csv, name)) { \
            double lo, hi; \
            if (look_range(name, cls, &lo, &hi)) { out->field = bh_rnd(seed, name, lo, hi); out->rolled |= (bit); } \
        } \
    } while (0)
    ROLL(exposure, "exposure", BH_RF_EXPOSURE);
    ROLL(disk_brightness, "disk-brightness", BH_RF_DISK_BRIGHTNESS);
    ROLL(bloom, "bloom", BH_RF_BLOOM);
    ROLL(fringe, "fringe", BH_RF_FRINGE);
    ROLL(beaming, "beaming", BH_RF_BEAMING);
    ROLL(star_density, "star-density", BH_RF_STAR_DENSITY);
    ROLL(nebula, "nebula", BH_RF_NEBULA);
    ROLL(jet, "jet", BH_RF_JET);
    ROLL(hot_sector, "hot-sector", BH_RF_HOT_SECTOR);
    ROLL(temperature, "temperature", BH_RF_TEMPERATURE);
    ROLL(rotation_speed, "rotation-speed", BH_RF_ROTATION_SPEED);
    ROLL(precession, "precession", BH_RF_PRECESSION);
    ROLL(disk_outer, "disk-outer", BH_RF_DISK_OUTER);
    ROLL(jet_length, "jet-length", BH_RF_JET_LENGTH);
    ROLL(spin, "spin", BH_RF_SPIN);
    ROLL(ripples, "ripples", BH_RF_RIPPLES);
    ROLL(speed, "speed", BH_RF_SPEED);
    ROLL(palette_transition, "palette-transition", BH_RF_PALETTE_TRANSITION);
#undef ROLL

    /* Palette: any of the ten, unless pinned. */
    if (!bh_randomize_pinned(pinned_csv, "palette")) {
        snprintf(out->palette, sizeof out->palette, "%s", BH_PALETTES[bh_stream(seed, "palette") % 10u]);
        out->rolled |= BH_RF_PALETTE;
    }

    /* Style and camera path are deliberately NOT rolled: both move whole
     * bundles (enhanced features, per-scene camera) and are the preset's /
     * user's job. Documented in docs/BLACKHOLE-RANDOMIZE.md. */
    out->style[0] = 0;

    if (mode == BH_RAND_EVERYTHING && !bh_randomize_pinned(pinned_csv, "preset")) {
        /* The presets this class may run: min-gpu-class at or below the
         * current class, unless the heavy pool was opted into. */
        size_t allowed_n = 0;
        uint32_t idx[256];
        int limit = heavy_pool ? 2 : cls;
        for (size_t i = 0; i < npresets && allowed_n < 256; i++) {
            if (presets[i].id[0] && bh_class_rank(presets[i].min_class) <= limit)
                idx[allowed_n++] = (uint32_t)i;
        }
        if (allowed_n == 0) return -1;
        const BhRandomPreset *pick = &presets[idx[bh_stream(seed, "preset") % allowed_n]];
        snprintf(out->preset, sizeof out->preset, "%s", pick->id);
        out->rolled |= BH_RF_PRESET;
    }
    return 0;
}

/* ---- preset catalogue (filesystem) ------------------------------------ */

static void preset_min_class(const char *path, char *out, size_t sz) {
    snprintf(out, sz, "weak");
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[600];
    int in = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
        if (line[0] == '[') { in = (strcmp(line, "[preset]") == 0); continue; }
        if (!in) continue;
        if (!strncmp(line, "min-gpu-class=", 14)) { snprintf(out, sz, "%s", line + 14); break; }
    }
    fclose(f);
}

/* Same search order as ncz_harness_cfg.c's preset_dirs(). */
static int catalogue_dirs(const char *group, char dirs[4][512]) {
    int n = 0;
    const char *e = getenv("NCZ_PRESET_DIR"), *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (e && *e) snprintf(dirs[n++], 512, "%s", e);
    if (x && *x) snprintf(dirs[n++], 512, "%s/ncz-screensavers/presets/%s", x, group);
    else if (h && *h) snprintf(dirs[n++], 512, "%s/.local/share/ncz-screensavers/presets/%s", h, group);
    snprintf(dirs[n++], 512, "/usr/share/ncz-screensavers/presets/%s", group);
    snprintf(dirs[n++], 512, "assets/screensaver-chooser/options/presets/%s", group);
    return n;
}

static int cmp_preset(const void *a, const void *b) {
    return strcmp(((const BhRandomPreset *)a)->id, ((const BhRandomPreset *)b)->id);
}

size_t bh_randomize_catalogue(const char *group, BhRandomPreset *out, size_t cap) {
    if (!group || !out || cap == 0) return 0;
    char dirs[4][512];
    int n = catalogue_dirs(group, dirs);
    size_t used = 0;
    for (int i = 0; i < n && used < cap; i++) {
        DIR *d = opendir(dirs[i]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) && used < cap) {
            size_t l = strlen(e->d_name);
            if (l < 6 || strcmp(e->d_name + l - 5, ".conf") != 0) continue;
            if (l - 5 >= sizeof out[used].id) continue;
            char id[64];
            memcpy(id, e->d_name, l - 5); id[l - 5] = 0;
            int dup = 0;
            for (size_t k = 0; k < used; k++)
                if (!strcmp(out[k].id, id)) { dup = 1; break; }
            if (dup) continue;
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dirs[i], e->d_name);
            snprintf(out[used].id, sizeof out[used].id, "%s", id);
            preset_min_class(path, out[used].min_class, sizeof out[used].min_class);
            used++;
        }
        closedir(d);
    }
    if (used) qsort(out, used, sizeof *out, cmp_preset);
    return used;
}
