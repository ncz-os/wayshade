/* SPDX-License-Identifier: Apache-2.0 */
/* nsw_melee_camera.c - frames the Melee fight like a camera operator would.
 *
 * Each frame the camera fits a box around what matters, in its own rotated
 * frame: every ship still in the fight, where each is heading in the next
 * half second, a shot about to land, and the planet only when a ship is near
 * it. The box gets a 15% margin and sets the zoom, bounded so that no hull is
 * ever shorter than 6% of the frame height and none taller than the
 * configured closest zoom. Zooming out is quick (quicker still when
 * something is at the edge), zooming in slow and eased, and a zoom-in has to
 * be worth 10% before the camera commits to it, so it never pumps.
 *
 * When the ships are too far apart to fit at the 6% floor, the camera rolls
 * to lay their spread along the long side of the frame. Sustained close
 * combat turns into a slow orbit. A kill freezes the battle for a moment
 * (in the simulation) while the camera punches in 1.3x on the wreck, then
 * drifts back. Shake is only for kills and never more than 2 px at 1080p.
 * Each new battle opens on a cut to the arrival, not a zoom out from the
 * last one's wreck.
 */
#include "nsw_melee_camera.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float wrap_angle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}
static float ease(float x) {
    x = clampf(x, 0.f, 1.f);
    return x * x * (3 - 2 * x);
}
static float hull_len(const NswShip *s) { return 2.f * NSW_RACE[s->race].scale; }

void nsw_cam_init(NswCam *c, int fixed, float max_frac, float seed) {
    memset(c, 0, sizeof *c);
    c->fixed = fixed;
    c->max_frac = clampf(max_frac, 0.1f, 0.4f);
    c->seed = seed;
    c->dance_dir = 1.f;
}

/* Who the camera is watching. In the outro: the winners, the wrecks for a
 * moment, and after a mutual kill both wrecks for the whole hold. */
static int framed(const NswWorld *w, const NswShip *s) {
    if (w->phase == PH_OUTRO) {
        if (w->winner_team >= 0)
            return (s->alive && s->team == w->winner_team) || (!s->alive && w->battle_t - s->death_t < 2.5f);
        return 1;
    }
    return s->alive || w->battle_t - s->death_t < 1.5f;
}

typedef struct { float x0, x1, y0, y1, cr, sr; } Box;
static void box_add(Box *b, float x, float y, float rad) {
    float rx = b->cr * x - b->sr * y, ry = b->sr * x + b->cr * y;
    b->x0 = fminf(b->x0, rx - rad); b->x1 = fmaxf(b->x1, rx + rad);
    b->y0 = fminf(b->y0, ry - rad); b->y1 = fmaxf(b->y1, ry + rad);
}

