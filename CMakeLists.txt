cmake_minimum_required(VERSION 3.20)
project(endgame VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(ENDGAME_ARCH "native" CACHE STRING
    "Target architecture: native, armv8-neon, x86-64-v2, avx2, bmi2, avx512")
set_property(CACHE ENDGAME_ARCH PROPERTY STRINGS
    native armv8-neon x86-64-v2 avx2 bmi2 avx512)
option(ENDGAME_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
option(ENDGAME_LTO "Enable link-time optimisation in release builds" ON)
set(ENDGAME_PGO "off" CACHE STRING "Profile-guided optimisation: off, generate, use")
set(ENDGAME_PGO_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH "Directory for PGO profiles")
option(ENDGAME_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# Architecture flags
# ---------------------------------------------------------------------------
set(ENDGAME_ARCH_FLAGS "")
set(ENDGAME_ARCH_DEFS "")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _proc)
if(_proc MATCHES "arm64|aarch64")
  set(ENDGAME_IS_ARM ON)
else()
  set(ENDGAME_IS_ARM OFF)
endif()

if(ENDGAME_ARCH STREQUAL "native")
  if(ENDGAME_IS_ARM)
    list(APPEND ENDGAME_ARCH_FLAGS -mcpu=native)
  else()
    list(APPEND ENDGAME_ARCH_FLAGS -march=native)
  endif()
elseif(ENDGAME_ARCH STREQUAL "armv8-neon")
  if(APPLE)
    list(APPEND ENDGAME_ARCH_FLAGS -mcpu=apple-m1)
  else()
    list(APPEND ENDGAME_ARCH_FLAGS -march=armv8-a)
  endif()
elseif(ENDGAME_ARCH STREQUAL "x86-64-v2")
  list(APPEND ENDGAME_ARCH_FLAGS -march=x86-64-v2)
elseif(ENDGAME_ARCH STREQUAL "avx2")
  list(APPEND ENDGAME_ARCH_FLAGS -march=x86-64-v3)
elseif(ENDGAME_ARCH STREQUAL "bmi2")
  # avx2 + use PEXT for slider attacks (only fast on Intel Haswell+ / Zen 3+)
  list(APPEND ENDGAME_ARCH_FLAGS -march=x86-64-v3)
  list(APPEND ENDGAME_ARCH_DEFS USE_PEXT)
elseif(ENDGAME_ARCH STREQUAL "avx512")
  list(APPEND ENDGAME_ARCH_FLAGS -march=x86-64-v4)
else()
  message(FATAL_ERROR "Unknown ENDGAME_ARCH '${ENDGAME_ARCH}'")
endif()
message(STATUS "endgame: arch=${ENDGAME_ARCH} flags=${ENDGAME_ARCH_FLAGS}")

# ---------------------------------------------------------------------------
# Common compile settings (applied through an interface target)
# ---------------------------------------------------------------------------
add_library(endgame_flags INTERFACE)
target_compile_options(endgame_flags INTERFACE ${ENDGAME_ARCH_FLAGS} -Wall -Wextra)
target_compile_definitions(endgame_flags INTERFACE ${ENDGAME_ARCH_DEFS})
target_compile_options(endgame_flags INTERFACE
    $<$<CONFIG:Debug>:-Werror -O1 -g>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O3 -g>)
target_compile_definitions(endgame_flags INTERFACE $<$<CONFIG:Release>:NDEBUG>)

if(WIN32)
  target_compile_definitions(endgame_flags INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

if(ENDGAME_SANITIZE)
  target_compile_options(endgame_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(endgame_flags INTERFACE -fsanitize=address,undefined)
endif()

if(ENDGAME_PGO STREQUAL "generate")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(endgame_flags INTERFACE -fprofile-instr-generate=${ENDGAME_PGO_DIR}/endgame-%p.profraw)
    target_link_options(endgame_flags INTERFACE -fprofile-instr-generate)
  else()
    target_compile_options(endgame_flags INTERFACE -fprofile-generate -fprofile-dir=${ENDGAME_PGO_DIR})
    target_link_options(endgame_flags INTERFACE -fprofile-generate)
  endif()
elseif(ENDGAME_PGO STREQUAL "use")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(endgame_flags INTERFACE -fprofile-instr-use=${ENDGAME_PGO_DIR}/endgame.profdata -Wno-profile-instr-unprofiled)
  else()
    target_compile_options(endgame_flags INTERFACE -fprofile-use -fprofile-dir=${ENDGAME_PGO_DIR} -fprofile-partial-training)
  endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(endgame_flags INTERFACE Threads::Threads)

if(ENDGAME_LTO AND NOT ENDGAME_SANITIZE)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg LANGUAGES CXX)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
endif()

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ENDGAME_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ENDGAME_CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_library(endgame_core STATIC ${ENDGAME_CORE_SOURCES})
target_include_directories(endgame_core PUBLIC src)
target_link_libraries(endgame_core PUBLIC endgame_flags)

add_executable(endgame src/main.cpp)
target_link_libraries(endgame PRIVATE endgame_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ENDGAME_TESTS)
  enable_testing()
  file(GLOB ENDGAME_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(endgame_tests ${ENDGAME_TEST_SOURCES})
  target_include_directories(endgame_tests SYSTEM PRIVATE tests/third_party)
  target_link_libraries(endgame_tests PRIVATE endgame_core)
  target_compile_definitions(endgame_tests PRIVATE
      ENDGAME_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests"
      DOCTEST_CONFIG_USE_STD_HEADERS)
  add_test(NAME unit COMMAND endgame_tests)
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
option(ENDGAME_TOOLS "Build developer tools (Texel tuner)" ON)
if(ENDGAME_TOOLS)
  add_executable(texel tools/texel/texel.cpp)
  target_link_libraries(texel PRIVATE endgame_core)
endif()
