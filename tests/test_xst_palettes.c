/* SPDX-License-Identifier: Apache-2.0 */
/* test_xst_palettes.c - the contrast constraints of the role palettes
 * (src/xst_palettes.h, which documents the rules). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "xst_palettes.h"

static double lin(unsigned c) { double v = c / 255.0; return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double L(unsigned h) { return 0.2126 * lin((h >> 16) & 255) + 0.7152 * lin((h >> 8) & 255) + 0.0722 * lin(h & 255); }
static double CR(unsigned a, unsigned b) {
    double la = L(a), lb = L(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}
static void oklab(unsigned h, double o[3]) {
    double r = lin((h >> 16) & 255), g = lin((h >> 8) & 255), b = lin(h & 255);
    double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    o[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    o[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    o[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}
static double dE(unsigned a, unsigned b) {
    double x[3], y[3]; oklab(a, x); oklab(b, y);
    return sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
}

int main(void) {
    int fails = 0, ncls[3] = {0, 0, 0};
    for (int i = 0; i < XST_NPALETTES; i++) {
        const XstPalette *p = &XST_PALETTES[i];
        char why[200] = "";
        for (const char *c = p->name; *c; c++)
            if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) snprintf(why, sizeof why, "bad name");
        for (int j = 0; j < i; j++) if (!strcmp(p->name, XST_PALETTES[j].name)) snprintf(why, sizeof why, "duplicate name");
        if (!strstr(p->tags, p->cls == XST_PAL_DARK ? "dark" : p->cls == XST_PAL_LIGHT ? "light" : "mineral"))
            snprintf(why, sizeof why, "class tag missing");
        if (p->cls == XST_PAL_DARK) {
            if (L(p->bg) > 0.03) snprintf(why, sizeof why, "bg too light (L=%.3f)", L(p->bg));
            else if (CR(p->body, p->bg) < 7) snprintf(why, sizeof why, "body/bg CR %.2f < 7", CR(p->body, p->bg));
            else if (CR(p->accent, p->bg) < 4.5) snprintf(why, sizeof why, "accent/bg CR %.2f < 4.5", CR(p->accent, p->bg));
            else if (CR(p->accent2, p->bg) < 2.5) snprintf(why, sizeof why, "accent2/bg CR %.2f < 2.5", CR(p->accent2, p->bg));
        } else if (p->cls == XST_PAL_LIGHT) {
            double cr = CR(p->body, p->bg);
            if (L(p->bg) < 0.30 || L(p->bg) > 0.70) snprintf(why, sizeof why, "bg L=%.3f outside 0.30..0.70", L(p->bg));
            else if (L(p->body) <= L(p->bg) || cr < 1.15 || cr > 2.0) snprintf(why, sizeof why, "body/bg CR %.2f outside 1.15..2 or darker", cr);
            else if (L(p->accent) < L(p->body)) snprintf(why, sizeof why, "accent darker than body");
            else if (CR(p->bg, p->accent2) < 1.8) snprintf(why, sizeof why, "accent2 shadow CR %.2f < 1.8", CR(p->bg, p->accent2));
        } else {
            if (L(p->body) < L(p->bg)) snprintf(why, sizeof why, "body darker than bg");
            else if (CR(p->accent, p->body) < 2.5) snprintf(why, sizeof why, "vein/body CR %.2f < 2.5", CR(p->accent, p->body));
            else if (CR(p->accent2, p->body) < 1.4) snprintf(why, sizeof why, "intrusion/body CR %.2f < 1.4", CR(p->accent2, p->body));
        }
        /* the accent must stand apart: from the body (dark, mineral) or, for a
         * light palette whose accent is a near-white highlight, from the ground */
        unsigned ref = p->cls == XST_PAL_LIGHT ? p->bg : p->body;
        if (!why[0] && dE(p->accent, ref) < 0.06) snprintf(why, sizeof why, "accent too close (dE %.3f)", dE(p->accent, ref));
        if (why[0]) { fprintf(stderr, "FAIL %s: %s\n", p->name, why); fails++; }
        ncls[p->cls]++;
    }
    if (ncls[0] < 16 || ncls[1] < 10 || ncls[2] < 6) { fprintf(stderr, "FAIL: too few palettes per class\n"); fails++; }
    if (fails) return 1;
    printf("test_xst_palettes: %d palettes ok (dark %d, light %d, mineral %d)\n", XST_NPALETTES, ncls[0], ncls[1], ncls[2]);
    return 0;
}
