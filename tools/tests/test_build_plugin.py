# SPDX-License-Identifier: Apache-2.0
"""build-plugin.sh must fail loudly: non-zero and no output file on a broken source."""

import os
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = pathlib.Path(os.environ.get("NCZ_TEST_SDK", pathlib.Path.home() / "ss-sdk"))


def _sdk_arch_matches_host():
    """Return True iff the SDK's libsingularity.so is for the host architecture.

    A mismatch means `ld` rejects the prebuilt library with
    "file in wrong format" and the test cannot exercise the
    build script's behaviour — the failure is environmental,
    not a defect in build-plugin.sh. Skip with a precise reason
    so the rest of the suite (and the host's own CI gate) keeps
    a clean green.
    """
    lib = SDK / "lib" / "libsingularity.so"
    if not lib.is_file():
        return False
    out = subprocess.run(
        ["file", "-b", str(lib)],
        capture_output=True,
        text=True,
        check=False,
    )
    descr = out.stdout.strip().lower()
    host = os.uname().machine.lower()
    # Map common ELF machine strings to host arch tokens.
    arch_aliases = {
        "x86-64": ("x86-64", "x86_64", "amd64"),
        "aarch64": ("aarch64", "arm64"),
        "armv7l": ("armv7l", "armhf", "arm"),
    }
    for _key, members in arch_aliases.items():
        if host in members:
            host_keys = members
            break
    else:
        host_keys = (host,)
    return any(key in descr for key in host_keys)


pytestmark = pytest.mark.skipif(
    not (SDK / "lib/pkgconfig/singularity-1.0.pc").exists()
    or not shutil.which("valac")
    or not _sdk_arch_matches_host(),
    reason="needs valac and a Singularity SDK (NCZ_TEST_SDK) "
    "for the host architecture",
)


def run(src, out):
    env = dict(os.environ, NCZ_PLUGIN_SRC=str(src))
    return subprocess.run(
        ["sh", str(ROOT / "tools/build-plugin.sh"), str(SDK), str(out)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )


def test_broken_source_fails_loudly(tmp_path):
    src = tmp_path / "plugin"
    shutil.copytree(ROOT / "plugin", src)
    with (src / "screensaver/screensaver.vala").open("a") as f:
        f.write("\nthis is not vala;\n")
    out = tmp_path / "out"
    proc = run(src, out)
    assert proc.returncode != 0
    assert "error" in proc.stderr.lower()
    assert not (out / "libscreensaver.so").exists()


def test_good_source_builds(tmp_path):
    out = tmp_path / "out"
    proc = run(ROOT / "plugin", out)
    assert proc.returncode == 0, proc.stderr
    assert (out / "libscreensaver.so").is_file()
