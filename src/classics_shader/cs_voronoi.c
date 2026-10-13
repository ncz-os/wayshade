/* cs_voronoi.c - shader-engine port of xscreensaver "voronoi".
 *
 * voronoi, Copyright (c) 2007-2018 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * What this port keeps: the sim (sites appear near the middle in bursts, drift
 * with slowly growing random acceleration, the whole field zooms outward every
 * zoom-delay seconds so old sites leave and new ones are added), the random
 * smooth colormap, the flat colored cells (each pixel takes the color of its
 * nearest site, measured in the same normalized 0..1 x 0..1 space, so cell
 * shapes are stretched with the screen exactly as before) and the small
 * rotating five-spike marker on each site.
 *
 * What changed: no immediate mode.  classic style draws each site as an
 * instanced cone (the GL1 original drew each cone with glBegin) into the depth
 * buffer, one draw call.  enhanced style computes the cells in a fragment
 * shader from a uniform buffer of sites, which also gives the distance to the
 * nearest cell border, so borders are anti-aliased, glow, and new sites fade
 * in from the color of the cell they were born in.
 */
#include "cs_common.h"

#define MAXN 128
#define STEP_DT 0.02   /* the original ran at delay 20000 us = 50 steps/s */

typedef struct {
    float x, y, dx, dy, ddx, ddy;
    float col[3], col2[3], borrow[3];
    int rot;
    float birth;         /* sim time of creation */
} vnode;

typedef struct {
    int style;           /* 0 classic, 1 enhanced */
    int npoints; float point_size, point_speed, point_delay, zoom_speed, zoom_delay;
    float speed;
    cs_rng rng;
    cs_clock clk;
    double simt;
    vnode nodes[MAXN + 8]; int n;
    enum { M_WAITING, M_ADDING, M_ZOOMING } mode;
    int adding; double last_time;
    float zooming, zoom_toward[2];
    XColor *colors; int ncolors;
    int w, h;
    GLuint prog_cone, prog_star, prog_cells, prog_glow;
    GLuint vao_cone, vao_star, vao_empty, vbo_cone, vbo_star, vbo_inst[3], ubo;
    int inst_ring;
    GLint u_cone_off, u_star_res, u_star_size, u_cells_res, u_cells_n, u_cells_t;
} vstate;
static vstate *V;

/* ---------------- options ---------------- */
static const ncz_opt_def VOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"points", NCZ_OPT_INT, "25", 3, 100, NULL, NULL, NULL, "Points per burst",
     "How many sites are added each time the field restarts (xscreensaver -points).", "Motion"},
    {"point-size", NCZ_OPT_FLOAT, "9", 0, 40, NULL, NULL, NULL, "Marker size",
     "Size of the rotating markers in pixels at 1080 lines; 0 hides them (xscreensaver -point-size).", "Look"},
    {"point-speed", NCZ_OPT_FLOAT, "1", 0, 10, NULL, NULL, NULL, "Drift",
     "How fast the sites accelerate while drifting (xscreensaver -point-speed).", "Motion"},
    {"point-delay", NCZ_OPT_FLOAT, "0.05", 0.01, 2, NULL, NULL, NULL, "Add delay (s)",
     "Seconds between new sites while a burst is being added (xscreensaver -point-delay).", "Motion"},
    {"zoom-speed", NCZ_OPT_FLOAT, "1", 0.1, 5, NULL, NULL, NULL, "Zoom speed",
     "Speed of the outward zoom (xscreensaver -zoom-speed).", "Motion"},
    {"zoom-delay", NCZ_OPT_FLOAT, "15", 2, 300, NULL, NULL, NULL, "Zoom delay (s)",
     "Seconds of drifting between zooms (xscreensaver -zoom-delay).", "Motion"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof VOPTS / sizeof VOPTS[0]; *prefix = "NCZ_VORONOI_"; *group = "voronoi"; return VOPTS;
}

