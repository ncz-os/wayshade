/* SPDX-License-Identifier: Apache-2.0 */
/* xst_palettes.h - role-based palettes for the xshadertoy family.
 *
 * Each palette assigns four roles (sRGB hex):
 *   bg      the ground: empty space, deep water, the stone or plaster itself
 *   body    the subject: filaments, cloth, light pools, the stone's lit face
 *   accent  the brightest or most saturated moments: highlights, cores, veins
 *   accent2 a secondary tone for depth: shadow color, intrusion, far layer
 *
 * Classes and the contrast constraints tests/test_xst_palettes.c enforces
 * (WCAG relative luminance L, contrast ratio CR = (L1 + 0.05) / (L2 + 0.05)):
 *   DARK     L(bg) <= 0.03; CR(body, bg) >= 7; CR(accent, bg) >= 4.5;
 *            CR(accent2, bg) >= 2.5
 *   LIGHT    0.30 <= L(bg) <= 0.70; body lighter than bg with
 *            1.15 <= CR(body, bg) <= 2.0 (quiet materials); accent at least
 *            as light as body; CR(bg, accent2) >= 1.8 (a readable shadow tone)
 *   MINERAL  L(body) >= L(bg); CR(accent, body) >= 2.5 (veins read against
 *            the stone); CR(accent2, body) >= 1.4
 * and the accent stands apart by an Oklab distance >= 0.06: from the body
 * (DARK, MINERAL) or from the ground (LIGHT, whose accent is a near-white
 * highlight), so no palette collapses to one color.
 *
 * Tags (a shader's //@saver family= picks palettes with any matching tag):
 *   dark light mineral warm cool ink night water stone textile botanical aurora
 *
 * Provenance: set by eye for these savers (2026-09-29, LEAD 27), several
 * derived from the lava-lamp pairings LEAD 25 collected (cream-on-blue ->
 * ultramarine-cream, lavender/cream -> aubergine-rose) with the liquid
 * darkened into a ground; the stone and marble grounds follow the named
 * stones (travertine, Carrara, Rosso Levanto, Verde Alpi, Nero Marquina).
 */
#ifndef XST_PALETTES_H
#define XST_PALETTES_H

enum { XST_PAL_DARK = 0, XST_PAL_LIGHT = 1, XST_PAL_MINERAL = 2 };

typedef struct {
    const char *name;
    int         cls;
    const char *tags;
    unsigned    bg, body, accent, accent2; /* 0xRRGGBB, sRGB */
} XstPalette;

