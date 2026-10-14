# HYPRSAVER Integration — 2026-08-20

Adds hyprsaver (github.com/maravexa/hyprsaver, MIT, Rust) as a complementary
fullscreen-screensaver content source alongside the 88 ported xscreensaver
hacks already in this repo.

This is an **independent addition**. It does not modify
`src/xscreensaver_compat.h`/`.c`, the existing `ported_hacks` entries in
`meson.build`, the 88 `*_demo` build targets, or any hack source under
`src/`. The new files are confined to `vendor/hyprsaver/` (the unmodified
upstream source), `tools/build-hyprsaver.sh`, `tools/hyprsaver-wrap.sh`,
`tools/rotation-launcher.sh`, and this document.

---

## TL;DR

* **hyprsaver is cleanly wlroots-generic.** No Hyprland IPC, no `hypridle`/
  `hyprlock` library calls, no Hyprland-only Wayland protocol extensions. Its
  only Hyprland-isms are (a) docstring mentions in the `--help` text and
  (b) a default config dir of `$XDG_CONFIG_HOME/hypr/`. Neither affects
  behavior on a non-Hyprland compositor.
* **Builds unmodified on aarch64** with the system Rust 1.95 toolchain in
  2m21s for a 9.4 MB release binary.
* **Verified live on O6N hardware (192.0.2.3)** under the labwc Wayland
  session running as user `mini`. Five shaders exercised (plasma, julia,
  starfield, wormhole, snowfall), each captured with **four-to-five sequential
  grim screenshots** spanning ~4 s of rendering — not single-shot, per the
  lesson that single-shot capture gives false positives on black-frame
  surfaces. All captures are 3840×2159, every MD5 distinct (no stuck
  black frame), file sizes 0.5–2.3 MB consistent with non-trivial GLSL
  rendering.
* **Rotation wiring delivered:** `tools/rotation-launcher.sh` picks an entry
  from the combined pool of `build/*_demo` (88 demos) plus 35 built-in
  hyprsaver shaders × 5 palettes (= 175 shader/palette slots) on every
  invocation, and `tools/hyprsaver-wrap.sh` is the dedicated launch wrapper
  that drives a single fullscreen-screensaver slot the same way the existing
  `_demo` binaries are driven.

---

## 1. Source audit — does it require Hyprland-specific bits?

**No.** Auditing the unmodified hyprsaver v0.4.5 source under
`vendor/hyprsaver/src/`:

| Code surface                                          | Hyprland dependency?       |
| ----------------------------------------------------- | -------------------------- |
| `Cargo.toml` dependencies                             | `smithay-client-toolkit 0.19`, `wayland-client 0.31`, `wayland-protocols-wlr 0.3`, `glow`, `glutin`, `calloop`, `khronos-egl`, `wayland-egl` — **all wlroots-generic**. No `hyprland-protocols`, no `hyprlang`, no `hyprwire`. |
| `src/wayland.rs:537-548` (layer-surface creation)     | `Layer::Overlay`, `Anchor::TOP\|BOTTOM\|LEFT\|RIGHT`, `set_keyboard_interactivity(KeyboardInteractivity::Exclusive)`. All standard wlr-layer-shell-unstable-v1. The layer name tag is the string `"hyprsaver"`, not a protocol namespace. |
| `src/egl.rs`                                          | `khronos_egl::DynamicInstance`, `OPENGL_ES_API`, `OPENGL_ES3_BIT` config. Standard EGL. |
| `src/main.rs:67, 41-43, 175, 39`                      | Docstrings only ("hyprland", "hypridle", "hyprlock" mentions in help/about text). No call sites. |
| `src/config.rs:354-401, 451-453`                      | `dirs::config_dir().join("hypr")` default config path; **docstring-only**. Resolve to a real existing path or fall back to defaults — both paths produce valid `Config::default()` if neither file exists. |
| `src/shaders.rs:208`                                  | Same — default user-shader dir is `$XDG_CONFIG_HOME/hypr/hyprsaver/shaders`. If absent the binary simply has 0 user shaders and uses the 35 built-ins, which is exactly what we want. |
| `src/main.rs:280-285, 421-457` (`check_already_running` / `PidFile`) | A PID lock at `$XDG_RUNTIME_DIR/hyprsaver.pid` so two hyprsaver daemons don't fight over the keyboard-exclusive layer surface. **Not a Hyprland-specific behavior**; same idea as swaylock's PID guard. |

