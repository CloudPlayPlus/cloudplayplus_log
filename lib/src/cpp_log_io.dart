import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

import 'cpp_log_types.dart';

/// Dart FFI facade over the single process-wide native logging runtime.
class CppLog {
  CppLog._();

  static final CppLog instance = CppLog._();

  static const int _maxCachedTags = 128;

  bool _bindingsReady = false;
  bool _started = false;
  CppLogLevel _minLevel = CppLogLevel.info;
  ReceivePort? _port;
  void Function(String batch)? _onLogBatch;
  final Map<String, Pointer<Utf8>> _tagPointers = <String, Pointer<Utf8>>{};

  _InitializeDart? _initialize;
  _InitDart? _initDartApi;
  _SetPortDart? _setPort;
  _SetMinLevelDart? _setMinLevel;
  _EmitDart? _emit;
  _UseFileSinkDart? _useFileSink;
  _UseFileSinkExDart? _useFileSinkEx;
  _FlushDart? _flush;
  _DroppedCountDart? _droppedCount;
  _ShutdownDart? _shutdown;

  bool get available => _ensureBindings();
  bool get started => _started;
  bool get initialized => _started;
  int get droppedCount => _droppedCount?.call() ?? 0;

  /// Initializes the async runtime and its rotating file sink.
  ///
  /// Returns false instead of throwing when the native library or log file
  /// cannot be opened.
  bool initialize({
    required String filePath,
    CppLogLevel minLevel = CppLogLevel.info,
    int maxBytes = 5 * 1024 * 1024,
    int maxFiles = 3,
    int queueCapacity = 8192,
  }) {
    if (!_ensureBindings()) return false;
    final Pointer<Utf8> path = filePath.toNativeUtf8();
    try {
      _minLevel = minLevel;
      _setMinLevel!(minLevel.index);
      final int result = _initialize!(path, maxBytes, maxFiles, queueCapacity);
      _started = result == 0;
      return _started;
    } catch (_) {
      return false;
    } finally {
      malloc.free(path);
    }
  }

  /// Attaches an optional native-to-Dart live stream.
  ///
  /// File persistence stays native; this port is intended for an opt-in debug
  /// panel and is not part of the primary write path.
  Future<void> start({
    void Function(String batch)? onLogBatch,
    CppLogLevel minLevel = CppLogLevel.info,
  }) async {
    if (!_ensureBindings()) return;
    try {
      final int result = _initDartApi!(NativeApi.initializeApiDLData);
      if (result != 0) return;

      _onLogBatch = onLogBatch;
      _port?.close();
      final ReceivePort port = ReceivePort()..listen(_onMessage);
      _port = port;
      _minLevel = minLevel;
      _setMinLevel!(minLevel.index);
      _setPort!(port.sendPort.nativePort);
      _started = true;
    } catch (_) {
      _port?.close();
      _port = null;
    }
  }

  bool isEnabled(CppLogLevel level) {
    return _started && level.index >= _minLevel.index;
  }

  void setMinLevel(CppLogLevel level) {
    _minLevel = level;
    _setMinLevel?.call(level.index);
  }

  /// Copies one UTF-8 message into the native bounded queue and returns.
  void emit(CppLogLevel level, String tag, String message) {
    final _EmitDart? emit = _emit;
    if (emit == null || !isEnabled(level)) return;

    final _NativeTag nativeTag = _nativeTag(tag);
    final Pointer<Utf8> nativeMessage = message.toNativeUtf8();
    try {
      emit(level.index, nativeTag.pointer, nativeMessage);
    } finally {
      if (nativeTag.temporary) {
        malloc.free(nativeTag.pointer);
      }
      malloc.free(nativeMessage);
    }
  }

  void useFileSink(String path, {int? maxBytes, int? maxFiles}) {
    if (!_ensureBindings()) return;
    final Pointer<Utf8> nativePath = path.toNativeUtf8();
    try {
      if (maxBytes != null || maxFiles != null) {
        _useFileSinkEx!(
          nativePath,
          maxBytes ?? (5 * 1024 * 1024),
          maxFiles ?? 3,
        );
      } else {
        _useFileSink!(nativePath);
      }
      _started = true;
    } finally {
      malloc.free(nativePath);
    }
  }

