# SPDX-License-Identifier: Apache-2.0
"""make-plugin-deb.sh: the package layout and the Singularity dependency floor."""

import os
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/make-plugin-deb.sh"

pytestmark = pytest.mark.skipif(
    not (shutil.which("dpkg-deb") and shutil.which("dpkg")),
    reason="needs dpkg-deb and dpkg",
)


def build(tmp_path, env_extra=None, plugin_text=None, with_icon=True):
    src = tmp_path / "build"
    src.mkdir()
    (src / "libscreensaver.so").write_bytes(b"\x7fELF placeholder")
    plugin = ROOT / "plugin/screensaver/screensaver.plugin"
    (src / "screensaver.plugin").write_text(
        plugin_text if plugin_text is not None else plugin.read_text()
    )
    if with_icon:
        shutil.copy(ROOT / "plugin/screensaver/dev.ncz.screensaver.svg", src)
    out = tmp_path / "plugin.deb"
    env = dict(os.environ, TMPDIR=str(tmp_path))
    env.pop("NCZ_SINGULARITY_MIN", None)
    env.update(env_extra or {})
    proc = subprocess.run(
        ["sh", str(SCRIPT), "amd64", str(src), str(out), "9.9.9"],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    return proc, out


def field(deb, name):
    return subprocess.run(
        ["dpkg-deb", "-f", str(deb), name],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()


def test_layout_and_unversioned_default(tmp_path):
    proc, out = build(tmp_path)
    assert proc.returncode == 0, proc.stderr
    listing = subprocess.run(
        ["dpkg-deb", "-c", str(out)], capture_output=True, text=True, check=True
    ).stdout
    plugdir = "./opt/singularity/lib/singularity/plugins/screensaver/"
    assert plugdir + "libscreensaver.so" in listing
    assert plugdir + "screensaver.plugin" in listing
    assert field(out, "Package") == "ncz-screensavers-plugin"
    assert field(out, "Version") == "9.9.9"
    deps = [d.strip() for d in field(out, "Depends").split(",")]
    assert "ncz-singularity-desktop" in deps


def test_singularity_floor(tmp_path):
    proc, out = build(tmp_path, {"NCZ_SINGULARITY_MIN": "20261006"})
    assert proc.returncode == 0, proc.stderr
    deps = [d.strip() for d in field(out, "Depends").split(",")]
    assert "ncz-singularity-desktop (>= 20261006)" in deps


def test_bad_floor_fails_loudly(tmp_path):
    proc, out = build(tmp_path, {"NCZ_SINGULARITY_MIN": "not a version"})
    assert proc.returncode != 0
    assert "NCZ_SINGULARITY_MIN" in proc.stderr
    assert not out.exists()


def installed_plugin_file(out, tmp_path):
    ex = tmp_path / "extract"
    subprocess.run(["dpkg-deb", "-x", str(out), str(ex)], check=True)
    return ex, (
        ex / "opt/singularity/lib/singularity/plugins/screensaver/screensaver.plugin"
    ).read_text()


def keys(text):
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def test_metadata_the_plugins_page_reads(tmp_path):
    # "Version Unknown" and the generic icon came from keys the file did not have.
    proc, out = build(tmp_path)
    assert proc.returncode == 0, proc.stderr
    ex, text = installed_plugin_file(out, tmp_path)
    k = keys(text)
    assert k["Version"] == "9.9.9"
    assert k["Icon"] == "dev.ncz.screensaver"
    assert k["X-Singularity-Category"] in {
        "panel",
        "dock",
        "wallpapers",
        "search",
        "files",
        "lock",
        "tools",
        "other",
    }
    assert k["Module"] == "screensaver"
    icon = (
        ex / "opt/singularity/share/icons/hicolor/scalable/apps" / (k["Icon"] + ".svg")
    )
    assert icon.is_file() and icon.read_text().lstrip().startswith("<?xml")


def test_version_comes_from_the_build_not_the_source(tmp_path):
    stale = (ROOT / "plugin/screensaver/screensaver.plugin").read_text()
    proc, out = build(tmp_path, plugin_text=stale + "Version=0.0.1\n")
    assert proc.returncode == 0, proc.stderr
    _, text = installed_plugin_file(out, tmp_path)
    assert text.count("Version=") == 1
    assert keys(text)["Version"] == "9.9.9"


@pytest.mark.parametrize(
    "missing", ["Icon", "X-Singularity-Category", "Authors", "Description"]
)
def test_missing_metadata_fails_loudly(tmp_path, missing):
    text = "\n".join(
        line
        for line in (ROOT / "plugin/screensaver/screensaver.plugin")
        .read_text()
        .splitlines()
        if not line.startswith(missing + "=")
    )
    proc, out = build(tmp_path, plugin_text=text + "\n")
    assert proc.returncode != 0
    assert missing in proc.stderr
    assert not out.exists()


def test_unknown_category_fails_loudly(tmp_path):
    text = (
        (ROOT / "plugin/screensaver/screensaver.plugin")
        .read_text()
        .replace("X-Singularity-Category=lock", "X-Singularity-Category=screensavers")
    )
    proc, _out = build(tmp_path, plugin_text=text)
    assert proc.returncode != 0
    assert "X-Singularity-Category" in proc.stderr


def test_missing_icon_file_fails_loudly(tmp_path):
    proc, _out = build(tmp_path, with_icon=False)
    assert proc.returncode != 0
    assert "dev.ncz.screensaver.svg" in proc.stderr


def test_installed_manifest_is_app_shipped(tmp_path):
    proc, out = build(tmp_path)
    assert proc.returncode == 0, proc.stderr
    ext = tmp_path / "x"
    subprocess.run(["dpkg-deb", "-x", str(out), str(ext)], check=True)
    text = (
        ext / "opt/singularity/lib/singularity/plugins/screensaver/screensaver.plugin"
    ).read_text()
    assert "X-Singularity-App=dev.sinty.desktop" in text.splitlines()
