# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "cpp_log")
# NOTE: C is enabled (in addition to CXX) so the vendored Dart DL API glue
# `../src/third_party/dart_dl/dart_api_dl.c` is compiled and linked; with
# CXX-only, CMake treats .c files as non-compiled and the Dart_*_DL symbols go
# missing at link time.
project(${PROJECT_NAME} LANGUAGES CXX C)

# Shared native sources live one level up in `src/` so the same core can be
# compiled by other platforms' build files later.
set(CPP_LOG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# The native library target. Its OUTPUT_NAME is what the Dart side opens
# (`cpp_log.dll`), and its name must match the entry in
# `cpp_log_bundled_libraries` below.
add_library(cpp_log SHARED
  "${CPP_LOG_SRC_DIR}/cpp_log_core.cc"
  "${CPP_LOG_SRC_DIR}/third_party/dart_dl/dart_api_dl.c"
)

set_target_properties(cpp_log PROPERTIES
  OUTPUT_NAME "cpp_log"
  # std::filesystem (FileSink rotation) requires C++17.
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

target_include_directories(cpp_log PRIVATE "${CPP_LOG_SRC_DIR}")

# Export the C ABI + core symbols with __declspec(dllexport). Consumers that
# link the import library see dllimport; `dart:ffi` lookups ignore both.
target_compile_definitions(cpp_log PRIVATE CPP_LOG_BUILDING_DLL)

# The vendored Dart DL glue is third-party SDK code; don't hold it to our
# warning bar (it trips C4152 function/data pointer conversion by design). `/w`
# silences all warnings for just that translation unit.
set_source_files_properties(
  "${CPP_LOG_SRC_DIR}/third_party/dart_dl/dart_api_dl.c"
  PROPERTIES COMPILE_FLAGS "/w"
)

# List of absolute paths to libraries that should be bundled with the app so
# `DynamicLibrary.open('cpp_log.dll')` resolves at runtime.
set(cpp_log_bundled_libraries
  $<TARGET_FILE:cpp_log>
  PARENT_SCOPE
)
