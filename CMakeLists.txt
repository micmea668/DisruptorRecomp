cmake_minimum_required(VERSION 3.20)
project(DisruptorRecomp C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to the pinned PSXRecomp checkout")

# The project deliberately uses the redistributable OpenBIOS backend only.
set(PSXRECOMP_BIOS_STEMS "OpenBIOS" CACHE STRING "" FORCE)
set(PSX_RECOMP_UI OFF CACHE BOOL "" FORCE)

include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_control_probe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor_mouse_aim.cpp"
    DEBUG_PORT 4624
    WINDOW_TITLE "Disruptor Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    EXE_NAME "DisruptorRecompiled"
)

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(disruptor-mouse-aim-test
        tests/disruptor_mouse_aim_test.cpp)
    target_include_directories(disruptor-mouse-aim-test PRIVATE
        tests/stubs)
    target_compile_definitions(disruptor-mouse-aim-test PRIVATE PSX_SDL3=1)
    target_compile_features(disruptor-mouse-aim-test PRIVATE cxx_std_17)

    add_executable(disruptor-presentation-yaw-math-test
        tests/presentation_yaw_math_test.cpp)
    target_compile_features(disruptor-presentation-yaw-math-test PRIVATE cxx_std_17)

    add_test(NAME disruptor_mouse_aim
        COMMAND disruptor-mouse-aim-test)
    set_tests_properties(disruptor_mouse_aim PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_presentation_yaw_math
        COMMAND disruptor-presentation-yaw-math-test)

    add_test(NAME disruptor_mouse_aim_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_mouse_aim_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=1;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_modern_controls_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_modern_controls_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_MODERN_CONTROLS=1;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_high_precision_camera_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_high_precision_camera_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=1")
endif()
