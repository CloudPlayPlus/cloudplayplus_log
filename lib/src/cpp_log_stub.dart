import 'cpp_log_types.dart';

/// No-op implementation used by Flutter Web.
class CppLog {
  CppLog._();

  static final CppLog instance = CppLog._();

  bool get available => false;
  bool get started => false;
  bool get initialized => false;
  int get droppedCount => 0;

  bool initialize({
    required String filePath,
    CppLogLevel minLevel = CppLogLevel.info,
    int maxBytes = 5 * 1024 * 1024,
    int maxFiles = 3,
    int queueCapacity = 8192,
  }) => false;

  Future<void> start({
    void Function(String batch)? onLogBatch,
    CppLogLevel minLevel = CppLogLevel.info,
  }) async {}

  bool isEnabled(CppLogLevel level) => false;
  void setMinLevel(CppLogLevel level) {}
  void emit(CppLogLevel level, String tag, String message) {}
  void useFileSink(String path, {int? maxBytes, int? maxFiles}) {}
  void flush() {}
  void stop() {}
}
