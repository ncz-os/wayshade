// SPDX-License-Identifier: Apache-2.0
/* attractor.frag — strange-attractor basin colouring.
 *
 * For each pixel, walk the attractor's discrete ODE from a per-pixel
 * initial condition and use the iteration at which the orbit first
 * leaves a unit box (or its final position) to colour the pixel. The
 * visual is the "basin of attraction" of the attractor in the
 * initial-condition plane.
 *
 * Five attractors shipped (selected by int(u_params.x) mod 5):
 *   0 = Lorenz (sigma=10, rho=u_params.y, beta=8/3)
 *   1 = Rossler (a=0.2, b=0.2, c=5.7)
 *   2 = Henon  (a=1.4, b=0.3)
 *   3 = Pickover (alpha=2.24, beta=0.43, gamma=0.79, delta=-0.65)
 *   4 = Popcorn
 *
 * Layout of u_params (vec4):
 *   x = attractor id.
 *   y = rho (Lorenz) or scale (others).
 *   z = strength.
 *   w = unused.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */


void main() {
    int at = int(u_params.x + 0.5) % 5;
    float tm = u_time * u_speed;
    float rho = u_params.y * (1.0 + 0.06 * sin(tm * 0.2));
    float strength = u_params.z;

    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    /* Slowly rotate the field of starting points. */
    float ra = tm * 0.1;
    p = mat2(cos(ra), sin(ra), -sin(ra), cos(ra)) * p;
    /* Initial condition per pixel. */
    float x0 = p.x * 2.5;
    float y0 = p.y * 2.5;
    float z0 = 0.5 * sin(p.x * 6.28 + p.y * 4.71);

    float x = x0, y = y0, z = z0;
    int escaped_at = -1;
    int max_iter = u_iterations;
    if (max_iter > 800) max_iter = 800;

    for (int i = 0; i < 800; i++) {
        if (i >= max_iter) break;
        float nx = x, ny = y, nz = z;
        if (at == 0) {
            /* Lorenz: sigma=10, beta=8/3. */
            float sigma = 10.0;
            float beta  = 8.0 / 3.0;
            float dt = 0.005 * strength;
            nx = x + sigma * (y - x) * dt;
            ny = y + (x * (rho - z) - y) * dt;
            nz = z + (x * y - beta * z) * dt;
        } else if (at == 1) {
            /* Rossler: a=0.2, b=0.2, c=5.7. */
            float a = 0.2, b = 0.2, c = 5.7;
            float dt = 0.05 * strength;
            nx = x + (-y - z) * dt;
            ny = y + (x + a * y) * dt;
            nz = z + (b + z * (x - c)) * dt;
        } else if (at == 2) {
            /* Henon map (discrete). */
            nx = 1.4 - x * x + 0.3 * y;
            ny = x;
            nz = z;
        } else if (at == 3) {
            /* Pickover: classical 4D sinusoidal projection. */
            nx = sin(rho * y) - z * cos(0.43 * x + y);
            ny = z * sin(0.79 * x) - cos(2.24 * y);
            nz = sin(0.65 * x) - y * cos(0.42 * z + y);
        } else {
            /* Popcorn: f, h, g sinusoidal. */
            float f = 0.5, h = 0.5, g = 0.5;
            nx = x + h * sin(y * 3.14159);
            ny = y + g * sin(x * 3.14159);
            nz = z + f * sin(0.7 * (x + y));
        }

        if (abs(nx) > 1e3 || abs(ny) > 1e3 || abs(nz) > 1e3 ||
            !fract_finite(nx) || !fract_finite(ny) || !fract_finite(nz)) {
            escaped_at = i;
            break;
        }
        x = nx; y = ny; z = nz;
    }

    /* Color: iteration count at escape, or position-based colour if
     * the orbit stayed bounded. */
    float col;
    if (escaped_at >= 0) {
        col = float(escaped_at) / float(max_iter);
    } else {
        /* Position hash. */
        col = 0.5 + 0.5 * sin(0.21 * x + 0.37 * y + 0.13 * z);
    }
    col = clamp(col, 0.0, 1.0);
    if (!fract_finite(col)) col = 0.0;

    vec3 rgb = fract_palette(col);
    rgb *= u_exposure;

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}