/* ---------------- sim (mirrors voronoi.c) ---------------- */
static vnode *add_node(vstate *s, float x, float y) {
    if (s->n >= MAXN) {                       /* drop the oldest (index 0) */
        memmove(&s->nodes[0], &s->nodes[1], (MAXN - 1) * sizeof s->nodes[0]);
        s->n = MAXN - 1;
    }
    vnode *nn = &s->nodes[s->n++];
    memset(nn, 0, sizeof *nn);
    nn->x = x; nn->y = y;
    int i = cs_rand_int(&s->rng, s->ncolors);
    for (int k = 0; k < 3; k++) {
        float c = (k == 0 ? s->colors[i].red : k == 1 ? s->colors[i].green : s->colors[i].blue) / 65536.0f;
        nn->col[k] = c; nn->col2[k] = c * 0.7f;
    }
    /* enhanced: fade in from the color of the cell the site is born inside */
    float best = 1e9f; nn->borrow[0] = nn->col[0]; nn->borrow[1] = nn->col[1]; nn->borrow[2] = nn->col[2];
    for (int j = 0; j < s->n - 1; j++) {
        float dx = s->nodes[j].x - x, dy = s->nodes[j].y - y, d = dx * dx + dy * dy;
        if (d < best) { best = d; memcpy(nn->borrow, s->nodes[j].col, sizeof nn->borrow); }
    }
    nn->ddx = cs_rand(&s->rng) * 0.000001f * s->point_speed * cs_rand_sign(&s->rng);
    nn->ddy = cs_rand(&s->rng) * 0.000001f * s->point_speed * cs_rand_sign(&s->rng);
    nn->rot = cs_rand_int(&s->rng, 360) * cs_rand_sign(&s->rng);
    nn->birth = (float)s->simt;
    return nn;
}

static void step_sim(vstate *s) {
    s->simt += STEP_DT;
    /* move_points */
    for (int i = 0; i < s->n; i++) {
        vnode *nn = &s->nodes[i];
        nn->x += nn->dx; nn->y += nn->dy;
        if (s->mode == M_WAITING) { nn->dx += nn->ddx; nn->dy += nn->ddy; }
        nn->rot += (nn->rot < 0 ? -1 : 1);
    }
    /* prune_points */
    for (int i = 0; i < s->n; ) {
        vnode *nn = &s->nodes[i];
        if (nn->x < -5 || nn->x > 5 || nn->y < -5 || nn->y > 5) {
            memmove(nn, nn + 1, (s->n - i - 1) * sizeof *nn); s->n--;
        } else i++;
    }
    /* state_change */
    double now = s->simt;
    switch (s->mode) {
    case M_WAITING:
        if (s->last_time + s->zoom_delay <= now) {
            vnode *tn = s->n ? &s->nodes[s->n - 1] : NULL;   /* original: list head = newest */
            s->zoom_toward[0] = tn ? tn->x : 0.5f; s->zoom_toward[1] = tn ? tn->y : 0.5f;
            s->mode = M_ZOOMING; s->zooming = 1; s->last_time = now;
        }
        break;
    case M_ADDING:
        if (s->last_time + s->point_delay <= now) {
            add_node(s, cs_bellrand(&s->rng, 0.5f) + 0.25f, cs_bellrand(&s->rng, 0.5f) + 0.25f);
            s->last_time = now;
            if (--s->adding <= 0) { s->adding = 0; s->mode = M_WAITING; s->last_time = now; }
        }
        break;
    case M_ZOOMING: {
        float tick = sinf(s->zooming * (float)M_PI);
        float scale = 1 + tick * 0.02f * s->zoom_speed;
        s->zooming -= 0.01f * s->zoom_speed;
        if (s->zooming < 0) s->zooming = 0;
        if (s->zooming > 0) {
            if (scale < 1) scale = 1;
            for (int i = 0; i < s->n; i++) {
                float x = (s->nodes[i].x - s->zoom_toward[0]) * scale, y = (s->nodes[i].y - s->zoom_toward[1]) * scale;
                s->nodes[i].x = x + s->zoom_toward[0]; s->nodes[i].y = y + s->zoom_toward[1];
            }
        }
        if (s->zooming <= 0) { s->mode = M_ADDING; s->adding = s->npoints; s->last_time = now; }
        break; }
    }
}

/* ---------------- shaders ---------------- */
static const char *VS_CONE = CS_GLSL(
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec4 aSite;      /* x, y, rot, - */
    layout(location = 2) in vec4 aCol;
    flat out vec3 vCol;
    void main() {
        /* glTranslatef(x,y,0) glScalef(10,10,1) then glOrtho(0,1,1,0,-1,1) */
        vec2 p = aSite.xy + aPos.xy * 10.0;
        gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, -aPos.z, 1.0);
        vCol = aCol.rgb;
    });
static const char *FS_FLAT = CS_GLSL(
    flat in vec3 vCol; out vec4 o;
    void main() { o = vec4(vCol, 1.0); });

