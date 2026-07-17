// Verifies the native C++ FileSink rotation end-to-end, over the real exported
// C ABI, using the freshly built cpp_log.dll.
//
// This mirrors the approach of the app's
// test/core/utils/logging/file_log_sink_test.dart (tiny size cap → force many
// rollovers → assert archives are bounded), but exercises the *native* sink
// rather than the Dart one, since rotation for native-origin logs lives in C++.
//
// The test is an integration-style unit test: it drives the sink through FFI.
// If the DLL has not been built yet (e.g. `flutter test` run before
// `flutter build windows`), or cannot be loaded in this environment, the test
// skips rather than failing — keeping `flutter test` green without a build.
@TestOn('vm')
library;

import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';

typedef _SetMinLevelNative = Void Function(Int32);
typedef _SetMinLevelDart = void Function(int);

typedef _EmitNative = Void Function(Int32, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmitDart = void Function(int, Pointer<Utf8>, Pointer<Utf8>);

typedef _UseFileSinkExNative = Void Function(Pointer<Utf8>, Int64, Int32);
typedef _UseFileSinkExDart = void Function(Pointer<Utf8>, int, int);

typedef _ShutdownNative = Void Function();
typedef _ShutdownDart = void Function();

/// Locates the freshly built plugin DLL under the example's build output.
String? _findDll() {
  if (!Platform.isWindows) return null;
  const candidates = <String>[
    'example/build/windows/x64/runner/Debug/cpp_log.dll',
    'example/build/windows/x64/runner/Release/cpp_log.dll',
    'example/build/windows/x64/plugins/cpp_log/Debug/cpp_log.dll',
    'example/build/windows/x64/plugins/cpp_log/Release/cpp_log.dll',
  ];
  for (final c in candidates) {
    final f = File(c);
    if (f.existsSync()) return f.absolute.path;
  }
  // Fallback: walk the build tree for any cpp_log.dll.
  final buildDir = Directory('example/build');
  if (buildDir.existsSync()) {
    for (final e in buildDir.listSync(recursive: true, followLinks: false)) {
      if (e is File && e.path.replaceAll('\\', '/').endsWith('/cpp_log.dll')) {
        return e.absolute.path;
      }
    }
  }
  return null;
}

Future<void> _waitUntil(bool Function() cond, Duration timeout) async {
  final deadline = DateTime.now().add(timeout);
  while (!cond() && DateTime.now().isBefore(deadline)) {
    await Future<void>.delayed(const Duration(milliseconds: 50));
  }
}

void main() {
  test('native FileSink rotates by size and caps archives', () async {
    final dllPath = _findDll();
    if (dllPath == null) {
      markTestSkipped(
        'cpp_log.dll not found — build it first: '
        'cd example && flutter build windows --debug',
      );
      return;
    }

    final DynamicLibrary lib;
    try {
      lib = DynamicLibrary.open(dllPath);
    } catch (e) {
      markTestSkipped('cpp_log.dll present but could not be loaded here ($e)');
      return;
    }

    final setMinLevel = lib
        .lookupFunction<_SetMinLevelNative, _SetMinLevelDart>(
      'cpp_log_set_min_level',
    );
    final emit =
        lib.lookupFunction<_EmitNative, _EmitDart>('cpp_log_emit');
    final useFileSinkEx = lib
        .lookupFunction<_UseFileSinkExNative, _UseFileSinkExDart>(
      'cpp_log_use_file_sink_ex',
    );
    final shutdown =
        lib.lookupFunction<_ShutdownNative, _ShutdownDart>('cpp_log_shutdown');

    final tempDir = Directory.systemTemp.createTempSync('cpp_log_native_');
    final appLog = '${tempDir.path}${Platform.pathSeparator}app.log';
    String rotated(int i) => '$appLog.$i';

    final tagPtr = 'TEST'.toNativeUtf8();
    try {
      setMinLevel(0); // trace and up — emit everything

      final pathPtr = appLog.toNativeUtf8();
      // Tiny 256-byte cap forces many rollovers; cap archives at 3.
      useFileSinkEx(pathPtr, 256, 3);
      malloc.free(pathPtr);

      // ~55 bytes/line * 200 lines ≈ 11 KB → dozens of rotations.
      final payload = 'x' * 40;
      for (var i = 0; i < 200; i++) {
        final msgPtr = 'line $i $payload'.toNativeUtf8();
        emit(2, tagPtr, msgPtr); // info
        malloc.free(msgPtr);
      }

      // Stop the drain thread: flushes all pending records and closes the file.
      shutdown();
      await _waitUntil(
        () => File(appLog).existsSync() && File(rotated(1)).existsSync(),
        const Duration(seconds: 5),
      );

      // Active file always exists.
      expect(File(appLog).existsSync(), isTrue, reason: 'active app.log exists');
      // After many rollovers at least one archive exists...
      expect(
        File(rotated(1)).existsSync(),
        isTrue,
        reason: 'app.log.1 exists after rotations',
      );
      // ...but archives are bounded to maxFiles=3, so app.log.4 must not.
      expect(
        File(rotated(4)).existsSync(),
        isFalse,
        reason: 'archives capped at maxFiles=3',
      );
      // The most recent line landed in the active file.
      expect(File(appLog).readAsStringSync(), contains('line 199'));
      // Every persisted line keeps the shared format: "<ts> [INFO ] [TEST] ...".
      expect(
        File(appLog).readAsStringSync(),
        matches(RegExp(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[INFO \] \[TEST\] ')),
      );
    } finally {
      malloc.free(tagPtr);
      if (tempDir.existsSync()) {
        try {
          tempDir.deleteSync(recursive: true);
        } catch (_) {}
      }
    }
  });
}
