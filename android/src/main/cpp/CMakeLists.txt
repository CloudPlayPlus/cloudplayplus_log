# cpp_log — Android NDK build (invoked by android/build.gradle externalNativeBuild).
#
# Compiles the shared native core from ../../../../src plus the JNI shim into
# libcpp_log.so. NOT verified on Android — structural build glue only.
cmake_minimum_required(VERSION 3.10)

project(cpp_log LANGUAGES CXX C)

# From android/src/main/cpp back up to the plugin root, then into the shared src.
set(CPP_LOG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src")
set(CPP_LOG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include")
set(SPDLOG_INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/spdlog/include")

add_library(cpp_log SHARED
  "${CPP_LOG_SRC_DIR}/cpp_log_core.cc"
  "${CPP_LOG_SRC_DIR}/third_party/dart_dl/dart_api_dl.c"
  "cpp_log_jni.cc"
)

set_target_properties(cpp_log PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

# The JNI shim (cpp_log_jni.cc) and the core both include headers from ../src.
target_include_directories(cpp_log
  PUBLIC "${CPP_LOG_INCLUDE_DIR}"
  PRIVATE
    "${CPP_LOG_SRC_DIR}"
    "${SPDLOG_INCLUDE_DIR}"
)

# Parity with the other platforms; on non-Windows the export macro is the
# visibility attribute regardless, so this define is a harmless no-op here.
target_compile_definitions(cpp_log PRIVATE CPP_LOG_BUILDING_DLL)

# Android system log lib (linked for completeness; the core itself does not use
# __android_log, but the JNI shim/host may want it).
find_library(android-log-lib log)
target_link_libraries(cpp_log PRIVATE ${android-log-lib})
