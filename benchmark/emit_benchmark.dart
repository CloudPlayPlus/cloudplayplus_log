import 'dart:io';

import 'package:cpp_log/cpp_log.dart';

const int _messageCount = 100000;
const int _queueCapacity = 131072;
const String _message =
    'Remote frame decoded; stream=desktop-0 sequence=184467 latency_us=1200';

void main() {
  final Directory directory = Directory.systemTemp.createTempSync(
    'cpp_log_benchmark_',
  );
  final String logPath =
      '${directory.path}${Platform.pathSeparator}benchmark.log';

  try {
    final bool initialized = CppLog.instance.initialize(
      filePath: logPath,
      minLevel: CppLogLevel.info,
      maxBytes: 64 * 1024 * 1024,
      maxFiles: 1,
      queueCapacity: _queueCapacity,
    );
    if (!initialized) {
      stderr.writeln('Unable to initialize cpp_log.');
      exitCode = 1;
      return;
    }

    // Warm up FFI lookup, UTF-8 conversion, and the native producer path.
    for (var index = 0; index < 1000; index++) {
      CppLog.instance.emit(CppLogLevel.info, 'BENCHMARK', _message);
    }

    final Stopwatch stopwatch = Stopwatch()..start();
    for (var index = 0; index < _messageCount; index++) {
      CppLog.instance.emit(CppLogLevel.info, 'BENCHMARK', _message);
    }
    stopwatch.stop();

    final double nanosecondsPerMessage =
        stopwatch.elapsedMicroseconds * 1000 / _messageCount;
    final double messagesPerSecond =
        _messageCount * 1000000 / stopwatch.elapsedMicroseconds;
    final int dropped = CppLog.instance.droppedCount;

    stdout
      ..writeln('messages=$_messageCount')
      ..writeln('producer_elapsed_us=${stopwatch.elapsedMicroseconds}')
      ..writeln(
        'producer_ns_per_message=${nanosecondsPerMessage.toStringAsFixed(1)}',
      )
      ..writeln(
        'producer_messages_per_second=${messagesPerSecond.toStringAsFixed(0)}',
      )
      ..writeln('dropped=$dropped');
  } finally {
    CppLog.instance.stop();
    directory.deleteSync(recursive: true);
  }
}
