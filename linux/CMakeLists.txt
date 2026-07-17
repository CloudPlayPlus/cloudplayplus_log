# cpp_log — Linux build glue (ffiPlugin).
#
# Mirrors windows/CMakeLists.txt: compiles the shared native core from ../src
# into a `libcpp_log.so` that the Flutter tool bundles with the app, so
# `DynamicLibrary.open('libcpp_log.so')` resolves at runtime (see
# lib/cpp_log.dart _openLibrary).
#
# NOT verified on Linux yet — structural build glue only. The core sources are
# the same ones compiled on Windows; only the Linux/GCC/Clang build path is
# unvalidated.
cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME "cpp_log")
# C is enabled alongside CXX so the vendored Dart DL glue (a .c file) is compiled
# and its Dart_*_DL symbols are linked (same rationale as the Windows build).
project(${PROJECT_NAME} LANGUAGES CXX C)

# Shared native sources live one level up in ../src, shared with every platform.
set(CPP_LOG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_library(cpp_log SHARED
  "${CPP_LOG_SRC_DIR}/cpp_log_core.cc"
  "${CPP_LOG_SRC_DIR}/third_party/dart_dl/dart_api_dl.c"
)

set_target_properties(cpp_log PROPERTIES
  OUTPUT_NAME "cpp_log"
  # std::filesystem (FileSink rotation) requires C++17.
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  # Only the symbols decorated with the export macro
  # (__attribute__((visibility("default")))) should be exported.
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
)

target_include_directories(cpp_log PRIVATE "${CPP_LOG_SRC_DIR}")

# Parity with the Windows build. On non-Windows the export macro resolves to the
# visibility attribute regardless of this define, so it is a harmless no-op here.
target_compile_definitions(cpp_log PRIVATE CPP_LOG_BUILDING_DLL)

# The drain thread + std::filesystem need pthread and (on older GCC) stdc++fs.
find_package(Threads REQUIRED)
target_link_libraries(cpp_log PRIVATE Threads::Threads)
# GCC < 9 / Clang < 9 split std::filesystem into a separate library. Link it only
# where it exists so newer toolchains (which fold it into libstdc++) don't error.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(cpp_log PRIVATE stdc++fs)
endif()

# List of absolute paths to libraries bundled with the app so the Dart FFI open
# resolves at runtime (same contract as the Windows build file).
set(cpp_log_bundled_libraries
  $<TARGET_FILE:cpp_log>
  PARENT_SCOPE
)
