import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('macOS podspec compiles wrappers rooted inside the pod', () {
    final podspec = File('macos/cpp_log.podspec').readAsStringSync();

    expect(podspec, contains("s.source_files = 'Classes/**/*.{h,c,cc}'"));
    expect(podspec, contains('CPP_LOG_AVAILABLE=1'));
    expect(podspec, isNot(contains("'../src/cpp_log_core.cc'")));
    expect(
      podspec,
      isNot(contains("'../src/third_party/dart_dl/dart_api_dl.c'")),
    );
  });

  test('macOS wrappers include both shared native implementations', () {
    expect(
      File('macos/Classes/cpp_log_core_wrapper.cc').readAsStringSync(),
      contains('#include "../../src/cpp_log_core.cc"'),
    );
    expect(
      File('macos/Classes/dart_api_dl_wrapper.c').readAsStringSync(),
      contains('#include "../../src/third_party/dart_dl/dart_api_dl.c"'),
    );
    expect(
      File('macos/Classes/cpp_log_apple.h').readAsStringSync(),
      contains('#include "../../include/cpp_log/cpp_log_apple.h"'),
    );
  });
}
