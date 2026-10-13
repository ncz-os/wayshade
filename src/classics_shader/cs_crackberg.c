/* cs_crackberg.c - shader-engine port of xscreensaver "crackberg".
 *
 * crackberg; Matus Telgarsky [ catachresis@cmu.edu ] 2005
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Kept line for line from the original: the triangular-tile ("trile") terrain
 * generator with midpoint displacement and shared edges, the grow / fall /
 * yeast morphs that build and dismantle tiles at the edge of the view, the
 * visibility walk, the drunken camera, the four color schemes (plain, ice,
 * magma, vomit) with their water/lava, flat per-triangle lighting.
 *
 * Replaced: each tile used to be compiled into a display list with glBegin.
 * A tile is now generated once into a static vertex buffer (position, color,
 * normal, water flag) and drawn with one call and a model matrix uniform;
 * the two-sided light of the original runs in the fragment shader.
 * enhanced style adds distance fog into a sky gradient, a fill light so the
 * back-lit slopes keep their shape, glossy water and glowing lava, and FXAA.
 */
#include "cs_common.h"
#include "cs_post.h"

#ifndef RAND_MAX
#define RAND_MAX 2147483647
#endif
#ifndef countof
#define countof(x) ((int)(sizeof(x) / sizeof((x)[0])))
#endif

#define DEF_NSUBDIVS   "4"
#define DEF_BORING     "False"
#define DEF_CRACK      "True"
#define DEF_WATER      "True"
#define DEF_FLAT       "True"
#define DEF_COLOR      "random"
#define DEF_LIT        "True"
#define DEF_VISIBILITY "0.6"
#define DEF_LETTERBOX  "False"

/***************************
 ** macros
 ** */

#define M_RAD7_4        0.661437827766148
#define M_SQRT3_2       0.866025403784439
#define M_PI_180        0.0174532925199433
#define M_180_PI        57.2957795130823
#define MSPEED_SCALE    1.1
#define AVE3(a,b,c)     ( ((a) + (b) + (c)) / 3.0 )
#define MAX_ZDELTA      0.35
#define DISPLACE(h,d)   (h+(random()/(double)RAND_MAX-0.5)*2*MAX_ZDELTA/(1<<d))
#define MEAN(x,y)       ( ((x) + (y)) / 2.0 )
#define TCOORD(x,y)     (cberg->heights[(cberg->epoints * (y) - ((y)-1)*(y)/2 + (x))])
#define sNCOORD(x,y,p)  (cberg->norms[3 * (cberg->epoints * (y) - ((y)-1)*(y)/2 + (x)) + (p)])
#define SET_sNCOORD(x,y, down, a,b,c,d,e,f)   \
    sNCOORD(x,y,0) = AVE3(a-d, 0.5 * (b-e), -0.5 * (c-f)); \
    sNCOORD(x,y,1) = ((down) ? -1 : +1) * AVE3(0.0, M_SQRT3_2 * (b-e), M_SQRT3_2 * (c-f)); \
    sNCOORD(x,y,2) = (2*dx)
#define fNCOORD(x,y,w,p)  \
    (cberg->norms[3 * (2*(y)*cberg->epoints-((y)+1)*((y)+1) + 1 + 2 * ((x)-1) + (w)) + (p)])
#define SET_fNCOORDa(x,y, down, dz00,dz01) \
    fNCOORD(x,y,0,0) = (down) * (dy) * (dz01); \
    fNCOORD(x,y,0,1) = (down) * ((dz01) * (dx) / 2 - (dx) * (dz00)); \
    fNCOORD(x,y,0,2) = (down) * (dx) * (dy)
#define SET_fNCOORDb(x,y, down, dz10,dz11) \
    fNCOORD(x,y,1,0) = (down) * (dy) * (dz10); \
    fNCOORD(x,y,1,1) = (down) * ((dz11) * (dx) - (dx) * (dz10) / 2); \
    fNCOORD(x,y,1,2) = (down) * (dx) * (dy)



typedef struct _cberg_state cberg_state;
typedef struct _Trile Trile;
typedef struct {
    void (*init)(Trile *);
    void (*free)(Trile *);
    void (*draw)(Trile *);
    void (*init_iter)(Trile *, cberg_state *);
    void (*dying_iter)(Trile *, cberg_state *);
} Morph;
typedef struct {
    char *id;
    void (*land)(cberg_state *, double, float *);
    void (*water)(cberg_state *, double, float *);
    double bg[4];
} Color;
enum { TRILE_NEW, TRILE_INIT, TRILE_STABLE, TRILE_DYING, TRILE_DELETE };
struct _Trile {
    int x,y; /*center coords; points up if (x+y)%2 == 0, else down*/
    short state;
    short visible;
    double *l,*r,*v; /*only edges need saving*/
    GLuint vao, vbo; int nverts;
    void *morph_data;
    const Morph *morph;
    struct _Trile *left, *right, *parent; /* for bst, NOT spatial */
    struct _Trile *next_free, *next0; /* for memory allocation */
};
enum { MOTION_AUTO = 0, MOTION_MANUAL = 1 };
struct _cberg_state {
    Trile *trile_head;
    double x,y,z, yaw,roll,pitch, dx,dy,dz, dyaw,droll,dpitch, elapsed;
    double prev_frame;
    int motion_state;
    double mspeed;
    double fovy, aspect, zNear, zFar;
    const Color *color;
    int count;
    unsigned int epoints, tpoints, ntris, tnorms;
    double *heights, *norms;
    Trile *free_head;
    Trile *all_triles;
    double draw_elapsed;
    double dx0;
    double vs0r,vs0g,vs0b, vs1r, vs1g, vs1b,
           vf0r,vf0g,vf0b, vf1r, vf1g, vf1b;
    float bgc[3];
    /* port state */
    int style, w, h; float speed, bloom; int aa;
    cs_rng rng;
    cs_post post; int post_ok;
    GLuint prog, prog_sky, vao_empty;
    cs_mat4 P, V;
};

static unsigned int nsubdivs;
static int crack, boring, do_water, flat, lit, letterbox;
static float visibility;
static char *color;
static cberg_state *CB;
static double g_now_scale = 1.0;

