#!/usr/bin/env python3
"""Run the reference app's headless --dump; exit 77 (skip) when it cannot start here."""

import json
import os
import pathlib
import subprocess
import sys
import tempfile

try:
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
except (ImportError, ValueError):
    sys.exit(77)
schema = (
    pathlib.Path(sys.argv[1]).resolve().parents[2]
    / "config/dev.ncz.screensaver.gschema.xml"
)
with tempfile.TemporaryDirectory() as tmp:
    (pathlib.Path(tmp) / schema.name).write_bytes(schema.read_bytes())
    if subprocess.run(["glib-compile-schemas", tmp], check=False).returncode != 0:
        sys.exit(77)
    env = dict(os.environ, GSETTINGS_SCHEMA_DIR=tmp, GSETTINGS_BACKEND="memory")
    env.setdefault(
        "NCZ_SCREENSAVER_CATALOG",
        str(schema.parents[1] / "assets/screensaver-chooser/hacks.tsv"),
    )
    proc = subprocess.run(
        [sys.executable, sys.argv[1], "--dump"],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )
try:
    dump = json.loads(proc.stdout)
except ValueError:
    print(proc.stdout[-300:], proc.stderr[-300:])
    sys.exit(1)

# Re-derive the floor from the catalog the app actually reads instead of a
# hard-coded 79/80: the Cygnus integration catalogue is the generated
# hacks.tsv (tools/generate-catalog.py output), and counting the same file
# the --dump reads keeps this gate from drifting again when the ship set
# changes.  This is also what debian/rules compares against.
catalog = pathlib.Path(env["NCZ_SCREENSAVER_CATALOG"])
try:
    floor = sum(
        1
        for line in catalog.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    )
except OSError as exc:
    print(f"cannot read catalog {catalog}: {exc}")
    sys.exit(1)
sys.exit(0 if len(dump.get("catalog", [])) >= floor else 1)
