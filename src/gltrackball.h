/* gltrackball.h — compatibility shim.
 *
 * Upstream ships hacks/glx/gltrackball.h declaring the trackball API. We
 * implement that API in xscreensaver_compat.{h,c} (inertly — there is no
 * pointer input behind a screensaver), so vendoring upstream's header would
 * only produce conflicting declarations. Hacks include this file by name, so
 * it has to exist; it just forwards.
 */
#ifndef NCZ_GLTRACKBALL_H
#define NCZ_GLTRACKBALL_H
#include "ncz_hack_shim.h"
#endif