static const ncz_opt_def CBOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"nsubdivs", NCZ_OPT_INT, "4", 1, 5, NULL, NULL, NULL, "Tile detail",
     "Subdivisions per terrain tile; each step quadruples the triangles (xscreensaver -nsubdivs).", "Density"},
    {"visibility", NCZ_OPT_FLOAT, "0.6", 0.2, 1.0, NULL, NULL, NULL, "Visibility",
     "How far ahead terrain is generated; larger sees farther but draws more (xscreensaver -visibility).", "Density"},
    {"scheme", NCZ_OPT_ENUM, "random", 0, 0, "random,plain,ice,magma,vomit", NULL, NULL, "Color scheme",
     "Terrain color scheme (xscreensaver -color).", "Look"},
    {"crack", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Crack",
     "Tiles grow, fall or spin into place instead of appearing at once (xscreensaver -crack).", "Motion"},
    {"water", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Water", "Flood the low ground (xscreensaver -water).", "Look"},
    {"flat", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Flat shading",
     "One lighting value per triangle, for the faceted look (xscreensaver -flat).", "Look"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof CBOPTS / sizeof CBOPTS[0]; *prefix = "NCZ_CRACKBERG_"; *group = "crackberg"; return CBOPTS;
}

/* forward decls for trile_new */
static Trile *triles_find(Trile *tr, int x, int y);
static Trile *trile_alloc(cberg_state *cberg);
static const Morph *select_morph(void);

static void trile_calc_sides(cberg_state *cberg, 
                             Trile *new, int x, int y, Trile *root)
{
    unsigned int i,j,k; 
    int dv = ( (x + y) % 2 ? +1 : -1); /* we are pointing down or up*/
    Trile *l, *r, *v; /* v_ertical */


    if (root) {
        l = triles_find(root, x-1, y);
        r = triles_find(root, x+1, y);  
        v = triles_find(root, x,y+dv); 
    } else
        l = r = v = NULL;

    if (v) {
        for (i = 0; i != cberg->epoints; ++i)
            new->v[i] = v->v[i];
    } else {
        if (l)          new->v[0] = l->l[0];
        else if (!root) new->v[0] = DISPLACE(0,0);
        else { 
            Trile *tr; /* all of these tests needed.. */
            if ( (tr = triles_find(root, x-1, y + dv)) )
                new->v[0] = tr->l[0];
            else if ( (tr = triles_find(root, x-2, y)) )
                new->v[0] = tr->r[0];
            else if ( (tr = triles_find(root, x-2, y + dv)) )
                new->v[0] = tr->r[0];
            else
                new->v[0] = DISPLACE(0,0);
        }

        if (r)          new->v[cberg->epoints-1] = r->l[0];
        else if (!root) new->v[cberg->epoints-1] = DISPLACE(0,0);
        else {
            Trile *tr;
            if ( (tr = triles_find(root, x+1, y + dv)) )
                new->v[cberg->epoints-1] = tr->l[0];
            else if ( (tr = triles_find(root, x+2, y)) )
                new->v[cberg->epoints-1] = tr->v[0];
            else if ( (tr = triles_find(root, x+2, y + dv)) )
                new->v[cberg->epoints-1] = tr->v[0];
            else
                new->v[cberg->epoints-1] = DISPLACE(0,0);
        }

        for (i = ((1 << nsubdivs) >> 1), k =1; i; i >>= 1, ++k)
            for (j = i; j < cberg->epoints; j += i * 2)
                new->v[j] = DISPLACE(MEAN(new->v[j-i], new->v[j+i]), k);
    }
        
    if (l) {
        for (i = 0; i != cberg->epoints; ++i)
            new->l[i] = l->r[i];
    } else {
        if (r)          new->l[0] = r->v[0];
        else if (!root) new->l[0] = DISPLACE(0,0);
        else {
            Trile *tr;
            if ( (tr = triles_find(root, x-1, y-dv)) )
                new->l[0] = tr->r[0];
            else if ( (tr = triles_find(root, x+1, y-dv)) )
                new->l[0] = tr->v[0];
            else if ( (tr = triles_find(root, x, y-dv)) )
                new->l[0] = tr->l[0];
            else 
                new->l[0] = DISPLACE(0,0);
        }

        new->l[cberg->epoints - 1] = new->v[0];

        for (i = ((1 << nsubdivs) >> 1), k =1; i; i >>= 1, ++k)
            for (j = i; j < cberg->epoints; j += i * 2)
                new->l[j] = DISPLACE(MEAN(new->l[j-i], new->l[j+i]), k);
    }

    if (r) {
        for (i = 0; i != cberg->epoints; ++i)
            new->r[i] = r->l[i];
    } else {
        new->r[0] = new->v[cberg->epoints - 1];
        new->r[cberg->epoints - 1] = new->l[0];

        for (i = ((1 << nsubdivs) >> 1), k =1; i; i >>= 1, ++k)
            for (j = i; j < cberg->epoints; j += i * 2)
                new->r[j] = DISPLACE(MEAN(new->r[j-i], new->r[j+i]), k);
    }
}

static void trile_calc_heights(cberg_state *cberg, Trile *new)
{
    unsigned int i, j, k, h;

    for (i = 0; i < cberg->epoints - 1; ++i) { /* copy in sides */
        TCOORD(i,0) = new->v[i];
        TCOORD(cberg->epoints - 1 - i, i) = new->r[i];
        TCOORD(0, cberg->epoints - 1 - i) = new->l[i];
    }

    for (i = ((1 << nsubdivs) >> 2), k =1; i; i >>= 1, ++k)
        for (j = 1; j < (1 << k); ++j)
            for (h = 1; h <= (1<<k) - j; ++h) {
                TCOORD( i*(2*h - 1), i*(2*j - 1) ) = /*rights*/
                  DISPLACE(MEAN(TCOORD( i*(2*h - 2), i*(2*j + 0) ),
                                TCOORD( i*(2*h + 0), i*(2*j - 2) )), k);

                TCOORD( i*(2*h + 0), i*(2*j - 1) ) = /*lefts*/
                  DISPLACE(MEAN(TCOORD( i*(2*h + 0), i*(2*j - 2) ),
                                TCOORD( i*(2*h + 0), i*(2*j + 0) )), k);

                TCOORD( i*(2*h - 1), i*(2*j + 0) ) = /*verts*/
                  DISPLACE(MEAN(TCOORD( i*(2*h - 2), i*(2*j + 0) ),
                                TCOORD( i*(2*h + 0), i*(2*j + 0) )), k);
            }
}

static void trile_calc_flat_norms(cberg_state *cberg, Trile *new)
{
    unsigned int x, y;
    int down = (((new->x + new->y) % 2) ? -1 : +1);
    double dz00,dz01,dz10,dz11, a,b,c,d;
    double dy = down * M_SQRT3_2 / (1 << nsubdivs);
    double dx = cberg->dx0;

    for (y = 0; y < cberg->epoints - 1; ++y) {
        a = TCOORD(0,y);
        b = TCOORD(0,y+1);
        for (x = 1; x < cberg->epoints - 1 - y; ++x) {
            c = TCOORD(x,y);
            d = TCOORD(x,y+1);

            dz00 = b-c;
            dz01 = a-c;
            dz10 = b-d;
            dz11 = c-d;
            
            SET_fNCOORDa(x,y, down, dz00,dz01);
            SET_fNCOORDb(x,y, down, dz10,dz11);

            a = c;
            b = d;
        }

        c = TCOORD(x,y);
        dz00 = b-c;
        dz01 = a-c;
        SET_fNCOORDa(x,y, down, dz00, dz01);
    }
}

static void trile_calc_smooth_norms(cberg_state *cberg, Trile *new)
{
    unsigned int i,j, down = (new->x + new->y) % 2;
    double prev, cur, next;
    double dx = cberg->dx0;

    /** corners -- assume level (bah) **/
    cur = TCOORD(0,0);
    SET_sNCOORD(0,0, down,
        cur,cur,TCOORD(0,1),TCOORD(1,0),cur,cur);
    cur = TCOORD(cberg->epoints-1,0);
    SET_sNCOORD(cberg->epoints-1,0, down,
        TCOORD(cberg->epoints-2,0),TCOORD(cberg->epoints-2,1),cur,cur,cur,cur);
    cur = TCOORD(0,cberg->epoints-1);
    SET_sNCOORD(0,cberg->epoints-1, down,
        cur,cur,cur,cur,TCOORD(1,cberg->epoints-2),TCOORD(0,cberg->epoints-2));


    /** sides **/
    /* vert */
    prev = TCOORD(0,0);
    cur = TCOORD(1,0);
    for (i = 1; i < cberg->epoints - 1; ++i) {
        next = TCOORD(i+1,0);
        SET_sNCOORD(i,0, down, prev,TCOORD(i-1,1),TCOORD(i,1), next,cur,cur);
        prev = cur;
        cur = next;
    }

    /* right */
    prev = TCOORD(cberg->epoints-1,0);
    cur = TCOORD(cberg->epoints-2,0);
    for (i = 1; i < cberg->epoints - 1; ++i) {
        next = TCOORD(cberg->epoints-i-2,i+1);
        SET_sNCOORD(cberg->epoints-i-1,i, down, TCOORD(cberg->epoints-i-2,i),next,cur,
                                        cur,prev,TCOORD(cberg->epoints-i-1,i-1));
        prev = cur;
        cur = next;
    }
        
    /* left */
    prev = TCOORD(0,0);
    cur = TCOORD(0,1);
    for (i = 1; i < cberg->epoints - 1; ++i) {
        next = TCOORD(0,i+1);
        SET_sNCOORD(0,i, down, cur,cur,next,TCOORD(1,i),TCOORD(1,i-1),prev);
        prev = cur;
        cur = next;
    }


    /** fill in **/
    for (i = 1; i < cberg->epoints - 2; ++i) {
        prev = TCOORD(0,i);
        cur = TCOORD(1,i);
        for (j = 1; j < cberg->epoints - i - 1; ++j) {
            next = TCOORD(j+1,i);
            SET_sNCOORD(j,i, down, prev,TCOORD(j-1,i+1),TCOORD(j,i+1),
                            next,TCOORD(j+1,i-1),TCOORD(j,i-1));
            prev = cur;
            cur = next;
        }
    }
}


/* ---- vertex generation (replaces trile_light / trile_draw_vertex / trile_render) ---- */
typedef struct { float p[3], c[3], n[3], water; } cvert;

static void trile_normal(cberg_state *cberg, unsigned int x, unsigned int y, unsigned int which, float *n)
{
    if (flat) {
        if (x) { n[0] = fNCOORD(x,y,which,0); n[1] = fNCOORD(x,y,which,1); n[2] = fNCOORD(x,y,which,2); }
        else   { n[0] = fNCOORD(1,y,0,0);     n[1] = fNCOORD(1,y,0,1);     n[2] = fNCOORD(1,y,0,2); }
    } else {
        n[0] = sNCOORD(x,y+which,0); n[1] = sNCOORD(x,y+which,1); n[2] = sNCOORD(x,y+which,2);
    }
}

static void make_vertex(cberg_state *cberg, cvert *out, unsigned int ix, unsigned int iy, unsigned int which,
                        double x, double y, double zcur)
{
    if (do_water && zcur <= 0.0) {
        float c[3]; cberg->color->water(cberg, zcur, c);
        memcpy(out->c, c, sizeof c);
        out->n[0] = 0; out->n[1] = 0; out->n[2] = 1;
        out->p[0] = (float)x; out->p[1] = (float)y; out->p[2] = 0; out->water = 1;
    } else {
        float c[3]; cberg->color->land(cberg, zcur, c);
        memcpy(out->c, c, sizeof c);
        if (lit) trile_normal(cberg, ix, iy, which, out->n); else { out->n[0] = 0; out->n[1] = 0; out->n[2] = 1; }
        out->p[0] = (float)x; out->p[1] = (float)y; out->p[2] = (float)zcur; out->water = 0;
    }
}

static void trile_render(cberg_state *cberg, Trile *new)
{
    double cornerx = 0.5 * new->x - 0.5, cornery;
    double dy = M_SQRT3_2 / (1 << nsubdivs);
    double z0,z1,z2;
    unsigned int x, y;
    size_t cap = (size_t)cberg->ntris * 3 + 64;
    cvert *strip = malloc(sizeof(cvert) * (cberg->epoints * 2 + 4));
    cvert *tris = malloc(sizeof(cvert) * cap);
    int nt = 0;
    if ((new->x + new->y) % 2) { cornery = (new->y + 0.5)*M_SQRT3_2; dy = -dy; }
    else cornery = (new->y - 0.5) * M_SQRT3_2;
    for (y = 0; y < cberg->epoints - 1; ++y) {
        double dx = cberg->dx0;
        int ns = 0;
        z0 = TCOORD(0,y); z1 = TCOORD(0,y+1); z2 = TCOORD(1,y);
        make_vertex(cberg, &strip[ns++], 0,y,0, cornerx,cornery, z0);
        make_vertex(cberg, &strip[ns++], 0,y,1, cornerx+0.5*dx,cornery+dy, z1);
        for (x = 1; x < cberg->epoints - 1 - y; ++x) {
            make_vertex(cberg, &strip[ns++], x,y,0, cornerx+x*dx,cornery, z2);
            z0 = TCOORD(x, y+1);
            make_vertex(cberg, &strip[ns++], x,y,1, cornerx+(x+0.5)*dx,cornery+dy, z0);
            z1 = z0; z0 = z2; z2 = TCOORD(x+1,y);
        }
        make_vertex(cberg, &strip[ns++], x,y,0, cornerx + x*dx, cornery, z2);
        /* strip -> triangles; flat: the last (provoking) vertex decides color and normal */
        for (int k = 0; k + 2 < ns; k++) {
            cvert a = strip[k], b = strip[k+1], c = strip[k+2];
            if (k & 1) { cvert t = a; a = b; b = t; }
            int allwater = strip[k].water > 0.5f && strip[k+1].water > 0.5f && strip[k+2].water > 0.5f;
            if (flat && !(cberg->style && allwater)) {   /* enhanced: keep the water depth gradient smooth */
                memcpy(a.c, strip[k+2].c, sizeof a.c); memcpy(b.c, strip[k+2].c, sizeof b.c);
                memcpy(a.n, strip[k+2].n, sizeof a.n); memcpy(b.n, strip[k+2].n, sizeof b.n);
                a.water = b.water = strip[k+2].water;
            }
            if (nt + 3 > (int)cap) break;
            tris[nt++] = a; tris[nt++] = b; tris[nt++] = c;
        }
        cornerx += dx/2;
        cornery += dy;
    }
    if (!new->vao) { glGenVertexArrays(1, &new->vao); glGenBuffers(1, &new->vbo); }
    glBindVertexArray(new->vao);
    glBindBuffer(GL_ARRAY_BUFFER, new->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cvert) * nt, tris, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(cvert), (void *)offsetof(cvert, p));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(cvert), (void *)offsetof(cvert, c));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(cvert), (void *)offsetof(cvert, n));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(cvert), (void *)offsetof(cvert, water));
    glBindVertexArray(0);
    new->nverts = nt;
    free(strip); free(tris);
}
static Trile *trile_new(cberg_state *cberg, int x,int y,Trile *parent,Trile *root)
{
    Trile *new;

    new = trile_alloc(cberg);

    new->x = x;
    new->y = y;
    new->state = TRILE_NEW;
    new->parent = parent;
    new->left = new->right = NULL;
    new->visible = 1;

    new->morph = select_morph();
    new->morph->init(new);

    trile_calc_sides(cberg, new, x, y, root);
    trile_calc_heights(cberg, new);

    if (lit) {
        if (flat)   trile_calc_flat_norms(cberg, new);
        else        trile_calc_smooth_norms(cberg, new);
    }

    trile_render(cberg, new);
    return new;
}

