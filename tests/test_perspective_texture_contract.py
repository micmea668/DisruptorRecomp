#!/usr/bin/env python3
"""Structural contract for presentation-only perspective texture correction.

The audit intentionally reads only reviewed overlay sources, so it is safe in a
fresh checkout before the disposable ``psxrecomp`` dependency is populated.
"""

from __future__ import annotations

import pathlib
import re
import sys
from math import isclose


ROOT = pathlib.Path(__file__).resolve().parents[1]
OVERLAY = ROOT / "psxrecomp-overlay" / "runtime"
MAIN = OVERLAY / "src" / "main.cpp"
GPU = OVERLAY / "src" / "gpu.c"
GL = OVERLAY / "src" / "gpu_gl_renderer.c"
GTE = OVERLAY / "src" / "gte.cpp"
RENDER = OVERLAY / "src" / "gpu_render.c"
RENDER_HEADER = OVERLAY / "include" / "gpu_render.h"
GTE_HEADER = OVERLAY / "include" / "gte_precision.h"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_match(source: str, name: str) -> re.Match[str]:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"missing function: {name}")
    return match


def function_body(source: str, name: str) -> str:
    match = function_match(source, name)
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


def function_signature(source: str, name: str) -> str:
    match = function_match(source, name)
    line_start = source.rfind("\n", 0, match.start()) + 1
    return source[line_start : source.index("{", match.start())]


def source_span(source: str, start: str, end: str) -> str:
    begin = source.find(start)
    require(begin >= 0, f"missing source marker: {start}")
    finish = source.find(end, begin + len(start))
    require(finish >= 0, f"missing source marker: {end}")
    return source[begin:finish]


def compact(source: str) -> str:
    return re.sub(r"\s+", "", source)


def interpolate_uv(
        weights: tuple[float, float, float],
        uvs: tuple[tuple[float, float], tuple[float, float], tuple[float, float]],
        q: tuple[float, float, float]) -> tuple[float, float]:
    """Reference the shader's noperspective (uv*q)/q reconstruction."""
    denominator = sum(weight * reciprocal
                      for weight, reciprocal in zip(weights, q))
    require(denominator > 0.0,
            "deterministic perspective fixture must have positive q")
    return tuple(
        sum(weight * uv[axis] * reciprocal
            for weight, uv, reciprocal in zip(weights, uvs, q)) /
        denominator
        for axis in (0, 1)
    )


def require_uv_close(actual: tuple[float, float],
                     expected: tuple[float, float], message: str) -> None:
    require(all(isclose(a, e, rel_tol=1.0e-12, abs_tol=1.0e-12)
                for a, e in zip(actual, expected)), message)