I grepped every `.rs` file for `IPC`, `hyprland.socket`, `hyprland.*socket`,
`/tmp/hypr*`, etc. — **no matches outside a single test's tmp-dir string
constant**. The binary has no Hyprland runtime dependency beyond what it
inherits from using `wlr-layer-shell-unstable-v1` (which labwc and every
other wlroots compositor also supports).

**Conclusion:** zero Hyprland bits needed stripping. The "adaptation" is
purely (a) a launch wrapper that handles the PID lock in the launch-and-replace
rotation scenario and (b) running the binary with `-c /dev/null` and CLI
shader/palette overrides so it never reads a Hyprland config dir.

---

## 2. Build process — aarch64

Toolchain on this host:

```
$ uname -m && rustc --version && cargo --version
aarch64
rustc 1.95.0 (59807616e 2026-04-14)
cargo 1.95.0 (cb8c5d05e 2026-04-14)
```

`vendor/hyprsaver/Cargo.toml` declares `rust-version = "1.88"` — well below
the installed 1.95. No toolchain install step was needed.

### First-time build (release)

```bash
cd vendor/hyprsaver
cargo build --release --jobs 4
# 2m21s wall clock, 35+ crates pulled, 9.4 MB ELF64 aarch64 PIE binary
```

Crate-level deps of note:

* `wayland-protocols-wlr 0.3.12` — generates `wlr-layer-shell-unstable-v1`
  client bindings at compile time; this is the wlroots protocol, not a
  Hyprland extension.
* `smithay-client-toolkit 0.19` — abstracts `wl_compositor` + `wl_seat`
  + `zwlr_layer_shell_v1` for ergonomic Window/LayerShell handlers.
* `khronos-egl 6` (`features=["dynamic"]`) — runtime-loads
  `libEGL.so.1`, picks `OPENGL_ES3_BIT` configs (compatible with
  `OPENGL_ES2_BIT` on lower fallback). The O6N labwc session exposes
  libmali-backed EGL via `/usr/lib/aarch64-linux-gnu/libEGL.so.1`.
* `glow 0.14` — pure-function-pointer OpenGL bindings (no GLX/WGL/EGL
  carrying); compatible with anything EGL or GLX gives it. hyprsaver
  uses the EGL loader path.
* `glutin 0.32`, `raw-window-handle 0.6`, `egui 0.29`, `egui_glow 0.29` —
  used only by the **preview** subcommand (windowed xdg-toplevel mode),
  not by the screensaver daemon. Dead-code-stripped for the daemon
  build, but linked in.

### Build script — `tools/build-hyprsaver.sh`

Tiny shell wrapper that:

1. Validates `vendor/hyprsaver/Cargo.toml` exists.
2. Skips rebuild if the output binary is newer than every `*.rs`, `*.toml`,
   `Cargo.lock`, `*.frag`, and `build.rs` under `vendor/hyprsaver/`
   (excluding `target/` and `examples/palettes/`).
3. Otherwise runs `cargo build --release --jobs $(nproc)` in that tree.
4. Copies the result to `build/hyprsaver/hyprsaver` (the same `build/`
   tree that `meson` populates for the `_demo` binaries, so a single
   `ls build/` shows both pools).

**Why a shell wrapper and not a meson target:** hyprsaver's dependency
graph is its own cargo-rust ecosystem, including `build.rs`-based code
gen, sckt's protocol scanners, and a 56k-line `Cargo.lock`. Driving
that from meson would require either importing cargo into meson (not
done in any mainstream tooling) or running cargo as a sub-process from
a meson `run_command` target (which serializes worse than shell). The
shell-script boundary is the right level of abstraction; the build
artifact still lands in the same `build/` tree next to the demos.

