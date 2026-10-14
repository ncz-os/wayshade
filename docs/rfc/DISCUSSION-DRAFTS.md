# GitHub Discussion drafts (NOT posted)

Two draft posts, one per RFC. They are deliberately under 200 words each and
polite. Before posting, replace `<link to RFC 0001>` / `<link to RFC 0002>` with
the gist or branch URL, and replace `<model-version>` with the concrete model
token. If a post ends up containing no assisted content, drop the two trailer
lines.

The trailer format is the singularityos-lab one: `Assisted-by:` followed by
`AI-Scope:` (hyphenated), one sentence, ASCII punctuation.

---

## Discussion 1: Sensors configuration layer

Hi Mirko,

Following our 2026-09-14/16 thread: I wrote up the sensors configuration layer
we agreed on, now that sensors is an opt-in plugin rather than core.

The short version: `SensorMonitor` already gives us one backend for
CPU/GPU/NPU/VPU/memory/storage/temps/fans/clocks. The RFC leaves that alone and
adds only a configuration model on top: enumerate the categories the backend
actually reports, let the user toggle and group them, pick a visualisation, and
choose what the top-of-panel summary strip shows. Categories the hardware does
not report are never offered, and every key has a real consumer in the same
change.

It also picks up the six `sensors-*` keys from desktop #267 and proposes a home
for them so the core schema stops growing.

I would value your read on two things: whether the new keys should live in a
plugin-owned schema, and whether the Performance page's fan section should share
the same model.

Full RFC: `<link to RFC 0001>`

Assisted-by: Claude Code:<model-version>
AI-Scope: drafted the sensors configuration RFC summary and its open questions.

---

## Discussion 2: Screensaver integration

Hi Mirko,

Separate question: Singularity has a real lock screen and greeter, but no
screensaver concept. I wrote a design for what a generic saver would need from
the session and where the boundary should sit.

The core is ordering and ownership. Idle notification, then the saver, then the
lock, then output power off; on input, reverse it. The saver never holds the
lock and never sees credentials, runs unprivileged, and is killed on unlock.
That keeps the security property where it belongs, in the lock client.

I propose a small vendor-neutral contract rather than a shell feature: saver
discovery through `.desktop` entries, an optional `org.freedesktop.ScreenSaver`
name, and a staged plan where stage 0 is a plugin with no shell change at all.
Stage 2 is the only stage that touches upstream, and it would carry no
downstream code or daemon names.

The open questions are in the RFC; the main one is whether you want a
screensaver at all.

Full RFC: `<link to RFC 0002>`

Assisted-by: Claude Code:<model-version>
AI-Scope: drafted the screensaver integration RFC and its open questions.
