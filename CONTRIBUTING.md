# Contributing to wayshade

* **Canonical repository:** <https://gitlab.com/ncz-os/wayshade>. Open issues and
  merge requests there. Any copy on another forge is a read-only mirror; changes
  sent to a mirror are not merged.
* **Issues:** describe the GPU, driver, compositor and screensaver id, and attach
  the output of `ncz-screensaver doctor` and `<id>_gles3 --help` if relevant.
  Report security problems privately to the maintainer by email
  (Jason Perlow <jperlow@gmail.com>) rather than in a public issue.
* **Changes:** one concern per merge request. Run `meson test -C build` and
  `python3 -m pytest tools/tests launcher/tests` before sending. C uses 4-space
  indentation; keep comments short.
* **Licensing:** new original code is Apache-2.0 with an
  `SPDX-License-Identifier: Apache-2.0` header. Ported or vendored code keeps its
  upstream license and notice; add it to `NOTICES.md`, `debian/copyright` and
  `docs/PROVENANCE.tsv` in the same change. By contributing you agree your
  contribution is released under the license of the files it touches.
