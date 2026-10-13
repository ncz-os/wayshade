/* cs_gibson.c - shader-engine port of xscreensaver "gibson".
 *
 * gibson, Copyright (c) 2020-2025 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Hacking the Gibson, as per the 1995 classic film, HACKERS.
 *
 * In the movie, this was primarily a practical effect: the towers were
 * edge-lit etched perspex, each about four feet tall.
 *
 * Kept from the original: the grid of glowing translucent towers that rise
 * out of the floor and scroll toward the viewer, the five-face towers whose
 * faces randomly show small scrolling hex/number text or one big red menu
 * line, the random swapping of face contents between towers, the circuit-
 * board floor (same random cell generator), the passing billboards
 * (ACCESS GRANTED / ACCESS DENIED ...), the wandering, tilting camera (100
 * degree perspective), additive blending, linear fog on the towers and
 * exp2 fog on the floor.
 *
 * Changed: the GL1 version compiled 360 display lists with per-quad texture
 * state and replayed each as a separate flush.  Every text quad of every
 * face now lives once in a static instance buffer; the vertex shader places
 * it on whichever tower currently owns it (a uniform table), so a frame is
 * about eight draw calls.  The original rasterized text with a system font;
 * this port draws the same strings with a built-in 5x7 bitmap font so it has
 * no font dependency.  enhanced style adds glow, sharper text, anti-aliasing
 * and a soft vignette.
 */
#include "cs_common.h"
#include "cs_post.h"
#include "rotator.h"

#define STEP_DT 0.02              /* original delay 20000 us */
#define MAXT 100
#define GROUND_QUAD_SIZE 30
#define CELLS 20

typedef struct { float x, y, h; unsigned face_mode; int fg[5], bg[5]; /* set ids displayed */ } tower;

typedef struct {
    int style; float speed; int columns, gw, gh, gd, do_tex; float gs;
    float bloom; int aa;
    cs_rng rng; cs_clock clk;
    rotator *rot, *rot2;
    float ground_y, billboard_y; const char *billboard_text; int bb_index;
    int ntowers; tower towers[MAXT];
    int startup_p;
    float tower_color[4], tower_color2[4], edge_color[4], bg_color[4], ground_color[4], ground_dark[4];
    int w, h;
    cs_post post; int post_ok;
    GLuint prog_body, prog_text, prog_ground, prog_black, prog_bb;
    GLuint vao_body, vbo_body, vao_ground, vbo_ground, vao_bg, vbo_bg, vao_fg, vbo_fg, vao_panel, vbo_panel, vao_e;
    int n_body_verts, n_ground_verts, n_bg, n_fg, n_panel;
    GLuint tex[3]; float tex_aspect[3]; int tex_lines[3], tex_w[3], tex_h[3];
    /* billboard atlas: 10 strings */
    float bb_u1[10], bb_v1[10], bb_u2[10], bb_v2[10], bb_w[10], bb_h[10];
    int nbg_sets, nfg_sets;
    float disp_bg[MAXT * 5 + 8], disp_fg[MAXT * 5 + 8];
    float rx, ry, rz, px, py, pz, r2x, r2y, r2z;
} gstate;
static gstate *G;

