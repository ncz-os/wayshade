/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_goo.h - the default magmasimplex engine ("goo"): molten wax
 * bodies simulated on the CPU in three depth layers, rendered as implicit
 * fields with milky-wax optics in one full-screen pass (vendor/magmasimplex/
 * goo.frag). The older grid fluid engine stays available as engine=fluid.
 */
#ifndef MAGMASIMPLEX_GOO_H
#define MAGMASIMPLEX_GOO_H
#include <stdint.h>
#include "magmasimplex_goo_cfg.h"

typedef struct MsGoo MsGoo;

/* cfg must be resolved (ms_goo_cfg_resolve). palette_fixed: the user or a
 * scene chose the palette, so chapters keep it. Returns NULL on failure (the
 * reason is printed). Needs a current GLES3 context. */
MsGoo *ms_goo_create(uint32_t seed, const MsGooConfig *cfg, int palette_fixed);
void ms_goo_draw(MsGoo *g, int w, int h);
void ms_goo_destroy(MsGoo *g);

#endif
