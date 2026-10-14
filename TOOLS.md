# TOOLS.md — Local Notes (ncz-screensavers agent)

Skills define HOW tools work. This file is for OUR specifics — the
stuff that is unique to this catalogue and that the agent loop must
remember between sessions.

## Display policy — build-host is the visible screen, build-host is the lab

**Effective 2026-09-26.** See also
`docs/superpowers/specs/2026-09-26-agent-dispatch-failure-modes.md`
§ "Test-host policy".

### Never launch a renderer on build-host without the lock

build-host (192.0.2.84) is the screen a human is watching right now.
Concurrent renderers split the GPU, make any frame-rate judgement
meaningless, and have repeatedly been left running for hours after the
job that launched them finished. The operator's previous three clear-ups
on 2026-09-26 included one `blackhole_gles3` that ran 22 minutes
alongside a shadertoy rotation.

| Action | Tool |
|---|---|
| Take the display for a timed run | `ncz-display-run -t 30 /path/to/binary` |
| See who holds it | `ncz-display-run --who` |
| Release | `ncz-display-run --stop` |

The helper takes an exclusive `flock`, kills any existing renderer
first, refuses if busy. **Never launch via `sudo -u jasonperlow`
chains** — that produced a process neither account could reap.

### Default to build-host for any rendering or capture

build-host (192.0.2.85) is the automated lab. Parallel work belongs
there.

| | |
|---|---|
| Host | `$LAB_TARGET` (user@host, from the environment) |
| Login | `SSHPASS` in the environment (password auth via `sshpass -e`) |
| SSH alias | `build-host` (configured in `~/.ssh/config`) |
| Build dir | `~/ncz-screensavers/build/blackhole_gles3` (after rebuild) |
| Compositor | live `wayland-0` on `/run/user/$(id -u)` |
| Headroom | `~/build-tmp/` is the safe work area; `/tmp` is tmpfs and fills |

## SSH wrapper — every build-host call is timed

The wrapper below is the only sanctioned way to call build-host. It
implements the lessons in `2026-09-26-agent-dispatch-failure-modes.md`:
hard `timeout` per call, strict prompt count, strict connect timeout.

```sh
# Alias to use everywhere:
peg() {
  local t="${PEGASUS_TIMEOUT:-60}"
  timeout "$t" sshpass -e ssh \
    -o NumberOfPasswordPrompts=1 -o ConnectTimeout=10 \
    build-host "$@"
}
```

Use it as `peg 'pgrep -a -f "_gles3"'` etc. If a call times out, **do
not retry blindly** — note it and move on.

## Mandatory cleanup gate before reporting

After ANY renderer work on EITHER host:

```sh
pgrep -a -f "_gles3"                          # local
peg 'pgrep -a -f "_gles3"; echo END_PEGASUS'   # remote
```

If either matches a renderer YOU started, kill it (kill by full PID,
not by `pkill -f` — see failure mode #2, pgrep/pkill matches the shell
whose command line contains the pattern). An agent that leaves a
renderer alive has not finished, whatever its report says.

## build-host hazards — known and observed

1. **`mapscroller_gles3` wedges and never exits.** Observed at 2h18m
   and again at 4min. Exclude it from sweeps, or enforce a hard
   per-target `timeout`.
2. **`/tmp` is tmpfs and fills.** Once full, `fwrite` reports success
   while files land 0-byte — silent data loss that reads as "black
   frames". Use `~/build-tmp/`. Before any large capture sweep, check
   headroom:

   ```sh
   peg 'df -h ~/build-tmp /tmp'
   ```

3. **No `cage`/`sway`/`weston` on build-host** but the lab compositor is
   already running (`wayland-0` on `/run/user/$(id -u)`). For headless
   capture, use the existing socket; do NOT start your own compositor.

4. **Local clone may be stale.** `~/ncz-screensavers` on build-host is a
   clone of the same repo. Before any build/capture work, sync it to
   `origin/master`:

   ```sh
   peg 'cd ~/ncz-screensavers && git stash && git fetch origin master \
        && git reset --hard origin/master'
   ```

5. **Rebuild after sync.** `build/blackhole_gles3` is stale until you
   `ninja -C build blackhole_gles3`. Keep a build log in
   `~/build-tmp/<target>-rebuild.log`.

## How to run a timed, seeded, framed capture of blackhole_gles3

```sh
RUN_DIR=~/build-tmp/bh-launch-<label>
mkdir -p "$RUN_DIR"
peg "NCZ_BLACKHOLE_SEED=<seed> NCZ_FRAME_DUMP=$RUN_DIR \
     timeout <N>s ~/ncz-screensavers/build/blackhole_gles3 \
     >$RUN_DIR/launch.stderr 2>&1"
# Frames 4 + every 60th land as <RUN_DIR>/frame_<8hex>.png
# [diag] blackhole line lands in launch.stderr
# Copy both back locally for inspection.
```

Then verify nothing is left running on either host. THEN report.