static const ncz_opt_def GOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"grid-width", NCZ_OPT_INT, "6", 1, 12, NULL, NULL, NULL, "Grid width", "Towers across (xscreensaver -grid-width).", "Density"},
    {"grid-height", NCZ_OPT_INT, "7", 1, 20, NULL, NULL, NULL, "Tower height", "Height of the towers (xscreensaver -grid-height).", "Density"},
    {"grid-depth", NCZ_OPT_INT, "6", 1, 12, NULL, NULL, NULL, "Grid depth", "Rows of towers (xscreensaver -grid-depth).", "Density"},
    {"spacing", NCZ_OPT_FLOAT, "2", 1, 6, NULL, NULL, NULL, "Spacing", "Space between towers (xscreensaver -spacing).", "Density"},
    {"columns", NCZ_OPT_INT, "5", 1, 10, NULL, NULL, NULL, "Text columns", "Text columns per tower face (xscreensaver -columns).", "Density"},
    {"texture", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Text", "Show the text on the tower faces (xscreensaver -texture).", "Look"},
    {"ground-color", NCZ_OPT_STRING, "#8A2BE2", 0, 0, NULL, NULL, NULL, "Floor color", "Circuit-board floor lines, as #RRGGBB (xscreensaver groundColor).", "Look"},
    {"tower-color", NCZ_OPT_STRING, "#4444FF", 0, 0, NULL, NULL, NULL, "Tower color", "Tower glass, as #RRGGBB (xscreensaver towerColor).", "Look"},
    {"text-color", NCZ_OPT_STRING, "#DDDDFF", 0, 0, NULL, NULL, NULL, "Text color", "Small tower text, as #RRGGBB (xscreensaver towerText).", "Look"},
    {"text2-color", NCZ_OPT_STRING, "#FF0000", 0, 0, NULL, NULL, NULL, "Big text color", "Big tower text and billboards, as #RRGGBB (xscreensaver towerText2).", "Look"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof GOPTS / sizeof GOPTS[0]; *prefix = "NCZ_GIBSON_"; *group = "gibson"; return GOPTS;
}
static void parse_hex(const char *s, const char *dflt, float *c) {
    unsigned v = 0;
    if (!(s && s[0] == '#' && strlen(s) == 7 && sscanf(s + 1, "%x", &v) == 1)) sscanf(dflt + 1, "%x", &v);
    c[0] = ((v >> 16) & 255) / 255.0f; c[1] = ((v >> 8) & 255) / 255.0f; c[2] = (v & 255) / 255.0f; c[3] = 1;
}
static double rnd01(void) { return random() / (double)RAND_MAX; }

/* ---------------- built-in 5x7 font ---------------- */
static const char *GLYPHS[][2] = {
 {"A","01110,10001,10001,11111,10001,10001,10001"},{"B","11110,10001,10001,11110,10001,10001,11110"},
 {"C","01110,10001,10000,10000,10000,10001,01110"},{"D","11110,10001,10001,10001,10001,10001,11110"},
 {"E","11111,10000,10000,11110,10000,10000,11111"},{"F","11111,10000,10000,11110,10000,10000,10000"},
 {"G","01110,10001,10000,10111,10001,10001,01111"},{"H","10001,10001,10001,11111,10001,10001,10001"},
 {"I","01110,00100,00100,00100,00100,00100,01110"},{"J","00111,00010,00010,00010,00010,10010,01100"},
 {"K","10001,10010,10100,11000,10100,10010,10001"},{"L","10000,10000,10000,10000,10000,10000,11111"},
 {"M","10001,11011,10101,10101,10001,10001,10001"},{"N","10001,11001,10101,10011,10001,10001,10001"},
 {"O","01110,10001,10001,10001,10001,10001,01110"},{"P","11110,10001,10001,11110,10000,10000,10000"},
 {"Q","01110,10001,10001,10001,10101,10010,01101"},{"R","11110,10001,10001,11110,10100,10010,10001"},
 {"S","01111,10000,10000,01110,00001,00001,11110"},{"T","11111,00100,00100,00100,00100,00100,00100"},
 {"U","10001,10001,10001,10001,10001,10001,01110"},{"V","10001,10001,10001,10001,10001,01010,00100"},
 {"W","10001,10001,10001,10101,10101,11011,10001"},{"X","10001,10001,01010,00100,01010,10001,10001"},
 {"Y","10001,10001,01010,00100,00100,00100,00100"},{"Z","11111,00001,00010,00100,01000,10000,11111"},
 {"0","01110,10001,10011,10101,11001,10001,01110"},{"1","00100,01100,00100,00100,00100,00100,01110"},
 {"2","01110,10001,00001,00010,00100,01000,11111"},{"3","11110,00001,00001,01110,00001,00001,11110"},
 {"4","00010,00110,01010,10010,11111,00010,00010"},{"5","11111,10000,11110,00001,00001,10001,01110"},
 {"6","00110,01000,10000,11110,10001,10001,01110"},{"7","11111,00001,00010,00100,01000,01000,01000"},
 {"8","01110,10001,10001,01110,10001,10001,01110"},{"9","01110,10001,10001,01111,00001,00010,01100"},
 {">","10000,01000,00100,00010,00100,01000,10000"},{".","00000,00000,00000,00000,00000,01100,01100"},
 {"-","00000,00000,00000,11111,00000,00000,00000"},{"{","00110,01000,01000,10000,01000,01000,00110"},
 {"}","01100,00010,00010,00001,00010,00010,01100"},{"[","01110,01000,01000,01000,01000,01000,01110"},
 {"]","01110,00010,00010,00010,00010,00010,01110"},
};
static const char *glyph_rows(char ch) {
    for (size_t i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++) if (GLYPHS[i][0][0] == ch) return GLYPHS[i][1];
    return NULL;
}
#define FS_PX 4                       /* pixels per font dot */
#define CELL_W (6 * FS_PX)
#define CELL_H (8 * FS_PX)
/* Renders newline-separated text into an R8 texture; returns size and line count. */
static GLuint text_texture(const char *text, int *w, int *h, int *lines) {
    int maxc = 0, nl = 0, cur = 0;
    for (const char *p = text; *p; p++) { if (*p == '\n') { nl++; if (cur > maxc) maxc = cur; cur = 0; } else cur++; }
    if (cur) { nl++; if (cur > maxc) maxc = cur; }
    if (maxc < 1) maxc = 1; if (nl < 1) nl = 1;
    if (nl > 400) nl = 400;
    int W = maxc * CELL_W, H = nl * CELL_H;
    unsigned char *px = calloc((size_t)W * H, 1);
    int line = 0, col = 0;
    for (const char *p = text; *p && line < nl; p++) {
        if (*p == '\n') { line++; col = 0; continue; }
        const char *rows = glyph_rows(*p);
        if (rows) {
            for (int ry = 0; ry < 7; ry++) for (int rx = 0; rx < 5; rx++) {
                if (rows[ry * 6 + rx] != '1') continue;
                for (int dy = 0; dy < FS_PX; dy++) for (int dx = 0; dx < FS_PX; dx++) {
                    int X = col * CELL_W + rx * FS_PX + dx, Y = line * CELL_H + (ry + 0) * FS_PX + dy + FS_PX / 2;
                    if (X < W && Y < H) px[(size_t)Y * W + X] = 255;
                }
            }
        }
        col++;
    }
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    free(px);
    *w = W; *h = H; *lines = nl;
    return t;
}

/* ---------------- text content: init_text() of the original ---------------- */
static const char *const MENU[] = {
   "\nACCESS TO THIS COMPUTER AND\nITS DATA IS RESTRICTED TO\nAUTHORIZED PERSONNEL ONLY\n\n",
   "\n  PASSWORD ACCEPTED\n             GOD\n\n",
   "PERSONNEL   >>>\n", "SEA ROUTINGS   >>>\n", "GARBAGE   >>>\n", "COMP. SERVICING   >>>\n",
   "COMPANY BUDGETS   >>>\n", "SCIENTIFIC BUDGETS   >>>\n", "COMPANY POLICIES   >>>\n", "ANNUAL RETURNS   >>>\n",
   "MINE RESEARCH   >>>\n", "CENTRAL LIBRARY   >>>\n", "QUANTATIVE SPEC.   >>>\n", "PAYMENT LEVELS   >>>\n",
   "CENTRAL SERVER   >>>\n", "GARBAGE   >>>\n", "KNMTS. DVPNT.   >>>\n", "LICENSING   >>>\n", "RELATIONS   >>>\n",
   "TIME SHEET RECS.   >>>\n", "RD. PRT. ROUTINGS   >>>\n", "RECRUITMENT   >>>\n", "TNKR. EXPENDITURE   >>>\n",
   "MINE DEVELOPMENT   >>>\n", "GARBAGE   >>>\n", "ANNUAL BUDGETS   >>>\n", "OIL LOCATIONS   >>>\n",
   "TIME SHEET RECS.   >>>\n", "RD. PRT. ROUTINGS   >>>\n", "KINEMATICS   >>>\n", "TPS. REPORTS   >>>\n",
   "BLAST FRNC. STATUS   >>>\n", "ACCOUNTANTS   >>>\n", "SHIPPING FORCASTS   >>>\n", "INDST. REPORTS   >>>\n",
   "EXPLOR. DVLT.   >>>\n", "WRHSE. EXPEND.   >>>\n", "GARBAGE   >>>\n", "RELOCATIONS   >>>\n",
   "AIRFREIGHT STATUS   >>>\n", "TPGC. EXPEND.   >>>\n", "SEA-BOARD LAWS   >>>\n", "COMPOSITE PLANTS   >>>\n",
   "NUCLEAR RESEARCH   >>>\n", "BALLAST REPORTS   >>>\n",
   "\nCONFIDENTAL\nFILES\nDO NOT DELETE\nBEFORE FINAL\nBACKUP IS COMPLETED\n\n",
   "\nFILE 1\nWAITING FOR BACK-UP\n\nFILE 2\nWAITING FOR BACK-UP\n\nFILE 3\nWAITING FOR BACK-UP\n\nFILE 4\nWAITING FOR BACK-UP\n\n",
};
#define NMENU ((int)(sizeof MENU / sizeof MENU[0]))
static const char *const BILLBOARDS[] = {
    "ACCESS GRANTED", "ACCESS GRANTED", "ACCESS DENIED", "ACCESS DENIED", "ACCESS DENIED", "ACCESS DENIED",
    "ACCESS DENIED", "PASSWORD ACCEPTED", " GIVE ME\nA COOKIE", "MESS WITH THE BEST\n  DIE LIKE THE REST",
};

/* ---------------- geometry ---------------- */
typedef struct { float p[3]; float k; } gv;       /* k: color class for the body */
static gv *body_v; static int body_n, body_cap;
static void bpush(float x, float y, float z, float k) {
    if (body_n >= body_cap) { body_cap = body_cap ? body_cap * 2 : 1024; body_v = realloc(body_v, sizeof(gv) * body_cap); }
    body_v[body_n].p[0] = x; body_v[body_n].p[1] = y; body_v[body_n].p[2] = z; body_v[body_n].k = k; body_n++;
}
static void bquad(float k, float z, float x1, float y1, float x2, float y2) {
    bpush(x1, y1, z, k); bpush(x2, y1, z, k); bpush(x2, y2, z, k);
    bpush(x1, y1, z, k); bpush(x2, y2, z, k); bpush(x1, y2, z, k);
}
/* draw_tower_face mode 0: background + four edge strips, for face f (attribute k = face*2 + class) */
static void body_face(int f, float height) {
    float m = 0.015f, z = -0.0005f;
    bquad(f * 2 + 0, z * 2, 0, 0, 1, height);                    /* background */
    bquad(f * 2 + 1, z, 0, 0, m, height);                        /* left */
    bquad(f * 2 + 1, 0, 1 - m, 0, 1, height);                    /* right */
    bquad(f * 2 + 1, 0, m, 0, 1 - m, m);                         /* bottom */
    bquad(f * 2 + 1, z, m, height - m, 1 - m, height);           /* top */
}

/* text quads: one instance = 12 floats [set, face, x1,y1,x2,y2, u1,v1,u2,v2, z, kind]; */
typedef struct { float set, face, x1, y1, x2, y2, u1, v1, u2, v2, z, k; } tq;
static tq *tq_bg, *tq_fg, *tq_pn; static int ntq_bg, ntq_fg, ntq_pn, cap_bg, cap_fg, cap_pn;
static void tq_push(tq **arr, int *n, int *cap, tq q) {
    if (*n >= *cap) { *cap = *cap ? *cap * 2 : 1024; *arr = realloc(*arr, sizeof(tq) * *cap); }
    (*arr)[(*n)++] = q;
}
/* draw_tower_face_text() for face f of tower/set id `set`; which 0 = small background text, 1 = big text */
static void text_quads(gstate *s, int set, int f, float height, int which) {
    int n = which ? 1 : 0;
    float sx = 1.0f / (which ? 1 : s->columns);
    float sy = which ? height * 0.8f : sx * 4;
    float tex_lines = which ? 3.0f : 8.0f;
    float margin = 0.2f;
    float m2 = margin / 2 / (which ? 1 : s->columns);
    float m3 = m2 / (which ? 1 : height);
    float h2 = height * (which ? 1 - margin : 1);
    float lines_in_tex = (float)s->tex_lines[n];
    float vspan = tex_lines / lines_in_tex;
    for (float x1 = 0; x1 < 1.0f - 1e-4f; x1 += sx) {
        float x2 = x1 + sx;
        for (float y2 = h2; y2 > 0; y2 -= sy) {
            float y1 = y2 - sy * (1 - margin);
            float toff = (float)rnd01();
            float vspan_q = vspan;
            if (y1 < 0) { vspan_q = vspan * (y2 / (y2 - y1)); y1 = 0; }
            tq q = { (float)set, (float)f, x1 + m2, y1 + m3, x2 - m2, y2 - m3, 0, toff, 1, toff + vspan_q, which ? 0.05f : 0.0f, (float)which };
            tq_push(which ? &tq_fg : &tq_bg, which ? &ntq_fg : &ntq_bg, which ? &cap_fg : &cap_bg, q);
            if (which) {                                   /* translucent white panel behind the big text */
                tq p = q; p.z = -0.05f; p.x1 -= 0.03f; p.y1 -= 0.03f; p.x2 += 0.03f; p.y2 += 0.03f; p.k = 2;
                p.x1 = x1 + m2 - 0.03f; p.y1 = y1 + m3 - 0.03f; p.x2 = x2 - m2 + 0.03f; p.y2 = y2 - m3 + 0.03f;
                tq_push(&tq_pn, &ntq_pn, &cap_pn, p);
            }
            if (which) break;
        }
    }
}

/* the floor: draw_ground() of the original, baked into list space */
static float *gr_v; static int gr_n, gr_cap;
static void gpush(cs_mat4 m, float x, float y, float z, float kind) {
    if (gr_n >= gr_cap) { gr_cap = gr_cap ? gr_cap * 2 : 4096; gr_v = realloc(gr_v, sizeof(float) * 4 * gr_cap); }
    float *o = gr_v + 4 * gr_n++;
    o[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
    o[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
    o[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
    o[3] = kind;
}
static void gstrip(cs_mat4 m, float kind, const float *v, int n) {      /* GL_QUAD_STRIP of n vertices */
    for (int i = 0; i + 3 < n; i += 2) {
        const float *a = v + i * 3, *b = v + (i + 1) * 3, *c = v + (i + 2) * 3, *d = v + (i + 3) * 3;
        gpush(m, a[0], a[1], a[2], kind); gpush(m, b[0], b[1], b[2], kind); gpush(m, c[0], c[1], c[2], kind);
        gpush(m, b[0], b[1], b[2], kind); gpush(m, d[0], d[1], d[2], kind); gpush(m, c[0], c[1], c[2], kind);
    }
}
static void build_ground(void) {
    cs_mat4 base = cs_mul(cs_scale(1.0f / CELLS, 1.0f / CELLS, 1), cs_translate(-CELLS / 2.0f, -CELLS / 2.0f, 0));
    base = cs_mul(base, cs_translate(0.5f, 0, 0));
    float z = -0.005f, cs = 1.0f;
    /* clipping quad (dark) */
    {
        float q[4][3] = {{0, 0, z}, {CELLS * cs, 0, z}, {CELLS * cs, CELLS * cs, z}, {0, CELLS * cs, z}};
        int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; i++) gpush(base, q[idx[i]][0], q[idx[i]][1], q[idx[i]][2], 0);
    }
    for (int y = 0; y < CELLS; y++) for (int x = 0; x < CELLS; x++) {
        float a = 0, b = 1.0f / 3, c = 2.0f / 3, d = 1.0f, w = 0.02f;
        cs_mat4 m = cs_mul(base, cs_translate((float)x, (float)y, 0));
        switch (random() % 4) {
        case 0: m = cs_mul(m, cs_rotate(90, 0, 0, 1)); m = cs_mul(m, cs_translate(0, -1, 0)); break;
        case 1: m = cs_mul(m, cs_rotate(-90, 0, 0, 1)); m = cs_mul(m, cs_translate(-1, 0, 0)); break;
        case 2: m = cs_mul(m, cs_rotate(180, 0, 0, 1)); m = cs_mul(m, cs_translate(-1, -1, 0)); break;
        default: break;
        }
        switch (random() % 2) {
        case 0: m = cs_mul(m, cs_scale(-1, -1, 1)); m = cs_mul(m, cs_translate(-1, -1, 0)); break;
        default: break;
        }
        switch (random() % 2) {
        case 0: {
            float s1[] = { a, b + w, 0,  a, b - w, 0,  b + w, a, 0,  b - w, a, 0 };
            gstrip(m, 1, s1, 4);
            float s2[] = { a, c + w, 0,  a, c - w, 0,  b + w, c + w, 0,  b, c - w, 0,  c + w, b + w, 0,  c - w, b, 0,  c + w, a, 0,  c - w, a, 0 };
            gstrip(m, 1, s2, 8);
            break; }
        default: {
            float s1[] = { a + w, d, 0,  a, d, 0,  a + w, d, 0,  a, d - w, 0,  b + w, c - w, 0,  b - w, c - w, 0,  b + w, a, 0,  b - w, a, 0 };
            gstrip(m, 1, s1, 8);
            float s2[] = { b + w, d, 0,  b - w, d, 0,  c + w, c - w, 0,  c - w, c - w, 0,  c + w, a, 0,  c - w, a, 0 };
            gstrip(m, 1, s2, 6);
            break; }
        }
    }
}

/* ---------------- shaders ---------------- */
static const char *VS_BODY = CS_GLSL(
    layout(location = 0) in vec4 aP;            /* x, y, z in face space; w = face*2 + class */
    uniform mat4 uVP; uniform mat4 uFace[5]; uniform vec4 uTower[100]; uniform mat4 uMV;
    uniform vec3 uColB; uniform vec4 uColE; uniform vec4 uLight;
    out vec4 vCol; out float vFog;
    void main() {
        int k = int(aP.w + 0.5); int face = k / 2; int cls = k - face * 2;
        vec4 T = uTower[gl_InstanceID];
        vec4 fp = uFace[face] * vec4(aP.xyz, 1.0);
        vec3 world = fp.xyz + vec3(-0.5, 0.5, 0.0) + vec3(T.x, T.y, T.z);
        vec4 e = uMV * vec4(world, 1.0);
        gl_Position = uVP * vec4(world, 1.0);
        vec3 N = normalize(mat3(uMV) * (mat3(uFace[face]) * vec3(0.0, 0.0, 1.0)));
        float lit = 0.4 + max(dot(N, normalize(uLight.xyz)), 0.0);
        vec4 c = cls == 0 ? vec4(uColB, 1.0) : uColE;
        vCol = vec4(c.rgb * lit, c.a);
        vFog = clamp((100.0 - abs(e.z)) / 100.0, 0.0, 1.0);
    });
static const char *FS_BODY = CS_GLSL(
    in vec4 vCol; in float vFog; out vec4 o; uniform float uEnh;
    void main() { o = vec4(vCol.rgb * vFog, vCol.a); });

static const char *VS_TEXT = CS_GLSL(
    layout(location = 0) in vec4 aSF;           /* set, face, kind(unused), - */
    layout(location = 1) in vec4 aRect;         /* x1 y1 x2 y2 */
    layout(location = 2) in vec4 aUV;           /* u1 v1 u2 v2 */
    layout(location = 3) in vec2 aZK;           /* z, kind */
    uniform mat4 uVP; uniform mat4 uFace[5]; uniform vec4 uTower[100]; uniform mat4 uMV;
    uniform vec4 uDisp[136];                    /* per set: displayed tower index, or -1 */
    uniform vec4 uCol; uniform vec4 uLight; uniform float uUvScale;
    out vec2 vUv; out vec4 vCol; out float vFog;
    void main() {
        int set = int(aSF.x + 0.5); int face = int(aSF.y + 0.5);
        float disp = uDisp[set >> 2][set & 3];
        int m = gl_VertexID % 6; int q = (m == 0) ? 0 : (m == 1) ? 1 : (m == 2) ? 2 : (m == 3) ? 0 : (m == 4) ? 2 : 3;
        float fx = (q == 1 || q == 2) ? 1.0 : 0.0, fy = (q == 2 || q == 3) ? 1.0 : 0.0;
        vec3 fpos = vec3(mix(aRect.x, aRect.z, fx), mix(aRect.y, aRect.w, fy), aZK.x);
        if (disp < -0.5) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vUv = vec2(0.0); vCol = vec4(0.0); vFog = 0.0; return; }
        vec4 T = uTower[int(disp + 0.5)];
        vec3 world = (uFace[face] * vec4(fpos, 1.0)).xyz + vec3(-0.5, 0.5, 0.0) + vec3(T.x, T.y, T.z);
        vec4 e = uMV * vec4(world, 1.0);
        gl_Position = uVP * vec4(world, 1.0);
        vec3 N = normalize(mat3(uMV) * (mat3(uFace[face]) * vec3(0.0, 0.0, 1.0)));
        float lit = 0.4 + max(dot(N, normalize(uLight.xyz)), 0.0);
        vUv = vec2(mix(aUV.x, aUV.z, fx), mix(aUV.w, aUV.y, fy));    /* v grows downward in the texture, y grows up */
        vCol = vec4(uCol.rgb * lit, uCol.a); vFog = clamp((100.0 - abs(e.z)) / 100.0, 0.0, 1.0);
    });
static const char *FS_TEXT = CS_GLSL(
    in vec2 vUv; in vec4 vCol; in float vFog; out vec4 o; uniform sampler2D uTex; uniform float uUseTex; uniform float uEnh;
    void main() {
        float a = uUseTex > 0.5 ? texture(uTex, vUv).r : 1.0;
        o = vec4(vCol.rgb * vFog, vCol.a * a);
    });

static const char *VS_GROUND = CS_GLSL(
    layout(location = 0) in vec4 aP;            /* list-space x y z, kind */
    uniform mat4 uVP; uniform mat4 uMV; uniform mat4 uM; uniform vec3 uCol; uniform vec3 uDark; uniform vec4 uLight;
    out vec3 vCol; out float vFog;
    void main() {
        vec4 w = uM * vec4(aP.xyz, 1.0);
        vec4 e = uMV * w;
        gl_Position = uVP * w;
        vec3 N = normalize(mat3(uMV) * mat3(uM) * vec3(0.0, 0.0, 1.0));
        float lit = 0.4 + max(dot(N, normalize(uLight.xyz)), 0.0);
        vCol = (aP.w < 0.5 ? uDark : uCol) * lit;
        float d = length(e.xyz);
        vFog = exp(-pow(0.015 * d, 2.0));
    });
static const char *FS_GROUND = CS_GLSL(
    in vec3 vCol; in float vFog; out vec4 o; uniform float uEnh;
    void main() { o = vec4(vCol * vFog, 1.0); });

static const char *VS_BLACK = CS_GLSL(
    uniform mat4 uVP; uniform vec4 uTower[100]; uniform float uZ;
    void main() {
        int q = (gl_VertexID % 6); int i = (q == 0) ? 0 : (q == 1) ? 1 : (q == 2) ? 2 : (q == 3) ? 0 : (q == 4) ? 2 : 3;
        vec2 c = vec2((i == 1 || i == 2) ? 0.5 : -0.5, (i == 2 || i == 3) ? 0.5 : -0.5);
        vec4 T = uTower[gl_InstanceID];
        gl_Position = uVP * vec4(vec3(T.x, T.y, 0.0) + vec3(c, uZ), 1.0);
    });
static const char *FS_BLACK = CS_GLSL(out vec4 o; void main() { o = vec4(0.0, 0.0, 0.0, 1.0); });

static const char *VS_BB = CS_GLSL(
    uniform mat4 uVP; uniform mat4 uM; uniform vec4 uRect;        /* x1 y1 x2 y2 in string units */
    uniform vec4 uUV; uniform float uPanel;
    out vec2 vUv;
    void main() {
        int q = (gl_VertexID % 6); int i = (q == 0) ? 0 : (q == 1) ? 1 : (q == 2) ? 2 : (q == 3) ? 0 : (q == 4) ? 2 : 3;
        float fx = (i == 1 || i == 2) ? 1.0 : 0.0, fy = (i == 2 || i == 3) ? 1.0 : 0.0;
        vec3 p = vec3(mix(uRect.x, uRect.z, fx), mix(uRect.y, uRect.w, fy), 0.0);
        gl_Position = uVP * (uM * vec4(p, 1.0));
        vUv = vec2(mix(uUV.x, uUV.z, fx), mix(uUV.w, uUV.y, fy));
    });
static const char *FS_BB = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform sampler2D uTex; uniform vec4 uCol; uniform float uPanel;
    void main() { float a = uPanel > 0.5 ? 1.0 : texture(uTex, vUv).r; o = vec4(uCol.rgb, uCol.a * a); });

static void make_inst_vao(GLuint *vao, GLuint *vbo, tq *arr, int n) {
    glGenVertexArrays(1, vao); glGenBuffers(1, vbo);
    glBindVertexArray(*vao); glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tq) * (n ? n : 1), arr, GL_STATIC_DRAW);
    /* layout: [set face x1 y1][x2 y2 u1 v1][u2 v2 z k] -> re-map to (set,face,-,-) (x1 y1 x2 y2) (u1 v1 u2 v2) (z k) */
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 48, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 48, (void *)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 48, (void *)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 48, (void *)40);
    for (int k = 0; k < 4; k++) glVertexAttribDivisor(k, 1);
    glBindVertexArray(0);
}

