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

    def run(lst, status=None, gpus=None, gsettings=None):
        gpus_list = gpus or [DISPLAY_GPU]
        # Default status uses the synthetic per-GPU verdict fields (mirrors a
        # launcher >= 0.7.11). Tests that pass an explicit `status` (e.g. the
        # legacy fallback path) get the override.
        (fake / "list.json").write_text(json.dumps(lst))
        (fake / "status.json").write_text(
            json.dumps(status if status is not None else _verdict_status(gpus_list))
        )
        (fake / "gpus.json").write_text(json.dumps(gpus_list))
        # The memory backend is per process, so keys are set in the child.
        out = subprocess.run(
            [PY, "-c", WRAPPER, str(APP), json.dumps(gsettings or {})],
            env=env,
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(out.stdout)

    return run


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
