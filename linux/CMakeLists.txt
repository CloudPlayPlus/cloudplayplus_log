# cpp_log — Linux build glue (ffiPlugin).
#
# Mirrors windows/CMakeLists.txt: compiles the shared native core from ../src
# into a `libcpp_log.so` that the Flutter tool bundles with the app, so
# `DynamicLibrary.open('libcpp_log.so')` resolves at runtime (see
# lib/src/cpp_log_io.dart _openLibrary).
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
set(CPP_LOG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")
set(SPDLOG_INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/spdlog/include")

add_library(cpp_log SHARED
  "${CPP_LOG_SRC_DIR}/cpp_log_core.cc"
  "${CPP_LOG_SRC_DIR}/third_party/dart_dl/dart_api_dl.c"
)

set_target_properties(cpp_log PROPERTIES
  OUTPUT_NAME "cpp_log"
  # The shared core uses C++17 facilities such as std::clamp.
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  # Only the symbols decorated with the export macro
  # (__attribute__((visibility("default")))) should be exported.
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
)

target_include_directories(cpp_log
  PUBLIC "${CPP_LOG_INCLUDE_DIR}"
  PRIVATE
    "${CPP_LOG_SRC_DIR}"
    "${SPDLOG_INCLUDE_DIR}"
)

# Parity with the Windows build. On non-Windows the export macro resolves to the
# visibility attribute regardless of this define, so it is a harmless no-op here.
target_compile_definitions(cpp_log PRIVATE CPP_LOG_BUILDING_DLL)

# The asynchronous worker requires the platform thread library.
find_package(Threads REQUIRED)
target_link_libraries(cpp_log PRIVATE Threads::Threads)

# List of absolute paths to libraries bundled with the app so the Dart FFI open
# resolves at runtime (same contract as the Windows build file).
set(cpp_log_bundled_libraries
  $<TARGET_FILE:cpp_log>
  PARENT_SCOPE
)
