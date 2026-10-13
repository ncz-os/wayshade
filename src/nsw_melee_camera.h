/* SPDX-License-Identifier: Apache-2.0 */
/* nsw_melee_camera.h - the Melee camera: frames the fight, zooms in when the
 * ships close and pulls out when they part. Pure C with no GL, so the
 * battle tests can fly it over replays and measure what a viewer would see.
 */
#ifndef NSW_MELEE_CAMERA_H
#define NSW_MELEE_CAMERA_H
#include "nsw_melee.h"

#define NSW_CAM_MIN_FRAC 0.06f      /* a hull is never shorter than 6% of the frame height */

typedef struct {
    /* configuration */
    int fixed;                      /* 1: the whole arena, no zoom, no roll */
    float max_frac;                 /* the closest zoom: a hull at most this share of the frame height */
    float seed;
    /* state: centre (world), zoom (pixels per world unit), roll (radians) */
    float cx, cy, zoom, rot, rot_v;
    int ready;
    float hold_z;                   /* zoom target after hysteresis */
    float punch, punch_t;           /* kill punch-in, and wall time since the kill */
    NswV2 punch_p;
    float punch_room;               /* how far the punch may zoom (1..1.3) without losing a ship */
    float punch_shift;              /* 1 when the kill ended the battle: centre on the wreck */
    int kills_seen;
    int battle_seen;                /* a new battle is a hard cut, not a long zoom out */
    float shake, sx, sy;            /* shake offset in pixels (never more than 2 px at 1080p) */
    float close_t, dance, dance_dir;/* sustained close combat turns into a slow orbit */
    unsigned frame;
    int floor_limited;              /* last update: the ships did not fit at the 6% floor */
} NswCam;

void nsw_cam_init(NswCam *c, int fixed, float max_frac, float seed);
/* Advance by dt wall seconds for a width x height frame. */
void nsw_cam_update(NswCam *c, const NswWorld *w, int width, int height, float dt);
/* World to output pixels (origin bottom left, as GL draws). */
void nsw_cam_to_px(const NswCam *c, int width, int height, float x, float y, float *px, float *py);
/* The on-screen position of a moving thing drawn `lag` seconds behind. */
static inline NswV2 nsw_cam_lagged(NswV2 p, NswV2 v, float lag) {
    NswV2 q = {p.x - v.x * lag, p.y - v.y * lag};
    return q;
}

#endif
