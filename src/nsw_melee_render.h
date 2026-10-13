/* SPDX-License-Identifier: Apache-2.0 */
/* nsw_melee_render.h - GL renderer for the Melee world (see nsw_melee.h). */
#ifndef NSW_MELEE_RENDER_H
#define NSW_MELEE_RENDER_H
#include "nsw_melee.h"

typedef struct NswRender NswRender;

enum { NSW_PAL_RACE, NSW_PAL_EMBER, NSW_PAL_ICE, NSW_PAL_PHOSPHOR };

/* How the fight looks (the hack's options). */
typedef struct {
    int cam_fixed;              /* 1: the whole arena, no zoom or roll */
    float max_frac;             /* closest zoom: largest share of the frame height one hull may fill (0.1-0.4) */
    int palette;                /* NSW_PAL_*: each race's own hue, or a family every race is drawn from */
    float glow;                 /* glow strength, x the default (0-2); edges are unaffected */
    float trails;               /* engine trail strength, x the default (0-2) */
} NswLook;
void nsw_look_default(NswLook *l);

/* weak: quarter-size glow buffer and a lighter background. look may be NULL
 * (the defaults). Needs a current GLES 3 context. Returns NULL on failure
 * (the reason is printed). */
NswRender *nsw_render_create(int weak, uint32_t seed, const NswLook *look);
void nsw_render_draw(NswRender *r, const NswWorld *w, int width, int height, float dt);
void nsw_render_destroy(NswRender *r);
int nsw_render_lines(const NswRender *r);

#endif
