cmake_minimum_required(VERSION 3.20)
project(DisruptorRecomp C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "DisruptorRecomp supports 64-bit Windows builds only. "
        "Use an x64 Visual Studio developer environment.")
endif()

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to the pinned PSXRecomp checkout")

option(DISRUPTOR_DEV_MENU
    "Build the in-game Disruptor settings and diagnostics menu"
    ON)

set(DISRUPTOR_EXTRA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_cheats.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_control_probe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_mouse_aim.cpp"
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
    target_compile_features(disruptor-imgui PUBLIC cxx_std_17)
    if(WIN32)
        target_compile_definitions(
            disruptor-imgui PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>")
    endif()

    list(APPEND DISRUPTOR_EXTRA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_dev_menu.cpp")
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
    PSX_HAS_DISRUPTOR_VERTICAL_CAMERA=1)
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

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_test(NAME disruptor_geometry_provenance_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_geometry_provenance_contract.py")

    add_test(NAME disruptor_phase_a_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_phase_a_contract.py")

    add_test(NAME disruptor_perspective_texture_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_perspective_texture_contract.py")

    add_test(NAME disruptor_dev_menu_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dev_menu_contract.py")

    add_test(NAME disruptor_cheats_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cheats_contract.py")

    add_test(NAME disruptor_vertical_camera_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vertical_camera_contract.py")

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
    target_compile_features(disruptor-user-settings-test PRIVATE cxx_std_17)
    add_test(NAME disruptor_user_settings_persistence
        COMMAND disruptor-user-settings-test)

    add_executable(disruptor-mouse-aim-test
        tests/disruptor_mouse_aim_test.cpp)
    target_include_directories(disruptor-mouse-aim-test PRIVATE
        tests/stubs)
    target_compile_definitions(disruptor-mouse-aim-test PRIVATE PSX_SDL3=1)
    target_compile_features(disruptor-mouse-aim-test PRIVATE cxx_std_17)

    add_executable(disruptor-cheats-test
        tests/disruptor_cheats_test.cpp)
    target_include_directories(disruptor-cheats-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-cheats-test PRIVATE cxx_std_17)

    add_executable(disruptor-vertical-camera-test
        tests/disruptor_vertical_camera_test.cpp)
    target_include_directories(disruptor-vertical-camera-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    target_compile_features(disruptor-vertical-camera-test PRIVATE cxx_std_17)

    add_executable(disruptor-presentation-yaw-math-test
        tests/presentation_yaw_math_test.cpp)
    target_compile_features(disruptor-presentation-yaw-math-test PRIVATE cxx_std_17)

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
        cxx_std_17)

    add_test(NAME disruptor_mouse_aim
        COMMAND disruptor-mouse-aim-test)
    add_test(NAME disruptor_cheats
        COMMAND disruptor-cheats-test)
    add_test(NAME disruptor_vertical_camera
        COMMAND disruptor-vertical-camera-test)
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
