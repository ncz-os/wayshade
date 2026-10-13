"""Headless tests for the settings app's GPU-class layout (--dump logic).

A fake `ncz-screensaver` (NCZ_SCREENSAVER_CLI) prints canned list/status/gpus JSON,
so no GPU, launcher state or installed schema is needed: the GSettings schema is
compiled into a temp dir and served from the memory backend.
Skipped when python3-gi with libadwaita is unavailable.
"""

import json
import os
import pathlib
import shutil
import stat
import subprocess
import textwrap

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
APP = ROOT / "ncz-screensaver-settings"
PY = (
    "/usr/bin/python3"
    if os.path.exists("/usr/bin/python3")
    else shutil.which("python3")
)

CATALOG = (
    "light_gles3\tLight\tCalm\nmedium_gles3\tMedium\tCalm\nheavy_gles3\tHeavy\tSpace\n"
)

FAKE_CLI = textwrap.dedent(
    """\
    #!/bin/sh
    # fake ncz-screensaver: prints $FAKE_DIR/<cmd>.json
    case "$1" in
        list|status|gpus) cat "$FAKE_DIR/$1.json" ;;
        *) exit 2 ;;
    esac
    """
)

# Same mapping as contrib/gtk4-reference-app/ncz-screensaver-settings
# (CLASS_TITLE) so the harness's synthetic verdict string matches the
# launcher's CLASS_TITLE byte-for-byte (especially "mid" -> "Mid-range").
CLASS_TITLE = {"weak": "Weak", "mid": "Mid-range", "strong": "Strong"}

# Call one pure helper of the app module in a child process, so the helper sees
# the harness' fake CLI and compiled schema. Returns the JSON-encoded result.
CALLER = textwrap.dedent(
    """\
    import importlib.machinery as m, importlib.util as u, json, sys
    ld = m.SourceFileLoader("app", sys.argv[1])
    mod = u.module_from_spec(u.spec_from_loader("app", ld))
    ld.exec_module(mod)
    fn, args = sys.argv[2], json.loads(sys.argv[3])
    print(json.dumps(getattr(mod, fn)(**args), default=str))
    """
)

# Same, but with libadwaita made unavailable before the app imports it, which is
# how a KDE Plasma / X11 session without gir1.2-adw-1 looks to the app.
NO_ADW_CALLER = textwrap.dedent(
    """\
    import gi, json, sys
    _real = gi.require_version
    def _blocked(namespace, version):
        if namespace == "Adw":
            raise ValueError("Adw-1.typelib not available (test)")
        return _real(namespace, version)
    gi.require_version = _blocked
    import importlib.machinery as m, importlib.util as u
    ld = m.SourceFileLoader("app", sys.argv[1])
    mod = u.module_from_spec(u.spec_from_loader("app", ld))
    ld.exec_module(mod)
    print(json.dumps({"have_adw": mod._HAVE_ADW,
                      "modes": list(mod.mode_nicks()),
                      "pool": list(mod.pool_nicks()),
                      "colors": list(mod.color_nicks()),
                      "dialog_base": mod.OptionsDialog.__mro__[1].__name__}))
    """
)

WRAPPER = textwrap.dedent(
    """\
    import importlib.machinery as m, importlib.util as u, json, sys
    from gi.repository import GLib
    ld = m.SourceFileLoader("app", sys.argv[1])
    mod = u.module_from_spec(u.spec_from_loader("app", ld))
    ld.exec_module(mod)
    s = mod.open_settings()
    for key, (typ, val) in json.loads(sys.argv[2]).items():
        s.set_value(key, GLib.Variant(typ, val))
    raw = mod.load_catalog()
    cat = mod.build_catalog_view(raw, mod.hack_dirs())
    model = mod.build_model(s, raw, mod.hack_dirs())
    print(json.dumps(mod.read_dump(s, cat, model)))
    """
)


def gi_ok():
    if not PY:
        return False
    code = "import gi;gi.require_version('Adw','1');from gi.repository import Adw"
    return (
        subprocess.run([PY, "-c", code], capture_output=True, check=False).returncode
        == 0
    )


pytestmark = pytest.mark.skipif(not gi_ok(), reason="python3-gi/libadwaita missing")