static const char *VS_STAR = CS_GLSL(
    layout(location = 0) in vec3 aV;         /* x, y in marker units; z = 0 */
    layout(location = 1) in vec4 aSite;
    layout(location = 3) in vec4 aCol2;
    layout(location = 4) in vec3 aBary;
    uniform vec2 uRes; uniform float uSize;
    out vec3 vBary; flat out vec3 vCol;
    void main() {
        float a = radians(aSite.z + 180.0);
        float c = cos(a), s = sin(a);
        vec2 r = vec2(c * aV.x - s * aV.y, s * aV.x + c * aV.y);
        vec2 p = aSite.xy + r * uSize / uRes;
        gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
        vCol = aCol2.rgb; vBary = aBary;
    });
static const char *FS_STAR = CS_GLSL(
    in vec3 vBary; flat in vec3 vCol; out vec4 o; uniform float uAA;
    void main() {
        float e = min(min(vBary.x, vBary.y), vBary.z);
        float a = uAA > 0.5 ? smoothstep(0.0, max(fwidth(e) * 1.25, 1e-5), e) : 1.0;
        o = vec4(vCol, a);
    });

static const char *FS_CELLS_HEAD = "#define MAXN 128\nlayout(std140) uniform Sites { vec4 sPos[MAXN]; vec4 sCol[MAXN]; };\n"
    "uniform int uN; uniform vec2 uRes; uniform float uBloom; uniform float uAA;\n";
static const char *FS_CELLS_BODY = CS_GLSL(
    in vec2 vUv; out vec4 o;
    void main() {
        vec2 p = vec2(vUv.x, 1.0 - vUv.y);
        float d1 = 1e9; int i1 = 0;
        for (int i = 0; i < uN; i++) { vec2 q = p - sPos[i].xy; float d = dot(q, q); if (d < d1) { d1 = d; i1 = i; } }
        vec2 a = sPos[i1].xy;
        float e = 1e9; vec2 nb = vec2(0.0); int i2 = i1;
        for (int i = 0; i < uN; i++) {
            if (i == i1) continue;
            vec2 n = sPos[i].xy - a; float l = max(length(n), 1e-6);
            float ee = dot(0.5 * (a + sPos[i].xy) - p, n) / l;
            if (ee < e) { e = ee; i2 = i; nb = n / l; }
        }
        float epx = e / max(length(nb / uRes), 1e-9);              /* pixels to the border */
        vec3 ca = sCol[i1].rgb, cb = sCol[i2].rgb;
        float d = sqrt(d1);
        vec3 fill = ca * (1.02 - 0.55 * d) + ca * 0.22 * exp(-d * 9.0);
        float fade = min(sPos[i1].z, sPos[i2].z);                  /* 0 while newborn */
        float lw = 1.1 * uRes.y / 1080.0 + 0.6;
        float line = (uAA > 0.5 ? 1.0 - smoothstep(lw - 0.75, lw + 0.75, epx) : step(epx, lw)) * fade;
        float halo = exp(-epx / (14.0 * uRes.y / 1080.0)) * 0.35 * uBloom * fade;
        vec3 bc = mix(ca, cb, 0.5) * 1.5 + 0.18;
        vec3 col = fill + bc * halo * 0.6;
        col = mix(col, bc, line * 0.85);
        col += ca * 0.18 * exp(-d * 60.0) * uBloom;                /* glow at the site */
        o = vec4(cs_dither(col, gl_FragCoord.xy), 1.0);
    });

