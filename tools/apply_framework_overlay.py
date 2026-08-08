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
    new_relative_paths: set[Path] = set()
    for line_number, raw_line in enumerate(
        manifest.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        allow_new = line.startswith("+")
        if allow_new:
            line = line[1:].strip()
            if not line:
                parser.error(
                    f"empty new-file entry in {manifest} at line {line_number}"
                )
        posix_path = PurePosixPath(line)
        if posix_path.is_absolute() or ".." in posix_path.parts:
            parser.error(
                f"unsafe path in {manifest} at line {line_number}: {line}"
            )
        relative = Path(*posix_path.parts)
        relative_paths.append(relative)
        if allow_new:
            new_relative_paths.add(relative)

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

    destinations = [framework / relative for relative in relative_paths]
    missing_existing = [
        relative
        for destination, relative in zip(destinations, relative_paths)
        if relative not in new_relative_paths and not destination.is_file()
    ]
    invalid_new = [
        relative
        for destination, relative in zip(destinations, relative_paths)
        if relative in new_relative_paths
        and (not destination.parent.is_dir()
             or (destination.exists() and not destination.is_file()))
    ]
    if missing_existing or invalid_new:
        print(
            "Refusing to apply the overlay because the pinned framework layout "
            "does not match:",
            file=sys.stderr,
        )
        for path in missing_existing:
            print(f"  missing {path}", file=sys.stderr)
        for path in invalid_new:
            print(f"  invalid destination for new file {path}", file=sys.stderr)
        return 1

    # Manifest membership is the review boundary. Only '+' entries may add a
    # file inside an existing framework directory; unmarked entries retain the
    # pinned-layout typo/removal guard.
    for source, destination in zip(sources, destinations):
        shutil.copy2(source, destination)

    mismatched = [
        relative
        for source, destination, relative in zip(
            sources, destinations, relative_paths
        )
        if not filecmp.cmp(
            source,
            destination,
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