/* ---------------- state ---------------- */
static float ease_in_out_sine(float t) { return -(cosf((float)M_PI * t) - 1) / 2; }

static void animate_towers(gstate *s) {
    float min = -3, max = s->gd * (1 + s->gs) - s->gs - 1;
    float yspeed = 0.05f;
    for (int ii = 0; ii < 20; ii++) {
        if (0 == (random() % 20)) {                      /* trade two towers' bg sets */
            int i = random() % s->ntowers, j = random() % s->ntowers, k = random() % 5;
            int t = s->towers[i].bg[k]; s->towers[i].bg[k] = s->towers[j].bg[k]; s->towers[j].bg[k] = t;
        }
        {                                                /* trade two towers' fg sets */
            int i = random() % s->ntowers, j = random() % s->ntowers, k = random() % 5;
            int t = s->towers[i].fg[k]; s->towers[i].fg[k] = s->towers[j].fg[k]; s->towers[j].fg[k] = t;
        }
        for (int jj = 0; jj < s->ntowers; jj++)
            for (int kk = 0; kk < 5; kk++) {
                int frames = 500, fg_chance = (kk == 0 ? 100000 : 10);
                unsigned o = !!(s->towers[jj].face_mode & (1u << kk));
                unsigned n = !!((random() % frames) ? o : (0 == (random() % fg_chance)));
                s->towers[jj].face_mode = (s->towers[jj].face_mode & ~(1u << kk)) | (n << kk);
            }
    }
    for (int ii = 0; ii < s->ntowers; ii++) {
        tower *t = &s->towers[ii];
        t->h += 0.01f; if (t->h > 1) t->h = 1;
        t->y -= yspeed;
        if (t->y < min) { t->h = 0; t->y = max; }
    }
    s->ground_y -= yspeed / GROUND_QUAD_SIZE;
    if (s->ground_y < 1) s->ground_y += 1;
    s->billboard_y -= yspeed;
    if (s->billboard_y < min || !s->billboard_text) {
        s->bb_index = random() % 10;
        s->billboard_text = BILLBOARDS[s->bb_index];
        s->billboard_y = max * (1 + (float)rnd01() * 8);
    }
    double x, y, z;
    get_position(s->rot, &x, &y, &z, True); s->px = (float)x; s->py = (float)y; s->pz = (float)z;
    get_position(s->rot2, &x, &y, &z, True); s->r2x = (float)x; s->r2y = (float)y; s->r2z = (float)z;
    if (s->startup_p && s->towers[s->ntowers - 1].h >= 1) s->startup_p = 0;
}

