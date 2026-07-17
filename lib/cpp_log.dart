/// cpp_log — Dart FFI surface for the native logging core.
///
/// Opens the plugin library, initializes the dynamically-linked Dart API with
/// [NativeApi.initializeApiDLData], and hands native a Dart *native port*. The
/// core's background drain thread posts batched, already-formatted, timestamped
/// log lines to that port via `Dart_PostCObject`; each arriving batch is routed
/// by [CppLogKind] and, for the log channel, forwarded to the caller-supplied
/// `onLogBatch` (wire this to the app's on-disk log sink) so native and Dart
/// logs share one ordered `app.log`.
///
/// Native-only processes (services, headless workers) can instead skip the port
/// and call [useFileSink] to have the core write a rotating file directly.
///
/// Line format is byte-for-byte identical to the Dart side:
///   `yyyy-MM-dd HH:mm:ss.mmm [LEVEL] [TAG] message`.
///
/// Windows is the validated path. Resolution for Android/Linux (`libcpp_log.so`)
/// and Apple (statically-linked symbols via `DynamicLibrary.process()`) is wired
/// in [_openLibrary] alongside the build glue under the platform folders, but is
/// NOT yet verified on those platforms — an unresolved library/symbol simply
/// leaves the bridge disabled (no-op), so shipping Windows is unaffected.
library;

import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

/// Severity, low → high. Index maps to the native ABI (0=trace .. 4=error).
enum CppLogLevel { trace, debug, info, warn, error }

/// Record channel. Index maps to the native ABI (0=log, 1=metric, 2=event).
/// Only [log] is emitted end-to-end today; the others are reserved so the
/// routing switch is future-proof.
enum CppLogKind { log, metric, event }

/// Native ↔ Dart logging bridge (singleton).
class CppLog {
  CppLog._();

  static final CppLog instance = CppLog._();

  bool _started = false;
  ReceivePort? _port;
  void Function(String batch)? _onLogBatch;

  _SetPortDart? _setPort;
  _SetMinLevelDart? _setMinLevel;
  _EmitDart? _emit;
  _UseFileSinkDart? _useFileSink;
  _UseFileSinkExDart? _useFileSinkEx;
  _ShutdownDart? _shutdown;

  /// True once the library is open, the Dart API is initialized, and a port is
  /// installed.
  bool get started => _started;

  /// Best-effort startup. Never throws: a missing library/symbol (e.g. an
  /// unsupported platform) simply leaves the bridge disabled.
  ///
  /// [onLogBatch] receives each batch of newline-joined log lines on the log
  /// channel — forward it to the app's file sink. [minLevel] sets the native
  /// minimum severity (default [CppLogLevel.info]).
  Future<void> start({
    void Function(String batch)? onLogBatch,
    CppLogLevel minLevel = CppLogLevel.info,
  }) async {
    if (_started) return;
    final DynamicLibrary? lib = _openLibrary();
    if (lib == null) return;
    try {
      final _InitDart init =
          lib.lookupFunction<_InitNative, _InitDart>('cpp_log_init_dart_api');
      _setPort =
          lib.lookupFunction<_SetPortNative, _SetPortDart>('cpp_log_set_port');
      _setMinLevel = lib.lookupFunction<_SetMinLevelNative, _SetMinLevelDart>(
        'cpp_log_set_min_level',
      );
      _emit = lib.lookupFunction<_EmitNative, _EmitDart>('cpp_log_emit');
      _useFileSink = lib.lookupFunction<_UseFileSinkNative, _UseFileSinkDart>(
        'cpp_log_use_file_sink',
      );
      _useFileSinkEx =
          lib.lookupFunction<_UseFileSinkExNative, _UseFileSinkExDart>(
        'cpp_log_use_file_sink_ex',
      );
      _shutdown =
          lib.lookupFunction<_ShutdownNative, _ShutdownDart>('cpp_log_shutdown');

      final int rc = init(NativeApi.initializeApiDLData);
      if (rc != 0) {
        _clearBindings();
        return;
      }

      _onLogBatch = onLogBatch;
      final ReceivePort rp = ReceivePort()..listen(_onMessage);
      _port = rp;
      _setMinLevel!(minLevel.index);
      _setPort!(rp.sendPort.nativePort);
      _started = true;
    } catch (_) {
      _clearBindings();
      _port?.close();
      _port = null;
    }
  }

  // Kind routing. Today the native DartPortSink posts the log channel as a bare
  // String (kept compatible with the app's `onBatch` contract). A future
  // metric/event channel would arrive as a `[kindIndex, payload]` pair; the
  // second branch handles that shape so no wire change is needed to add it.
  void _onMessage(dynamic msg) {
    if (msg is String) {
      _dispatch(CppLogKind.log, msg);
    } else if (msg is List &&
        msg.length == 2 &&
        msg[0] is int &&
        msg[1] is String) {
      final int i = msg[0] as int;
      final CppLogKind kind = (i >= 0 && i < CppLogKind.values.length)
          ? CppLogKind.values[i]
          : CppLogKind.log;
      _dispatch(kind, msg[1] as String);
    }
  }