static Trile *trile_alloc(cberg_state *cberg)
{
    Trile *new;

    if (cberg->free_head) {
        new = cberg->free_head;
        cberg->free_head = cberg->free_head->next_free;
    } else {
        ++cberg->count;
        if (!(new = calloc(1, sizeof(Trile)))
         || !(new->l = (double *) calloc(sizeof(double), cberg->epoints * 3))) {
            perror(progname);
            ncz_harness_die(1);
        }
        new->r = new->l + cberg->epoints;
        new->v = new->r + cberg->epoints;
        new->next0 = cberg->all_triles;
        cberg->all_triles = new;
#ifdef DEBUG
        printf("needed to alloc; [%d]\n", cberg->count);
#endif
    }
    return new;
}


static void trile_free(cberg_state *cberg, Trile *tr)
{
    /* keep the GL objects: the record is recycled and trile_render re-fills them */
    tr->morph->free(tr);
    tr->next_free = cberg->free_head;
    cberg->free_head = tr;
}

/* draw one tile with a model matrix */
static void draw_tile_with(Trile *tr, cs_mat4 model)
{
    cberg_state *cb = CB;
    cs_mat4 MV = cs_mul(cb->V, model);
    float n3[9]; cs_mat3_of(&MV, n3);
    glUniformMatrix4fv(glGetUniformLocation(cb->prog, "uMV"), 1, GL_FALSE, MV.m);
    glUniformMatrix3fv(glGetUniformLocation(cb->prog, "uN"), 1, GL_FALSE, n3);
    glFrontFace(((tr->x + tr->y) % 2) ? GL_CW : GL_CCW);
    glBindVertexArray(tr->vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, tr->nverts, 1);
}
static void trile_draw_vanilla(Trile *tr) { draw_tile_with(tr, cs_identity()); }
static void trile_draw(Trile *tr, void *ignore)
{
    if (tr->state == TRILE_STABLE) trile_draw_vanilla(tr);
    else tr->morph->draw(tr);
}
/***************************
 ** Trile morph functions. 
 **  select function at bottom (forward decls sucls) 
 ** */