static void init_gibson(ModeInfo *mi) {
    gstate *s = calloc(1, sizeof *s); G = s;
    s->style = cs_style(); s->speed = (float)cs_opt_f("speed", 1);
    s->columns = (int)cs_opt_i("columns", 5); s->gw = (int)cs_opt_i("grid-width", 6); s->gh = (int)cs_opt_i("grid-height", 7);
    s->gd = (int)cs_opt_i("grid-depth", 6); s->gs = (float)cs_opt_f("spacing", 2); s->do_tex = cs_opt_b("texture", 1);
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    if (s->gw * s->gd > MAXT) s->gd = MAXT / s->gw;
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&s->rng, seed); srandom(seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    parse_hex(cs_opt_s("text-color", "#DDDDFF"), "#DDDDFF", s->tower_color);
    parse_hex(cs_opt_s("text2-color", "#FF0000"), "#FF0000", s->tower_color2);
    parse_hex(cs_opt_s("tower-color", "#4444FF"), "#4444FF", s->bg_color);
    parse_hex(cs_opt_s("ground-color", "#8A2BE2"), "#8A2BE2", s->ground_color);
    memcpy(s->edge_color, s->bg_color, sizeof s->edge_color); s->edge_color[3] = 0.7f;
    s->ground_dark[0] = s->bg_color[0] * 0.05f; s->ground_dark[1] = s->bg_color[1] * 0.05f; s->ground_dark[2] = s->bg_color[2] * 0.3f;
    s->ntowers = s->gw * s->gd;
    s->startup_p = 1;
    s->rot = make_rotator(0, 0, 0, 0, 0.007, True);
    s->rot2 = make_rotator(0, 0, 0, 0, 0.01, True);

    /* text (init_text) */
    {
        int lines = 20;
        char *t1 = calloc(NMENU * 2 * 40 + 64, 1), *t0 = calloc(lines * 40 + 64, 1), *p;
        p = t1;
        for (int i = 0; i < NMENU; i++) { int n = random() % NMENU; strcat(p, MENU[n]); p += strlen(p); }
        p = t0;
        for (int i = 0; i < lines; i++) {
            switch (random() % 11) {
            case 0: sprintf(p, "%X\n", (unsigned)(random() % 0xFFFFFFFF)); break;
            case 1: sprintf(p, "%X\n", (unsigned)(random() % 0xFFFFFF)); break;
            case 2: sprintf(p, "%X\n", (unsigned)(random() % 0xFFFF)); break;
            case 3: sprintf(p, "%d\n", (int)(random() % 0xFFFFFF)); break;
            case 4: sprintf(p, "%d\n", (int)(random() % 0xFFFF)); break;
            case 5: sprintf(p, "%d\n", (int)(random() % 0xFFF)); break;
            case 6: strcat(p, "00000000\n"); break;
            case 7: sprintf(p, "{{{{{{{{\n"); break;
            case 8: sprintf(p, "[][][][][][]\n"); break;
            case 9: sprintf(p, "DEFAULT\n"); break;
            case 10: sprintf(p, "\n"); break;
            }
            p += strlen(p);
        }
        s->tex[0] = text_texture(t0, &s->tex_w[0], &s->tex_h[0], &s->tex_lines[0]);
        s->tex[1] = text_texture(t1, &s->tex_w[1], &s->tex_h[1], &s->tex_lines[1]);
        free(t0); free(t1);
        /* billboard atlas: the ten strings stacked, each padded to a common width */
        {
            char big[1024] = ""; int startline[10], nlines[10];
            int line = 0;
            for (int i = 0; i < 10; i++) {
                startline[i] = line; nlines[i] = 1;
                for (const char *c = BILLBOARDS[i]; *c; c++) if (*c == '\n') nlines[i]++;
                strcat(big, BILLBOARDS[i]); strcat(big, "\n"); line += nlines[i] + 0;
                if (0) strcat(big, "\n");
            }
            /* every string starts on a fresh line because we appended "\n" after each */
            int lc = 0;
            for (int i = 0; i < 10; i++) { startline[i] = lc; lc += nlines[i]; }
            s->tex[2] = text_texture(big, &s->tex_w[2], &s->tex_h[2], &s->tex_lines[2]);
            for (int i = 0; i < 10; i++) {
                int maxc = 0, cur = 0;
                for (const char *c = BILLBOARDS[i]; ; c++) { if (*c == '\n' || !*c) { if (cur > maxc) maxc = cur; cur = 0; if (!*c) break; } else cur++; }
                s->bb_u1[i] = 0; s->bb_u2[i] = (float)(maxc * CELL_W) / s->tex_w[2];
                s->bb_v1[i] = (float)(startline[i] * CELL_H) / s->tex_h[2];
                s->bb_v2[i] = (float)((startline[i] + nlines[i]) * CELL_H) / s->tex_h[2];
                s->bb_w[i] = (float)(maxc * CELL_W) / CELL_H * 0.5f;
                s->bb_h[i] = (float)nlines[i] * 0.5f;
                s->bb_w[i] *= 2.0f;
            }
        }
    }
    /* tower geometry + text quads */
    body_n = 0;
    for (int f = 0; f < 5; f++) body_face(f, f == 0 ? 1.0f : (float)s->gh);
    s->n_body_verts = body_n;
    {
        int nsets = s->ntowers * 5;
        s->nbg_sets = s->nfg_sets = nsets;
        ntq_bg = ntq_fg = ntq_pn = 0;
        for (int t = 0; t < s->ntowers; t++)
            for (int f = 0; f < 5; f++) {
                int set = t * 5 + f;
                float hf = (f == 0) ? 1.0f : (float)s->gh;
                text_quads(s, set, f, hf, 0);
                text_quads(s, set, f, hf * 0.7f, 1);
                s->towers[t].bg[f] = set; s->towers[t].fg[f] = set;
            }
        s->n_bg = ntq_bg; s->n_fg = ntq_fg; s->n_panel = ntq_pn;
    }
    for (int i = 0; i < s->ntowers; i++) {}
    {
        int gwv = s->gw, gdv = s->gd;
        float ww = gwv * (1 + s->gs) - s->gs, hh = gdv * (1 + s->gs) - s->gs;
        for (int y = 0; y < gdv; y++) for (int x = 0; x < gwv; x++) {
            tower *t = &s->towers[y * gwv + x];
            t->x = (gwv > 1) ? (x * ww / (gwv - 1)) - ww / 2 : 0;
            t->y = (y * hh / gdv) + 6;
            t->h = 0 - y / (float)gdv / 2;
        }
    }
    build_ground();
    s->prog_body = cs_program(VS_BODY, FS_BODY, "gibson body");
    s->prog_text = cs_program(VS_TEXT, FS_TEXT, "gibson text");
    s->prog_ground = cs_program(VS_GROUND, FS_GROUND, "gibson ground");
    s->prog_black = cs_program(VS_BLACK, FS_BLACK, "gibson black");
    s->prog_bb = cs_program(VS_BB, FS_BB, "gibson billboard");
    if (!s->prog_body || !s->prog_text || !s->prog_ground || !s->prog_black || !s->prog_bb) return;
    glGenVertexArrays(1, &s->vao_body); glGenBuffers(1, &s->vbo_body);
    glBindVertexArray(s->vao_body); glBindBuffer(GL_ARRAY_BUFFER, s->vbo_body);
    glBufferData(GL_ARRAY_BUFFER, sizeof(gv) * body_n, body_v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glGenVertexArrays(1, &s->vao_ground); glGenBuffers(1, &s->vbo_ground);
    glBindVertexArray(s->vao_ground); glBindBuffer(GL_ARRAY_BUFFER, s->vbo_ground);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * gr_n, gr_v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void *)0);
    s->n_ground_verts = gr_n;
    glBindVertexArray(0);
    make_inst_vao(&s->vao_bg, &s->vbo_bg, tq_bg, s->n_bg);
    make_inst_vao(&s->vao_fg, &s->vbo_fg, tq_fg, s->n_fg);
    make_inst_vao(&s->vao_panel, &s->vbo_panel, tq_pn, s->n_panel);
    glGenVertexArrays(1, &s->vao_e);
    free(body_v); free(gr_v); free(tq_bg); free(tq_fg); free(tq_pn); body_v = NULL; gr_v = NULL; tq_bg = tq_fg = tq_pn = NULL;
    animate_towers(s);
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 1);
    fprintf(stderr, "[diag] gibson: style=%s seed=%u towers=%d text quads=%d+%d ground verts=%d\n", s->style ? "enhanced" : "classic", seed,
            s->ntowers, s->n_bg, s->n_fg, s->n_ground_verts);
}

