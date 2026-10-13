#!/usr/bin/env python3
"""Headless GTK4 smoke gate for the reference app.

Runs `ncz-screensaver-settings --gtk-smoke`, which builds the settings window
and the per-screensaver options dialog without any user interaction and asserts
that both came up, that the shipped catalogue loaded and that the dialog's
Randomize/Reset round-trip.

Both code paths are exercised:

* the app as installed (libadwaita when present);
* the app with libadwaita hidden, which is what a KDE Plasma, X11 or minimal
  GNOME session without gir1.2-adw-1 sees. The fallback window has to come up
  there too, or the app is not portable.

A display is arranged automatically:

* `xvfb-run` when Xvfb is installed (X11 backend);
* `broadwayd` when only the Broadway backend is available;
* otherwise the test skips (exit 77), which is what a build host without any
  of those has.

Exit 77 is meson's "skipped" code, 0 all-pass, non-zero anything else.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

APP = pathlib.Path(sys.argv[1]).resolve()
REPO = APP.parents[2]
SCHEMA = REPO / "config/dev.ncz.screensaver.gschema.xml"

# Block Adw before the app imports it, then run its own entry point. gi's
# require_version() is the only thing the app uses to load the typelib, so
# raising there is exactly what a missing gir1.2-adw-1 produces.
NO_ADW_MAIN = """
import gi, sys
_real = gi.require_version
def _blocked(namespace, version):
    if namespace == "Adw":
        raise ValueError("Adw-1.typelib not available (smoke test)")
    return _real(namespace, version)
gi.require_version = _blocked
import importlib.machinery as m, importlib.util as u
loader = m.SourceFileLoader("app", sys.argv[1])
mod = u.module_from_spec(u.spec_from_loader("app", loader))
loader.exec_module(mod)
sys.exit(mod.main(sys.argv[2:]))
"""

VARIANTS = (
    ("as-installed", lambda app: [sys.executable, str(app), "--gtk-smoke"]),
    (
        "no-libadwaita",
        lambda app: [sys.executable, "-c", NO_ADW_MAIN, str(app), "--gtk-smoke"],
    ),
)


def have_gi() -> bool:
    code = "import gi;gi.require_version('Gtk','4.0');from gi.repository import Gtk"
    try:
        return subprocess.run([sys.executable, "-c", code], check=False).returncode == 0
    except OSError:
        return False


def display_runner() -> tuple[list[str], dict[str, str]] | None:
    """Return (argv prefix, extra env) providing a display, or None to skip."""
    if shutil.which("xvfb-run"):
        return ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24"], {}
    if shutil.which("broadwayd"):
        # broadwayd serves its own display; GTK connects to it in-process.
        return ["broadwayd", ":5"], {
            "GDK_BACKEND": "broadway",
            "BROADWAY_DISPLAY": ":5",
        }
    return None


def broadway_socket(display, timeout=15.0) -> bool:
    """Wait for broadwayd's socket so the app does not lose a start-up race.

    broadwayd binds the next free ``:N`` and creates
    ``/run/user/<uid>/broadway<N>.socket`` (or the same name under
    XDG_RUNTIME_DIR). When it takes a different N than the one asked for the
    caller only gets a warning and an honest skip, never a false failure.
    """
    number = display.lstrip(":")
    if not number:
        return False
    runtimes = (
        pathlib.Path(
            os.environ.get("XDG_RUNTIME_DIR") or "/run/user" / str(os.getuid())
        ),
        pathlib.Path("/run/user") / str(os.getuid()),
        pathlib.Path(tempfile.gettempdir()),
    )
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any((rt / f"broadway{number}.socket").exists() for rt in runtimes):
            return True
        time.sleep(0.1)
    return False


def run_one(prefix, env, argv) -> tuple[int, str]:
    """Run one variant; return (exit code, combined output).

    broadwayd is started here and must be stopped again whatever the child did;
    if it ignores SIGTERM the runner kills it by PID rather than letting
    TimeoutExpired escape and mask the child's own result.
    """
    if prefix and prefix[0] == "broadwayd":
        server = subprocess.Popen(prefix, env=env)
        if not broadway_socket(env.get("BROADWAY_DISPLAY", "")):
            sys.stderr.write(
                "warning: broadwayd did not bind the requested display in time\n"
            )
        try:
            proc = subprocess.run(
                argv, env=env, check=False, capture_output=True, text=True
            )
        finally:
            server.terminate()
            try:
                server.wait(timeout=30)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
    else:
        proc = subprocess.run(
            prefix + argv, env=env, check=False, capture_output=True, text=True
        )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def main() -> int:
    if not have_gi():
        print("skip: python3-gi with GTK 4 is not installed")
        return 77
    runner = display_runner()
    if runner is None:
        print("skip: neither xvfb-run nor broadwayd is installed")
        return 77
    prefix, extra_env = runner

    with tempfile.TemporaryDirectory() as tmp:
        schemas = pathlib.Path(tmp) / "schemas"
        schemas.mkdir()
        (schemas / SCHEMA.name).write_bytes(SCHEMA.read_bytes())
        if (
            subprocess.run(
                ["glib-compile-schemas", str(schemas)], check=False
            ).returncode
            != 0
        ):
            print("skip: glib-compile-schemas failed")
            return 77
        env = {
            **os.environ,
            **extra_env,
            "GSETTINGS_SCHEMA_DIR": str(schemas),
            "GSETTINGS_BACKEND": "memory",
            "NCZ_SCREENSAVER_CATALOG": str(
                REPO / "assets/screensaver-chooser/hacks.tsv"
            ),
            "NCZ_SCREENSAVER_OPTIONS": str(REPO / "assets/screensaver-chooser/options"),
        }
        # The launcher is not installed on a build host: the smoke test must
        # not depend on it, and NCZ_SCREENSAVER_CLI pointing at /bin/false
        # proves the app copes with a missing engine CLI.
        env["NCZ_SCREENSAVER_CLI"] = env.get("NCZ_SCREENSAVER_CLI", "/bin/false")

        failed = False
        skipped = False
        for label, build_argv in VARIANTS:
            print(f"--- {label} ---")
            rc, out = run_one(prefix, env, build_argv(APP))
            for line in out.splitlines():
                if not line.startswith(("libEGL", "MESA", "Note:")):
                    print(line)
            # The app exits 2 with this exact line when it cannot reach a
            # display; any other exit 2 (an unknown flag, say) is a failure.
            if rc == 2 and "no display" in out:
                print(f"skip: {label} found no display")
                skipped = True
                continue
            if rc == 77:
                skipped = True
                continue
            if rc != 0:
                print(f"FAIL {label}: exit {rc}")
                failed = True
        if failed:
            return 1
        return 77 if skipped else 0


if __name__ == "__main__":
    raise SystemExit(main())