/*** first the basic growing morph */

static void grow_init(Trile *tr)
{
    if (!tr->morph_data)
      tr->morph_data = (void *) malloc(sizeof(double));
    *((double *)tr->morph_data) = 0.02; /* not 0; avoid normals crapping */
}

static void grow_free(Trile *tr)
{
    if (tr->morph_data) free(tr->morph_data);
    tr->morph_data = 0;
}

static void grow_draw(Trile *tr)
{
    draw_tile_with(tr, cs_scale(1.0f, 1.0f, (float)*((double *)tr->morph_data)));
}

static void grow_init_iter(Trile *tr, cberg_state *cberg)
{
    *((double *)(tr->morph_data)) = *((double *)tr->morph_data) + cberg->elapsed;
    if (*((double *)tr->morph_data) >= 1.0)
        tr->state = TRILE_STABLE;
}

static void grow_dying_iter(Trile *tr, cberg_state *cberg)
{
    *((double *)tr->morph_data) = *((double *)tr->morph_data) - cberg->elapsed;
    if (*((double *)tr->morph_data) <= 0.02) /* XXX avoid fast del/cons? */
        tr->state = TRILE_DELETE;
}

/**** falling morph ****/

static void fall_init(Trile *tr)
{
    if (!tr->morph_data)
      tr->morph_data = (void *) malloc(sizeof(double));
    *((double *)tr->morph_data) = 0.0;
}

static void fall_free(Trile *tr)
{
    if (tr->morph_data) free(tr->morph_data);
    tr->morph_data = 0;
}

static void fall_draw(Trile *tr)
{
    draw_tile_with(tr, cs_translate(0.0f, 0.0f, (float)((0.5 - *((double *)tr->morph_data)) * 8)));
}

static void fall_init_iter(Trile *tr, cberg_state *cberg)
{
    *((double *)(tr->morph_data)) = *((double *)tr->morph_data) + cberg->elapsed;
    if (*((double *)tr->morph_data) >= 0.5)
        tr->state = TRILE_STABLE;
}

static void fall_dying_iter(Trile *tr, cberg_state *cberg)
{
    *((double *)tr->morph_data) = *((double *)tr->morph_data) - cberg->elapsed;
    if (*((double *)tr->morph_data) <= 0.0) /* XXX avoid fast del/cons? */
        tr->state = TRILE_DELETE;
}

/**** yeast morph ****/

static void yeast_init(Trile *tr)
{
    if (!tr->morph_data)
      tr->morph_data = (void *) malloc(sizeof(double));
    *((double *)tr->morph_data) = 0.02;
}

static void yeast_free(Trile *tr)
{
    if (tr->morph_data) free(tr->morph_data);
    tr->morph_data = 0;
}

static void yeast_draw(Trile *tr)
{
    float x = (float)(tr->x * 0.5), y = (float)(tr->y * M_SQRT3_2), z = (float)*((double *)tr->morph_data);
    cs_mat4 m = cs_translate(x, y, 0);
    m = cs_mul(m, cs_rotate(z * 360.0f, 0, 0, 1));
    m = cs_mul(m, cs_scale(z, z, z));
    m = cs_mul(m, cs_translate(-x, -y, 0));
    draw_tile_with(tr, m);
}

static void yeast_init_iter(Trile *tr, cberg_state *cberg)
{
    *((double *)(tr->morph_data)) = *((double *)tr->morph_data) + cberg->elapsed;
    if (*((double *)tr->morph_data) >= 1.0)
        tr->state = TRILE_STABLE;
}

