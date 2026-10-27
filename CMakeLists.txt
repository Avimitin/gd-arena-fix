cmake_minimum_required(VERSION 3.25)
project(gd-arena-fix LANGUAGES C CXX ASM_MASM RC)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "gd-arena-fix requires the Windows x64 MSVC toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(GD_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "DLL, INI and test output directory")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GD_OUTPUT_DIR}/$<0:>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GD_OUTPUT_DIR}/$<0:>")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${GD_OUTPUT_DIR}/$<0:>")

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
include(CTest)

add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c
)
target_compile_options(minhook PRIVATE /W3)

add_library(gd-core OBJECT
    src/platform.cpp
    src/graphics.cpp
    src/timing.cpp
    src/timers.cpp
    src/scene_timing.cpp
    src/result_timing.cpp
    src/effect_timing.cpp
    src/song_timing.cpp
    src/service_timing.cpp
    src/system_timing.cpp
    src/tutorial_timing.cpp
    src/drum_tutorial_timing.cpp
    src/summary_timing.cpp
    src/minigame_timing.cpp
    src/arena_timing.cpp
    src/options_timing.cpp
    src/inline_hook.cpp
    src/note_diagnostics.cpp
    src/selection_diagnostics.cpp
    src/animations.cpp
    src/animation_shim.asm
    src/inline_shim.asm
)
target_compile_options(gd-core PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:/W4;/EHsc>")
# Preserve direct function addresses for the assembly bridges and unwind checks.
target_link_options(gd-core INTERFACE /INCREMENTAL:NO)
target_link_libraries(gd-core PUBLIC minhook bcrypt user32 d3d9)

# spice2x loads the DLL through LoadLibrary; it exports no link-time API.
add_library(gd-arena-fix MODULE src/entry.cpp src/licenses.rc)
target_link_libraries(gd-arena-fix PRIVATE gd-core)
# The resource compiler resolves license paths independently of the build tree.
target_include_directories(gd-arena-fix PRIVATE "${CMAKE_SOURCE_DIR}")
set_property(SOURCE src/licenses.rc PROPERTY OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/LICENSE;${CMAKE_SOURCE_DIR}/third_party/minhook/LICENSE.txt")

add_custom_target(default-ini ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GD_OUTPUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/gd-arena-fix.ini" "${GD_OUTPUT_DIR}/gd-arena-fix.ini"
    VERBATIM
)
add_dependencies(gd-arena-fix default-ini)

if(BUILD_TESTING)
    add_executable(gd-arena-fix-test tests/smoke.cpp tests/inline_fixture.asm)
    target_link_libraries(gd-arena-fix-test PRIVATE gd-core)
    target_link_options(gd-arena-fix-test PRIVATE "/MAP:${GD_OUTPUT_DIR}/gd-arena-fix-test.map")
    add_dependencies(gd-arena-fix-test default-ini)

    add_test(NAME config-default COMMAND gd-arena-fix-test --check-config 120 0)
    add_test(NAME config-high-refresh COMMAND gd-arena-fix-test --check-config 144 0)
    set_tests_properties(config-high-refresh PROPERTIES
        ENVIRONMENT "GDMOD_Refresh=144;GDMOD_FPS=144;GDMOD_Experimental=0")
    add_test(NAME animation-clock COMMAND gd-arena-fix-test --test-animation-clock)
    add_test(NAME timing COMMAND gd-arena-fix-test --test-timing)
    add_test(NAME inline-hook COMMAND gd-arena-fix-test --test-inline-hook)
    add_test(NAME game-profiles COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/profile_data.py")
    set_tests_properties(config-default config-high-refresh animation-clock timing inline-hook
        PROPERTIES WORKING_DIRECTORY "${GD_OUTPUT_DIR}")
endif()

add_custom_target(dist-zip
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/package.py"
        --dist-dir "${GD_OUTPUT_DIR}" --output-dir "${CMAKE_SOURCE_DIR}/artifacts"
    DEPENDS gd-arena-fix
    VERBATIM
)
add_custom_target(format
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/format.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(format-check
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/format.py" --check
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)
