# cpp_log — iOS build glue (ffiPlugin).
#
# Compiles the shared native logging core from ../src into the cpp_log pod; the
# app reaches the C ABI over dart:ffi via DynamicLibrary.process() (see
# lib/src/cpp_log_io.dart _openLibrary).
#
# The host Podfile uses use_frameworks!, so this pod becomes cpp_log.framework,
# a dynamic framework loaded at launch; DynamicLibrary.process() resolves the
# exported cpp_log_* symbols from it.
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
  # ../src paths leave this pod as a framework with no implementation (and no
  # cpp_log_* symbols for dart:ffi). Keep tiny wrappers under ios/Classes that
  # include the shared sources; preserve_paths keeps those sources available.
  s.source_files = 'Classes/**/*.{h,c,cc}'
  s.public_header_files = 'Classes/cpp_log_apple.h'
  s.preserve_paths = '../src/**/*', '../include/**/*', '../third_party/spdlog/**/*'

  s.dependency 'Flutter'
  s.platform = :ios, '12.0'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    # See macos/cpp_log.podspec: harmless on non-Windows, kept for parity.
    'GCC_PREPROCESSOR_DEFINITIONS' =>
      '$(inherited) CPP_LOG_BUILDING_DLL=1 CPP_LOG_AVAILABLE=1',
    'HEADER_SEARCH_PATHS' =>
      '"${PODS_TARGET_SRCROOT}/../src" "${PODS_TARGET_SRCROOT}/../include" ' \
      '"${PODS_TARGET_SRCROOT}/../third_party/spdlog/include"',
    # Flutter FFI plugins are built for physical devices + arm64 simulators.
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
  }
  s.swift_version = '5.0'
end