static void yeast_dying_iter(Trile *tr, cberg_state *cberg)
{
    *((double *)tr->morph_data) = *((double *)tr->morph_data) - cberg->elapsed;
    if (*((double *)tr->morph_data) <= 0.02) /* XXX avoid fast del/cons? */
        tr->state = TRILE_DELETE;
}

/**** identity morph ****/

static void identity_init(Trile *tr)
{ tr->state = TRILE_STABLE; }

static void identity_free(Trile *tr)
{}

static void identity_draw(Trile *tr)
{ trile_draw_vanilla(tr); }

static void identity_init_iter(Trile *tr, cberg_state *cberg)
{}

static void identity_dying_iter(Trile *tr, cberg_state *cberg)
{ tr->state = TRILE_DELETE; }

/** now to handle selection **/

static const Morph morphs[] = {
    {grow_init, grow_free, grow_draw, grow_init_iter, grow_dying_iter},
    {fall_init, fall_free, fall_draw, fall_init_iter, fall_dying_iter},
    {yeast_init, yeast_free, yeast_draw, yeast_init_iter, yeast_dying_iter},
    {identity_init,  /*always put identity last to skip it..*/
        identity_free, identity_draw, identity_init_iter, identity_dying_iter}
};    

static const Morph *select_morph(void)
{ 
    int nmorphs = countof(morphs);
    if (crack)
        return &morphs[random() % (nmorphs-1)]; 
    else if (boring)
        return &morphs[nmorphs-1]; 
    else
        return morphs;
}


/***************************
 ** Trile superstructure functions. 
 **  */


static void triles_set_visible(cberg_state *cberg, Trile **root, int x, int y)
{
    Trile *parent = NULL, 
          *iter = *root;
    int goleft=0;

    while (iter != NULL) {
        parent = iter;
        goleft = (iter->x > x || (iter->x == x && iter->y > y));
        if (goleft)
            iter = iter->left;
        else if (iter->x == x && iter->y == y) {
            iter->visible = 1;
            return;
        } else
            iter = iter->right;
    }

    if (parent == NULL)
        *root = trile_new(cberg, x,y, NULL, NULL);
    else if (goleft)
        parent->left = trile_new(cberg, x,y, parent, *root);
    else
        parent->right = trile_new(cberg, x,y, parent, *root);
}

static unsigned int triles_foreach(Trile *root, void (*f)(Trile *, void *), 
  void *data)
{
    if (root == NULL) 
        return 0;
    
    f(root, data);
    return 1 + triles_foreach(root->left, f, data) 
      + triles_foreach(root->right, f, data);
}

static void triles_update_state(Trile **root, cberg_state *cberg)
{
    int process_current = 1;
    if (*root == NULL)
        return;

    while (process_current) {
        if ( (*root)->visible ) {
            if ( (*root)->state == TRILE_INIT )
                (*root)->morph->init_iter(*root, cberg);
            else if ( (*root)->state == TRILE_DYING ) {
                (*root)->state = TRILE_INIT;
                (*root)->morph->init_iter(*root, cberg);
            } else if ( (*root)->state == TRILE_NEW ) 
                (*root)->state = TRILE_INIT;

            (*root)->visible = 0;
        } else {
            if ( (*root)->state == TRILE_STABLE )
                (*root)->state = TRILE_DYING;
            else if ( (*root)->state == TRILE_INIT ) {
                (*root)->state = TRILE_DYING;
                (*root)->morph->dying_iter(*root, cberg);
            } else if ( (*root)->state == TRILE_DYING )
                (*root)->morph->dying_iter(*root, cberg);
        }

        if ( (*root)->state == TRILE_DELETE ) {
            Trile *splice_me;
            process_current = 1;

            if ((*root)->left == NULL) {
                splice_me = (*root)->right;
                if (splice_me)
                    splice_me->parent = (*root)->parent;
                else 
                    process_current = 0;
            } else if ((*root)->right == NULL) {
                splice_me = (*root)->left;
                splice_me->parent = (*root)->parent;
            } else {
                Trile *tmp;
                for (splice_me = (*root)->right; splice_me->left != NULL; )
                    splice_me = splice_me->left;
                tmp = splice_me->right;

                if (tmp) tmp->parent = splice_me->parent;

                if (splice_me == splice_me->parent->left)
                    splice_me->parent->left = tmp;
                else
                    splice_me->parent->right = tmp;

                splice_me->parent = (*root)->parent;
                splice_me->left = (*root)->left;
                (*root)->left->parent = splice_me;
                splice_me->right = (*root)->right;
                if ((*root)->right)
                    (*root)->right->parent = splice_me;
            }
            trile_free(cberg, *root);
            *root = splice_me;
        } else
            process_current = 0;
    }

    if (*root) {
        triles_update_state(&((*root)->left), cberg);
        triles_update_state(&((*root)->right), cberg);
    } 
}

static Trile *triles_find(Trile *tr, int x, int y)
{
    while (tr && !(tr->x == x && tr->y == y))
        if (x < tr->x || (x == tr->x && y < tr->y))
            tr = tr->left;
        else
            tr = tr->right;
    return tr;
}


/***************************
 ** Trile superstructure visibility functions. 
 **  strategy fine, implementation lazy&retarded =/
 **  */

#ifdef DEBUG
static double x_shit, y_shit;
#endif