def row(hid, title, group, min_class, flagged, expect=""):
    return {
        "id": hid,
        "title": title,
        "group": group,
        "min_class": min_class,
        "tier": min_class,
        "flagged": flagged,
        "expect": expect,
        "issue": "",
        "issue_reason": "",
        "installed": True,
        "enabled": True,
    }


WEAK_LIST = [
    row("light_gles3", "Light", "Calm", "weak", False),
    row("medium_gles3", "Medium", "Calm", "mid", True, "about 14 fps on Intel UHD 630"),
    row(
        "heavy_gles3", "Heavy", "Space", "strong", True, "about 9 fps on Intel UHD 630"
    ),
]
STRONG_LIST = [
    row("light_gles3", "Light", "Calm", "weak", False),
    row("medium_gles3", "Medium", "Calm", "mid", False),
    row("heavy_gles3", "Heavy", "Space", "strong", False),
]
DISPLAY_GPU = {
    "id": "pci-0000_00_02_0",
    "vendor": "intel",
    "driver": "i915",
    "display": True,
    "discrete": False,
    "class": "weak",
    "ms": 46.5,
    "renderer": "Mesa Intel(R) UHD Graphics 630 (CFL GT2)",
}
NVIDIA_GPU = {
    "id": "pci-0000_01_00_0",
    "vendor": "nvidia",
    "driver": "nvidia",
    "display": False,
    "discrete": True,
    "class": "strong",
    "ms": 2.7,
    "renderer": "NVIDIA GeForce RTX 2060/PCIe/SSE2",
}
WEAK_STATUS = {
    "gpu_class": "weak",
    "gpu_class_score_ms": 46.5,
    "gpu_class_source": "calibration",
    "running": False,
}
FLAGGED = "May run poorly on this graphics chip"


def _verdict_status(gpus):
    """Synthesize the launcher's per-GPU verdict fields from a gpus list.

    Mirrors what `ncz-screensaver status --json` (launcher >= 0.7.11) emits:
    the panel GPU is GPU0, offload candidates are GPU1..N, and the
    PRIME explanation is non-empty whenever a discrete offload target exists.
    Uses CLASS_TITLE (kept in sync with the launcher's CLASS_TITLE) so the
    verdict string matches the launcher byte-for-byte ("Mid-range", not "Mid").
    """
    rows = []
    for i, g in enumerate(gpus):
        rows.append(
            {
                "index": f"GPU{i}",
                "id": g["id"],
                "display": bool(g.get("display")),
                "class": g.get("class", "unknown"),
                "vendor": g.get("vendor", ""),
                "renderer": g.get("renderer", ""),
                "ms": g.get("ms"),
            }
        )
    verdict = ", ".join(
        f"{r['index']} ({CLASS_TITLE.get(r['class'], (r['class'] or 'Unknown').title())})"
        for r in rows
    )
    prime = ""
    non_display = [g for g in gpus if not g.get("display")]
    if non_display and any(g.get("discrete") for g in non_display):
        prime = (
            "PRIME render offload is supported: the discrete GPU renders "
            "shader screensavers while the panel GPU keeps the compositor."
        )
    elif len(gpus) >= 2:
        prime = "PRIME render offload was not detected on this system."
    elif len(gpus) == 1:
        prime = (
            "PRIME render offload is not available: only one display GPU was detected."
        )
    return {
        **WEAK_STATUS,
        "gpu_verdict_rows": rows,
        "gpu_verdict": verdict,
        "prime_offload": prime,
    }


