# cpp_log — macOS build glue (ffiPlugin).
#
# Compiles the shared native logging core from ../src into a static library that
# the Flutter tool links into the host app; the app then reaches the C ABI over
# dart:ffi via DynamicLibrary.process() (see lib/src/cpp_log_io.dart _openLibrary).
#
# NOT verified on macOS yet — structural build glue only. The C ABI compiled
# here is the same one validated on Windows; only the CocoaPods/Xcode build path
# is unvalidated.
#
# Run `pod lib lint` from this directory to sanity-check the spec before relying
# on it.
Pod::Spec.new do |s|
  s.name             = 'cpp_log'
  s.version          = '0.1.0'
  s.summary          = 'Shared native logging core (FFI) for CloudPlayPlus.'
  s.description      = <<-DESC
Process-wide asynchronous spdlog runtime that funnels Dart and native records
into the same rotating app.log.
                       DESC
  s.homepage         = 'https://www.cloudplayplus.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'CloudPlayPlus' => 'dev@cloudplayplus.com' }
  s.source           = { :path => '.' }

  # Compile the shared core + the vendored Dart DL glue that lives one level up
  # in ../src (shared with the Windows/Linux/Android builds), plus expose the
  # Objective-C adapter header. Path-based (development) pods may reference files
  # outside the pod directory; preserve_paths keeps ../src and ../include around.
  s.source_files = [
    '../src/cpp_log_core.cc',
    '../src/third_party/dart_dl/dart_api_dl.c',
    '../include/cpp_log/cpp_log_apple.h',
  ]
  s.public_header_files = '../include/cpp_log/cpp_log_apple.h'
  s.preserve_paths = '../src/**/*', '../include/**/*', '../third_party/spdlog/**/*'

  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.14'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    # Match the Windows target: export the C ABI symbols. On non-Windows the
    # export macro resolves to __attribute__((visibility("default"))), so this
    # define is harmless but kept for parity.
    'GCC_PREPROCESSOR_DEFINITIONS' => 'CPP_LOG_BUILDING_DLL=1',
    'HEADER_SEARCH_PATHS' =>
      '"${PODS_TARGET_SRCROOT}/../src" "${PODS_TARGET_SRCROOT}/../include" ' \
      '"${PODS_TARGET_SRCROOT}/../third_party/spdlog/include"',
  }
  s.swift_version = '5.0'
end