static void calc_points(cberg_state *cberg, double *x1,double *y1, 
        double *x2,double *y2, double *x3,double *y3, double *x4,double *y4)
{
    double zNear, x_nearcenter, y_nearcenter, nhalfwidth, x_center, y_center;


    /* could cache these.. bahhhhhhhhhhhhhh */
    double halfheight = tan(cberg->fovy / 2 * M_PI_180) * cberg->zNear;
    double fovx_2 = atan(halfheight * cberg->aspect / cberg->zNear) * M_180_PI; 
    double zFar = cberg->zFar + M_RAD7_4;
    double fhalfwidth = zFar * tan(fovx_2 * M_PI_180)
                      + M_RAD7_4 / cos(fovx_2 * M_PI_180);
    double x_farcenter = cberg->x + zFar * cos(cberg->yaw * M_PI_180);
    double y_farcenter = cberg->y + zFar * sin(cberg->yaw * M_PI_180);
    *x1 = x_farcenter + fhalfwidth * cos((cberg->yaw - 90) * M_PI_180);
    *y1 = y_farcenter + fhalfwidth * sin((cberg->yaw - 90) * M_PI_180);
    *x2 = x_farcenter - fhalfwidth * cos((cberg->yaw - 90) * M_PI_180);
    *y2 = y_farcenter - fhalfwidth * sin((cberg->yaw - 90) * M_PI_180);

#ifdef DEBUG
    printf("pos (%.3f,%.3f) @ %.3f || fovx: %f || fovy: %f\n", 
            cberg->x, cberg->y, cberg->yaw, fovx_2 * 2, cberg->fovy);
    printf("\tfarcenter: (%.3f,%.3f) || fhalfwidth: %.3f \n"
           "\tp1: (%.3f,%.3f) || p2: (%.3f,%.3f)\n",
            x_farcenter, y_farcenter, fhalfwidth, *x1, *y1, *x2, *y2);
#endif

    if (cberg->z - halfheight <= 0) /* near view plane hits xy */
        zNear = cberg->zNear - M_RAD7_4;
    else /* use bottom of frustum */
        zNear = cberg->z / tan(cberg->fovy / 2 * M_PI_180) - M_RAD7_4;
    nhalfwidth = zNear * tan(fovx_2 * M_PI_180)
               + M_RAD7_4 / cos(fovx_2 * M_PI_180);
    x_nearcenter = cberg->x + zNear * cos(cberg->yaw * M_PI_180);
    y_nearcenter = cberg->y + zNear * sin(cberg->yaw * M_PI_180);
    *x3 = x_nearcenter - nhalfwidth * cos((cberg->yaw - 90) * M_PI_180);
    *y3 = y_nearcenter - nhalfwidth * sin((cberg->yaw - 90) * M_PI_180);
    *x4 = x_nearcenter + nhalfwidth * cos((cberg->yaw - 90) * M_PI_180);
    *y4 = y_nearcenter + nhalfwidth * sin((cberg->yaw - 90) * M_PI_180);

#ifdef DEBUG
    printf("\tnearcenter: (%.3f,%.3f) || nhalfwidth: %.3f\n"
           "\tp3: (%.3f,%.3f) || p4: (%.3f,%.3f)\n",
            x_nearcenter, y_nearcenter, nhalfwidth, *x3, *y3, *x4, *y4);
#endif


    /* center can be average or the intersection of diagonals.. */
#if 0
    {
        double c = nhalfwidth * (zFar -zNear) / (fhalfwidth + nhalfwidth);
        x_center = x_nearcenter + c * cos(cberg->yaw * M_PI_180);
        y_center = y_nearcenter + c * sin(cberg->yaw * M_PI_180);
    }
#else
    x_center = (x_nearcenter + x_farcenter) / 2;
    y_center = (y_nearcenter + y_farcenter) / 2;
#endif
#ifdef DEBUG
    x_shit = x_center;
    y_shit = y_center;
#endif
    
#define VSCALE(p)   *x##p = visibility * *x##p + (1-visibility) * x_center; \
                    *y##p = visibility * *y##p + (1-visibility) * y_center

    VSCALE(1);
    VSCALE(2);
    VSCALE(3);
    VSCALE(4);
#undef VSCALE
}

/* this is pretty stupid.. */
static inline void minmax4(double a, double b, double c, double d, 
  double *min, double *max)
{
    *min = *max = a;

    if (b > *max)       *max = b;
    else if (b < *min)  *min = b;
    if (c > *max)       *max = c;
    else if (c < *min)  *min = c;
    if (d > *max)       *max = d;
    else if (d < *min)  *min = d;
}

typedef struct {
    double min, max, start, dx;
} LS;

