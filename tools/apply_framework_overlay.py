#!/usr/bin/env python3
"""Apply this project's reviewed source overlay to a pinned PSXRecomp tree."""

from __future__ import annotations

import argparse
import filecmp
import shutil
import sys
from pathlib import Path, PurePosixPath


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--framework",
        type=Path,
        required=True,
        help="path to the pinned PSXRecomp checkout",
    )
    parser.add_argument(
        "--overlay",
        type=Path,
        default=PROJECT_ROOT / "psxrecomp-overlay",
        help="overlay root (default: this repository's psxrecomp-overlay)",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=PROJECT_ROOT / "PSXRECOMP_OVERLAY_FILES.txt",
        help="reviewed overlay-file manifest",
    )
    args = parser.parse_args()

    framework = args.framework.resolve()
    overlay = args.overlay.resolve()
    manifest = args.manifest.resolve()
    if not overlay.is_dir():
        parser.error(f"framework overlay does not exist: {overlay}")
    if not manifest.is_file():
        parser.error(f"framework overlay manifest does not exist: {manifest}")
    if not (framework / "runtime" / "runtime.cmake").is_file():
        parser.error(f"not a PSXRecomp checkout: {framework}")

    relative_paths: list[Path] = []
    for line_number, raw_line in enumerate(
        manifest.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        posix_path = PurePosixPath(line)
        if posix_path.is_absolute() or ".." in posix_path.parts:
            parser.error(
                f"unsafe path in {manifest} at line {line_number}: {line}"
            )
        relative_paths.append(Path(*posix_path.parts))

    if not relative_paths:
        parser.error(f"framework overlay manifest is empty: {manifest}")
    if len(relative_paths) != len(set(relative_paths)):
        parser.error(f"framework overlay manifest contains duplicates: {manifest}")

    sources = [overlay / relative for relative in relative_paths]
    absent_sources = [
        relative
        for source, relative in zip(sources, relative_paths)
        if not source.is_file()
    ]
    if absent_sources:
        print("Reviewed framework overlay files are missing:", file=sys.stderr)
        for path in absent_sources:
            print(f"  missing {path}", file=sys.stderr)
        return 1

    missing = [
        relative
        for relative in relative_paths
        if not (framework / relative).is_file()
    ]
    if missing:
        print(
            "Refusing to apply the overlay because the pinned framework layout "
            "does not match:",
            file=sys.stderr,
        )
        for path in missing:
            print(f"  missing {path}", file=sys.stderr)
        return 1

    for source, relative in zip(sources, relative_paths):
        shutil.copy2(source, framework / relative)

    mismatched = [
        relative
        for source, relative in zip(sources, relative_paths)
        if not filecmp.cmp(
            source,
            framework / relative,
            shallow=False,
        )
    ]
    if mismatched:
        print("Framework overlay verification failed:", file=sys.stderr)
        for path in mismatched:
            print(f"  mismatch {path}", file=sys.stderr)
        return 1

    print(f"Applied and verified {len(sources)} PSXRecomp overlay files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
