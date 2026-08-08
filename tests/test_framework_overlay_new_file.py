#!/usr/bin/env python3
"""Verify that reviewed overlays may add files inside known framework dirs."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
APPLIER = ROOT / "tools" / "apply_framework_overlay.py"
PROJECT_OVERLAY = ROOT / "psxrecomp-overlay"
PROJECT_MANIFEST = ROOT / "PSXRECOMP_OVERLAY_FILES.txt"


def run_applier(
    framework: pathlib.Path,
    overlay: pathlib.Path,
    manifest: pathlib.Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(APPLIER),
            "--framework",
            str(framework),
            "--overlay",
            str(overlay),
            "--manifest",
            str(manifest),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = pathlib.Path(temp_dir)
        framework = temp / "framework"
        overlay = temp / "overlay"
        include_dir = framework / "runtime" / "include"
        overlay_include_dir = overlay / "runtime" / "include"
        include_dir.mkdir(parents=True)
        overlay_include_dir.mkdir(parents=True)
        (framework / "runtime" / "runtime.cmake").write_text(
            "# fixture\n", encoding="utf-8"
        )
        expected = "#pragma once\n"
        (overlay_include_dir / "new_header.h").write_text(
            expected, encoding="utf-8"
        )
        manifest = temp / "manifest.txt"
        manifest.write_text(
            "+runtime/include/new_header.h\n", encoding="utf-8"
        )

        result = run_applier(framework, overlay, manifest)
        if result.returncode != 0:
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)
            return 1
        installed = include_dir / "new_header.h"
        if installed.read_text(encoding="utf-8") != expected:
            print("new manifest-listed overlay file was not copied", file=sys.stderr)
            return 1

        unexpected = overlay_include_dir / "unexpected.h"
        unexpected.write_text(expected, encoding="utf-8")
        manifest.write_text(
            "runtime/include/unexpected.h\n", encoding="utf-8"
        )
        rejected = run_applier(framework, overlay, manifest)
        if rejected.returncode == 0 or (include_dir / "unexpected.h").exists():
            print("unmarked new overlay file was not rejected", file=sys.stderr)
            return 1

        # Exercise the real reviewed manifest as well. Existing entries get
        # placeholder destinations; '+' entries deliberately start absent.
        real_framework = temp / "real-framework"
        (real_framework / "runtime").mkdir(parents=True)
        (real_framework / "runtime" / "runtime.cmake").write_text(
            "# fixture\n", encoding="utf-8"
        )
        for raw_line in PROJECT_MANIFEST.read_text(
            encoding="utf-8"
        ).splitlines():
            entry = raw_line.strip()
            if not entry or entry.startswith("#"):
                continue
            allow_new = entry.startswith("+")
            relative = pathlib.Path(entry[1:] if allow_new else entry)
            destination = real_framework / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if not allow_new:
                destination.write_text("fixture\n", encoding="utf-8")

        real_result = run_applier(
            real_framework, PROJECT_OVERLAY, PROJECT_MANIFEST
        )
        if real_result.returncode != 0:
            print(real_result.stdout, end="")
            print(real_result.stderr, end="", file=sys.stderr)
            return 1
        installed_shared_header = (
            real_framework / "runtime" / "include" / "gte_precision.h"
        )
        if not installed_shared_header.is_file():
            print("real manifest did not install gte_precision.h", file=sys.stderr)
            return 1

    print("framework overlay new-file test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
