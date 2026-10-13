# Black-hole shader port

`blackhole_gles3` ports photon-path and disk logic from Adriwin06/black-hole
revision `b62f3cbd680b905e9fe4c36d6bdc0db423c6bb7c` (MIT). The exact upstream
license is in `blackhole-LICENSE.txt`.

The port retains the Schwarzschild Binet equation `u'' = -u + 1.5 u^2`,
leapfrog geodesic integration, disk-plane intersections, radial temperature,
Doppler brightening, and time-advected turbulence. It is rewritten as one
GLES 3.0 fullscreen shader. Launch randomization varies Schwarzschild scale,
disk temperature/density/rotation, inclination, orbit, optional jet, and
procedural stars; camera orbit and turbulence evolve continuously.

No upstream assets are included. The CC-BY-NC Milky Way texture named in
upstream `COPYRIGHT.md` is replaced by a procedural sky. The secondary
hydrogendeuteride implementation was inspected but no code was copied.
