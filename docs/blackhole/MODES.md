# Black hole color modes

`NCZ_BLACKHOLE_COLORS` (set by the launcher from the `blackhole-color-mode` key):

- `stylized` (default): the original art-directed palette, unchanged.
- `kipthorne`: the Interstellar / Gargantua look. Warm amber-to-gold blackbody (1900 K to 4300 K), no Doppler shift, no gravitational shift, no beaming. Rotation-symmetric.
- `faithful`: physically shifted color. Circular-orbit speed for a Schwarzschild hole, v^2 = 0.5/(r-1); line-of-sight Doppler factor D = sqrt(1-v^2)/(1-v sin a); gravitational redshift sqrt(1-1/r). Observed temperature scales with (D*grav)^2 (the shift is deliberately exaggerated so it reads at normal viewing inclination) and brightness with D^3 (beaming). The approaching side is bright blue-white, the receding side dim amber-red.

Measured (docs/blackhole/modes-metrics.txt), per-pixel chroma distance kipthorne vs faithful over disk pixels: build-host min 0.079, mean 0.103 (7 frames); O6N min 0.070, mean 0.114 (15 frames). The earlier reconstruction measured 0.001 to 0.002. Screenshots: modes-peg.jpg and modes-o6n.jpg (columns stylized, kipthorne, faithful).

Future work (not done): smooth timed transition between color modes or a random-mode playlist. It needs a crossfade uniform and a launcher option, and was left out to keep this change small and safe.
