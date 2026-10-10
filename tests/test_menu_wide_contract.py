#!/usr/bin/env python3
"""Source contract for the front end's backdrop on a wide screen: where the GPU and the present step call it."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


gpu = read("psxrecomp-overlay/runtime/src/gpu.c")
host = read("psxrecomp-overlay/runtime/src/main.cpp")
kinds = read("psxrecomp-overlay/runtime/include/gpu_ws_frame_kind.h")
state = read("psxrecomp-overlay/runtime/src/boot_state.c")
cmake = read("CMakeLists.txt")

require(
    "static int ws_menu_wanted(void) { return ws_mode == 1 && ws_xnum != ws_xden; }" in gpu
    and "    if (!ws_engaged()) return 0;\n    if (displayed ? gpu_ws_displayed_menu() : ws_menu_frame()) return 0;\n"
    in gpu
    and gpu.index("    if (displayed ? gpu_ws_displayed_menu() : ws_menu_frame()) return 0;")
    < gpu.index("    if (!ws_game_mode_of(displayed)) return 1;     /* full-2D screen */"),
    "a front-end frame is stretched like a world's, in squash mode only, and that is asked before the frame is judged flat",
)
require(
    "    kinds->buffers[slot].menu = 0;\n" in kinds
    and "    if (kinds->buffers[kinds->drawing].known) kinds->buffers[kinds->drawing].menu = 1;\n" in kinds
    and "flat = 0" not in kinds[kinds.index("static inline void psx_ws_frame_kinds_menu_begin(") :],
    "a front-end frame stays a flat one for everything that asks about worlds, and each drawing area starts as none",
)
take = gpu[
    gpu.index("static void ws_menu_take_picture(void) {") : gpu.index("static void gp0_commit_cpu_to_vram(void) {")
]
require(
    "    if (!ws_menu_fresh || !ws_menu_wanted()) {\n        ws_menu_give_back();\n        return;\n    }\n"
    "    if (!ws_menu_lent) gr_vram_transfer_out(PSX_WS_MENU_KEPT_X, 0, PSX_WS_MENU_LOGO_WIDE, PSX_WS_MENU_LOGO_ROWS, ws_menu_under);\n"
    in take
    and "memcpy(&picture[row * PSX_WS_MENU_WORDS], &vram[row * 1024 + PSX_WS_MENU_X], PSX_WS_MENU_WORDS * sizeof(uint16_t));\n"
    "    memcpy(ws_menu_uploaded, &picture[PSX_WS_MENU_LOGO_TOP * PSX_WS_MENU_WORDS], sizeof(ws_menu_uploaded));\n"
    "    psx_ws_menu_logo_mask(picture, PSX_WS_MENU_WORDS, palette, mask);\n"
    "    psx_ws_menu_keep_logo(picture, PSX_WS_MENU_WORDS, palette, mask, ws_menu_kept);\n"
    "    gr_vram_transfer_in(PSX_WS_MENU_KEPT_X, 0, PSX_WS_MENU_LOGO_WIDE, PSX_WS_MENU_LOGO_ROWS, ws_menu_kept);\n"
    in take
    and "    psx_ws_menu_paint(picture, PSX_WS_MENU_WORDS, mask);\n"
    "    memcpy(ws_menu_painted, &picture[PSX_WS_MENU_LOGO_TOP * PSX_WS_MENU_WORDS], sizeof(ws_menu_painted));\n"
    "    gr_vram_transfer_in(PSX_WS_MENU_X, PSX_WS_MENU_LOGO_TOP, PSX_WS_MENU_WORDS, PSX_WS_MENU_LOGO_ROWS, ws_menu_painted);\n"
    "    ws_menu_logo = ws_menu_lent = ws_menu_rows = 1;\n}"
    in take
    and "vram[" not in take.replace("&vram[row * 1024 + PSX_WS_MENU_X]", ""),
    "a picture with the logo is taken on a wide screen from a copy: the logo and the paint go to the renderer alone, "
    "what the logo replaces there is remembered first, and the CPU copy stays as the game uploaded it",
)
back = gpu[gpu.index("static void ws_menu_return(int x, int y") : gpu.index("static void ws_menu_take_picture(void) {")]
require(
    "    gr_vram_transfer_out(x, y, w, h, now);\n"
    "    if (psx_ws_menu_return(now, w, w, h, put, before)) gr_vram_transfer_in(x, y, w, h, now);\n"
    in back
    and "    if (ws_menu_lent) ws_menu_return(PSX_WS_MENU_KEPT_X, 0, PSX_WS_MENU_LOGO_WIDE, PSX_WS_MENU_LOGO_ROWS, ws_menu_kept, ws_menu_under);\n"
    "    if (ws_menu_rows) ws_menu_return(PSX_WS_MENU_X, PSX_WS_MENU_LOGO_TOP, PSX_WS_MENU_WORDS, PSX_WS_MENU_LOGO_ROWS, ws_menu_painted, ws_menu_uploaded);\n"
    "    ws_menu_lent = ws_menu_rows = ws_menu_logo = 0;\n"
    in back
    and "    ws_menu_due = 1;\n    ws_menu_halves = 0;\n    ws_menu_yield_drawing_area();\n"
    "    psx_ws_frame_kinds_area(&ws_frame_kinds, draw_area_left, draw_area_top, WS_GTE_GAME_MODE_MIN_VERTS);\n"
    in gpu
    and "    if (ws_menu_due && opcode >= 0x20 && opcode <= 0x7F && (opcode & 0xFC) != 0x64) {" in gpu
    and "        ws_menu_due = 0;\n        ws_menu_give_back();\n    }\n"
    "    /* Draw-census: capture every drawing primitive's first vertex + camera. */\n"
    in gpu
    and "    const int menu_shape = psx_ws_menu_backdrop_half(x0, y0, w, h) & menu_page;\n    if (ws_menu_due) {" in gpu
    and "        ws_menu_due = 0;\n        if (!menu_shape) ws_menu_give_back();\n    }\n    if (menu_shape) {\n"
    in gpu,
    "the renderer's video memory is given back word by word before the first primitive of a frame that does not begin "
    "with the backdrop",
)
require(
    "static void ws_menu_yield(int x, int y, int w, int h, int written) {\n"
    "    if (!psx_ws_menu_reaches(x, y, w, h)) return;\n    ws_menu_give_back();\n"
    "    if (written) ws_menu_fresh = 0;\n}\n"
    in back
    and "    vram_write_h = (h == 0) ? 0x200 : (uint16_t)h;\n"
    "    if (!psx_ws_menu_is_picture(vram_write_x, vram_write_y, vram_write_w, vram_write_h))"
    in gpu
    and "        ws_menu_yield(vram_write_x, vram_write_y, vram_write_w, vram_write_h, 1);" in gpu
    and "    vram_read_h = (h == 0) ? 0x200 : (uint16_t)h;\n"
    "    ws_menu_yield(vram_read_x, vram_read_y, vram_read_w, vram_read_h, 0);\n"
    in gpu
    and "            ws_menu_yield(src_x, src_y, w, h, 0);\n            ws_menu_yield(dst_x, dst_y, w, h, 1);\n"
    "            gr_copy_rect(src_x, src_y, dst_x, dst_y, w, h);\n"
    in gpu
    and "    ws_menu_yield((int)dst_x, (int)dst_y, (int)width, (int)height, 1);\n"
    "    gr_fill_rect((int)dst_x, (int)dst_y, (int)width, (int)height, color16);\n"
    in gpu
    and "    ws_menu_yield((int)draw_area_left, (int)draw_area_top, (int)draw_area_right - (int)draw_area_left + 1, "
    "(int)draw_area_bottom - (int)draw_area_top + 1, 1);\n"
    in gpu
    and "                     (int)draw_area_right, (int)draw_area_bottom);\n    ws_menu_yield_drawing_area();\n"
    in gpu,
    "before the game uploads, reads, copies, fills or may draw where the logo and the paint are, they are given back, "
    "and what it writes there makes the CPU copy of the picture stale",
)
require(
    "    PstW w; uint32_t n = gpu_snapshot_bytes();\n    ws_menu_give_back();" in gpu
    and "give_back" not in gpu[gpu.index("void gpu_cosim_snapshot_write(uint8_t *p) {") :].split("\n}\n", 1)[0]
    and state.index("write_module_section(f, BS_SEC_GPU, gpu_snapshot_bytes, gpu_snapshot_write);")
    < state.index("gr_vram_transfer_out(0, 0, VRAM_W, VRAM_H, vbuf);")
    and "    if (!gpu_snap_parse(&r)) return 0;\n    ws_menu_lent = ws_menu_rows = ws_menu_logo = ws_menu_fresh = 0;"
    in gpu,
    "a save state reads the renderer's video memory after the logo and the paint are given back, and a loaded one owes nothing",
)
require(
    "    depth24_note_upload(vram_write_x, vram_write_w);\n"
    "    if (psx_ws_menu_is_picture(vram_write_x, vram_write_y, vram_write_w, vram_write_h)) {\n"
    "        ws_menu_rows = 0; /* the renderer's rows are the game's again */\n"
    "        ws_menu_fresh = psx_ws_menu_has_logo(&vram[PSX_WS_MENU_X], 1024, ws_menu_palette());\n"
    "        ws_menu_take_picture();\n    }\n" in gpu,
    "the whole picture is judged and taken when it has arrived, and its rows in the renderer are then the game's own",
)
rect = gpu[gpu.index("static void gp0_exec_textured_rect(void) {") : gpu.index("/* Execute 1x1 dot (GP0 0x68-0x6B) */")]
require(
    "    const int menu_page = texpage_y != 0 || texpage_colors != 1 || clut_x > 1024 - 256 ? 0 : "
    "texpage_x == PSX_WS_MENU_X / 64 ? 1 : texpage_x == (PSX_WS_MENU_X + 128) / 64 ? 2 : 0;"
    in rect
    and "    const int menu_shape = psx_ws_menu_backdrop_half(x0, y0, w, h) & menu_page;\n" in rect
    and "    if (menu_shape) {\n        ws_menu_clut_x = clut_x;\n        ws_menu_clut_y = clut_y;\n"
    "        if (!ws_menu_logo) ws_menu_take_picture();"
    in rect
    and "    const int menu_half = ws_menu_logo ? menu_shape : 0;\n"
    "    if (menu_half && ws_menu_wanted()) psx_ws_frame_kinds_menu_begin(&ws_frame_kinds);\n"
    in rect,
    "the backdrop is a rectangle of the right size drawn from the picture's own pages with a palette that fits its row: "
    "it takes the picture again when the logo was given back, and makes the frame a front-end one",
)
require(
    "static int ws_menu_rect(int32_t *x0, int w) { return ws_menu_frame() ? psx_ws_menu_squash_rect(x0, w, ws_xnum, ws_xden) : w; }\n"
    in gpu
    and "    if (ws_menu_frame()) {\n        if (!menu_half) ws_w = ws_menu_rect(&x0, w);" in rect
    and "    } else if (ws_active() && w > 0) {\n" in rect
    and rect.index("    if (ws_menu_frame()) {") < rect.index("ws_hud_widget_rect(") < rect.index("ws_hud_user_scale(")
    and "    *out_h = 0;\n    if (ws_menu_frame()) return ws_menu_rect(x0, w);\n    if (!ws_active()) return 0;\n"
    in gpu
    and "    ws_expand_fullscreen_rect(&x0, y0, &w, h);\n    w = ws_menu_rect(&x0, w);\n" in gpu
    and "    parse_vertex(gp0_cmd_buf[1], &x, &y);\n    (void)ws_menu_rect(&x, 1);\n" in gpu
    and "    const int w = ws_menu_rect(&x0, 8);\n" in gpu
    and "    gr_draw_flat_rect(x0, y0, w, 8, color);\n" in gpu
    and "            const int w = ws_menu_rect(&x0, 16);\n" in gpu
    and "            gr_draw_flat_rect(x0, y0, w, 16, color);\n" in gpu,
    "on a front-end frame every rectangle but the backdrop is squashed about the centre, and no rule of the HUD's applies",
)
require(
    "    if (count <= 0) return;\n    if (ws_menu_frame()) {" in gpu
    and "        psx_ws_menu_squash_corners(vx, count, ws_xnum, ws_xden);\n        return;\n    }\n" in gpu
    and all(
        "    ws_nw_hud_shift_vertices(vx, "
        in gpu[gpu.index(f"static void gp0_exec_{name}(void) {{") :].split("\nstatic void ", 1)[0]
        for name in (
            "mono_tri",
            "mono_quad",
            "shaded_tri",
            "shaded_quad",
            "textured_tri",
            "textured_quad",
            "shaded_textured_tri",
            "shaded_textured_quad",
            "mono_line",
            "shaded_line",
        )
    ),
    "and so is every polygon and line: each handler passes its corners through the one place that does it",
)
require(
    "    if (ws_menu_halves & menu_half) ws_menu_halves = 0;\n    ws_menu_halves |= menu_half;\n"
    "    if (ws_menu_halves != 3) return;\n"
    in rect
    and "    if (ws_menu_frame()) psx_ws_menu_logo_span(PSX_WS_MENU_WIDE / 2, ws_xnum, ws_xden, &logo_left, &logo_right);\n"
    in rect
    and "                                 0, 0, PSX_WS_MENU_LOGO_WIDE, PSX_WS_MENU_LOGO_ROWS,\n"
    "                                 clut_x, clut_y, PSX_WS_MENU_KEPT_PAGE);\n"
    in rect
    and rect.index("gr_draw_textured_rect(x0, y0, w, h, u0, v0, clut_x, clut_y, current_texpage());")
    < rect.index("    if (ws_menu_halves != 3) return;"),
    "the kept logo is drawn once both halves of one frame's backdrop are, squashed on a wide frame and in its old place on any other",
)
require(
    "                fmv_frame || mdec_recently_active(2) || gpu_ws_displayed_menu() != 0);" in host
    and "int  gpu_ws_displayed_menu(void);" in read("psxrecomp-overlay/runtime/include/gpu.h"),
    "nothing is put in between the frames of a front-end screen",
)
require(
    "+runtime/include/gpu_ws_menu.h" in read("PSXRECOMP_OVERLAY_FILES.txt").splitlines()
    and "    add_test(NAME disruptor_gpu_ws_menu\n        COMMAND disruptor-gpu-ws-menu-test)\n" in cmake
    and 'tests/test_menu_wide_contract.py")' in cmake,
    "the header is part of the overlay and its tests remain registered",
)

print("Disruptor wide front-end backdrop source contract: PASS")