static const XstPalette XST_PALETTES[] = {
    /* dark: ink, night, water */
    {"navy-cream",        XST_PAL_DARK, "dark|ink|night|warm",   0x0e1526, 0xe9dcc6, 0xd9b48a, 0x7f93b5},
    {"ink-bone",          XST_PAL_DARK, "dark|ink",              0x0b0c10, 0xe6e1d6, 0xc9b495, 0x6f7a8a},
    {"midnight-amber",    XST_PAL_DARK, "dark|night|warm",       0x0a1020, 0xf0c27a, 0xe08a48, 0x5d7fa8},
    {"deep-teal-pearl",   XST_PAL_DARK, "dark|water|cool",       0x06171a, 0xd7ece6, 0x74c9ba, 0xc9a46b},
    {"aubergine-rose",    XST_PAL_DARK, "dark|ink|warm",         0x160b16, 0xefd2d6, 0xd97f96, 0x8a78b8},
    {"forest-moss",       XST_PAL_DARK, "dark|botanical|warm",   0x0a140e, 0xd8e4c2, 0x9cc276, 0xc8a45e},
    {"slate-ice",         XST_PAL_DARK, "dark|cool|night",       0x0d1218, 0xdce8f2, 0x92b8dc, 0xa4b48c},
    {"charcoal-copper",   XST_PAL_DARK, "dark|ink|warm",         0x111010, 0xe8d0b8, 0xd0845a, 0x6f8f98},
    {"obsidian-jade",     XST_PAL_DARK, "dark|botanical|cool",   0x070b0a, 0xcfe8d6, 0x5cba92, 0xd0b070},
    {"indigo-saffron",    XST_PAL_DARK, "dark|night|warm",       0x0c0b24, 0xf2dca0, 0xe8a93a, 0x8f84d8},
    {"graphite-coral",    XST_PAL_DARK, "dark|ink|warm",         0x121315, 0xf0dcd2, 0xe58a72, 0x7fa7a0},
    {"night-aurora",      XST_PAL_DARK, "dark|night|cool",       0x050912, 0xbff5dc, 0x62dca8, 0xb47ad8},
    {"polar-violet",      XST_PAL_DARK, "dark|night|cool",       0x060814, 0xd6e6ff, 0x96aeff, 0xd887c8},
    {"sea-glass",         XST_PAL_DARK, "dark|water|cool",       0x071419, 0xd4f0ea, 0x84d4c8, 0xe6c79a},
    {"ember-night",       XST_PAL_DARK, "dark|night|warm",       0x0e0806, 0xf4d6b0, 0xe8904a, 0xb0503a},
    {"plum-gold",         XST_PAL_DARK, "dark|ink|warm",         0x140a14, 0xf0dcb0, 0xd8ac48, 0xa86a9a},
    {"prussian-silver",   XST_PAL_DARK, "dark|water|cool|night", 0x0a1422, 0xe4e8ee, 0xa2b6d0, 0xc8a878},
    {"oxblood-linen",     XST_PAL_DARK, "dark|ink|warm",         0x140707, 0xefe2d0, 0xd2685c, 0xd8b070},
    {"ultramarine-cream", XST_PAL_DARK, "dark|water|night",      0x070c2a, 0xf2e8d2, 0x7e96f0, 0xe8c060},
    {"moss-lilac",        XST_PAL_DARK, "dark|botanical",        0x0c120c, 0xe2dcef, 0xae98dc, 0x9ac070},
    {"abyss-cyan",        XST_PAL_DARK, "dark|water|cool",       0x020a12, 0xc8f0f8, 0x58c8e8, 0x3a7aa8},
    {"lagoon-sand",       XST_PAL_DARK, "dark|water|warm",       0x04161c, 0xf0e6c8, 0x6cc8c0, 0xc89a60},
    /* aurora: body = oxygen green (557.7 nm), accent = the red upper glow
     * (630 nm), accent2 = the nitrogen fringe at the lower edge */
    {"oxygen-green",      XST_PAL_DARK, "dark|night|cool|aurora", 0x03060c, 0x6ef0a8, 0xe0607a, 0x9a7ae0},
    {"crimson-arc",       XST_PAL_DARK, "dark|night|aurora",      0x06050c, 0xf08aa0, 0xffc8b8, 0x7a9af0},
    {"polar-teal",        XST_PAL_DARK, "dark|night|cool|aurora", 0x02070c, 0x7ae8d8, 0xc88af0, 0x5a8af0},
    /* chitin: near-monochrome carapace tones (bone, gunmetal, charcoal, slate,
     * verdigris, steel green). body is the LIT tone (the shader darkens it into
     * a material), accent the verdigris patina, accent2 one muted tone (dull
     * brass or bone) for a few percent of the runs. No saturated red, no blue-white. */
    {"bone-charcoal",     XST_PAL_DARK, "dark|chitin",           0x0b0b0c, 0xd9d3c1, 0x7fa398, 0xa0876a},
    {"gunmetal",          XST_PAL_DARK, "dark|chitin",           0x08090a, 0xaaafb2, 0x6f9a8a, 0x94826a},
    {"verdigris-iron",    XST_PAL_DARK, "dark|chitin",           0x070a09, 0xa9c0b4, 0x5f9c88, 0xb0a48a},
    {"slate-bone",        XST_PAL_DARK, "dark|chitin",           0x0a0c0e, 0xc7c4b6, 0x8494a0, 0x9a8f76},
    {"steel-green",       XST_PAL_DARK, "dark|chitin",           0x070b09, 0xb0bcae, 0x78a088, 0x9a9078},
    {"charcoal-verdigris",XST_PAL_DARK, "dark|chitin",           0x0c0d0d, 0x9fb0a6, 0x5c9a86, 0x8f7f68},
    /* light: stone, plaster, textile, paper */
    {"travertine",        XST_PAL_LIGHT, "light|stone|warm",     0xcbbfa8, 0xe9e0cf, 0xfff7e6, 0x8c7a60},
    {"carrara",           XST_PAL_LIGHT, "light|stone|cool",     0xc9c8c2, 0xeceae4, 0xfbfaf6, 0x6d7280},
    {"warm-limestone",    XST_PAL_LIGHT, "light|stone|warm",     0xc4b294, 0xe6d8bc, 0xfff4dc, 0x8a7050},
    {"sandstone-rose",    XST_PAL_LIGHT, "light|stone|warm",     0xc8a58e, 0xecd4c2, 0xfff0e4, 0x8a5f4c},
    {"plaster-sage",      XST_PAL_LIGHT, "light|stone|botanical",0xc2c6b2, 0xe4e6d6, 0xfbfcef, 0x6e7a5a},
    {"linen-ochre",       XST_PAL_LIGHT, "light|textile|warm",   0xd2c3a3, 0xefe4cc, 0xfff8e8, 0x9a7630},
    {"pearl-grey",        XST_PAL_LIGHT, "light|textile|cool",   0xb9bcc2, 0xe0e3e8, 0xf8f9fb, 0x6a7280},
    {"celadon",           XST_PAL_LIGHT, "light|stone|cool",     0xaec4b8, 0xdbe8df, 0xf4fbf6, 0x4f7a68},
    {"blush-clay",        XST_PAL_LIGHT, "light|stone|warm",     0xcfa99a, 0xefd9cf, 0xfff4ee, 0x94574a},
    {"oat-indigo",        XST_PAL_LIGHT, "light|textile",        0xcfc7b4, 0xebe5d6, 0xfbf8ef, 0x3f4f7a},
    {"slate-paper",       XST_PAL_LIGHT, "light|textile|cool",   0xaab2b8, 0xd8dde0, 0xf4f6f7, 0x4c5a66},
    {"honey-marble",      XST_PAL_LIGHT, "light|stone|warm",     0xc9ac7a, 0xead6ae, 0xfff6e0, 0x7a5a34},
    {"camel-silk",        XST_PAL_LIGHT, "light|textile|warm",   0xbfa482, 0xe2cdae, 0xfbeedb, 0x7a5c3c},
    {"fog-lavender",      XST_PAL_LIGHT, "light|textile|cool",   0xbcb6c8, 0xe0dce8, 0xf8f6fc, 0x5c5478},
    /* mineral: stone body with veins and intrusions */
    {"rosso-levanto",     XST_PAL_MINERAL, "mineral|stone|warm", 0x2a1414, 0x7a3a36, 0xe8d8cc, 0x5a8a70},
    {"verde-alpi",        XST_PAL_MINERAL, "mineral|stone|cool", 0x0e1a14, 0x2f5a44, 0xd8e8dc, 0xb89a60},
    {"calacatta-gold",    XST_PAL_MINERAL, "mineral|stone|warm", 0x9a948a, 0xe6e2da, 0x6a5a44, 0xc8a050},
    {"nero-marquina",     XST_PAL_MINERAL, "mineral|stone|ink",  0x0c0c0e, 0x2a2a2e, 0xe6e2da, 0xb89a60},
    {"amethyst-quartz",   XST_PAL_MINERAL, "mineral|stone|cool", 0x1a1024, 0x5a3a78, 0xe8dcf0, 0x9ac0b0},
    {"malachite",         XST_PAL_MINERAL, "mineral|stone|cool", 0x04140e, 0x1d6a4a, 0xbfe8cf, 0x0a3a28},
    {"lapis",             XST_PAL_MINERAL, "mineral|stone|night",0x060a24, 0x1f3a8a, 0xe6c060, 0xd8e0f0},
    {"sienna-onyx",       XST_PAL_MINERAL, "mineral|stone|warm", 0x2a1a0e, 0x8a5a30, 0xf2e2c4, 0x3a2414},
    {"grey-bardiglio",    XST_PAL_MINERAL, "mineral|stone|cool", 0x3a3e44, 0x8a9098, 0xeef0f2, 0x4a5058},
};
#define XST_NPALETTES ((int)(sizeof XST_PALETTES / sizeof XST_PALETTES[0]))
#endif
