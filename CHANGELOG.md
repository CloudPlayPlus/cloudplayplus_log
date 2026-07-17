## 0.0.1

* Initial local release: shared native logging core for CloudPlayPlus.
* C++ core (`src/cpp_log_core.{h,cc}`, `src/cpp_log_c.h`): `cpplog` namespace,
  `Level` (trace/debug/info/warn/error) and `Kind` (log/metric/event) enums,
  a pluggable `Sink` interface, an 8192-entry ring buffer with drop-oldest +
  loss counting, a batching drain thread (512 records / 30 ms), native-side
  timestamps, and an atomic minimum level (default `info`).
* Two sinks: `DartPortSink` (posts batches to a Dart isolate over a native port
  via `Dart_PostCObject`) and `FileSink` (append + size-based rotation +
  archive cap; defaults 2 MiB × 5).
* Stable C ABI: `cpp_log_init_dart_api`, `cpp_log_set_port`,
  `cpp_log_set_min_level`, `cpp_log_emit`, `cpp_log_use_file_sink`,
  `cpp_log_use_file_sink_ex`, `cpp_log_shutdown`.
* Dart FFI wrapper `CppLog` with kind-routed batch delivery.
* Log line format is byte-for-byte identical to the Dart side:
  `yyyy-MM-dd HH:mm:ss.mmm [LEVEL] [TAG] message`.