  void flush() {
    try {
      _flush?.call();
    } catch (_) {}
  }

  void stop() {
    try {
      _setPort?.call(0);
    } catch (_) {}
    try {
      _shutdown?.call();
    } catch (_) {}
    _port?.close();
    _port = null;
    _onLogBatch = null;
    _started = false;
    for (final Pointer<Utf8> pointer in _tagPointers.values) {
      malloc.free(pointer);
    }
    _tagPointers.clear();
  }

  void _onMessage(Object? message) {
    if (message is String) {
      _onLogBatch?.call(message);
    }
  }

  _NativeTag _nativeTag(String tag) {
    final Pointer<Utf8>? cached = _tagPointers[tag];
    if (cached != null) {
      return _NativeTag(cached, false);
    }
    final Pointer<Utf8> pointer = tag.toNativeUtf8();
    if (_tagPointers.length < _maxCachedTags) {
      _tagPointers[tag] = pointer;
      return _NativeTag(pointer, false);
    }
    return _NativeTag(pointer, true);
  }

  bool _ensureBindings() {
    if (_bindingsReady) return true;
    final DynamicLibrary? library = _openLibrary();
    if (library == null) return false;
    try {
      _initialize = library.lookupFunction<_InitializeNative, _InitializeDart>(
        'cpp_log_initialize',
      );
      _initDartApi = library.lookupFunction<_InitNative, _InitDart>(
        'cpp_log_init_dart_api',
      );
      _setPort = library.lookupFunction<_SetPortNative, _SetPortDart>(
        'cpp_log_set_port',
      );
      _setMinLevel = library
          .lookupFunction<_SetMinLevelNative, _SetMinLevelDart>(
            'cpp_log_set_min_level',
          );
      _emit = library.lookupFunction<_EmitNative, _EmitDart>('cpp_log_emit');
      _useFileSink = library
          .lookupFunction<_UseFileSinkNative, _UseFileSinkDart>(
            'cpp_log_use_file_sink',
          );
      _useFileSinkEx = library
          .lookupFunction<_UseFileSinkExNative, _UseFileSinkExDart>(
            'cpp_log_use_file_sink_ex',
          );
      _flush = library.lookupFunction<_FlushNative, _FlushDart>(
        'cpp_log_flush',
      );
      _droppedCount = library
          .lookupFunction<_DroppedCountNative, _DroppedCountDart>(
            'cpp_log_dropped_count',
          );
      _shutdown = library.lookupFunction<_ShutdownNative, _ShutdownDart>(
        'cpp_log_shutdown',
      );
      _bindingsReady = true;
      return true;
    } catch (_) {
      return false;
    }
  }

  DynamicLibrary? _openLibrary() {
    try {
      if (Platform.isWindows) {
        return DynamicLibrary.open('cpp_log.dll');
      }
      if (Platform.isAndroid || Platform.isLinux) {
        return DynamicLibrary.open('libcpp_log.so');
      }
      if (Platform.isMacOS || Platform.isIOS) {
        return DynamicLibrary.process();
      }
    } catch (_) {
      return null;
    }
    return null;
  }
}

final class _NativeTag {
  const _NativeTag(this.pointer, this.temporary);

  final Pointer<Utf8> pointer;
  final bool temporary;
}

typedef _InitializeNative = Int32 Function(Pointer<Utf8>, Int64, Int32, Int32);
typedef _InitializeDart = int Function(Pointer<Utf8>, int, int, int);

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

typedef _FlushNative = Void Function();
typedef _FlushDart = void Function();

typedef _DroppedCountNative = Uint64 Function();
typedef _DroppedCountDart = int Function();

typedef _ShutdownNative = Void Function();
typedef _ShutdownDart = void Function();
