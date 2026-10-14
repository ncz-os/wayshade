# Final targeted visual-bug pass — 2026-09-25

This pass started from `dc54691` and tested only the 13 targets named in the
request.  Testing used real `grim` captures on build-host with NVIDIA RTX 2060
PRIME offload.  The retained full-resolution evidence is on build-host under
`~/build-tmp/finalfix-run-exclusive2/`; the local review copy is under
`~/build-tmp/finalfix-exclusive2/`.

The matrix's automated PASS threshold is not a correctness result for these
targets.  Manual review of the full-resolution captures still shows the known
failures below.

| Target | Focused result | Manual result / root-cause evidence |
|---|---|---|
| `mapscroller` | process abort (`134`) | `mapscroller.pl` is absent from build-host and from the repository. Stderr proves `execvp` fails. The helper child then calls the harness-wide fatal path from the forked process. Packaging the upstream network/cache helper, or replacing it with an in-process tile loader, is required; merely suppressing the abort would render an error screen rather than map content. |
| `crackberg` | automated PASS | Terrain remains separated into floating plates. The frame is genuinely produced by the hack, not capture bleed-through. |
| `lavalite` | automated PASS | Housing is present but the metaball interior remains empty. Recording texture enable/disable/bind state in emulated display lists was tested and did not restore it. |
| `highvoltage` | automated PASS | Frame remains a nearly solid pale field with no visible electrical model. Brightness flicker can satisfy pixel-diff but is not a content fix. |
| `cube21` | automated PASS | Only tiny malformed white fragments render. Converting legacy `GL_LUMINANCE` uploads to RGBA8 was tested and did not restore the cube. |
| `rubikblocks` | automated PASS | Only a small malformed monochrome block renders. The same luminance conversion did not restore the intended scene. |
| `unknownpleasures` | automated PASS | Frame remains a blown-out white rectangle with lines at the top. Dynamic display-list vertex storage prevents truncation but is not sufficient. |
| `gears` | automated PASS | Geometry remains a thin vertical stack of pastel blocks, not gears. |
| `glhanoi` | automated PASS | Floor and only a tiny partial tower render; disks remain absent. |
| `fliptext` | automated PASS | Output remains a solid cyan vertical bar. The current geometric fallback font does not represent glyphs and cannot reproduce flipping text. |
| `kallisti` | automated PASS | Apple silhouette remains solid white with no surface definition. |
| `dangerball` | automated PASS | Smooth sphere remains; spike geometry is absent. |
| `nakagin` | automated PASS | Building silhouette remains solid white with no capsule/window surface detail. |

## Investigation conclusions

- The `gears` / `glhanoi` / `fliptext` similarity is visual, not a single
  fallback branch: the first two are legacy display-list geometry users;
  `fliptext` depends on the texture-font subsystem, whose Wayland fallback
  currently draws character-sized solid quads.
- `kallisti`, `dangerball`, and `nakagin` do not share a missing texture-file
  path. `dangerball` is procedural and untextured. Their common symptom comes
  from incomplete fixed-function/display-list emulation (material state and
  compound procedural geometry), not one failed image load.
- Two plausible shared compatibility changes were implemented and tested on
  build-host, then reverted after full-resolution captures showed no correction:
  portable conversion of one-channel luminance textures, and recording legacy
  texture/enable state inside display lists.
- No target is marked fixed in this pass. Shipping animation-only flicker,
  primitive placeholders, or threshold-crossing output would contradict the
  required human visual standard.

The reasonable next step is a dedicated compatibility-layer project: add a
conformance harness for matrix composition, nested display lists, list-time GL
state, client arrays, materials, and texture formats, then compare those
fixtures against desktop OpenGL before revisiting these targets.