  void _dispatch(CppLogKind kind, String batch) {
    switch (kind) {
      case CppLogKind.log:
        _onLogBatch?.call(batch);
      case CppLogKind.metric:
      case CppLogKind.event:
        // Reserved channels: drained natively, no Dart consumer yet.
        break;
    }
  }

  /// Updates the native minimum level at runtime.
  void setMinLevel(CppLogLevel level) => _setMinLevel?.call(level.index);

  /// Emits one native-origin record through the full pipeline. Handy for
  /// diagnostics / end-to-end verification of the native → Dart → sink path.
  void emit(CppLogLevel level, String tag, String message) {
    final _EmitDart? emit = _emit;
    if (emit == null) return;
    final Pointer<Utf8> tagPtr = tag.toNativeUtf8();
    final Pointer<Utf8> msgPtr = message.toNativeUtf8();
    try {
      emit(level.index, tagPtr, msgPtr);
    } finally {
      malloc.free(tagPtr);
      malloc.free(msgPtr);
    }
  }

  /// Switches the native active sink to a rotating file at [path]. When
  /// [maxBytes] or [maxFiles] is given, the parameterized variant is used
  /// (defaults otherwise: 2 MiB per file, 5 archives). Records posted to a Dart
  /// port stop once a file sink is installed (a single active sink at a time).
  void useFileSink(String path, {int? maxBytes, int? maxFiles}) {
    final Pointer<Utf8> p = path.toNativeUtf8();
    try {
      if (maxBytes != null || maxFiles != null) {
        final _UseFileSinkExDart? ex = _useFileSinkEx;
        if (ex == null) return;
        ex(p, maxBytes ?? (2 * 1024 * 1024), maxFiles ?? 5);
      } else {
        final _UseFileSinkDart? f = _useFileSink;
        if (f == null) return;
        f(p);
      }
    } finally {
      malloc.free(p);
    }
  }

  /// Detaches the port, flushes and stops the native drain thread, and closes
  /// the receive port.
  void stop() {
    try {
      _setPort?.call(0);
    } catch (_) {}
    try {
      _shutdown?.call();
    } catch (_) {}
    _port?.close();
    _port = null;
    _started = false;
  }

  void _clearBindings() {
    _setPort = null;
    _setMinLevel = null;
    _emit = null;
    _useFileSink = null;
    _useFileSinkEx = null;
    _shutdown = null;
  }

  /// Opens the native library for the current platform, or returns null when
  /// it can't be resolved (leaving the bridge disabled — never throws).
  ///
  /// - **Windows** (validated): loads the bundled `cpp_log.dll`.
  /// - **Android / Linux** (build glue present, NOT verified): loads
  ///   `libcpp_log.so` built by the NDK / Linux CMake.
  /// - **Apple / iOS / macOS** (build glue present, NOT verified): the core is
  ///   linked statically into the app via the podspecs, so its symbols live in
  ///   the running process — resolve them with `DynamicLibrary.process()`
  ///   rather than opening a standalone dylib.
  DynamicLibrary? _openLibrary() {
    try {
      if (Platform.isWindows) {
        return DynamicLibrary.open('cpp_log.dll');
      }
      if (Platform.isAndroid || Platform.isLinux) {
        // NOT verified on Android/Linux — see plugin android/ and linux/ glue.
        return DynamicLibrary.open('libcpp_log.so');
      }
      if (Platform.isMacOS || Platform.isIOS) {
        // NOT verified on Apple — statically linked, symbols in-process.
        return DynamicLibrary.process();
      }
    } catch (_) {
      // Unsupported platform or missing library: leave the bridge disabled.
    }
    return null;
  }
}

// ---------------------------------------------------------------------------
// FFI typedefs for the exported C symbols (see src/cpp_log_c.h).
// ---------------------------------------------------------------------------

typedef _InitNative = IntPtr Function(Pointer<Void>);
typedef _InitDart = int Function(Pointer<Void>);

typedef _SetPortNative = Void Function(Int64);
typedef _SetPortDart = void Function(int);

typedef _SetMinLevelNative = Void Function(Int32);
typedef _SetMinLevelDart = void Function(int);

typedef _EmitNative = Void Function(Int32, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmitDart = void Function(int, Pointer<Utf8>, Pointer<Utf8>);

typedef _UseFileSinkNative = Void Function(Pointer<Utf8>);
typedef _UseFileSinkDart = void Function(Pointer<Utf8>);

typedef _UseFileSinkExNative = Void Function(Pointer<Utf8>, Int64, Int32);
typedef _UseFileSinkExDart = void Function(Pointer<Utf8>, int, int);

typedef _ShutdownNative = Void Function();
typedef _ShutdownDart = void Function();