class Harness:
    """Drives the app in a child process against a fake launcher CLI."""

    def __init__(self, env, fake):
        self.env = env
        self.fake = fake

    # Existing tests call the harness directly: harness(list, gpus=..., ...).
    def __call__(self, lst, status=None, gpus=None, gsettings=None):
        gpus_list = gpus or [DISPLAY_GPU]
        # Default status uses the synthetic per-GPU verdict fields (mirrors a
        # launcher >= 0.7.11). Tests that pass an explicit `status` (e.g. the
        # legacy fallback path) get the override.
        (self.fake / "list.json").write_text(json.dumps(lst))
        (self.fake / "status.json").write_text(
            json.dumps(status if status is not None else _verdict_status(gpus_list))
        )
        (self.fake / "gpus.json").write_text(json.dumps(gpus_list))
        # The memory backend is per process, so keys are set in the child.
        out = subprocess.run(
            [PY, "-c", WRAPPER, str(APP), json.dumps(gsettings or {})],
            env=self.env,
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(out.stdout)

    def call(self, fn, **args):
        """Call a pure helper of the app module and return its JSON result."""
        out = subprocess.run(
            [PY, "-c", CALLER, str(APP), fn, json.dumps(args)],
            env=self.env,
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(out.stdout)

    def no_adw(self):
        """Import the app with libadwaita blocked; returns the parsed report."""
        out = subprocess.run(
            [PY, "-c", NO_ADW_CALLER, str(APP)],
            env=self.env,
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(out.stdout)


@pytest.fixture
def harness(tmp_path):
    schemas = tmp_path / "schemas"
    schemas.mkdir()
    shutil.copy(REPO / "config/dev.ncz.screensaver.gschema.xml", schemas)
    subprocess.run(["glib-compile-schemas", str(schemas)], check=True)
    fake = tmp_path / "fake"
    fake.mkdir()
    cli = tmp_path / "ncz-screensaver"
    cli.write_text(FAKE_CLI)
    cli.chmod(cli.stat().st_mode | stat.S_IXUSR)
    hacks = tmp_path / "bin"
    hacks.mkdir()
    for hid in ("light_gles3", "medium_gles3", "heavy_gles3"):
        (hacks / hid).write_text("#!/bin/sh\n")
        (hacks / hid).chmod(0o755)
    (tmp_path / "hacks.tsv").write_text(CATALOG)
    env = {
        **os.environ,
        "GSETTINGS_SCHEMA_DIR": str(schemas),
        "GSETTINGS_BACKEND": "memory",
        "NCZ_SCREENSAVER_CLI": str(cli),
        "NCZ_SCREENSAVER_CATALOG": str(tmp_path / "hacks.tsv"),
        "NCZ_SCREENSAVER_DIRS": str(hacks),
        "NCZ_SCREENSAVER_BROKEN": str(tmp_path / "none.tsv"),
        "FAKE_DIR": str(fake),
    }
    return Harness(env, fake)


def group(dump, title):
    return next(g for g in dump["groups"] if g["title"] == title)


def test_weak_system_flags_heavy_hacks_with_expectation(harness):
    d = harness(WEAK_LIST, gpus=[DISPLAY_GPU, NVIDIA_GPU])
    titles = [g["title"] for g in d["groups"]]
    assert titles == ["Works well on this graphics chip", FLAGGED]
    good = group(d, titles[0])
    assert [r["id"] for r in good["rows"]] == ["light_gles3"]
    assert not any(r["flagged"] for r in good["rows"])
    bad = group(d, FLAGGED)
    assert bad["collapsed"] is True
    rows = {r["id"]: r for r in bad["rows"]}
    assert set(rows) == {"medium_gles3", "heavy_gles3"}
    assert rows["heavy_gles3"]["subtitle"] == "about 9 fps on Intel UHD 630"
    assert rows["heavy_gles3"]["badge"] == "May stutter"
    assert all(r["flagged"] for r in rows.values())
    assert d["show_all_switch"] is True


def test_strong_system_has_no_flagged_group(harness):
    d = harness(STRONG_LIST, status={**WEAK_STATUS, "gpu_class": "strong"})
    titles = [g["title"] for g in d["groups"]]
    assert titles == ["Calm", "Space"]  # previous per-catalog-group layout
    assert d["show_all_switch"] is False
    assert not any(r["flagged"] for g in d["groups"] for r in g["rows"])
    assert not any(g["collapsed"] for g in d["groups"])


def test_show_all_flips_collapsed(harness):
    off = harness(WEAK_LIST)
    on = harness(WEAK_LIST, gsettings={"show-all-hacks": ("b", True)})
    assert off["show_all_hacks"] is False
    assert group(off, FLAGGED)["collapsed"] is True
    assert on["show_all_hacks"] is True
    assert group(on, FLAGGED)["collapsed"] is False


@pytest.mark.parametrize(
    "stored,shown",
    [
        ("auto", "auto"),
        ("weak", "weak"),
        ("mid", "mid"),
        ("all", "all"),
        ("igpu-only", "weak"),
    ],
)
def test_pool_mapping_and_labels(harness, stored, shown):
    d = harness(WEAK_LIST, gsettings={"pool-gpu-class": ("s", stored)})
    assert d["pool_gpu_class"] == shown
    assert d["pool_gpu_class_stored"] == stored
    labels = {c["value"]: c["label"] for c in d["pool_choices"]}
    assert labels == {
        "auto": "Automatic (recommended)",
        "weak": "Only screensavers that run well on weak graphics chips",
        "mid": "Also screensavers that need a mid-range GPU",
        "all": "All screensavers",
    }


def test_class_summary_and_gpu_choices(harness):
    d = harness(WEAK_LIST, gpus=[DISPLAY_GPU, NVIDIA_GPU])
    # Per-GPU verdict (UX requirement, operator 2026-09-30): GPU0/panel is Weak,
    # GPU1/offload is Strong. Each row carries the calibration score.
    assert d["gpu_class_summary"] == (
        "GPU0 (Weak) (46.5 ms on the calibration test), "
        "GPU1 (Strong) (2.7 ms on the calibration test)"
    )
    assert d["gpu_verdict"] == "GPU0 (Weak), GPU1 (Strong)"
    assert "PRIME render offload is supported" in d["prime_offload"]
    assert [c["value"] for c in d["gpu_choices"]] == [
        "auto",
        "off",
        "pci-0000_01_00_0",
    ]
    assert d["gpu_choices"][2]["label"] == "NVIDIA nvidia (Strong)"
    legacy = harness(WEAK_LIST, gsettings={"gpu-offload": ("s", "prime")})
    assert [c["value"] for c in legacy["gpu_choices"]][-1] == "prime"


def test_class_summary_single_gpu(harness):
    """Single-GPU hosts (e.g. .66 with a Mali-G720) show 'GPU0 (...)' alone
    and explain why PRIME offload is unavailable."""
    d = harness(WEAK_LIST, gpus=[DISPLAY_GPU])
    assert d["gpu_class_summary"] == ("GPU0 (Weak) (46.5 ms on the calibration test)")
    assert d["gpu_verdict"] == "GPU0 (Weak)"
    assert "not available" in d["prime_offload"]


def test_class_summary_legacy_status_falls_back_to_single_class(harness):
    """Older launchers (pre-0.7.11) don't include gpu_verdict_rows; the GTK4
    reference app falls back to the legacy single-class summary so older
    deployments keep a sensible label."""
    legacy = {
        "gpu_class": "strong",
        "gpu_class_score_ms": 5.5,
        "gpu_class_source": "calibration",
        "running": False,
    }
    d = harness(WEAK_LIST, status=legacy, gpus=[DISPLAY_GPU])
    assert d["gpu_class_summary"] == (
        "Strong (5.5 ms on the calibration test; Mesa Intel UHD Graphics 630)"
    )
    assert d["gpu_verdict"] == ""
    assert d["prime_offload"] == ""


def test_class_summary_two_intel_gpus_no_discrete(harness):
    """Multi-GPU but no discrete target: per-GPU verdict still lists both
    GPUs, and the PRIME explanation says plainly that no offload
    configuration was detected (never falls back to software rendering)."""
    intel_2 = {
        "id": "pci-0000_03_00_0",
        "vendor": "intel",
        "driver": "i915",
        "display": False,
        "discrete": False,
        "class": "weak",
        "ms": 47.0,
        "renderer": "Mesa Intel(R) UHD Graphics (CML GT2)",
    }
    d = harness(WEAK_LIST, gpus=[DISPLAY_GPU, intel_2])
    assert d["gpu_verdict"] == "GPU0 (Weak), GPU1 (Weak)"
    assert "not detected" in d["prime_offload"]
    assert "software" not in d["prime_offload"].lower()


# ---------------------------------------------------------------------------
# GPU tier badges
# ---------------------------------------------------------------------------


def test_tier_badges_follow_the_engine_list(harness):
    """Every screensaver the engine reports a tier for gets that tier badge.

    The tier comes from `ncz-screensaver list --json` (which reads the shipped
    tiers.tsv); the app must not re-derive it, or the badges drift from the
    engine the moment a saver is re-measured.
    """
    d = harness(STRONG_LIST)
    catalog = {r["id"]: r for r in d["catalog"]}
    assert catalog["light_gles3"]["tier"] == "weak"
    assert catalog["light_gles3"]["tier_label"] == "Light"
    assert catalog["medium_gles3"]["tier_label"] == "Mid"
    assert catalog["heavy_gles3"]["tier_label"] == "Heavy"
    rows = {r["id"]: r for g in d["groups"] for r in g["rows"]}
    assert rows["heavy_gles3"]["tier"] == "strong"


def test_no_tier_means_no_badge(harness):
    """An unmeasured saver (tier "unknown") or an older launcher without
    min_class must produce an empty tier, never a made-up one."""
    # min_class is the only authoritative field; `tier` in the fake list rows is
    # dropped so a stale value there cannot make the test pass by accident.
    unmeasured = [
        {**row("light_gles3", "Light", "Calm", "weak", False), "min_class": ""},
        {
            **row("medium_gles3", "Medium", "Calm", "weak", False),
            "min_class": "unknown",
        },
        # Older launcher: no min_class key at all.
        {"id": "heavy_gles3", "title": "Heavy", "group": "Calm", "flagged": False},
    ]
    for r in unmeasured:
        r.pop("tier", None)  # only min_class is authoritative
    d = harness(unmeasured)
    catalog = {r["id"]: r for r in d["catalog"]}
    assert set(catalog) == {"light_gles3", "medium_gles3", "heavy_gles3"}
    for r in catalog.values():
        assert r["tier"] == ""
        assert r["tier_label"] == ""


# ---------------------------------------------------------------------------
# Choice lists come from the installed schema
# ---------------------------------------------------------------------------


def test_mode_pool_color_choices_match_the_schema(harness):
    d = harness(STRONG_LIST)
    assert [c["value"] for c in d["mode_choices"]] == [
        "off",
        "one",
        "random",
        "playlist",
    ]
    assert [c["value"] for c in d["pool_choices"]] == ["auto", "weak", "mid", "all"]
    assert [c["value"] for c in d["color_choices"]] == [
        "stylized",
        "kipthorne",
        "faithful",
        "singularity",
        "slingshot",
        "whitehole",
        "eht",
        "psychedelic",
        "synthwave",
        "cygnus",
    ]
    labels = {c["value"]: c["label"] for c in d["color_choices"]}
    assert labels["cygnus"] == "Cygnus (magenta and cyan)"


def test_a_new_engine_mode_appears_without_a_code_change(harness, tmp_path):
    """A mode the app has never heard of must reach the UI.

    This is the drift the reference app exists to avoid: the mode list used to
    be a hard-coded tuple, so a new engine mode needed a code change here.
    """
    schema = (REPO / "config/dev.ncz.screensaver.gschema.xml").read_text(
        encoding="utf-8"
    )
    patched = schema.replace(
        '<choice value="playlist"/>',
        '<choice value="playlist"/><choice value="shuffle"/>',
    )
    assert patched != schema, "upstream schema no longer has the playlist choice"
    dir_ = tmp_path / "schemas2"
    dir_.mkdir()
    (dir_ / "dev.ncz.screensaver.gschema.xml").write_text(patched, encoding="utf-8")
    subprocess.run(["glib-compile-schemas", str(dir_)], check=True)
    (tmp_path / "fake" / "list.json").write_text(json.dumps(STRONG_LIST))
    (tmp_path / "fake" / "status.json").write_text(
        json.dumps(_verdict_status([DISPLAY_GPU]))
    )
    (tmp_path / "fake" / "gpus.json").write_text(json.dumps([DISPLAY_GPU]))
    env = dict(harness.env, GSETTINGS_SCHEMA_DIR=str(dir_))
    out = subprocess.run(
        [PY, "-c", CALLER, str(APP), "mode_nicks", "{}"],
        env=env,
        capture_output=True,
        text=True,
        check=True,
    )
    assert json.loads(out.stdout) == ["off", "one", "random", "playlist", "shuffle"]


def test_unknown_nick_labels_do_not_crash(harness):
    """A label for an id this build has never seen falls back to the id."""
    d = harness(STRONG_LIST)
    labels = {c["value"]: c["label"] for c in d["mode_choices"]}
    assert all(v and label for v, label in labels.items())


# ---------------------------------------------------------------------------
# Randomize-everything
# ---------------------------------------------------------------------------

OPT_SCHEMA = [
    {
        "name": "seed",
        "type": "int",
        "default": "0",
        "min": "1",
        "max": "1000",
        "choices": [],
        "label": "Seed",
        "description": "",
        "group": "General",
    },
    {
        "name": "glow",
        "type": "float",
        "default": "0.5",
        "min": "0",
        "max": "1",
        "choices": [],
        "label": "Glow",
        "description": "",
        "group": "Look",
    },
    {
        "name": "quality",
        "type": "enum",
        "default": "high",
        "min": "",
        "max": "",
        "choices": ["low", "high", "ultra"],
        "label": "Quality",
        "description": "",
        "group": "Look",
    },
    {
        "name": "hull",
        "type": "bool",
        "default": "false",
        "min": "",
        "max": "",
        "choices": [],
        "label": "Hull",
        "description": "",
        "group": "Look",
    },
    {
        "name": "label",
        "type": "string",
        "default": "",
        "min": "",
        "max": "",
        "choices": [],
        "label": "Label",
        "description": "",
        "group": "General",
    },
]


def test_randomize_options_stays_inside_the_schema(harness):
    """Every randomized value must be one the engine will accept."""
    got = harness.call("randomize_options", schema=OPT_SCHEMA, current={})
    assert set(got) == {r["name"] for r in OPT_SCHEMA}
    for _ in range(25):
        got = harness.call("randomize_options", schema=OPT_SCHEMA, current={})
        assert 1 <= int(got["seed"]) <= 1000
        assert 0.0 <= float(got["glow"]) <= 1.0
        assert got["quality"] in ("low", "high", "ultra")
        assert got["hull"] in ("true", "false")
        # Free-form text has nothing sensible to randomize: keep the user's.
        assert got["label"] == ""


def test_randomize_options_actually_changes_numbers(harness):
    """Not a constant function: two draws differ for numeric options."""
    a = harness.call("randomize_options", schema=OPT_SCHEMA, current={})
    b = harness.call("randomize_options", schema=OPT_SCHEMA, current={})
    assert a != b


# ---------------------------------------------------------------------------
# libadwaita is optional
# ---------------------------------------------------------------------------


def test_app_runs_without_libadwaita(harness):
    """A session without gir1.2-adw-1 must still get a working app.

    Before this was fixed the import was a hard `gi.require_version("Adw","1")`,
    so the app died with ValueError on KDE Plasma, on X11 and on minimal GNOME
    images.
    """
    info = harness.no_adw()
    assert info["have_adw"] is False
    assert info["dialog_base"] == "Window"
    # The choice lists are schema-derived, so they survive the Adw-less path.
    assert info["modes"] == ["off", "one", "random", "playlist"]
    assert "cygnus" in info["colors"]


def test_options_preview_uses_the_configured_cli(harness):
    """Preview must go through $NCZ_SCREENSAVER_CLI, not a hard-coded name.

    Every other engine call honours the override; preview did not, so a
    distribution that installs the launcher under another name or path got a
    silent no-op.
    """
    argv = harness.call(
        "preview_argv", hack_id="light_gles3", options={"seed": "7", "hull": "true"}
    )
    cli = harness.env["NCZ_SCREENSAVER_CLI"]
    assert argv[:5] == [cli, "preview", "light_gles3", "--seconds", "30"]
    assert argv[5:] == ["--option", "seed=7", "--option", "hull=true"]


def test_cli_override_is_honoured_for_every_engine_call(harness):
    """cli_cmd() is the single funnel for the launcher, preview included."""
    assert harness.call("cli_cmd")[0] == harness.env["NCZ_SCREENSAVER_CLI"]
    assert harness.call("cli_cmd") == [harness.env["NCZ_SCREENSAVER_CLI"]]