static void reshape_gibson(ModeInfo *mi, int w, int h) {
    gstate *s = G; if (!s) return;
    s->w = w; s->h = h; glViewport(0, 0, w, h);
    if (s->post_ok) cs_post_resize(&s->post, w, h);
}

static void draw_gibson(ModeInfo *mi) {
    gstate *s = G; if (!s || !s->prog_body) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) animate_towers(s);

    /* camera: the original modelview chain */
    float aspect = (float)s->w / (float)s->h;
    cs_mat4 P = cs_perspective(100, aspect / 4.0f, 1.0f, 20.0f * s->gd * 1.5f * (1 + s->gs));
    cs_mat4 V = cs_translate(0, 0, -1);
    cs_mat4 M = cs_scale(10, 10, 10);
    M = cs_mul(M, cs_translate(0, -1, 0));
    M = cs_mul(M, cs_rotate(-82, 1, 0, 0));
    {
        float maxx = 40, maxy = 1.5f, maxz = 100;
        float minh = -(s->gh / 2.0f), maxh = -(s->gh / 20.0f);
        float x = s->px - 0.5f, z = minh + s->pz * (maxh - minh);
        M = cs_mul(M, cs_translate(x * s->gs * 0.005f, 0, z));
        M = cs_mul(M, cs_rotate(maxx / 2 - s->r2x * maxx, 1, 0, 0));
        M = cs_mul(M, cs_rotate(maxy / 2 - s->r2y * maxy, 0, 1, 0));
        M = cs_mul(M, cs_rotate(maxz / 2 - s->r2z * maxz, 0, 0, 1));
    }
    cs_mat4 MV = cs_mul(V, M), VP = cs_mul(P, MV);
    float Lg[4] = { 0.4f, 0.2f, 0.4f, 0 };

    /* tower table */
    float tw[MAXT * 4];
    for (int i = 0; i < s->ntowers; i++) {
        tower *t = &s->towers[i];
        float xo = (s->gw & 1) ? (s->gs + 1) / 2.0f : 0.0f;
        tw[i * 4] = t->x + xo; tw[i * 4 + 1] = t->y - 1;
        tw[i * 4 + 2] = -s->gh * ease_in_out_sine(1 - t->h < 0 ? 0 : (1 - t->h > 1 ? 1 : 1 - t->h)); tw[i * 4 + 3] = 0;
    }
    /* which set is shown where */
    for (int i = 0; i < s->nbg_sets + 8; i++) s->disp_bg[i] = s->disp_fg[i] = -1;
    for (int i = 0; i < s->ntowers; i++)
        for (int f = 0; f < 5; f++) {
            if (s->towers[i].face_mode & (1u << f)) s->disp_fg[s->towers[i].fg[f]] = (float)i;
            else s->disp_bg[s->towers[i].bg[f]] = (float)i;
        }
    /* face matrices */
    cs_mat4 F[5];
    F[0] = cs_translate(0, 0, (float)s->gh);          /* top, height = gh */
    { cs_mat4 m = cs_mul(cs_rotate(90, 1, 0, 0), cs_rotate(-90, 0, 1, 0)); F[1] = cs_mul(m, cs_translate(-1, 0, 0)); }
    { cs_mat4 m = cs_mul(cs_rotate(90, 1, 0, 0), cs_rotate(180, 0, 1, 0)); F[2] = cs_mul(m, cs_translate(-1, 0, 1)); }
    { cs_mat4 m = cs_mul(cs_rotate(90, 1, 0, 0), cs_rotate(90, 0, 1, 0)); F[3] = cs_mul(m, cs_translate(0, 0, 1)); }
    F[4] = cs_rotate(90, 1, 0, 0);
    float Fm[5 * 16]; for (int i = 0; i < 5; i++) memcpy(Fm + i * 16, F[i].m, 64);

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else glViewport(0, 0, s->w, s->h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    float enhf = s->style ? 1.f : 0.f;

    /* floor, twice, scrolling */
    glUseProgram(s->prog_ground);
    glUniformMatrix4fv(glGetUniformLocation(s->prog_ground, "uVP"), 1, GL_FALSE, VP.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog_ground, "uMV"), 1, GL_FALSE, MV.m);
    glUniform3fv(glGetUniformLocation(s->prog_ground, "uCol"), 1, s->ground_color);
    glUniform3fv(glGetUniformLocation(s->prog_ground, "uDark"), 1, s->ground_dark);
    glUniform4fv(glGetUniformLocation(s->prog_ground, "uLight"), 1, Lg);
    glUniform1f(glGetUniformLocation(s->prog_ground, "uEnh"), enhf);
    glBindVertexArray(s->vao_ground);
    for (int k = 0; k < 2; k++) {
        cs_mat4 G0 = cs_scale(GROUND_QUAD_SIZE, GROUND_QUAD_SIZE, 1);
        G0 = cs_mul(G0, cs_translate(0, s->ground_y - 1.5f + (float)k, 0));
        glUniformMatrix4fv(glGetUniformLocation(s->prog_ground, "uM"), 1, GL_FALSE, G0.m);
        glDisable(GL_CULL_FACE);
        glDrawArraysInstanced(GL_TRIANGLES, 0, s->n_ground_verts, 1);
    }
    /* scene translate for odd grids (the original does it inside the tower push) */
    /* clear the floor under the tower bases */
    glUseProgram(s->prog_black);
    glUniformMatrix4fv(glGetUniformLocation(s->prog_black, "uVP"), 1, GL_FALSE, VP.m);
    {
        float tb[MAXT * 4]; memcpy(tb, tw, sizeof(float) * 4 * s->ntowers);
        for (int i = 0; i < s->ntowers; i++) tb[i * 4 + 1] -= 0;       /* same tower table */
        glUniform4fv(glGetUniformLocation(s->prog_black, "uTower"), s->ntowers, tb);
        glUniform1f(glGetUniformLocation(s->prog_black, "uZ"), 0.01f);
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
        glBindVertexArray(s->vao_e);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, s->ntowers);
    }
    if (s->startup_p) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    /* tower bodies */
    glUseProgram(s->prog_body);
    glUniformMatrix4fv(glGetUniformLocation(s->prog_body, "uVP"), 1, GL_FALSE, VP.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog_body, "uMV"), 1, GL_FALSE, MV.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog_body, "uFace"), 5, GL_FALSE, Fm);
    glUniform4fv(glGetUniformLocation(s->prog_body, "uTower"), s->ntowers, tw);
    glUniform3fv(glGetUniformLocation(s->prog_body, "uColB"), 1, s->bg_color);
    glUniform4fv(glGetUniformLocation(s->prog_body, "uColE"), 1, s->edge_color);
    glUniform4fv(glGetUniformLocation(s->prog_body, "uLight"), 1, Lg);
    glUniform1f(glGetUniformLocation(s->prog_body, "uEnh"), enhf);
    glBindVertexArray(s->vao_body);
    glDrawArraysInstanced(GL_TRIANGLES, 0, s->n_body_verts, s->ntowers);

    /* text on the faces */
    if (s->do_tex) {
        glUseProgram(s->prog_text);
        glUniformMatrix4fv(glGetUniformLocation(s->prog_text, "uVP"), 1, GL_FALSE, VP.m);
        glUniformMatrix4fv(glGetUniformLocation(s->prog_text, "uMV"), 1, GL_FALSE, MV.m);
        glUniformMatrix4fv(glGetUniformLocation(s->prog_text, "uFace"), 5, GL_FALSE, Fm);
        glUniform4fv(glGetUniformLocation(s->prog_text, "uTower"), s->ntowers, tw);
        glUniform4fv(glGetUniformLocation(s->prog_text, "uLight"), 1, Lg);
        glUniform1f(glGetUniformLocation(s->prog_text, "uEnh"), enhf);
        glUniform1i(glGetUniformLocation(s->prog_text, "uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        /* translucent panels behind the big text (white, alpha 0.2) */
        float white[4] = { 1, 1, 1, 0.2f };
        glUniform4fv(glGetUniformLocation(s->prog_text, "uCol"), 1, white);
        glUniform1f(glGetUniformLocation(s->prog_text, "uUseTex"), 0.f);
        glUniform4fv(glGetUniformLocation(s->prog_text, "uDisp"), (s->nfg_sets + 3) / 4, s->disp_fg);
        glBindVertexArray(s->vao_panel);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, s->n_panel);
        glUniform1f(glGetUniformLocation(s->prog_text, "uUseTex"), 1.f);
        /* small text */
        glUniform4fv(glGetUniformLocation(s->prog_text, "uCol"), 1, s->tower_color);
        glUniform4fv(glGetUniformLocation(s->prog_text, "uDisp"), (s->nbg_sets + 3) / 4, s->disp_bg);
        glBindTexture(GL_TEXTURE_2D, s->tex[0]);
        glBindVertexArray(s->vao_bg);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, s->n_bg);
        /* big text */
        glUniform4fv(glGetUniformLocation(s->prog_text, "uCol"), 1, s->tower_color2);
        glUniform4fv(glGetUniformLocation(s->prog_text, "uDisp"), (s->nfg_sets + 3) / 4, s->disp_fg);
        glBindTexture(GL_TEXTURE_2D, s->tex[1]);
        glBindVertexArray(s->vao_fg);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, s->n_fg);
    }

    /* billboard */
    if (s->billboard_text) {
        int i = s->bb_index;
        float w = s->bb_w[i], h = s->bb_h[i];
        float sc = 1.0f / w * 0.95f, margin = w * 0.1f, margin2 = margin * 1.7f;
        cs_mat4 B = cs_translate(-0.5f, s->billboard_y, s->gh * 0.3f);
        B = cs_mul(B, cs_rotate(90, 1, 0, 0));
        B = cs_mul(B, cs_scale(sc, sc * 1.5f, sc));
        glUseProgram(s->prog_bb);
        glUniformMatrix4fv(glGetUniformLocation(s->prog_bb, "uVP"), 1, GL_FALSE, VP.m);
        glUniformMatrix4fv(glGetUniformLocation(s->prog_bb, "uM"), 1, GL_FALSE, B.m);
        float c[4] = { s->tower_color2[0], s->tower_color2[1], s->tower_color2[2], 0.6f };
        glUniform4fv(glGetUniformLocation(s->prog_bb, "uCol"), 1, c);
        glUniform1f(glGetUniformLocation(s->prog_bb, "uPanel"), 1.f);
        glUniform4f(glGetUniformLocation(s->prog_bb, "uRect"), -margin, -margin2, w + margin, h + margin2);
        glUniform4f(glGetUniformLocation(s->prog_bb, "uUV"), 0, 0, 1, 1);
        glBindVertexArray(s->vao_e);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
        if (s->do_tex) {
            c[3] = 1;
            glUniform4fv(glGetUniformLocation(s->prog_bb, "uCol"), 1, c);
            glUniform1f(glGetUniformLocation(s->prog_bb, "uPanel"), 0.f);
            glUniform4f(glGetUniformLocation(s->prog_bb, "uRect"), 0, 0, w, h);
            glUniform4f(glGetUniformLocation(s->prog_bb, "uUV"), s->bb_u1[i], s->bb_v1[i], s->bb_u2[i], s->bb_v2[i]);
            glUniform1i(glGetUniformLocation(s->prog_bb, "uTex"), 0);
            glBindTexture(GL_TEXTURE_2D, s->tex[2]);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
        }
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    if (enh) cs_post_end(&s->post, s->bloom * 0.3f, 0.9f, 0.4f, s->aa, 0);
}

