#!/usr/bin/env python3
"""Regression audit for Test 12's presentation-geometry provenance gate."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gpu.c"
GTE = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gte.cpp"


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"missing function: {name}")
    start = source.index("{", match.start())
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : pos]
    raise AssertionError(f"unterminated function: {name}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    source = GPU.read_text(encoding="utf-8")
    gte_source = GTE.read_text(encoding="utf-8")

    resolve = function_body(source, "resolve_precise_vertices")
    require("gte_precision_load_word" in resolve,
            "precise vertices must use packet-address provenance")
    require("gp0_cmd_source_addr == 0xFFFFFFFFu" in resolve,
            "commands without a source address must be rejected")
    require("gte_geometry_correction_lookup" not in resolve,
            "rounded packed-SXY fallback must never return")
    require("gte_geometry_correction_lookup" not in source,
            "gpu.c must not call or declare the ambiguous cache lookup")

    legacy_lookup = function_body(gte_source,
                                  "gte_geometry_correction_lookup")
    require("return 0;" in legacy_lookup and
            "s_geom_cache" not in legacy_lookup,
            "legacy packed-SXY ABI must remain hard-disabled")
    require("geom_note" not in gte_source and
            "GEOM_CACHE_SIZE" not in gte_source and
            "s_geom_cache" not in gte_source,
            "rounded packed-SXY storage must not exist")

    queue = function_body(source, "queue_precise_triangle")
    reject_pos = queue.index("gr_set_world_triangle(0)")
    accept_pos = queue.index("gr_set_world_triangle(1)")
    gate_pos = queue.index("!exact")
    require(reject_pos < gate_pos < accept_pos,
            "fractional yaw must activate only after exact provenance")

    triangle = function_body(source, "prepare_precise_triangle")
    require("resolve_precise_vertices" in triangle and
            "queue_precise_triangle" in triangle,
            "triangles must resolve exactly before queueing metadata")

    quad_names = (
        "gp0_exec_mono_quad",
        "gp0_exec_shaded_quad",
        "gp0_exec_textured_quad",
        "gp0_exec_shaded_textured_quad",
    )
    for name in quad_names:
        body = function_body(source, name)
        require(body.count("prepare_precise_quad_vertices") == 1,
                f"{name} must preflight all four vertices exactly once")
        require(body.count("queue_precise_triangle(precise_quad") == 2,
                f"{name} must give both raster triangles the same verdict")
        require("prepare_precise_triangle(" not in body,
                f"{name} must not fall back to per-triangle acceptance")

    geometry_set = function_body(source, "gpu_geometry_correction_set")
    texture_set = function_body(source, "gpu_texture_correction_set")
    require("s_texture_correction_enabled" in geometry_set,
            "geometry toggle must preserve texture provenance tracking")
    require("ws_geometry_correction" in texture_set,
            "texture toggle must preserve geometry provenance tracking")

    print("geometry provenance contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"geometry provenance contract: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