def main() -> int:
    main_cpp = MAIN.read_text(encoding="utf-8")
    gpu = GPU.read_text(encoding="utf-8")
    gl = GL.read_text(encoding="utf-8")
    gte = GTE.read_text(encoding="utf-8")
    render = RENDER.read_text(encoding="utf-8")
    render_header = RENDER_HEADER.read_text(encoding="utf-8")
    gte_header = GTE_HEADER.read_text(encoding="utf-8")

    # Deterministic reference math protects the intended interpolation rather
    # than only checking shader spelling.
    weights = (0.25, 0.25, 0.50)
    uvs = ((0.0, 0.0), (80.0, 0.0), (0.0, 40.0))
    affine = (
        sum(weight * uv[0] for weight, uv in zip(weights, uvs)),
        sum(weight * uv[1] for weight, uv in zip(weights, uvs)),
    )
    require_uv_close(interpolate_uv(weights, uvs, (1.0, 1.0, 1.0)),
                     affine,
                     "equal depths must reproduce affine interpolation")
    unequal = interpolate_uv(weights, uvs, (1.0, 0.5, 0.25))
    require_uv_close(unequal, (20.0, 10.0),
                     "known unequal depths must follow (uv*q)/q")
    require_uv_close(
        interpolate_uv(weights, uvs, (7.0, 3.5, 1.75)), unequal,
        "common reciprocal-depth scaling must cancel")

    quad_uv = ((0.0, 0.0), (64.0, 0.0),
               (0.0, 64.0), (64.0, 64.0))
    quad_q = (1.0, 0.5, 0.25, 0.125)
    # First half is 0,1,2; second is 2,1,3. The same point on edge 1-2
    # must reconstruct identically from both local vertex orderings.
    first_edge = interpolate_uv(
        (0.0, 0.4, 0.6),
        (quad_uv[0], quad_uv[1], quad_uv[2]),
        (quad_q[0], quad_q[1], quad_q[2]))
    second_edge = interpolate_uv(
        (0.6, 0.4, 0.0),
        (quad_uv[2], quad_uv[1], quad_uv[3]),
        (quad_q[2], quad_q[1], quad_q[3]))
    require_uv_close(first_edge, second_edge,
                     "quad halves must agree along their shared 1-2 diagonal")

    # Texture correction is independently switchable while geometry remains
    # enabled, allowing a meaningful same-geometry A/B comparison.
    env_pos = main_cpp.find('std::getenv("PSX_TEXTURE_CORRECTION")')
    require(env_pos >= 0,
            "main must expose the independent PSX_TEXTURE_CORRECTION gate")
    env_gate = main_cpp[env_pos : env_pos + 240]
    require("g_texture_correction" in env_gate,
            "the texture environment gate must update its own setting")
    require("g_geometry_correction =" not in env_gate,
            "the texture environment gate must not mutate geometry correction")
    require(re.search(
        r"gpu_texture_correction_set\s*\(\s*g_texture_correction\s*\?\s*1\s*:\s*0\s*\)",
        main_cpp) is not None,
        "startup must apply the independent texture-correction setting")
    require("gpu_geometry_correction_set" in main_cpp,
            "texture correction must not replace the geometry startup gate")

    # The GPU front-end must use renderer dispatch, and exact provenance must
    # gate q before any per-vertex depth is accepted.
    require("sw_set_perspective_triangle(" not in gpu,
            "gpu.c must not bypass the active renderer with a SW-only setter")
    texture_sig = function_signature(gpu, "prepare_texture_triangle")
    texture = function_body(gpu, "prepare_texture_triangle")
    resolve_texture = function_body(gpu, "resolve_texture_vertices")
    queue_texture = function_body(gpu, "queue_texture_triangle")
    require(re.search(r"\(\s*int\s+exact\b", texture_sig) is not None,
            "texture preparation must receive the exact-polygon verdict")
    exact_pos = resolve_texture.find("!exact")
    lookup_pos = resolve_texture.find("gte_precision_load_perspective_word_ex")
    require(0 <= exact_pos < lookup_pos,
            "depth lookup must activate only beyond the exact-polygon gate")
    require("qmax" in resolve_texture,
            "perspective q must come from reviewed exact depth provenance")
    require("s_texture_correction_yaw_active" in resolve_texture,
            "fractional yaw must disable perspective for the whole polygon")
    reset_pos = queue_texture.find("gr_set_perspective_triangle(0")
    accept_pos = queue_texture.find("gr_set_perspective_triangle(1")
    require(0 <= reset_pos < accept_pos,
            "failed q must reset before exact q can be queued")
    require("resolve_texture_vertices(exact" in texture and
            "queue_texture_triangle" in texture,
            "triangle wrapper must preserve the exact verdict through q queueing")

    prepare_sig = function_signature(gpu, "prepare_precise_triangle")
    prepare = function_body(gpu, "prepare_precise_triangle")
    require(re.search(r"\bint\s+prepare_precise_triangle\b", prepare_sig) is not None,
            "triangle precision preparation must return its exact verdict")
    require("return exact" in prepare,
            "textured triangles must be able to reuse the geometry verdict")

    for name in ("gp0_exec_textured_tri", "gp0_exec_shaded_textured_tri"):
        body = function_body(gpu, name)
        require(re.search(
            r"\b(?:const\s+)?int\s+\w+\s*=\s*prepare_precise_triangle\s*\(",
            body) is not None,
            f"{name} must retain the exact geometry verdict")
        require(re.search(r"prepare_texture_triangle\s*\(\s*\w+\s*,", body)
                is not None,
                f"{name} must pass its exact verdict into texture preparation")

    for name in ("gp0_exec_textured_quad", "gp0_exec_shaded_textured_quad"):
        body = function_body(gpu, name)
        require(body.count("prepare_precise_quad_vertices") == 1,
                f"{name} must preflight the whole quad once")
        require(body.count("resolve_texture_vertices(") == 1 and
                "precise_quad" in body,
                f"{name} must resolve all four q values once from the exact verdict")
        require(len(re.findall(
            r"queue_texture_triangle\s*\(\s*perspective_quad\b", body)) == 2,
                f"{name} must give both halves the same atomic q verdict")
        first_q = re.search(
            r"queue_texture_triangle\s*\(\s*perspective_quad\s*,\s*(\w+)\s*,"
            r"\s*0\s*,\s*1\s*,\s*2\s*\)", body)
        second_q = re.search(
            r"queue_texture_triangle\s*\(\s*perspective_quad\s*,\s*(\w+)\s*,"
            r"\s*2\s*,\s*1\s*,\s*3\s*\)", body)
        require(first_q is not None and second_q is not None and
                first_q.group(1) == second_q.group(1),
                f"{name} must preserve q order across the shared diagonal")

    # Perspective textures have a stricter depth contract than subpixel X/Y:
    # reject clamped far depth and the GTE near divide-overflow region without
    # weakening ordinary geometry lookup.
    require("GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID" in gte_header and
            "gte_precision_load_perspective_word_ex" in gte_header,
            "the strict perspective-depth lookup must be public and diagnosable")
    perspective_lookup = function_body(
        gte, "gte_precision_load_perspective_word_ex")
    require("precision_lookup_projection" in perspective_lookup and
            "!projection->perspective_valid" in perspective_lookup and
            "GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID" in perspective_lookup,
            "perspective lookup must layer its strict bit over exact provenance")
    projection = source_span(gte, "struct PreciseProjection", "};")
    require("perspective_valid" in projection,
            "the depth-validity verdict must travel with copied provenance")
    capture = function_body(gte, "gte_rtps_internal")
    for token in (
        "gte->H != 0",
        "gte->MAC3 > 0",
        "gte->MAC3 <= 0xFFFF",
        "(uint32_t)gte->SZ[3] * 2u > (uint32_t)gte->H",
    ):
        require(token in capture,
                f"strict perspective capture predicate missing: {token}")

    # Renderer API is optional per backend. The software implementation is
    # deliberately disconnected because its dormant path modifies native VRAM.
    require("void gr_set_perspective_triangle(" in render_header,
            "renderer facade must expose one-shot perspective metadata")
    require("(*set_perspective_triangle)(" in compact(render_header),
            "renderer vtable must carry one-shot perspective metadata")
    sw_backend = source_span(
        render, "static const GpuRenderBackend SW_BACKEND", "};")
    require(re.search(
        r"\.set_perspective_triangle\s*=\s*NULL", sw_backend) is not None,
        "software backend callback must remain NULL to preserve canonical VRAM")
    dispatch = function_body(render, "gr_set_perspective_triangle")
    require("g_b->set_perspective_triangle" in dispatch,
            "perspective metadata must dispatch through the active backend")
    require(dispatch.count("g_b->set_perspective_triangle") >= 2,
            "the optional perspective callback must be null-checked")

    # GL carries one normalized reciprocal depth per vertex. Location 11 is the
    # only new VAO attribute and offset 22 matches the 23-float CPU record.
    require("#define TEXV 23" in gl,
            "textured GL vertices must include one q component")
    tex_vs = source_span(gl, "static const char *TEX_VS", "static const char *TEX_FS")
    tex_fs = source_span(gl, "static const char *TEX_FS", "static const char *BLIT_VS")
    require("layout(location=11) in float a_q" in tex_vs,
            "TEX_VS must consume q at attribute location 11")
    gl_compact = compact(gl)
    require("p_glVertexAttribPointer(11,1,GL_FLOAT,GL_FALSE,st,(void*)(22*sizeof(float)))"
            in gl_compact,
            "VAO location 11 must read q from TEXV offset 22")
    require("p_glEnableVertexAttribArray(11)" in gl_compact,
            "the q vertex attribute must be enabled")
    require(re.search(r"vp\s*\[\s*22\s*\]\s*=", gl) is not None,
            "every queued textured vertex must initialize q")
    require(".set_perspective_triangle = glb_set_perspective_triangle" in gl,
            "the GL backend must register its one-shot q setter")
    gl_set = function_body(gl, "glb_set_perspective_triangle")
    require("sw_set_perspective_triangle" not in gl_set,
            "pre-context GL q must not fall through to canonical software VRAM")

    # Canonical drawing still uses affine v_uv. Only the independent visual
    # pass, and only an exact polygon with valid q, may select uv*q/q.
    require("noperspective out vec2 v_uv" in tex_vs and
            "noperspective in vec2 v_uv" in tex_fs,
            "the original canonical affine varying must remain intact")
    for token in ("u_visual > 0.5", "a_visual_flags > 1.5", "a_q > 0.0"):
        require(token in tex_vs,
                f"visual perspective gate is missing: {token}")
    require("abs(u_camera.x) <= 0.0000001" in tex_vs and
            "a_q/depth_den" not in compact(tex_vs),
            "fractional yaw must fail the primitive uniformly to affine")
    require("v_uvq" in tex_vs and "v_uvq" in tex_fs and
            re.search(r"a_uv\s*\*\s*q", tex_vs) is not None,
            "visual shader must interpolate uv*q and q separately")
    require("sample_uv" in tex_fs,
            "fragment sampling must use an explicit local corrected UV")
    require(re.search(r"sample_uv\s*=\s*v_uvq\.xy\s*/\s*v_uvq\.z", tex_fs)
            is not None,
            "the exact visual path must reconstruct UV as (uv*q)/q")
    require("floor(sample_uv.x)" in tex_fs and
            "floor(sample_uv.y)" in tex_fs,
            "nearest and bilinear setup must both start from sample_uv")
    require("floor(v_uv.x)" not in tex_fs and "floor(v_uv.y)" not in tex_fs,
            "no nearest/bilinear sampling may bypass the local corrected UV")

    # Perspective correction is attribute-only: it must not perturb clipping,
    # coverage, or any canonical geometry by repurposing homogeneous W.
    require("gl_Position.w" not in tex_vs,
            "texture correction must not mutate gl_Position.w")
    require(re.search(
        r"gl_Position\s*=\s*vec4\([^;]*0\.0\s*,\s*1\.0\s*\)",
        tex_vs, re.S) is not None,
        "TEX_VS must keep homogeneous W fixed at 1")
    flush = function_body(gl, "flush_tex_batch")
    canonical = flush.find(
        "geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 0)")
    visual = flush.find(
        "geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 1)")
    require(0 <= canonical < visual,
            "the canonical affine pass must execute before the visual pass")

    # Rectangle and UI decomposition never carry GTE-world metadata. Explicitly
    # clearing every one-shot prevents rejected or prior polygons leaking q.
    for name in ("gpu_flat_rect", "gpu_textured_rect"):
        body = function_body(gl, name)
        require("s_next_world = 0" in body and "s_next_precise = 0" in body,
                f"{name} must remain outside exact world correction")
        require(re.search(r"s_next_perspective\w*\s*=\s*0", body) is not None,
                f"{name} must explicitly clear one-shot perspective state")

    print("perspective texture structural contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"perspective texture structural contract: FAIL: {exc}",
              file=sys.stderr)
        raise SystemExit(1)