#define check_line(a, b)                     \
    if (fabs(y##a-y##b) > 0.001) {                    \
        ls[count].dx = (x##b-x##a)/(y##b-y##a);               \
        if (y##b > y##a) {                            \
            ls[count].start = x##a;                     \
            ls[count].min = y##a;                       \
            ls[count].max = y##b;                       \
        } else {                                  \
            ls[count].start = x##b;                     \
            ls[count].min = y##b;                       \
            ls[count].max = y##a;                       \
        }                                         \
        ++count;                                    \
    }

static unsigned int build_ls(cberg_state *cberg, 
                      double x1, double y1, double x2, double y2, 
                      double x3, double y3, double x4, double y4, LS *ls,
                      double *trough, double *peak)
{
    unsigned int count = 0;

    check_line(1, 2);
    check_line(2, 3);
    check_line(3, 4);
    check_line(4, 1);

    minmax4(y1, y2, y3, y4, trough, peak);
    return count;
}

#undef check_line

/*needs bullshit to avoid double counts on corners.*/
static void find_bounds(double y, double *left, double *right, LS *ls,
        unsigned int nls)
{
    double x;
    unsigned int i, set = 0;

    for (i = 0; i != nls; ++i)
        if (ls[i].min <= y && ls[i].max >= y) {
            x = (y - ls[i].min) * ls[i].dx + ls[i].start;
            if (!set) {
                *left = x;
                ++set;
            } else if (fabs(x - *left) > 0.001) {
                if (*left < x)
                    *right = x;
                else {
                    *right = *left;
                    *left = x;
                }
                return;
            }
        }

    /* just in case we somehow blew up */
    *left = 3.0;
    *right = -3.0;
}

static void mark_visible(cberg_state *cberg)
{
    double trough, peak, yval, left=0, right=0;
    double x1,y1, x2,y2, x3,y3, x4,y4;
    int start, stop, x, y;
    LS ls[4];
    unsigned int nls;

    calc_points(cberg, &x1,&y1, &x2,&y2, &x3,&y3, &x4,&y4);
    nls = build_ls(cberg, x1,y1, x2,y2, x3,y3, x4,y4, ls, &trough, &peak);

    start = (int) ceil(trough / M_SQRT3_2);
    stop = (int) floor(peak / M_SQRT3_2);
    
    for (y = start; y <= stop; ++y) {
        yval = y * M_SQRT3_2;
        find_bounds(yval, &left, &right, ls, nls);
        for (x = (int) ceil(left*2-1); x <= (int) floor(right*2); ++x) 
            triles_set_visible(cberg, &(cberg->trile_head), x, y);
    }
}



/***************************
 ** color schemes (same formulas as the original; they now return the color)
 ** */
static float clamp01f(double v) { return v < 0 ? 0.f : (v > 1 ? 1.f : (float)v); }
static void setc(float *c, double r, double g, double b) { c[0] = clamp01f(r); c[1] = clamp01f(g); c[2] = clamp01f(b); }
static void plain_land(cberg_state *cberg, double z, float *c)
{ setc(c, pow((z/0.35),4),  z/0.35, pow((z/0.35),4)); }
static void plain_water(cberg_state *cberg, double z, float *c)
{ setc(c, 0.0, (z+0.35)*1.6, 0.8); }
static void ice_land(cberg_state *cberg, double z, float *c)
{ setc(c, (0.35 - z)/0.35, (0.35 - z)/0.35, 1.0); }
static void ice_water(cberg_state *cberg, double z, float *c)
{ setc(c, 0.0, (z+0.35)*1.6, 0.8); }
static void magma_land(cberg_state *cberg, double z, float *c)
{ setc(c, z/0.35, z/0.2, 0); }
static void magma_lava(cberg_state *cberg, double z, float *c)
{ setc(c, (z+0.35)*1.6, (z+0.35), 0.0); }
static void vomit_solid(cberg_state *cberg, double z, float *c)
{
    double norm = fabs(z) / 0.35;
    setc(c, (1-norm) * cberg->vs0r + norm * cberg->vs1r,
            (1-norm) * cberg->vs0g + norm * cberg->vs1g,
            (1-norm) * cberg->vs0b + norm * cberg->vs1b);
}
static void vomit_fluid(cberg_state *cberg, double z, float *c)
{
    double norm = z / -0.35;
    setc(c, (1-norm) * cberg->vf0r + norm * cberg->vf1r,
            (1-norm) * cberg->vf0g + norm * cberg->vf1g,
            (1-norm) * cberg->vf0b + norm * cberg->vf1b);
}
static const Color colors[] = {
    {"plain", plain_land, plain_water, {0.0, 0.0, 0.0, 1.0}},
    {"ice", ice_land, ice_water, {0.0, 0.0, 0.0, 1.0}},
    {"magma", magma_land, magma_lava, {0.3, 0.3, 0.0, 1.0}},
    {"vomit", vomit_solid, vomit_fluid, {0.3, 0.3, 0.0, 1.0}}, /* no error! */
};
static double rnd01(void) { return random()/(double)RAND_MAX; }
static const Color *select_color(cberg_state *cberg)
{
    unsigned int ncolors = countof(colors);
    int idx = -1;
    if ( ! strcmp(color, "random") ) idx = random() % ncolors;
    else {
        unsigned int i;
        for (i = 0; i != ncolors; ++i)
            if ( ! strcmp(colors[i].id, color) ) { idx = i; break; }
        if (idx == -1) idx = 0;
    }
    if ( ! strcmp(colors[idx].id, "vomit") ) {
        cberg->vs0r = rnd01(); cberg->vs0g = rnd01(); cberg->vs0b = rnd01();
        cberg->vs1r = rnd01(); cberg->vs1g = rnd01(); cberg->vs1b = rnd01();
        cberg->vf0r = rnd01(); cberg->vf0g = rnd01(); cberg->vf0b = rnd01();
        cberg->vf1r = rnd01(); cberg->vf1g = rnd01(); cberg->vf1b = rnd01();
        cberg->bgc[0] = (float)rnd01(); cberg->bgc[1] = (float)rnd01(); cberg->bgc[2] = (float)rnd01();
    } else {
        for (int k = 0; k < 3; k++) cberg->bgc[k] = (float)colors[idx].bg[k];
    }
    return colors + idx;
}
static inline double drunken_rando(double cur_val, double max, double width)
{
    double r = random() / (double) RAND_MAX * 2;
    if (cur_val > 0)
        if (r >= 1)
            return cur_val + (r-1) * width * (1-cur_val/max);
        else
            return cur_val - r * width; 
    else
        if (r >= 1)
            return cur_val - (r-1) * width * (1+cur_val/max);
        else
            return cur_val + r * width; 
}



/* ---- shaders ---- */
static const char *VS = CS_GLSL(
    layout(location = 0) in vec3 aPos; layout(location = 1) in vec3 aCol;
    layout(location = 2) in vec3 aNrm; layout(location = 3) in float aWater;
    uniform mat4 uP; uniform mat4 uMV; uniform mat3 uN;
    out vec3 vCol; out vec3 vNrm; out vec3 vEye; out float vWater;
    void main() {
        vec4 e = uMV * vec4(aPos, 1.0);
        gl_Position = uP * e;
        vEye = e.xyz; vCol = aCol; vNrm = uN * aNrm; vWater = aWater;
    });
static const char *FS = CS_GLSL(
    in vec3 vCol; in vec3 vNrm; in vec3 vEye; in float vWater; out vec4 o;
    uniform float uLit; uniform float uEnh; uniform vec3 uFog; uniform float uFogD; uniform float uMagma;
    void main() {
        vec3 col = vCol;
        if (uLit > 0.5) {
            vec3 N = normalize(vNrm);
            if (!gl_FrontFacing) N = -N;
            vec3 L = normalize(vec3(0.0, -0.3, -2.0));       /* the original light, fixed to the eye */
            float d = max(dot(N, L), 0.0);
            col = vCol * (0.2 + d);
            if (uEnh > 0.5) {
                vec3 V = normalize(-vEye);
                float fill = 0.5 + 0.5 * N.y;                  /* soft sky fill from above */
                float dd = max(dot(N, normalize(vec3(-0.35, 0.7, 0.6))), 0.0);
                col = vCol * (0.16 + 0.75 * d + 0.42 * dd + 0.18 * fill);
                if (vWater > 0.5) {
                    vec3 H = normalize(L + V);
                    col += vec3(0.75, 0.85, 1.0) * pow(max(dot(vec3(0.0, 0.0, 1.0) * 0.0 + N, H), 0.0), 24.0) * 0.6;
                    col += vCol * 0.12;
                }
                if (uMagma > 0.5) col = mix(col, vCol * 1.3, 0.3);     /* lava and hot rock glow */
            }
        }
        if (uEnh > 0.5) {
            float dist = length(vEye);
            float f = 1.0 - exp(-pow(dist * uFogD, 2.0));
            col = mix(col, uFog, clamp(f, 0.0, 0.92));
        }
        o = vec4(col, 1.0);
    });
static const char *FS_SKY = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform vec3 uHorizon; uniform vec3 uZenith;
    void main() {
        float t = smoothstep(0.42, 1.0, vUv.y);
        o = vec4(mix(uHorizon, uZenith, t), 1.0);
    });

/* ---- lifecycle ---- */
static void reshape_crackberg(ModeInfo *mi, int w, int h)
{
    cberg_state *cberg = CB; if (!cberg) return;
    cberg->w = w; cberg->h = h;
    int h2;
    if (letterbox && (h2 = w * 9 / 16) < h) { glViewport(0, (h-h2)/2, w, h2); cberg->aspect = w/(double)h2; }
    else { glViewport(0, 0, w, h); cberg->aspect = w/(double)h; }
    cberg->P = cs_perspective((float)cberg->fovy, (float)cberg->aspect, (float)cberg->zNear, (float)cberg->zFar);
    if (cberg->post_ok) cs_post_resize(&cberg->post, w, h);
}

static void init_crackberg(ModeInfo *mi)
{
    cberg_state *cberg = calloc(1, sizeof *cberg); CB = cberg;
    cberg->style = cs_style(); cberg->speed = (float)cs_opt_f("speed", 1);
    cberg->bloom = (float)cs_opt_f("bloom", 1); cberg->aa = cs_opt_b("antialias", 1);
    nsubdivs = (unsigned)cs_opt_i("nsubdivs", 4) % 16;
    visibility = (float)cs_opt_f("visibility", 0.6);
    crack = cs_opt_b("crack", 1); boring = 0; do_water = cs_opt_b("water", 1);
    flat = cs_opt_b("flat", 1); lit = 1; letterbox = 0;
    color = (char *)cs_opt_s("scheme", "random");
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&cberg->rng, seed); srandom(seed);
    cberg->w = mi->xgwa.width; cberg->h = mi->xgwa.height;
    cberg->epoints = 1 + (1 << nsubdivs);
    cberg->tpoints = cberg->epoints * (cberg->epoints + 1) / 2;
    cberg->ntris = (1 << (nsubdivs << 1));
    cberg->tnorms = ( (flat) ? cberg->ntris : cberg->tpoints);
    cberg->dx0 = 1.0 / (1 << nsubdivs);
    cberg->heights = malloc(cberg->tpoints * sizeof(double));
    cberg->norms = malloc(3 * cberg->tnorms * sizeof(double));
    cberg->motion_state = MOTION_AUTO;
    cberg->mspeed = 1.0;
    cberg->z = 0.5; cberg->fovy = 60.0; cberg->zNear = 0.5; cberg->zFar = 5.0;
    cberg->draw_elapsed = 1.0;
    cberg->prog = cs_program(VS, FS, "crackberg");
    cberg->prog_sky = cs_program(CS_FULLSCREEN_VS, FS_SKY, "crackberg sky");
    glGenVertexArrays(1, &cberg->vao_empty);
    cberg->color = select_color(cberg);
    cberg->V = cs_identity();
    if (cberg->style) cberg->post_ok = cs_post_init(&cberg->post, cberg->w, cberg->h, 1);
    reshape_crackberg(mi, cberg->w, cberg->h);
    fprintf(stderr, "[diag] crackberg: style=%s seed=%u scheme=%s\n", cberg->style ? "enhanced" : "classic", seed, cberg->color->id);
}