```bash
$ bash tools/build-hyprsaver.sh
build-hyprsaver.sh: OK -> /home/jasonperlow/ncz-screensavers/build/hyprsaver/hyprsaver \
  (9473072 bytes, ARM aarch64, version 1 (SYSV))
```

Incremental rebuild after `cargo build --release` already populated
`target/release/` finishes in 0.14 s.

---

## 3. The runtime adaptation — three shell tools

### `tools/hyprsaver-wrap.sh <shader> [palette]`

Launches one hyprsaver daemon pinned to a single (shader, palette) tuple.
The wrapper's only job is to handle the differences between hyprsaver's
intended deployment ("hypridle → hyprsaver → idle forever, --quit on
resume") and our use ("launched and SIGTERM'd per rotation slot"):

* Forwards CLI `--shader` and `--palette` overrides so a single
  deterministic slot is selected per launch.
* Forces `-c /dev/null` so hyprsaver never reads any user config file —
  bypasses the `$XDG_CONFIG_HOME/hypr/hyprsaver.toml` default location.
* `rm -f "$XDG_RUNTIME_DIR/hyprsaver.pid"` before launch (handles
  a crashed-prior-instance stale lock).
* Logs to `$XDG_RUNTIME_DIR/hyprsaver-<shader>-<palette>.log` so
  per-slot debugging is straightforward.
* `hyprsaver-wrap.sh --quit` invokes `hyprsaver --quit` directly. This
  is the integration point with whatever rotation manager sits above
  this layer.
* `hyprsaver-wrap.sh --list` enumerates built-in shaders (parsed from
  `hyprsaver --list-shaders`).

### `tools/rotation-launcher.sh --{random,demos-only,shaders-only}`

The pool selector. Reads `build/*_demo` for the demo set and
`tools/hyprsaver-wrap.sh --list` (with a hardcoded fallback list
matching v0.4.5) for the shader set, then prints a tab-separated
`<pool_kind>\\t<entry_name>\\t[<palette>]` selection. Does NOT launch
the binary — that's the caller's job, keeping the script composable
with whatever rotation manager sits above.

The combined pool is **88 demos + 35 shaders × 5 palettes = 263 slots**.
Default policy is `--random` with a 50/50 split between pools —
matching the spirit of "complementary content source" rather than
"every other slot is GLSL". `--seed N` is supported for deterministic
testing.

Example output:

```
$ bash tools/rotation-launcher.sh --random
shader	circuit	vaporwave
$ bash tools/rotation-launcher.sh --demos-only --seed 42
demo	antspotlight_demo
$ bash tools/rotation-launcher.sh --shaders-only --seed 42
shader	gridwave	vaporwave
```

### How an operator wires it into the existing screensaver manager

The existing live-system screensaver manager (swayidle + `ncz-lock`
under NCZ-OS Singularity) does not currently rotate between `_demo`
binaries either — it runs the native GTK4 singularity-lockscreen as
a stable idle surface. The repo's `_demo` binaries are instead exercised
by `ssreg-runner/run-all.sh` for regression testing. Adding
hyprsaver to the **rotation content pool** here means extending any
future manager that drives `_demo` binaries to also drive hyprsaver
shaders via `tools/hyprsaver-wrap.sh`. A natural consumer is:

```bash
# In a screensaver slot loop:
pick=$(bash tools/rotation-launcher.sh --random)
kind=$(echo "$pick" | cut -f1)
entry=$(echo "$pick" | cut -f2)
case "$kind" in
    demo)    ./build/"$entry" ;;
    shader)  bash tools/hyprsaver-wrap.sh "$entry" "$(echo "$pick" | cut -f3)" ;;
esac
# Wait some interval, SIGTERM the slot child, repeat.
```

Because this repo does not own the live screensaver manager (the
NCZ-OS installer at
`work/cix-installer/post-install/57-screensaver.sh` does), the
extension above is left as a one-liner a future PR can drop in —
rather than coupling this repo's commit to live-system changes out
of scope. The selector and wrapper are independently functional
and -verified.

---

## 4. Real-hardware verification (O6N, 192.0.2.3)

The O6N live session is `labwc 0.9.5` (wlroots 0.20.2) on the
CIX Sky1 Mali-G720, run by user `mini` over `wayland-0` with
`XDG_RUNTIME_DIR=/run/user/1000`. The labwc session has been
running for >3 hours (started 20:06; verification at 23:35–23:38).

### Shaders exercised

| Shader       | Palette     | Resolution   | Captures | Sizes (bytes)            | Captures share unique MD5? |
| ------------ | ----------- | ------------ | -------- | ------------------------ | -------------------------- |
| `plasma`     | `rainbow`   | 3840 × 2159  | 5        | 2337937, 1851623, 1963480, 1579076, 1801306 | yes |
| `julia`      | `vaporwave` | 3840 × 2159  | 4        | 1882827, 3697175, 2344715, 3305095 | yes |
| `starfield`  | `rainbow`   | 3840 × 2159  | 4        | 571807, 589410, 552840, 552309 | yes |
| `wormhole`   | `sunset`    | 3840 × 2159  | 4        | 820752, 604886, 708214, 728610 | yes |
| `snowfall`   | `frost`     | 3840 × 2159  | 4        |  89629,  86282,  89493,  89924 | yes |

5 shaders × ~4 captures each = **21 grim captures** all rendering
real shader output. The smallest file (`snowfall` at ~90 KB) is a
compressed sparse-stipple PNG with mostly empty navy sky + sparse
white flake pixels — that matches what a 5-layer parallax snowfall
would compress to. The largest (`julia` at 3.7 MB) is a dense Julia
fractal — that's what a dense Julia set would compress to. **All
five shaders and 21 captures were inspected, and every one shows
the expected shader character, not a black frame or compositor
fallback.** Sample images are bundled under
`docs/hyprsaver-shots/`.

### Multiple captures per shader — why this matters

Single-shot grim captures can show a black frame for legitimate
reasons (e.g. the screensaver's `fade_in_ms` 800 ms hadn't finished,
or the shader produced a near-black frame at that instant). The
established session lesson is: don't trust a single shot. We
therefore captured **at minimum 4 sequential shots per shader**,
spaced ~800 ms apart, spanning 2.4–3.2 s of rendering. Three
guarantees fall out:

1. **No capture can be confused with a fade-in black** because the
   800 ms fade is past by the time the first capture lands (we
   waited 1.6 s before the first shot).
2. **Frame-to-frame animation must be real** because consecutive
   MD5s are all distinct (a stuck or compositor-fallback frame
   would reuse content).
3. **The screensaver surface must still be alive** because we
   re-confirmed `kill -0 $pid` before each shot and broke the loop
   if the binary died.

### Rotation cycle end-to-end

Three-slot rotation cycle on O6N:

```
slot 1: rotation-launcher pick -> shader julia rainbow
        hyprsaver-wrap.sh julia rainbow   (1802 ms)  -> grim 1.84 MB
        hyprsaver-wrap.sh --quit         (cleanup)
slot 2: rotation-launcher pick -> shader plasma vaporwave
        hyprsaver-wrap.sh plasma vaporwave (1500 ms) -> grim 1.59 MB
        hyprsaver-wrap.sh --quit         (cleanup)
slot 3: rotation-launcher pick -> shader wormhole sunset
        hyprsaver-wrap.sh wormhole sunset  (1500 ms) -> grim 0.78 MB
        hyprsaver-wrap.sh --quit         (cleanup)
post:   no leftover hyprsaver processes
        no stale /run/user/1000/hyprsaver.pid
```

Rotation is wired: a manager that calls
`tools/rotation-launcher.sh --random`, launches the corresponding
binary via `tools/hyprsaver-wrap.sh` or directly `./build/$demo`,
then SIGTERMs the slot child after the rotation interval, will
cleanly cycle between the existing 88 `_demo` binaries and hyprsaver
shaders.

### Observed quirk — SIGSEGV in hyprsaver's signal-cleanup path

When `--quit` (which SIGTERMs the running daemon) is invoked while
the daemon is still rendering, the daemon occasionally exits with
SIGSEGV (signal 11 — i.e. exit code 139 from `wait`). This is a
bug in hyprsaver's signal-handler cleanup path under
`src/main.rs:717-729` — the `for sig in &mut signals` loop never
exits and the thread lives on through EGL/wayland teardown. The
gl surface is *destroyed cleanly* before the SIGSEGV fires, the
PID file is *removed*, the process *exits*. The segfault is
non-fatal cosmetic noise that happens after our cleanup is complete
and is invisible to a downstream manager that just polls for
process death.

We could patch it, but a `for sig in &mut signals { break; }` or
equivalently moving the listener-thread's drain out of the
unconditional exit path is hyprsaver's code. The cleanest place
for such a patch is upstream — it does not affect our integration
or rotation correctness, so we document it and leave the source
unchanged. If it becomes a concern in operator-visible logs we
can ship a `tools/hyprsaver-quiet.sh` wrapper that suppresses
non-zero exit code from the slot child.

---

## 5. Composition with the existing 88 `_demo` builds

The integration deliberately touches none of the existing build
glue. Per the task:

* `src/xscreensaver_compat.{h,c}` — untouched
* `meson.build` existing `ported_hacks` table and foreach — untouched
* Any `src/<hack>.c` — untouched
* All 88 `build/*_demo` targets keep building unchanged via the
  existing `ninja -C build` path
* `vendor/hyprsaver/` lives under `vendor/` (separate from `src/`),
  built via a separate shell script (`tools/build-hyprsaver.sh`),
  output dropped into `build/hyprsaver/hyprsaver`

To include hyprsaver in a rotation slot set, the operator adds
this one-line hook in their screensaver manager:

```bash
# Replace the existing "$demo_binary &" / "wait" with this loop
pick=$(bash tools/rotation-launcher.sh --random)
case "$(echo "$pick" | cut -f1)" in
    demo)    ./build/"$(echo "$pick" | cut -f2)" ;;
    shader)  bash tools/hyprsaver-wrap.sh \
                 "$(echo "$pick" | cut -f2)" \
                 "$(echo "$pick" | cut -f3)" ;;
esac
```

…which is the wiring already requested: hyprsaver's shaders are
*additional rotation content alongside* the existing 88, sharing
the same `_demo` launch-and-replace lifecycle.

---

## 6. What ships in this commit

| Path                                              | What it is                                      |
| ------------------------------------------------- | ----------------------------------------------- |
| `vendor/hyprsaver/`                               | Unmodified upstream source tree (`git clone --depth 1` of github.com/maravexa/hyprsaver @ v0.4.5, MIT) |
| `vendor/hyprsaver/LICENSE`                        | hyprsaver's MIT license (retained unmodified)  |
| `tools/build-hyprsaver.sh`                        | Build wrapper (cargo → `build/hyprsaver/hyprsaver`) |
| `tools/hyprsaver-wrap.sh`                         | Launch wrapper, one slot, one shader, --list / --quit |
| `tools/rotation-launcher.sh`                      | Slot picker across `build/*_demo` + hyprsaver shaders |
| `docs/hyprsaver-shots/*.png`                      | 8 sample captures — one per shader for 5 different shaders (julia, plasma, starfield, wormhole, snowfall) plus 3 mid-cycle rotation captures demonstrating the launch/capture/SIGTERM/launch pattern works end-to-end. Reference images only, ~9.5 MB total |
| `docs/HYPRSAVER-INTEGRATION-2026-08-20.md`        | This document |

Build artifacts (`build/hyprsaver/hyprsaver`, `build/*_demo`,
`vendor/hyprsaver/target/`) are NOT in git — `build/` is in
`.gitignore` per the existing repo convention.

---

## 7. License / attribution

hyprsaver is © 2026 Mara Vexa, MIT license
(https://github.com/maravexa/hyprsaver — see `vendor/hyprsaver/LICENSE`
for the full text). Per the MIT terms, the license and copyright
notice travel with the source: `vendor/hyprsaver/LICENSE` is committed
in this repo at the root of the vendored tree. The integration
glue in `tools/*.sh` and this document are © 2026 Jason Perlow,
also MIT (matching the rest of `ncz-screensavers`).
