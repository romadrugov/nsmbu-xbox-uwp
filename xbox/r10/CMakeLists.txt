cmake_minimum_required(VERSION 3.23)
# R10: full *real* game/Cafe OS/GX2/Vulkan compiled using Windows Store.
# This target NEVER replaces R7D; Nintendo sources supplied in private CI.
project(nsmbu_real_uwp_compile C CXX)
if(NOT WINDOWS_STORE)
  message(FATAL_ERROR "R10 requires WindowsStore/UWP, not desktop Windows")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Game recompilation requires Clang musttail")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(NSMBU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(R10_GEN_DIR "" CACHE PATH "Private SHA-verified USA_V0 generated C")
set(R10_SDL3_INSTALL "" CACHE PATH "Precompiled MSVC C++/CX SDL3 UWP install prefix")
set(R10_GLSLANG_ROOT "" CACHE PATH "Pinned glslang source")
set(R10_VULKAN_HEADERS "" CACHE PATH "Pinned Vulkan-Headers source")
set(R10_ZLIB_ROOT "" CACHE PATH "Pinned zlib source for UWP Store CRT")
set(R10_LZ4_ROOT "" CACHE PATH "Pinned LZ4 source for UWP savestates")
foreach(path R10_GEN_DIR R10_SDL3_INSTALL R10_GLSLANG_ROOT R10_VULKAN_HEADERS R10_ZLIB_ROOT R10_LZ4_ROOT)
  if(NOT IS_DIRECTORY "${${path}}")
    message(FATAL_ERROR "Missing R10 dependency: ${path}")
  endif()
endforeach()
foreach(file "${R10_GEN_DIR}/funcs.h" "${R10_GEN_DIR}/table.c"
  "${R10_GEN_DIR}/imports.c" "${R10_VULKAN_HEADERS}/include/vulkan/vulkan.h"
  "${R10_SDL3_INSTALL}/include/SDL3/SDL.h"
  "${R10_GLSLANG_ROOT}/glslang/Public/ShaderLang.h"
  "${R10_ZLIB_ROOT}/zlib.h" "${R10_ZLIB_ROOT}/CMakeLists.txt"
  "${R10_LZ4_ROOT}/lib/lz4.h" "${R10_LZ4_ROOT}/lib/lz4.c")
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "Incomplete full engine input: ${file}")
  endif()
endforeach()
file(GLOB GAME_CODE CONFIGURE_DEPENDS "${R10_GEN_DIR}/code_*.c")
list(LENGTH GAME_CODE OWNER_CHUNKS)
if(NOT OWNER_CHUNKS EQUAL 93)
  message(FATAL_ERROR "Expected 93 actual private game source chunks; got ${OWNER_CHUNKS}")
endif()

# SDL3 upstream dropped UWP support. The pinned SDL3-uwp fork uses
# C++/CX (/ZW) and MUST be prebuilt with MSVC cl.exe. ClangCL cannot parse
# the WinRT hat syntax. Both are WindowsStore x64 /MD, so link ABI matches.
# Never compile SDL3 directly with the native game-code Clang toolchain.
list(PREPEND CMAKE_PREFIX_PATH "${R10_SDL3_INSTALL}")
find_package(SDL3 CONFIG REQUIRED)
if(TARGET SDL3::SDL3-static)
  set(R10_SDL_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
  set(R10_SDL_TARGET SDL3::SDL3)
else()
  message(FATAL_ERROR "Pinned SDL3-uwp static library package missing")
endif()
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "No zlib test executables in UWP" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "No Win32 installer binaries" FORCE)
# Do NOT use desktop MinGW zlib from R8 or a precompiled Win32 DLL:
# the real core.cpp includes zlib.h and needs the AppContainer Store CRT.
add_subdirectory("${R10_ZLIB_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/zlib" EXCLUDE_FROM_ALL)
if(NOT TARGET zlibstatic)
  message(FATAL_ERROR "Pinned zlib must expose the AppContainer static target")
endif()
# The full native savestate implementation calls LZ4_compress_default and
# LZ4_decompress_safe. Compile the actual implementation with the SAME
# Windows Store/Clang CRT; no Win32 DLL, no empty stubs.
add_library(r10_lz4 STATIC "${R10_LZ4_ROOT}/lib/lz4.c")
target_include_directories(r10_lz4 PUBLIC "${R10_LZ4_ROOT}/lib")
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
add_subdirectory("${R10_GLSLANG_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/glslang" EXCLUDE_FROM_ALL)

set(CEMU_DIR "${NSMBU_ROOT}/vendor/cemu")
set(IMGUI_DIR "${NSMBU_ROOT}/vendor/imgui")
set(FMT_DIR "${NSMBU_ROOT}/vendor/fmt/include")
set(LUAJIT_INCLUDE "${NSMBU_ROOT}/vendor/luajit/src")
foreach(dir "${CEMU_DIR}" "${IMGUI_DIR}" "${FMT_DIR}" "${LUAJIT_INCLUDE}")
  if(NOT IS_DIRECTORY "${dir}")
    message(FATAL_ERROR "NSMBU submodules missing: ${dir}")
  endif()
