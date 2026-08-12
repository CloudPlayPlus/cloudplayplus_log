# cpp_log — macOS build glue (ffiPlugin).
#
# Compiles the shared native logging core from ../src into a static library that
# the Flutter tool links into the host app; the app then reaches the C ABI over
# dart:ffi via DynamicLibrary.process() (see lib/src/cpp_log_io.dart _openLibrary).
#
# The CocoaPods/Xcode path is exercised through the CloudPlayPlus macOS host
# build; the exported C ABI and rotating file sink are verified there.
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

  # CocoaPods drops source_files entries that escape the pod root, so direct
  # ../src paths turn this development pod into a target with no implementation.
  # Keep tiny wrappers under macos/Classes and include the shared sources from
  # there; preserve_paths keeps those shared sources and headers available.
  s.source_files = 'Classes/**/*.{h,c,cc}'
  s.public_header_files = 'Classes/cpp_log_apple.h'
  s.preserve_paths = '../src/**/*', '../include/**/*', '../third_party/spdlog/**/*'

  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.14'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    # Match the Windows target: export the C ABI symbols. On non-Windows the
    # export macro resolves to __attribute__((visibility("default"))), so this
    # define is harmless but kept for parity.
    'GCC_PREPROCESSOR_DEFINITIONS' =>
      '$(inherited) CPP_LOG_BUILDING_DLL=1 CPP_LOG_AVAILABLE=1',
    'HEADER_SEARCH_PATHS' =>
      '"${PODS_TARGET_SRCROOT}/../src" "${PODS_TARGET_SRCROOT}/../include" ' \
      '"${PODS_TARGET_SRCROOT}/../third_party/spdlog/include"',
  }
  s.swift_version = '5.0'
end
