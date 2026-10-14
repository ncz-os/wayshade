#!/usr/bin/env python3
"""capture_parity.py BIN [ARGS...]: prove that capture mode's C display transform is the
shader's. Renders one fixed-seed frame twice with one sample per pixel: once with the
shader's own tail (NCZ_BLACKHOLE_CAPTURE_SHADER_TAIL=1, as on screen) and once in linear
radiance with the C tail. Fails if any channel differs by more than 1.5/255 (half-float
radiance and the 16-bit output account for the last code value).
Needs a Wayland session (WAYLAND_DISPLAY, XDG_RUNTIME_DIR) and a GPU."""
import os, subprocess, sys, tempfile
import numpy as np
from PIL import Image
binary, extra = sys.argv[1], sys.argv[2:]
tmp = tempfile.mkdtemp(prefix="bh-parity-", dir=os.getcwd())
common = [binary, "--seed=42", "--capture-at=37", "--capture-size=640x360", "--capture-supersample=1",
          "--capture-steps=260", *extra]
imgs = []
for tag, tail in (("shader", "1"), ("c", "0")):
    env = dict(os.environ, NCZ_BLACKHOLE_CAPTURE_SHADER_TAIL=tail)
    out = os.path.join(tmp, tag + "-%d.png")
    subprocess.run(common + [f"--capture={out}"], env=env, check=True, capture_output=True)
    imgs.append(np.asarray(Image.open(out % 0)).astype(np.float64) / 65535.0)
d = np.abs(imgs[0] - imgs[1])
print(f"max diff {d.max() * 255:.2f}/255, mean {d.mean() * 255:.4f}/255, pixels > 1/255: {(d.max(-1) > 1 / 255).mean() * 100:.3f}%")
sys.exit(0 if d.max() <= 1.5 / 255 else 1)