static void draw_crackberg(ModeInfo *mi)
{
    cberg_state *cberg = CB;
    if (!cberg || !cberg->prog) return;
    double cur_frame = ncz_now();
    if ( cberg->prev_frame ) {
        cberg->elapsed = (cur_frame - cberg->prev_frame) * cberg->speed;
        if (cberg->elapsed > 0.25) cberg->elapsed = 0.25;   /* a stall must not fling the camera */
        cberg->x += cberg->dx * cberg->elapsed;
        cberg->y += cberg->dy * cberg->elapsed;
        cberg->yaw += cberg->dyaw * cberg->elapsed;
        cberg->draw_elapsed += cberg->elapsed;
        if (cberg->draw_elapsed >= 0.8) {
            cberg->draw_elapsed = 0.0;
            cberg->dx = drunken_rando(cberg->dx, 2.5, 0.8);
            cberg->dy = drunken_rando(cberg->dy, 2.5, 0.8);
            cberg->dyaw = drunken_rando(cberg->dyaw, 40.0,  8.0);
        }
    }
    cberg->prev_frame = cur_frame;
    mark_visible(cberg);
    triles_update_state(&(cberg->trile_head), cberg);

    /* view: lookAt(0,0,0, 1,0,0, 0,0,1) * Rz(-yaw) * T(-pos) */
    cs_mat4 look = cs_identity();
    look.m[0] = 0;  look.m[4] = -1; look.m[8]  = 0;      /* row 0 = s = (0,-1,0) */
    look.m[1] = 0;  look.m[5] = 0;  look.m[9]  = 1;      /* row 1 = u = (0,0,1) */
    look.m[2] = -1; look.m[6] = 0;  look.m[10] = 0;      /* row 2 = -f = (-1,0,0) */
    cberg->V = cs_mul(cs_mul(look, cs_rotate((float)-cberg->yaw, 0, 0, 1)),
                      cs_translate((float)-cberg->x, (float)-cberg->y, (float)-cberg->z));

    int enh = cberg->style && cberg->post_ok;
    if (enh) cs_post_begin(&cberg->post); else glViewport(0, 0, cberg->w, cberg->h);
    glClearColor(cberg->bgc[0], cberg->bgc[1], cberg->bgc[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (cberg->style && cberg->prog_sky) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(cberg->prog_sky);
        float hz[3], zn[3];
        for (int k = 0; k < 3; k++) { hz[k] = cberg->bgc[k]; zn[k] = cberg->bgc[k] * 0.55f + (k == 2 ? 0.05f : 0.0f); }
        glUniform3fv(glGetUniformLocation(cberg->prog_sky, "uHorizon"), 1, hz);
        glUniform3fv(glGetUniformLocation(cberg->prog_sky, "uZenith"), 1, zn);
        glBindVertexArray(cberg->vao_empty);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    }
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glUseProgram(cberg->prog);
    glUniformMatrix4fv(glGetUniformLocation(cberg->prog, "uP"), 1, GL_FALSE, cberg->P.m);
    glUniform1f(glGetUniformLocation(cberg->prog, "uLit"), lit ? 1.f : 0.f);
    glUniform1f(glGetUniformLocation(cberg->prog, "uEnh"), cberg->style ? 1.f : 0.f);
    glUniform3fv(glGetUniformLocation(cberg->prog, "uFog"), 1, cberg->bgc);
    glUniform1f(glGetUniformLocation(cberg->prog, "uFogD"), 1.0f / (cberg->zFar * 0.95f));
    glUniform1f(glGetUniformLocation(cberg->prog, "uMagma"), !strcmp(cberg->color->id, "magma") ? 1.f : 0.f);
    triles_foreach(cberg->trile_head, trile_draw, (void *)cberg);
    glBindVertexArray(0);
    glFrontFace(GL_CCW);
    if (enh) cs_post_end(&cberg->post, cberg->bloom * (!strcmp(cberg->color->id, "magma") ? 0.35f : 0.25f), 1.0f, 0.35f, cberg->aa, 0);
}

static void free_crackberg(ModeInfo *mi)
{
    cberg_state *cberg = CB; if (!cberg) return;
    while (cberg->all_triles) {
        Trile *n = cberg->all_triles;
        cberg->all_triles = n->next0;
        if (n->vao) { glDeleteVertexArrays(1, &n->vao); glDeleteBuffers(1, &n->vbo); }
        free (n->l);
        if (n->morph_data) free (n->morph_data);
        free (n);
    }
    if (cberg->prog) glDeleteProgram(cberg->prog);
    if (cberg->prog_sky) glDeleteProgram(cberg->prog_sky);
    if (cberg->vao_empty) glDeleteVertexArrays(1, &cberg->vao_empty);
    cberg->free_head = NULL;
    if (cberg->post_ok) cs_post_free(&cberg->post);
    free(cberg->norms); free(cberg->heights); free(cberg); CB = NULL;
}
static Bool crackberg_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_crackberg(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt crackberg_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table crackberg_xscreensaver_function_table = {
    .name = "crackberg", .class_ = "Crackberg",
    .init_cb = init_crackberg, .draw_cb = draw_crackberg, .reshape_cb = reshape_crackberg,
    .event_cb = crackberg_handle_event, .free_cb = free_crackberg, .release_cb = release_crackberg,
    .opts = &crackberg_opts, .defaults_str = "",
};