/* ---------------- GL objects ---------------- */
static void build_geometry(vstate *s) {
    /* cone: apex (0,0,1), rim at z=0, 64 faces, fan of 66 vertices */
    float cone[66 * 3]; int k = 0;
    cone[k++] = 0; cone[k++] = 0; cone[k++] = 1;
    for (int i = 0; i <= 64; i++) { float th = (float)(2 * M_PI * i / 64); cone[k++] = cosf(th); cone[k++] = sinf(th); cone[k++] = 0; }
    /* star: 5 triangles, apex (0,1), base (-0.2,0) (0.2,0), rotated 72 deg steps */
    float star[15 * 6]; k = 0;
    for (int t = 0; t < 5; t++) {
        float a = (float)(t * 72.0 * M_PI / 180.0), c = cosf(a), sn = sinf(a);
        float v[3][2] = {{0, 1}, {-0.2f, 0}, {0.2f, 0}};
        float bary[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int j = 0; j < 3; j++) {
            star[k++] = c * v[j][0] - sn * v[j][1]; star[k++] = sn * v[j][0] + c * v[j][1]; star[k++] = 0;
            star[k++] = bary[j][0]; star[k++] = bary[j][1]; star[k++] = bary[j][2];
        }
    }
    glGenBuffers(1, &s->vbo_cone); glBindBuffer(GL_ARRAY_BUFFER, s->vbo_cone);
    glBufferData(GL_ARRAY_BUFFER, sizeof cone, cone, GL_STATIC_DRAW);
    glGenBuffers(1, &s->vbo_star); glBindBuffer(GL_ARRAY_BUFFER, s->vbo_star);
    glBufferData(GL_ARRAY_BUFFER, sizeof star, star, GL_STATIC_DRAW);
    glGenBuffers(3, s->vbo_inst);
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[i]);
        glBufferData(GL_ARRAY_BUFFER, MAXN * 12 * sizeof(float), NULL, GL_STREAM_DRAW);
    }
    glGenBuffers(1, &s->ubo); glBindBuffer(GL_UNIFORM_BUFFER, s->ubo);
    glBufferData(GL_UNIFORM_BUFFER, MAXN * 8 * sizeof(float), NULL, GL_STREAM_DRAW);
    glGenVertexArrays(1, &s->vao_empty);
    glGenVertexArrays(1, &s->vao_cone); glGenVertexArrays(1, &s->vao_star);
}
static void bind_instance_attribs(vstate *s, GLuint vao, GLuint vbo_static, int star) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_static);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, star ? 6 * sizeof(float) : 3 * sizeof(float), (void *)0);
    if (star) { glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float))); }
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[s->inst_ring]);
    GLsizei stride = 12 * sizeof(float);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void *)0);  glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float))); glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float))); glVertexAttribDivisor(3, 1);
}

