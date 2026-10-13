/* xlockmoreI.h — compatibility shim.
 *
 * Upstream's version pulls in xlockmore internals that assume a real X
 * server. Everything the GL hacks actually use from it is provided by
 * xscreensaver_compat.h, so this forwards rather than vendoring upstream.
 */
#ifndef NCZ_XLOCKMOREI_H
#define NCZ_XLOCKMOREI_H
#include "ncz_hack_shim.h"
#endif