void nsw_cam_update(NswCam *c, const NswWorld *w, int W, int H, float dt) {
    if (!(dt >= 0.f)) dt = 0.f;       /* also catches NaN */
    if (!(dt <= 0.1f)) dt = 0.1f;
    c->frame++;
    if (c->fixed) {
        c->cx = c->cy = c->rot = 0;
        c->zoom = 0.5f * fminf((float)W, (float)H) / (NSW_ARENA_R * 1.05f);
        c->ready = 1;
        return;
    }
    if (w->battle != c->battle_seen) { c->battle_seen = w->battle; c->ready = 0; c->punch = 0; c->dance = 0; c->close_t = 0; }
    const float k = fminf((float)W, (float)H) / 1080.f, lag = nsw_lag(w);
    Box b = {1e9f, -1e9f, 1e9f, -1e9f, cosf(c->rot), sinf(c->rot)}, core = b;
    float smin = 1e9f, smax = 0, far_d = 0, far_a = 0, sx = 0, sy = 0;
    int n = 0, near_planet = 0;
    for (int i = 0; i < w->nships; i++) {
        const NswShip *s = &w->ship[i];
        if (!framed(w, s)) continue;
        NswV2 v = s->alive ? s->v : (NswV2){0, 0};
        NswV2 p = nsw_cam_lagged(s->p, v, lag);
        float L = hull_len(s);
        box_add(&b, p.x, p.y, L * 0.8f);
        box_add(&core, p.x, p.y, L * 0.6f);
        sx += b.cr * p.x - b.sr * p.y;
        sy += b.sr * p.x + b.cr * p.y;
        box_add(&b, p.x + v.x * 0.3f, p.y + v.y * 0.3f, L * 0.3f);   /* where it is going */
        if (s->alive) { smin = fminf(smin, L); smax = fmaxf(smax, L); }
        if (hypotf(p.x, p.y) < w->planet_r + 0.45f) near_planet = 1;
        for (int j = 0; j < i; j++) {
            const NswShip *o = &w->ship[j];
            if (!framed(w, o)) continue;
            NswV2 op = nsw_cam_lagged(o->p, o->alive ? o->v : (NswV2){0, 0}, lag);
            float dx = p.x - op.x, dy = p.y - op.y, d = hypotf(dx, dy);
            if (d > far_d) { far_d = d; far_a = atan2f(dy, dx); }
        }
        n++;
    }
    if (near_planet || n == 0) box_add(&b, 0, 0, w->planet_r * 1.6f);
    if (n == 0) box_add(&b, 0, 0, 0.5f);
    /* A shot about to land matters too. */
    for (int q = 0; q < NSW_MAX_SHOTS; q++) {
        const NswShot *sh = &w->shot[q];
        if (!sh->alive) continue;
        for (int i = 0; i < w->nships; i++) {
            const NswShip *s = &w->ship[i];
            if (!s->alive || s->team == sh->team) continue;
            float dx = s->p.x - sh->p.x, dy = s->p.y - sh->p.y;
            if (dx * dx + dy * dy < 0.16f && dx * sh->v.x + dy * sh->v.y > 0) { box_add(&b, sh->p.x, sh->p.y, 0.03f); break; }
        }
    }
    if (smax == 0) smin = smax = 0.15f;
    /* Centre halfway between the box and the ships themselves, so heading
     * lead, a shot or the planet do not push the ships into one side of
     * the frame; the box half-sizes grow to still hold everything. */
    float mx = 0.5f * (b.x0 + b.x1), my = 0.5f * (b.y0 + b.y1);
    if (n > 0) { mx = 0.5f * (mx + sx / n); my = 0.5f * (my + sy / n); }
    float hx = fmaxf(fmaxf(b.x1 - mx, mx - b.x0), 0.05f), hy = fmaxf(fmaxf(b.y1 - my, my - b.y0), 0.05f);
    float tx = b.cr * mx + b.sr * my, ty = -b.sr * mx + b.cr * my;
    float zfit = fminf(0.5f * W / (hx * 1.15f), 0.5f * H / (hy * 1.15f));
    float zfloor = NSW_CAM_MIN_FRAC * H / smin, zceil = c->max_frac * H / smax;
    if (zceil < zfloor) zceil = zfloor;
    float tz = fminf(zfit, zceil);
    int floor_limited = tz < zfloor;
    if (floor_limited && n > 0) {
        /* Too spread out to fit at the floor: centre on the hulls themselves,
         * not on where they are heading or what is flying at them. */
        tz = zfloor;
        mx = 0.5f * (core.x0 + core.x1); my = 0.5f * (core.y0 + core.y1);
        tx = b.cr * mx + b.sr * my; ty = -b.sr * mx + b.cr * my;
    } else if (floor_limited) tz = zfloor;
    c->floor_limited = floor_limited;

    if (!c->ready) {
        c->cx = tx; c->cy = ty; c->zoom = c->hold_z = tz;
        c->kills_seen = w->kills;
        c->ready = 1;
    }
    /* Hysteresis: out whenever needed, in only when it gains 10%. */
    if (tz < c->hold_z || tz > c->hold_z * 1.10f) c->hold_z = tz;
    int at_edge = zfit < c->zoom * 1.02f;
    float tau = c->hold_z < c->zoom ? (at_edge ? 0.3f : 0.6f) : 1.2f;
    float step = (logf(c->hold_z) - logf(c->zoom)) * (1.f - expf(-dt / tau));
    float cap = (c->hold_z < c->zoom ? 1.2f : 0.5f) * dt;     /* no lurches: at most e^1.2 a second out, e^0.5 in */
    c->zoom *= expf(clampf(step, -cap, cap));
    float kp = 1.f - expf(-dt / 0.4f);
    c->cx += (tx - c->cx) * kp;
    c->cy += (ty - c->cy) * kp;

    /* Roll: align a spread that does not fit; orbit in a long close fight;
     * otherwise a slow dolly that changes direction now and then. */
    int close = w->phase == PH_FIGHT && zfit >= zceil;
    c->close_t = close ? c->close_t + dt : fmaxf(0.f, c->close_t - 2.f * dt);
    float want_dance = c->close_t > 3.f ? 1.f : 0.f;
    if (want_dance > 0 && c->dance < 0.01f) c->dance_dir = ((unsigned)c->kills_seen + c->frame) & 1u ? 1.f : -1.f;
    c->dance += (want_dance - c->dance) * (1.f - expf(-dt / 1.0f));
    float target_v;
    if (floor_limited && n >= 2) {
        float want = (W >= H ? 0.f : PI / 2) - far_a;
        float err = wrap_angle(want - c->rot);
        if (err > PI / 2) err -= PI;
        if (err < -PI / 2) err += PI;
        target_v = clampf(err * 2.5f, -0.8f, 0.8f);
    } else {
        float dolly = 0.03f * sinf((float)w->t * 0.045f + c->seed);
        target_v = dolly * (1 - c->dance) + 0.2f * c->dance_dir * c->dance;
    }
    c->rot_v += (target_v - c->rot_v) * (1.f - expf(-dt / 0.6f));
    c->rot = wrap_angle(c->rot + c->rot_v * dt);

    /* Kill: punch in on the wreck through the hit-stop and slow motion. */
    if (w->kills != c->kills_seen) {
        c->kills_seen = w->kills;
        c->punch = 1.f; c->punch_t = 0; c->punch_p = w->kill_p;
        c->shake = 1.f;
    }
    /* Mid-battle, the punch-in must not push a live ship out of frame; the
     * battle's last kill centres on the wreck. */
    c->punch_room = clampf(zfit / fmaxf(c->zoom, 1e-3f), 1.f, 1.3f);
    c->punch_shift = w->phase == PH_OUTRO ? 1.f : 0.f;
    if (c->punch > 0) {
        c->punch_t += dt;
        c->punch = c->punch_t < 1.75f ? 1.f : expf(-(c->punch_t - 1.75f) / 0.7f);
        if (c->punch < 0.01f) c->punch = 0;
    }
    c->shake *= expf(-dt / 0.15f);
    float ang = (float)c->frame * 2.39996f;
    c->sx = cosf(ang) * c->shake * 2.f * k;
    c->sy = sinf(ang) * c->shake * 2.f * k;
}

void nsw_cam_to_px(const NswCam *c, int W, int H, float x, float y, float *px, float *py) {
    float pe = c->punch * ease(c->punch_t / 0.2f);
    float z = c->zoom * (1.f + (c->punch_room - 1.f) * pe);
    float sh = 0.5f * pe * c->punch_shift;
    float cx = c->cx + (c->punch_p.x - c->cx) * sh, cy = c->cy + (c->punch_p.y - c->cy) * sh;
    float dx = x - cx, dy = y - cy, cr = cosf(c->rot), sr = sinf(c->rot);
    *px = 0.5f * W + (cr * dx - sr * dy) * z + c->sx;
    *py = 0.5f * H + (sr * dx + cr * dy) * z + c->sy;
}
