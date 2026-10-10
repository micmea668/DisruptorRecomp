cmake_minimum_required(VERSION 3.20)
project(DisruptorRecomp C CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "DisruptorRecomp supports 64-bit Windows builds only. "
        "Use an x64 Visual Studio developer environment.")
endif()

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to the pinned PSXRecomp checkout")

option(DISRUPTOR_DEV_MENU
    "Build the in-game Disruptor settings menu"
    ON)
option(DISRUPTOR_FRAME_INTERPOLATION
    "Build the experimental presentation-only frame interpolator"
    ON)

set(DISRUPTOR_EXTRA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_backdrop_tiles.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_billboard_aspect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_cheats.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_control_probe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_far_rendering.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_frame_rate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_intro_skip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_language.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_language_disc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_language_hints.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_language_source.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_restart.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_mouse_aim.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_render_arena.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_grown_quad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_hud_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_sprite_depth.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_vertical_camera.cpp")

# The project deliberately uses the redistributable OpenBIOS backend only.
set(PSXRECOMP_BIOS_STEMS "OpenBIOS" CACHE STRING "" FORCE)
set(PSX_RECOMP_UI OFF CACHE BOOL "" FORCE)

include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

if(DISRUPTOR_DEV_MENU)
    include(FetchContent)
    FetchContent_Declare(disruptor_imgui
        URL
            "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.6.tar.gz"
        URL_HASH
            "SHA256=5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(disruptor_imgui)

    if(PSX_SDL3)
        set(DISRUPTOR_IMGUI_SDL_BACKEND
            "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
    else()
        set(DISRUPTOR_IMGUI_SDL_BACKEND
            "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp")
    endif()

    add_library(disruptor-imgui STATIC
        "${disruptor_imgui_SOURCE_DIR}/imgui.cpp"
        "${disruptor_imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${disruptor_imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${disruptor_imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${DISRUPTOR_IMGUI_SDL_BACKEND}"
        "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(disruptor-imgui PUBLIC
        "${disruptor_imgui_SOURCE_DIR}"
        "${disruptor_imgui_SOURCE_DIR}/backends")
    target_link_libraries(disruptor-imgui PRIVATE ${PSX_SDL_LIBRARIES})
    target_compile_features(disruptor-imgui PUBLIC cxx_std_20)
    if(WIN32)
        target_compile_definitions(
            disruptor-imgui PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>")
    endif()

    list(APPEND DISRUPTOR_EXTRA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_dev_menu.cpp")
    if(DISRUPTOR_FRAME_INTERPOLATION)
        list(APPEND DISRUPTOR_EXTRA_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_present_rate.cpp")
    endif()
endif()

file(GLOB GAME_GENERATED_FULL CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_002.24.code_full.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_002.24.code_full_*.c")
set(GAME_GENERATED_DISPATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_002.24.code_dispatch.c")
set(DISRUPTOR_STATIC_OVERLAY_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
    set(DISRUPTOR_STATIC_OVERLAY_DEFAULT ON)
endif()
option(DISRUPTOR_USE_CAPTURED_STATIC_OVERLAY
    "Link generated/overlays_static.c when a private captured-code build has produced it"
    ${DISRUPTOR_STATIC_OVERLAY_DEFAULT})
set(GAME_OVERLAY_STATIC "")
if(DISRUPTOR_USE_CAPTURED_STATIC_OVERLAY)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
        message(FATAL_ERROR
            "DISRUPTOR_USE_CAPTURED_STATIC_OVERLAY is enabled, but the private "
            "generated/overlays_static.c file does not exist")
    endif()
    set(GAME_OVERLAY_STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
endif()

psxrecomp_add_runtime_target(psx-runtime
    GAME_GENERATED_FULL_C ${GAME_GENERATED_FULL}
    GAME_GENERATED_DISPATCH_C "${GAME_GENERATED_DISPATCH}"
    GAME_OVERLAY_STATIC_C "${GAME_OVERLAY_STATIC}"
    EXTRAS_SOURCES
        ${DISRUPTOR_EXTRA_SOURCES}
    DEBUG_PORT 4624
    WINDOW_TITLE "Disruptor Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    EXE_NAME "DisruptorRecompiled"
)
target_compile_definitions(psx-runtime PRIVATE
    PSX_HAS_DISRUPTOR_BACKDROP_TILES=1
    PSX_HAS_DISRUPTOR_BILLBOARD_ASPECT=1
    PSX_HAS_DISRUPTOR_FAR_RENDERING=1
    PSX_HAS_DISRUPTOR_INTRO_SKIP=1
    PSX_HAS_DISRUPTOR_LANGUAGE=1
    PSX_HAS_DISRUPTOR_SPRITE_DEPTH=1
    PSX_HAS_DISRUPTOR_GROWN_QUAD=1
    PSX_HAS_DISRUPTOR_VERTICAL_CAMERA=1)
if(NOT DISRUPTOR_FRAME_INTERPOLATION)
    target_compile_definitions(psx-runtime PRIVATE
        # Keep the experimental sources available, but compile out frame
        # blending, temporal capture and the high-refresh presenter by default.
        PSX_DISABLE_FRAME_INTERPOLATION=1)
endif()
if(DISRUPTOR_DEV_MENU)
    target_compile_definitions(psx-runtime PRIVATE DISRUPTOR_DEV_MENU=1)
    target_link_libraries(psx-runtime PRIVATE disruptor-imgui)
endif()
if(WIN32)
    target_compile_definitions(
        psx-runtime PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>")
endif()
if(MSVC)
    # audio_trace.c is the sole runtime source using C11 atomics. Keep the
    # generated C in MSVC's extension-compatible C mode: its static tables use
    # address constants that strict /std:c11 rejects.
    set_property(SOURCE "${PSXRECOMP_ROOT}/runtime/src/audio_trace.c"
        APPEND PROPERTY COMPILE_OPTIONS
        "/std:c11" "/experimental:c11atomics")
endif()

if(WIN32)
    # UTF-8 as the game's code page: a path in settings.toml may hold letters the system's own page lacks.
    if(MINGW)
        target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor.rc")
    else()
        target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor.manifest")
    endif()
endif()

include(CTest)
if(WIN32)
    add_subdirectory(src/launcher)
endif()
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_test(NAME disruptor_backdrop_tiles_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_backdrop_tiles_contract.py"
            --require-artifacts)

    add_test(NAME disruptor_depth24_sync_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_depth24_sync_contract.py")

    add_test(NAME disruptor_geometry_provenance_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_geometry_provenance_contract.py")

    add_test(NAME disruptor_phase_a_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_phase_a_contract.py")

    add_test(NAME disruptor_perspective_texture_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_perspective_texture_contract.py")

    add_test(NAME disruptor_pinned_corner_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pinned_corner_contract.py")

    add_test(NAME disruptor_dev_menu_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dev_menu_contract.py")

    add_test(NAME disruptor_cheats_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cheats_contract.py")

    add_test(NAME disruptor_vertical_camera_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vertical_camera_contract.py")

    add_test(NAME disruptor_far_rendering_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_far_rendering_contract.py")

    add_test(NAME disruptor_frame_kind_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frame_kind_contract.py")

    add_test(NAME disruptor_temporal_presenter_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_temporal_presenter_contract.py")

    add_test(NAME disruptor_hud_scale_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hud_scale_contract.py")

    add_test(NAME disruptor_hud_widget_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hud_widget_contract.py")

    add_test(NAME disruptor_grown_quad_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_grown_quad_contract.py"
            --require-artifacts)

    add_test(NAME disruptor_sprite_range_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sprite_range_contract.py"
            --require-artifacts)
    set_tests_properties(disruptor_sprite_range_contract
        PROPERTIES LABELS retail)

    add_test(NAME disruptor_intro_skip_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_intro_skip_contract.py"
            --require-artifacts)

    add_test(NAME disruptor_language_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_language_contract.py"
            --require-artifacts)

    add_test(NAME disruptor_language_disc_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_language_disc_contract.py"
            --require-artifacts)

    add_test(NAME disruptor_frame_rate_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frame_rate_contract.py"
            --require-artifacts)

    add_test(NAME disruptor_billboard_aspect_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_billboard_aspect_contract.py")

    add_test(NAME disruptor_launcher_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_launcher_contract.py")

    add_test(NAME disruptor_framework_overlay_new_file
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_framework_overlay_new_file.py")
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(disruptor-user-settings-test
        tests/user_settings_persistence_test.cpp
        "${PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp"
        "${PSXRECOMP_ROOT}/recompiler/src/ps1_exe_parser.cpp")
    target_include_directories(disruptor-user-settings-test PRIVATE
        "${PSXRECOMP_ROOT}/recompiler/src"
        "${PSXRECOMP_ROOT}/recompiler/include"
        "${PSXRECOMP_ROOT}/recompiler/lib/fmt/include"
        "${PSXRECOMP_ROOT}/recompiler/lib/toml11")
    target_compile_definitions(disruptor-user-settings-test PRIVATE
        FMT_HEADER_ONLY=1
        "$<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>"
        "$<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN>")
    target_compile_features(disruptor-user-settings-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_user_settings_persistence
        COMMAND disruptor-user-settings-test)

    add_executable(disruptor-mouse-aim-test
        tests/disruptor_mouse_aim_test.cpp)
    target_include_directories(disruptor-mouse-aim-test PRIVATE
        tests/stubs)
    target_compile_definitions(disruptor-mouse-aim-test PRIVATE PSX_SDL3=1)
    target_compile_features(disruptor-mouse-aim-test PRIVATE cxx_std_20)

    add_executable(disruptor-cheats-test
        tests/disruptor_cheats_test.cpp)
    target_include_directories(disruptor-cheats-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-cheats-test PRIVATE cxx_std_20)

    add_executable(disruptor-vertical-camera-test
        tests/disruptor_vertical_camera_test.cpp)
    target_include_directories(disruptor-vertical-camera-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-vertical-camera-test PRIVATE cxx_std_20)

    add_executable(disruptor-far-rendering-test
        tests/disruptor_far_rendering_test.cpp)
    target_include_directories(disruptor-far-rendering-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-far-rendering-test PRIVATE cxx_std_20)

    add_executable(disruptor-render-arena-test
        tests/disruptor_render_arena_test.cpp)
    target_include_directories(disruptor-render-arena-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-render-arena-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_render_arena COMMAND disruptor-render-arena-test)

    add_executable(disruptor-backdrop-tiles-test
        tests/disruptor_backdrop_tiles_test.cpp)
    target_include_directories(disruptor-backdrop-tiles-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-backdrop-tiles-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_backdrop_tiles
        COMMAND disruptor-backdrop-tiles-test)

    add_executable(disruptor-gpu-ws-frame-kind-test
        tests/gpu_ws_frame_kind_test.cpp)
    target_include_directories(disruptor-gpu-ws-frame-kind-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-ws-frame-kind-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_ws_frame_kind
        COMMAND disruptor-gpu-ws-frame-kind-test)

    add_executable(disruptor-gpu-ws-hud-scale-test
        tests/gpu_ws_hud_scale_test.cpp)
    target_include_directories(disruptor-gpu-ws-hud-scale-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-ws-hud-scale-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_ws_hud_scale
        COMMAND disruptor-gpu-ws-hud-scale-test)

    add_executable(disruptor-gpu-ws-hud-widget-test
        tests/gpu_ws_hud_widget_test.cpp)
    target_include_directories(disruptor-gpu-ws-hud-widget-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-ws-hud-widget-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_ws_hud_widget
        COMMAND disruptor-gpu-ws-hud-widget-test)

    add_executable(disruptor-intro-skip-test
        tests/disruptor_intro_skip_test.cpp)
    target_include_directories(disruptor-intro-skip-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-intro-skip-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_intro_skip
        COMMAND disruptor-intro-skip-test)

    add_executable(disruptor-frame-rate-test
        tests/disruptor_frame_rate_test.cpp)
    target_include_directories(disruptor-frame-rate-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-frame-rate-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_frame_rate
        COMMAND disruptor-frame-rate-test)

    add_executable(disruptor-language-test
        tests/disruptor_language_test.cpp)
    target_include_directories(disruptor-language-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-language-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_language
        COMMAND disruptor-language-test)

    add_executable(disruptor-language-hints-test
        tests/disruptor_language_hints_test.cpp)
    target_include_directories(disruptor-language-hints-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-language-hints-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_language_hints
        COMMAND disruptor-language-hints-test)

    add_executable(disruptor-language-disc-test
        tests/disruptor_language_disc_test.cpp)
    target_compile_features(disruptor-language-disc-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_language_disc
        COMMAND disruptor-language-disc-test)

    add_executable(disruptor-language-overlay-test
        tests/disruptor_language_overlay_test.cpp)
    target_include_directories(disruptor-language-overlay-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp-overlay/runtime/include"
        "${PSXRECOMP_ROOT}/runtime/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(disruptor-language-overlay-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_language_overlay
        COMMAND disruptor-language-overlay-test)
    add_test(NAME disruptor_language_overlay_tall
        COMMAND disruptor-language-overlay-test tall)

    add_executable(host-pick-slot-test
        tests/host_pick_slot_test.cpp)
    target_compile_features(host-pick-slot-test PRIVATE cxx_std_20)
    add_test(NAME host_pick_slot
        COMMAND host-pick-slot-test)

    add_executable(disruptor-restart-test
        tests/disruptor_restart_test.cpp)
    target_include_directories(disruptor-restart-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-restart-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_restart
        COMMAND disruptor-restart-test run "${CMAKE_CURRENT_BINARY_DIR}/disruptor_restart_test.txt")

    add_executable(disruptor-present-rate-test
        tests/disruptor_present_rate_test.cpp)
    target_include_directories(disruptor-present-rate-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/present_rate_fakes")
    target_compile_features(disruptor-present-rate-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_present_rate
        COMMAND disruptor-present-rate-test)

    add_executable(disruptor-grown-quad-test
        tests/disruptor_grown_quad_test.cpp)
    target_include_directories(disruptor-grown-quad-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-grown-quad-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_grown_quad
        COMMAND disruptor-grown-quad-test)

    add_executable(disruptor-sprite-depth-test
        tests/disruptor_sprite_depth_test.cpp)
    target_include_directories(disruptor-sprite-depth-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-sprite-depth-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_sprite_depth
        COMMAND disruptor-sprite-depth-test)

    add_executable(disruptor-billboard-aspect-test
        tests/disruptor_billboard_aspect_test.cpp)
    target_include_directories(disruptor-billboard-aspect-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-billboard-aspect-test PRIVATE cxx_std_20)

    add_executable(disruptor-gpu-temporal-sprite-test
        tests/gpu_temporal_sprite_test.cpp)
    target_include_directories(disruptor-gpu-temporal-sprite-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-temporal-sprite-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_temporal_sprite
        COMMAND disruptor-gpu-temporal-sprite-test)

    add_executable(disruptor-gpu-ws-tag-match-test
        tests/gpu_ws_tag_match_test.cpp)
    target_include_directories(disruptor-gpu-ws-tag-match-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-ws-tag-match-test PRIVATE cxx_std_20)

    add_executable(disruptor-gpu-temporal-match-test
        tests/gpu_temporal_match_test.cpp)
    target_include_directories(disruptor-gpu-temporal-match-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-temporal-match-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_temporal_match
        COMMAND disruptor-gpu-temporal-match-test)

    add_executable(disruptor-gpu-temporal-mesh-test
        tests/gpu_temporal_mesh_test.cpp)
    target_include_directories(disruptor-gpu-temporal-mesh-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-temporal-mesh-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_temporal_mesh
        COMMAND disruptor-gpu-temporal-mesh-test)

    add_executable(disruptor-gpu-pinned-corner-test
        tests/gpu_pinned_corner_test.cpp)
    target_include_directories(disruptor-gpu-pinned-corner-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-gpu-pinned-corner-test PRIVATE cxx_std_20)
    add_test(NAME disruptor_gpu_pinned_corner
        COMMAND disruptor-gpu-pinned-corner-test)

    add_executable(disruptor-presentation-yaw-math-test
        tests/presentation_yaw_math_test.cpp)
    target_compile_features(disruptor-presentation-yaw-math-test PRIVATE cxx_std_20)

    add_executable(disruptor-gte-precision-store-pc-test
        tests/gte_precision_store_pc_test.cpp
        "${PSXRECOMP_ROOT}/runtime/src/gte.cpp")
    target_include_directories(disruptor-gte-precision-store-pc-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_definitions(disruptor-gte-precision-store-pc-test PRIVATE
        PSX_NO_DEBUG_TOOLS=1
        PSX_GTE_REGISTER_TEST=1
        PSX_ENABLE_BLOCK_CYCLES=1)
    target_compile_features(disruptor-gte-precision-store-pc-test PRIVATE
        cxx_std_20)

    add_test(NAME disruptor_mouse_aim
        COMMAND disruptor-mouse-aim-test)
    add_test(NAME disruptor_cheats
        COMMAND disruptor-cheats-test)
    add_test(NAME disruptor_vertical_camera
        COMMAND disruptor-vertical-camera-test)
    add_test(NAME disruptor_far_rendering
        COMMAND disruptor-far-rendering-test)
    add_test(NAME disruptor_billboard_aspect
        COMMAND disruptor-billboard-aspect-test)
    add_test(NAME disruptor_gpu_ws_tag_match
        COMMAND disruptor-gpu-ws-tag-match-test)
    set_tests_properties(disruptor_mouse_aim PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_presentation_yaw_math
        COMMAND disruptor-presentation-yaw-math-test)

    add_test(NAME disruptor_gte_precision_store_pc
        COMMAND disruptor-gte-precision-store-pc-test)

    add_test(NAME disruptor_mouse_aim_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_mouse_aim_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=1;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_vertical_look_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_vertical_look_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=1;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_modern_controls_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_modern_controls_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=1;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_high_precision_camera_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_high_precision_camera_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=1")
endif()