static void init_voronoi(ModeInfo *mi) {
    vstate *s = calloc(1, sizeof *s); V = s;
    s->style = cs_style();
    s->npoints = (int)cs_opt_i("points", 25);
    s->point_size = (float)cs_opt_f("point-size", 9);
    s->point_speed = (float)cs_opt_f("point-speed", 1);
    s->point_delay = (float)cs_opt_f("point-delay", 0.05);
    s->zoom_speed = (float)cs_opt_f("zoom-speed", 1);
    s->zoom_delay = (float)cs_opt_f("zoom-delay", 15);
    s->speed = (float)cs_opt_f("speed", 1);
    uint32_t seed = cs_seed_from_options();
    cs_rng_seed(&s->rng, seed); srandom(seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    s->ncolors = 128; s->colors = calloc(s->ncolors, sizeof(XColor));
    make_smooth_colormap(0, 0, 0, s->colors, &s->ncolors, False, False, False);
    build_geometry(s);
    bind_instance_attribs(s, s->vao_cone, s->vbo_cone, 0);
    bind_instance_attribs(s, s->vao_star, s->vbo_star, 1);
    glBindVertexArray(0);
    s->prog_cone = cs_program(VS_CONE, FS_FLAT, "voronoi cone");
    s->prog_star = cs_program(VS_STAR, FS_STAR, "voronoi star");
    if (s->style) {
        char fs[4096];
        snprintf(fs, sizeof fs, "%s%s%s", FS_CELLS_HEAD, CS_GLSL_COMMON, FS_CELLS_BODY);
        s->prog_cells = cs_program(CS_FULLSCREEN_VS, fs, "voronoi cells");
        if (!s->prog_cells) s->style = 0;                      /* fall back, already logged */
        else {
            GLuint bi = glGetUniformBlockIndex(s->prog_cells, "Sites");
            if (bi != GL_INVALID_INDEX) glUniformBlockBinding(s->prog_cells, bi, 0); else s->style = 0;
            s->u_cells_res = glGetUniformLocation(s->prog_cells, "uRes");
            s->u_cells_n = glGetUniformLocation(s->prog_cells, "uN");
        }
    }
    s->u_star_res = glGetUniformLocation(s->prog_star, "uRes");
    s->u_star_size = glGetUniformLocation(s->prog_star, "uSize");
    s->mode = M_ADDING; s->adding = s->npoints * 2; s->last_time = 0;
    fprintf(stderr, "[diag] voronoi: style=%s seed=%u points=%d\n", s->style ? "enhanced" : "classic", seed, s->npoints);
}

static void reshape_voronoi(ModeInfo *mi, int w, int h) {
    if (!V) return;
    V->w = w; V->h = h;
    glViewport(0, 0, w, h);
}

static void draw_voronoi(ModeInfo *mi) {
    vstate *s = V;
    if (!s || !s->prog_cone) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) step_sim(s);

    /* pack instance data: [x y rot 0][col 1][col2 1] */
    float inst[MAXN * 12]; int n = s->n;
    float ubo[MAXN * 8];
    for (int i = 0; i < n; i++) {
        vnode *nn = &s->nodes[n - 1 - i];        /* newest first, as the GL1 list order (matters only for ties) */
        float *q = inst + i * 12;
        q[0] = nn->x; q[1] = nn->y; q[2] = (float)nn->rot; q[3] = 0;
        q[4] = nn->col[0]; q[5] = nn->col[1]; q[6] = nn->col[2]; q[7] = 1;
        q[8] = nn->col2[0]; q[9] = nn->col2[1]; q[10] = nn->col2[2]; q[11] = 1;
        float age = (float)(s->simt - nn->birth), f = age / 0.9f; f = f > 1 ? 1 : f; f = f * f * (3 - 2 * f);
        float *u = ubo + i * 4, *c = ubo + MAXN * 4 + i * 4;
        u[0] = nn->x; u[1] = nn->y; u[2] = f; u[3] = 0;
        for (int k = 0; k < 3; k++) c[k] = nn->borrow[k] + (nn->col[k] - nn->borrow[k]) * f;
        c[3] = 1;
    }
    s->inst_ring = (s->inst_ring + 1) % 3;
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[s->inst_ring]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, n * 12 * sizeof(float), inst);
    bind_instance_attribs(s, s->vao_cone, s->vbo_cone, 0);
    bind_instance_attribs(s, s->vao_star, s->vbo_star, 1);

    glDisable(GL_BLEND);
    if (s->style) {
        glDisable(GL_DEPTH_TEST);
        glBindBuffer(GL_UNIFORM_BUFFER, s->ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof ubo, ubo);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, s->ubo);
        glUseProgram(s->prog_cells);
        glUniform2f(s->u_cells_res, (float)s->w, (float)s->h);
        glUniform1i(s->u_cells_n, n);
        glUniform1f(glGetUniformLocation(s->prog_cells, "uBloom"), (float)cs_opt_f("bloom", 1));
        glUniform1f(glGetUniformLocation(s->prog_cells, "uAA"), cs_opt_b("antialias", 1) ? 1.f : 0.f);
        glBindVertexArray(s->vao_empty);
        cs_gl_check("voronoi pre-cells");
        if (n > 0) glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1); else glClear(GL_COLOR_BUFFER_BIT);
        cs_gl_check("voronoi cells");
    } else {
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(s->prog_cone);
        glBindVertexArray(s->vao_cone);
        if (n > 0) glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 66, n);
    }
    if (s->point_size > 0 && n > 0 && s->prog_star) {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(s->prog_star);
        float sz = s->point_size * (s->h / 1080.0f);
        if (sz < 4) sz = 4;
        glUniform2f(s->u_star_res, (float)s->w, (float)s->h);
        glUniform1f(s->u_star_size, sz);
        glUniform1f(glGetUniformLocation(s->prog_star, "uAA"), s->style && cs_opt_b("antialias", 1) ? 1.f : 0.f);
        glBindVertexArray(s->vao_star);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 15, n);
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
}

static void free_voronoi(ModeInfo *mi) {
    vstate *s = V; if (!s) return;
    glDeleteProgram(s->prog_cone); glDeleteProgram(s->prog_star); if (s->prog_cells) glDeleteProgram(s->prog_cells);
    glDeleteBuffers(1, &s->vbo_cone); glDeleteBuffers(1, &s->vbo_star); glDeleteBuffers(3, s->vbo_inst); glDeleteBuffers(1, &s->ubo);
    glDeleteVertexArrays(1, &s->vao_cone); glDeleteVertexArrays(1, &s->vao_star); glDeleteVertexArrays(1, &s->vao_empty);
    free(s->colors); free(s); V = NULL;
}
static Bool voronoi_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_voronoi(ModeInfo *mi) { (void)mi; }

static ModeSpecOpt voronoi_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table voronoi_xscreensaver_function_table = {
    .name = "voronoi", .class_ = "Voronoi",
    .init_cb = init_voronoi, .draw_cb = draw_voronoi, .reshape_cb = reshape_voronoi,
    .event_cb = voronoi_handle_event, .free_cb = free_voronoi, .release_cb = release_voronoi,
    .opts = &voronoi_opts, .defaults_str = "",
};
