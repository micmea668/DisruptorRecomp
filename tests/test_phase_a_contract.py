#!/usr/bin/env python3
"""Structural safety contract for Modernisation Test 13 Phase A."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gpu.c"
GTE = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gte.cpp"
GL = ROOT / "psxrecomp-overlay" / "runtime" / "src" / "gpu_gl_renderer.c"
GPU_HEADER = ROOT / "psxrecomp-overlay" / "runtime" / "include" / "gpu.h"
GTE_HEADER = (
    ROOT / "psxrecomp-overlay" / "runtime" / "include" / "gte_precision.h"
)
MOUSE = ROOT / "src" / "disruptor_mouse_aim.cpp"
LAUNCHER = ROOT / "test13-launcher" / "Play Disruptor.ps1"
OVERLAY_LIST = ROOT / "PSXRECOMP_OVERLAY_FILES.txt"


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
    gpu = GPU.read_text(encoding="utf-8")
    gte = GTE.read_text(encoding="utf-8")
    gl = GL.read_text(encoding="utf-8")
    gpu_header = GPU_HEADER.read_text(encoding="utf-8")
    gte_header = GTE_HEADER.read_text(encoding="utf-8")
    mouse = MOUSE.read_text(encoding="utf-8")
    launcher = LAUNCHER.read_text(encoding="utf-8")
    overlay_list = OVERLAY_LIST.read_text(encoding="utf-8")

    # Provenance must validate the original address. Masking 0x1f801810 first
    # aliases the direct GP0 MMIO register to low RAM at 0x1810.
    source_address = function_body(gpu, "gp0_source_word_address")
    require("*out = gp0_cmd_source_addr + delta" in source_address,
            "GP0 provenance must preserve the full source address")
    require("0x1FFFFC" not in source_address,
            "the GP0 source helper must not mask an address into RAM")

    resolve = function_body(gpu, "resolve_precise_vertices")
    texture = function_body(gpu, "resolve_texture_vertices")
    for name, body in (("geometry", resolve), ("texture", texture)):
        require("gp0_source_word_address" in body,
                f"{name} lookup must use the full-address helper")
        lookup = ("gte_precision_load_word_ex" if name == "geometry" else
                  "gte_precision_load_perspective_word_ex")
        require(lookup in body,
                f"{name} lookup must retain detailed provenance results")
        require("0x1FFFFC" not in body,
                f"{name} lookup must not pre-mask the GP0 source")

    ram_gate = function_body(gte, "precision_ram_address")
    require("addr >= 0xC0000000u" in ram_gate,
            "the centralized lookup must reject unmapped KSEG2/KSEG3")
    require("mapped >= 0x00800000u" in ram_gate,
            "the centralized lookup must reject MMIO before RAM mirroring")
    load_ex = function_body(gte, "precision_lookup_projection")
    require("GTE_PRECISION_LOOKUP_SOURCE_NOT_RAM" in load_ex,
            "non-RAM provenance needs an explicit rejection reason")
    require("GTE_PRECISION_LOOKUP_SOURCE_UNALIGNED" in load_ex,
            "unaligned packet provenance must not alias an adjacent RAM word")
    require("GTE_PRECISION_LOOKUP_SATURATED" in load_ex,
            "saturated screen projections must be rejected")
    require("fixed16_integer_floor(precise_x[i]) != raw_x[i]" in resolve and
            "fixed16_integer_floor(precise_y[i]) != raw_y[i]" in resolve,
            "accepted precision must remain within the packed pixel")
    require("GPU_GEOMETRY_REJECT_ATOMIC_FALLBACK" in resolve and
            "(uint64_t)resolved" in resolve,
            "successful siblings of a failed polygon must remain accounted")
    require(gpu.count("precise_indices, (!rej_a) + (!rej_b)") == 4,
            "quad triangle totals must reflect the raster halves submitted")

    # The canonical pass must consume a dedicated GP0 attribute. It must never
    # reconstruct authority by flooring presentation precision.
    require("#define GEOV 9" in gl and "#define TEXV 23" in gl,
            "both GL vertex formats must carry canonical and visual positions")
    require("layout(location=3) in vec2 a_visual_pos" in gl,
            "flat geometry needs a separate visual-position attribute")
    require("layout(location=10) in vec2 a_visual_pos" in gl,
            "textured geometry needs a separate visual-position attribute")
    require(gl.count("vec2 p = corrected ? a_visual_pos : a_pos") == 2,
            "both shaders must select explicit canonical or visual positions")
    require("floor(a_pos)" not in gl,
            "canonical coordinates must never be inferred from precision")

    # Full yaw is deliberately independent from the X-only control, and the Y
    # horizon follows each GP0 drawing band rather than a VBlank-time snapshot.
    require(gl.count("yb = u_camera.w + (yb-u_camera.w)/den") == 2,
            "GEO and TEX shaders must apply the shared yaw denominator to Y")
    require("PSX_GEOMETRY_FULL_YAW" in gl,
            "full X/Y yaw must have an independent opt-in")
    require("PSX_GEOMETRY_COVERAGE_TINT" in gl,
            "coverage tint must have an independent opt-in")
    yaw_set = function_body(gpu, "gpu_geometry_camera_yaw_residual_set")
    require("s_geometry_camera_yaw_residual = yaw_units" in yaw_set and
            "geometry_camera_presentation_update()" in yaw_set,
            "the yaw API must retain residual state for horizon changes")
    horizon_set = function_body(
        gpu, "gpu_geometry_camera_projection_center_y_set")
    require("isfinite(center_y)" in horizon_set and
            "center_y >= 24.0" in horizon_set and
            "center_y <= 216.0" in horizon_set and
            "next = 120" in horizon_set,
            "the projection horizon must fail closed to the 240-line default")
    require("floor(center_y + 0.5)" in horizon_set and
            "geometry_camera_presentation_update()" in horizon_set,
            "horizon changes must be integer-exact and flush through yaw state")
    presentation_update = function_body(
        gpu, "geometry_camera_presentation_update")
    require("s_geometry_camera_projection_center_y" in presentation_update and
            "gr_set_presentation_yaw" in presentation_update and
            "draw_offset_y" not in presentation_update,
            "yaw must share the display-relative effective projection horizon")
    uniforms = function_body(gl, "geometry_visual_uniforms")
    require("s_off_y + s_yaw_center_y_relative" in uniforms,
            "each flushed GL batch must use its active framebuffer horizon")
    yaw_backend = function_body(gl, "glb_set_presentation_yaw")
    require("full_turn <= 0.0" in yaw_backend and
            "s_yaw_cos = 1.0f" in yaw_backend,
            "invalid yaw periods must produce an identity, not NaN")
    snapshot = function_body(gpu, "gpu_snapshot_read")
    require("gr_set_draw_offset(draw_offset_x, draw_offset_y)" in snapshot,
            "savestate restore must resynchronize the renderer's Y horizon")
    reset = function_body(gpu, "gpu_reset_state")
    require("gr_set_draw_offset(draw_offset_x, draw_offset_y)" in reset and
            "s_geometry_camera_projection_center_y = 120" in reset and
            "geometry_camera_presentation_update()" in reset,
            "GPU reset must resynchronize the renderer's Y horizon")

    # Diagnostics must be versioned, presentation-scoped, and sufficiently
    # detailed to explain coverage rather than reporting one opaque ratio.
    require("GPU_GEOMETRY_DIAGNOSTICS_VERSION" in gpu_header,
            "geometry diagnostics must expose a versioned ABI")
    for token in (
        "vertex_rejections",
        "primitive_candidates",
        "opcode_candidates",
        "depth_vertices",
        "screen_vertices",
        "correction_magnitude_buckets",
        "correction_accumulators_saturated",
        "partial_polygon_rejections",
        "partial_quad_rejections",
        "GPU_GEOMETRY_REJECT_ATOMIC_FALLBACK",
        "GPU_GEOMETRY_REJECT_RECTANGLE_FAST_PATH",
    ):
        require(token in gpu_header, f"missing diagnostic field: {token}")
    require("#define GPU_GEOMETRY_REJECT_REASON_COUNT 15u" in gpu_header,
            "version-1 rejection ordering must stay synchronized")
    require("geometry_diag_saturating_add" in gpu,
            "long-running correction moments must not wrap")
    detailed_stats = function_body(
        gpu, "gpu_geometry_correction_stats_detailed")
    require("out_size" in detailed_stats and "copy_size" in detailed_stats,
            "the versioned diagnostics ABI must honor caller capacity")
    require("geometry_diag_note_rectangle_fast_path" in gpu,
            "rectangle fast paths must remain visible in the denominator")
    require("raw_x, raw_y, z" in resolve,
            "spatial coverage must use display-relative raw projection pixels")
    require("geometry_diag version=%u" in mouse and
            '"cumulative"' in mouse and '"latest_live"' in mouse,
            "logs must emit cumulative and live-presentation scopes")
    require("runtime/include/gte_precision.h" in overlay_list,
            "the new shared provenance header must ship in the overlay")
    require("GTE_PRECISION_LOOKUP_RESULT_COUNT" in gte_header,
            "lookup result ordering must have a stable terminal count")

    # Test 13 must isolate each option and reject an invalid renderer/package.
    for token in (
        "PSX_GEOMETRY_FULL_YAW",
        "PSX_GEOMETRY_COVERAGE_TINT",
        "source_commit=([0-9a-f]{40})",
        "CHECKSUMS.sha256",
        "GL GPU pipeline ready (internal scale 4x",
        "GL pipeline init failed.*falling back to software renderer",
    ):
        require(token in launcher, f"Test 13 launcher guard missing: {token}")

    print("Phase A structural contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"Phase A structural contract: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