endforeach()
add_library(r10_gamecode STATIC ${GAME_CODE}
  "${R10_GEN_DIR}/table.c" "${R10_GEN_DIR}/imports.c")
target_include_directories(r10_gamecode PUBLIC
  "${NSMBU_ROOT}/runtime/include" "${R10_GEN_DIR}")
target_compile_options(r10_gamecode PRIVATE /O2 /W0
  /clang:-ffp-contract=off /clang:-fno-strict-aliasing)
target_compile_definitions(r10_gamecode PRIVATE
  WINAPI_FAMILY=WINAPI_FAMILY_APP)

# Real Cemu Latte shader decompiler from the upstream full runtime.
file(GLOB_RECURSE CEMU_SOURCES CONFIGURE_DEPENDS "${CEMU_DIR}/*.cpp")
list(FILTER CEMU_SOURCES EXCLUDE REGEX "LatteDecompilerEmitMSL[^/]*\\.cpp$")
add_library(r10_cemu_latte STATIC ${CEMU_SOURCES})
target_include_directories(r10_cemu_latte PUBLIC
  "${CEMU_DIR}" "${CEMU_DIR}/Cafe" "${FMT_DIR}"
  "${NSMBU_ROOT}/vendor/metal-cpp")
target_compile_options(r10_cemu_latte PRIVATE
  "/FI${CEMU_DIR}/cemu_shim.h" /W0)
target_compile_definitions(r10_cemu_latte PUBLIC ENABLE_VULKAN=1)

add_library(r10_imgui STATIC
  "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp" "${IMGUI_DIR}/imgui_widgets.cpp")