static void free_gibson(ModeInfo *mi) {
    gstate *s = G; if (!s) return;
    GLuint pr[] = { s->prog_body, s->prog_text, s->prog_ground, s->prog_black, s->prog_bb };
    for (int i = 0; i < 5; i++) if (pr[i]) glDeleteProgram(pr[i]);
    GLuint va[] = { s->vao_body, s->vao_ground, s->vao_bg, s->vao_fg, s->vao_panel, s->vao_e };
    for (int i = 0; i < 6; i++) if (va[i]) glDeleteVertexArrays(1, &va[i]);
    GLuint vb[] = { s->vbo_body, s->vbo_ground, s->vbo_bg, s->vbo_fg, s->vbo_panel };
    glDeleteBuffers(5, vb);
    glDeleteTextures(3, s->tex);
    if (s->post_ok) cs_post_free(&s->post);
    if (s->rot) free_rotator(s->rot);
    if (s->rot2) free_rotator(s->rot2);
    free(s); G = NULL;
}
static Bool gibson_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_gibson(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt gibson_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table gibson_xscreensaver_function_table = {
    .name = "gibson", .class_ = "Gibson",
    .init_cb = init_gibson, .draw_cb = draw_gibson, .reshape_cb = reshape_gibson,
    .event_cb = gibson_handle_event, .free_cb = free_gibson, .release_cb = release_gibson,
    .opts = &gibson_opts, .defaults_str = "",
};
