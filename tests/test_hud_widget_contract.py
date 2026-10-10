#!/usr/bin/env python3
"""Source contract for widescreen HUD widgets: the weapon list keeps one pivot."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start)]


cmake = read("CMakeLists.txt")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt")
gpu = read("psxrecomp-overlay/runtime/src/gpu.c")
header = read("psxrecomp-overlay/runtime/include/gpu.h")
module = read("src/disruptor_hud_widgets.cpp")

require(
    "+runtime/include/gpu_ws_hud_widget.h" in manifest.splitlines(),
    "overlay manifest must list the HUD widget arithmetic",
)
require(
    "    add_executable(disruptor-gpu-ws-hud-widget-test\n        tests/gpu_ws_hud_widget_test.cpp)\n" in cmake
    and "    add_test(NAME disruptor_gpu_ws_hud_widget\n        COMMAND disruptor-gpu-ws-hud-widget-test)\n" in cmake
    and '    add_test(NAME disruptor_hud_widget_contract\n        COMMAND "${Python3_EXECUTABLE}" -B\n'
    '            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hud_widget_contract.py")\n' in cmake
    and '"${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_hud_widgets.cpp"' in cmake,
    "the widget unit test, its contract and the game's widget list must remain in the build",
)
require(
    "void gpu_ws_set_hud_widgets(uint32_t table_pointer, int layers, const int32_t *boxes, int count);" in header,
    "the widget API must stay public",
)

lookup = body(gpu, "static const PsxWsHudWidget *ws_hud_widget(")
require(
    "    if (!ws_active() || psx_ws_prim_is_tagged()) return NULL;\n"
    "    return psx_ws_hud_widgets_take(&ws_hud, x0, y0, x1, y1, rectangle);" in lookup,
    "only an untagged primitive in widescreen may ask for a widget, and the tested rule alone answers",
)
setter = body(gpu, "void gpu_ws_set_hud_widgets(")
require(
    "    ws_hud_table_pointer = table_pointer;\n"
    "    memset(&ws_hud, 0, sizeof(ws_hud));\n"
    "    ws_hud.layers = layers;\n" in setter
    and "        const PsxWsHudWidget widget = { box[0], box[1], box[2], box[3], box[4], box[5] };\n"
    "        ws_hud.widgets[ws_hud.count++] = widget;" in setter
    and "i < count && i < PSX_WS_HUD_WIDGET_MAX" in setter,
    "a game's widgets must replace the earlier ones whole, in the order of their six numbers, and never past the table",
)
begin = body(gpu, "void gpu_ws_begin_linked_list(void)")
node = body(gpu, "void gpu_set_gp0_linked_list_node(")
end = body(gpu, "void gpu_ws_end_linked_list(void)")
area = body(gpu, "static void gp0_exec_draw_area_tl(void)")
require(
    "    psx_ws_hud_widgets_list(&ws_hud, ws_hud.count && ws_active() ? psx_read_word(ws_hud_table_pointer) : 0u);" in begin
    and "    psx_ws_hud_widgets_list_end(&ws_hud);" in end,
    "every linked list must be begun with what the game's table word names now, or nothing outside widescreen, and ended",
)
require(
    "    if (word_count == 0) {\n" in node
    and "        psx_ws_hud_widgets_node(&ws_hud, addr);\n    }" in node,
    "an empty node, and nothing else, must open a layer",
)
require(
    "    psx_ws_hud_widgets_frame(&ws_hud);" in area,
    "a new drawing area must start a new frame for the widgets",
)
require(
    gpu.count("psx_ws_hud_widgets_list(") == 1 and gpu.count("psx_ws_hud_widgets_list_end(") == 1
    and gpu.count("psx_ws_hud_widgets_node(") == 1 and gpu.count("psx_ws_hud_widgets_frame(") == 1
    and gpu.count("psx_ws_hud_widgets_take(") == 1,
    "nothing else in the renderer may move the widgets' state",
)

rect = body(gpu, "static int ws_hud_widget_rect(")
quad = body(gpu, "static void ws_hud_widget_quad(")
require(
    rect == "static int ws_hud_widget_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h) {\n"
    "    const PsxWsHudWidget *widget = ws_hud_widget(*x, *y, *x + *w, *y + *h, 1);\n"
    "    if (!widget) return 0;\n"
    "    psx_ws_hud_widget_rect(widget, x, y, w, h, ws_xnum, ws_xden, ws_hud_scale_percent);\n"
    "    return 1;",
    "a widget's rectangle must be looked up by its own bounds and mapped in place, with the squash and the HUD size, and nothing more",
)
require(
    "    if (!ws_hud.in_hud_layer || !ws_axis_aligned_quad(vx, vy)) return;" in quad
    and "    const PsxWsHudWidget *widget = ws_hud_widget(min_x, min_y, max_x, max_y, 0);\n    if (!widget) return;" in quad
    and "        psx_ws_hud_widget_corner(widget, &vx[i], &vy[i], ws_xnum, ws_xden, ws_hud_scale_percent);" in quad,
    "only an axis-aligned quad wholly inside a widget may be mapped, corner by corner",
)

fixed = body(gpu, "static int ws_sprt_fixed_transform(")
textured_rect = body(gpu, "static void gp0_exec_textured_rect(void)")
flat_quad = body(gpu, "static void gp0_exec_mono_quad(void)")
textured_quad = body(gpu, "static void gp0_exec_textured_quad(void)")
require(
    "    int32_t widget_w = w, widget_h = w;\n"
    "    if (ws_hud_widget_rect(x0, y0, &widget_w, &widget_h)) {\n"
    "        *out_h = (int)widget_h;\n"
    "        return (int)widget_w;\n"
    "    }\n"
    "    int auto_w = w;" in fixed
    and fixed.index("ws_tagged_anchor(&ax)") < fixed.index("ws_hud_widget_rect("),
    "a fixed-size sprite must try its widget after the tag and before every other HUD rule",
)
require(
    "            int32_t widget_w = w, widget_h = h;\n"
    "            if (ws_hud_widget_rect(&x0, &y0, &widget_w, &widget_h)) {\n"
    "                ws_w = (int)widget_w;\n"
    "                ws_h = (int)widget_h;\n"
    "            } else if (ws_auto_ui_transform_rect(&x0, y0, &corrected_w, h))" in textured_rect,
    "a textured rectangle must try its widget before every other HUD rule",
)
require(
    flat_quad.count("ws_hud_widget_quad(vx, vy);") == 1
    and flat_quad.index("ws_expand_fullscreen_rect(") < flat_quad.index("ws_hud_widget_quad(vx, vy);")
    < flat_quad.index("vx[i] += draw_offset_x;"),
    "a flat quad must be mapped after the full-screen case and before the draw offset",
)
require(
    textured_quad.count("ws_hud_widget_quad(vx, vy);") == 1
    and textured_quad.index("ws_tagged_anchor(&ws_ax)") < textured_quad.index("ws_hud_widget_quad(vx, vy);")
    < textured_quad.index("ws_auto_ui_transform_quad(vx, vy);"),
    "a textured quad must be mapped after the tag and before the automatic UI pass",
)
require(
    gpu.count("ws_hud_widget_quad(") == 3 and gpu.count("ws_hud_widget_rect(") == 4 and gpu.count("psx_ws_hud_widget_rect(") == 1,
    "no other primitive may be mapped as part of a widget",
)

require(
    "constexpr uint32_t kTablePointer = 0x80071490u;" in module
    and "constexpr int kHudLayers = 1;" in module
    and "constexpr int32_t kPanels[2][6] = {{16, 10, 128, 196, 0, 0}, {232, 10, 298, 170, 320, 0}};" in module
    and "    gpu_ws_set_hud_widgets(kTablePointer, kHudLayers, kPanels[0], 2);" in module
    and module.count("gpu_ws_set_hud_widgets(") == 1
    and "constexpr int kTextStrip = 128;" in module
    and "    gpu_ws_set_hud_text_strip(kTextStrip);\n" in module
    and module.count("gpu_ws_set_hud_text_strip(") == 1,
    "the game must declare where its table is, its HUD node, the weapon list's box with a pivot in the top left corner, "
    "the psionics list's with one in the top right, and how wide its printer makes a line of text",
)

print("Disruptor HUD widget source contract: PASS")