target_include_directories(r10_imgui PUBLIC "${IMGUI_DIR}")
target_compile_definitions(r10_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(r10_imgui PRIVATE /W0)

# Match the complete R8 Windows-native source set. This intentionally
# excludes the synthetic headless_dispatch and custom R7D fake/runtime code.
file(GLOB RT CONFIGURE_DEPENDS
  "${NSMBU_ROOT}/runtime/src/*.cpp" "${NSMBU_ROOT}/runtime/src/*.c"
  "${NSMBU_ROOT}/runtime/src/agent/*.cpp"
  "${NSMBU_ROOT}/runtime/src/audio/*.cpp"
  "${NSMBU_ROOT}/runtime/src/core/*.cpp"
  "${NSMBU_ROOT}/runtime/src/core/*.c"
  "${NSMBU_ROOT}/runtime/src/crash/*.cpp"
  "${NSMBU_ROOT}/runtime/src/hle/*.cpp"
  "${NSMBU_ROOT}/runtime/src/hooks/*.cpp"
  "${NSMBU_ROOT}/runtime/src/input/*.cpp"
  "${NSMBU_ROOT}/runtime/src/input/motion/*.cpp"
  "${NSMBU_ROOT}/runtime/src/mods/*.cpp"
  "${NSMBU_ROOT}/runtime/src/mods/sdk/*.cpp"
  "${NSMBU_ROOT}/runtime/src/overlay/*.cpp"
  "${NSMBU_ROOT}/runtime/src/save/*.cpp"
  "${NSMBU_ROOT}/runtime/src/test/*.cpp"
  "${NSMBU_ROOT}/runtime/src/text/*.cpp")
foreach(exclusion "nsmbu_guest_stubs\\.c$"
  "mod_lua_test_stubs\\.cpp$" "headless_dispatch\\.c$"
  "/(xbox_guest_vm|rpx_inspect|rpx_stage|r5_startup_metadata|r5c_link_audit|r5d_relocation_audit)\\.cpp$")
  list(FILTER RT EXCLUDE REGEX "${exclusion}")
endforeach()
list(APPEND RT
  "${NSMBU_ROOT}/runtime/src/platform/perf_hint.cpp"
  "${NSMBU_ROOT}/runtime/src/platform/touch_pad.cpp"
  "${NSMBU_ROOT}/runtime/src/gfx/render_prof.cpp"
  "${NSMBU_ROOT}/runtime/src/gfx/screenshot.cpp")
file(GLOB GX2 CONFIGURE_DEPENDS "${NSMBU_ROOT}/runtime/src/gx2/*.cpp")
file(GLOB VK CONFIGURE_DEPENDS "${NSMBU_ROOT}/runtime/src/gfx/vulkan/*.cpp")
set(GFX
  "${NSMBU_ROOT}/runtime/src/gfx/renderer.cpp"
  "${NSMBU_ROOT}/runtime/src/gfx/display_modes.cpp"
  "${NSMBU_ROOT}/runtime/src/gfx/depth_peek.cpp"
  ${VK}
  "${NSMBU_ROOT}/runtime/src/platform/input_sdl.cpp"
  "${NSMBU_ROOT}/runtime/src/platform/mouse_sdl.cpp"
  "${NSMBU_ROOT}/runtime/src/platform/display_rate.cpp")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(GUEST_STUBS "${CMAKE_CURRENT_BINARY_DIR}/generated/nsmbu_guest_stubs.c")
add_custom_command(OUTPUT "${GUEST_STUBS}"
  COMMAND "${Python3_EXECUTABLE}"
    "${NSMBU_ROOT}/recomp/guest_stubs.py" "${GUEST_STUBS}"
  DEPENDS "${NSMBU_ROOT}/recomp/guest_stubs.py" VERBATIM)
set(BUILD_INFO "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp")
add_custom_command(OUTPUT "${BUILD_INFO}"
  COMMAND "${CMAKE_COMMAND}" "-DSRC=${NSMBU_ROOT}" "-DOUT=${BUILD_INFO}"
    -P "${NSMBU_ROOT}/cmake/BuildInfo.cmake"
  DEPENDS "${NSMBU_ROOT}/cmake/BuildInfo.cmake" VERBATIM)
add_library(r10_full_engine STATIC ${RT} ${GX2} ${GFX}
  "${GUEST_STUBS}" "${BUILD_INFO}")
target_include_directories(r10_full_engine PUBLIC
  "${NSMBU_ROOT}/runtime/include"
  "${NSMBU_ROOT}/runtime/src"
  "${NSMBU_ROOT}/runtime/src/gx2"
  "${NSMBU_ROOT}/vendor/xxHash"
  "${R10_VULKAN_HEADERS}/include"
  "${LUAJIT_INCLUDE}" "${R10_GLSLANG_ROOT}"
  "${IMGUI_DIR}")
target_compile_definitions(r10_full_engine PUBLIC
  NSMBU_HAS_VULKAN=1 NSMBU_SDL_HOST=1 NSMBU_XBOX_UWP=1 VK_NO_PROTOTYPES=1
  WINAPI_FAMILY=WINAPI_FAMILY_APP _WIN32_WINNT=0x0A00
  NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(r10_full_engine PRIVATE
  /clang:-ffp-contract=off)
set_source_files_properties(${GX2} ${GFX}
  PROPERTIES COMPILE_OPTIONS "/FI${CEMU_DIR}/cemu_shim.h")
target_link_libraries(r10_full_engine PUBLIC
  r10_gamecode r10_cemu_latte r10_imgui ${R10_SDL_TARGET} zlibstatic r10_lz4
  glslang::glslang glslang::SPIRV
  glslang::glslang-default-resource-limits)
add_custom_target(r10_compile_full_uwp ALL
  DEPENDS r10_full_engine r10_gamecode r10_cemu_latte r10_imgui zlibstatic r10_lz4)
message(STATUS "R10 compiling ${OWNER_CHUNKS} real game units + full Cafe OS/GX2/Vulkan under UWP")
message(STATUS "No fake HLE or fake gameplay; binary link+runtime remains next milestone")

# R11 separate owner-private executable link gate; unlike R10 static .lib,
# this forces all full-game dependencies to resolve as one UWP program.
# No Nintendo-derived sources or app binaries enter the public repository.
option(R11_LINK_FULL_GAME "Enable real full NSMBU UWP executable link" OFF)
if(R11_LINK_FULL_GAME)
  set(R11_LUAJIT_LIB "" CACHE FILEPATH "Actual pinned LuaJIT UWP static archive")
  if(NOT EXISTS "${R11_LUAJIT_LIB}")
    message(FATAL_ERROR "Real R11 UWP LuaJIT static archive is missing; no Lua stubs allowed")
  endif()
  add_library(r11_luajit_store STATIC IMPORTED)
  set_target_properties(r11_luajit_store PROPERTIES
    IMPORTED_LOCATION "${R11_LUAJIT_LIB}")
  add_executable(r11_full_game_uwp WIN32
    "${NSMBU_ROOT}/xbox/r11/R11WinRTEntry.cpp"
    "${NSMBU_ROOT}/xbox/r11/compiler_rt_udivti3.cpp")
  target_compile_features(r11_full_game_uwp PRIVATE cxx_std_20)
  target_compile_definitions(r11_full_game_uwp PRIVATE
    NSMBU_XBOX_UWP=1 NOMINMAX=1 _CRT_SECURE_NO_WARNINGS=1
    WINAPI_FAMILY=WINAPI_FAMILY_APP)
  target_include_directories(r11_full_game_uwp PRIVATE
    "${R10_SDL3_INSTALL}/include")
  # All 93 owner C chunks, full game main(), Cafe OS, GX2, Vulkan,
  # glslang, and SDL3 WinRT must resolve together. No fabricated HLE.
  target_link_libraries(r11_full_game_uwp PRIVATE
    r10_full_engine r10_gamecode r10_cemu_latte r10_imgui
    ${R10_SDL_TARGET} r11_luajit_store zlibstatic r10_lz4
    glslang::glslang glslang::SPIRV
    glslang::glslang-default-resource-limits
    windowsapp runtimeobject d3d12 dxgi ws2_32 bcrypt)
  set_target_properties(r11_full_game_uwp PROPERTIES
    OUTPUT_NAME "NSMBUFullRuntimeR11"
    LINK_FLAGS "/INCREMENTAL:NO /APPCONTAINER /SUBSYSTEM:WINDOWS")
  message(STATUS "R11 real AppContainer game executable link ENABLED")
  message(STATUS "R11 contains no synthetic gameplay or packaged assets")
endif()
