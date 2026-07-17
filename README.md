# cpp_log

Shared **native logging core** for CloudPlayPlus, packaged as a local Flutter
FFI plugin. It gives C++ plugin code (and native-only processes) a structured,
leveled, batched logger whose records land in the **same `app.log`** as
Dart-origin logs — byte-for-byte identical format, one ordered file.

This is the reusable core behind the unified-logging plan. It is intentionally
self-contained (no other plugin deps) so any plugin can link it, or the app can
open the standalone `cpp_log.dll`.

## Log line format

Identical to the Dart side (`lib/core/utils/vlog.dart`):

```
yyyy-MM-dd HH:mm:ss.mmm [LEVEL] [TAG] message
```

Fixed-width labels: `[TRACE] [DEBUG] [INFO ] [WARN ] [ERROR]`. Timestamps are
captured natively **at the production moment**, never on the sink side.

## Architecture

```
CPPLOG / cpp_log_emit ─► ring buffer (8192, drop-oldest + loss count)
                              │
                     drain thread (batch 512 / 30 ms)
                              │
                        Sink::Write(batch)
                          ┌───┴────────────┐
                   DartPortSink        FileSink
              (Dart_PostCObject)   (rotating app.log)
```

* **Ring buffer** — 8192 records; on overrun the *oldest* is dropped (the
  producer never blocks) and the loss is counted and surfaced as a `[WARN ]`
  record on the next flush.
* **Drain thread** — coalesces up to 512 records or 30 ms into one sink write.
* **Pluggable `Sink`** — `struct Sink { virtual void Write(const std::string&) = 0; };`.
  The drain thread is sink-agnostic (no hard-coded transport).
* **`Kind` field** — every record carries a channel (`log` / `metric` / `event`).
  Only `log` is emitted today; `metric`/`event` are reserved so the routing
  switch (native and Dart) can grow without an ABI break.
* **`SetMinLevel`** — atomic, default `info`.

### Shipped sinks

* **`DartPortSink`** — holds a Dart native port and posts each batch as a string
  via `Dart_PostCObject` (works from any thread). The Dart side funnels batches
  into the app's on-disk sink.
* **`FileSink`** — append + size-based rotation + archive cap, parameterized by
  path / bytes-per-file / archive count (defaults 2 MiB × 5, matching the Dart
  `FileLogSink`). Layout: `app.log` active, `app.log.1` (newest) … `app.log.N`
  (oldest). For native-only processes with no Dart isolate.

## Public API

### C ABI — `src/cpp_log_c.h` (`extern "C"`, for Dart FFI / ObjC / Swift / C)

| Symbol | Purpose |
| --- | --- |
| `cpp_log_init_dart_api(void*)` | Init the DL Dart API (`NativeApi.initializeApiDLData`); returns 0 on success. |
| `cpp_log_set_port(int64_t)` | Install a `DartPortSink` (0 detaches). |
| `cpp_log_set_min_level(int32_t)` | 0=trace … 4=error. |
| `cpp_log_emit(int32_t, const char* tag, const char* msg)` | Emit one record. |
| `cpp_log_use_file_sink(const char* path)` | Switch to a rotating `FileSink` (defaults). |
| `cpp_log_use_file_sink_ex(const char* path, int64_t maxBytes, int32_t maxFiles)` | …with explicit rotation bounds. |
| `cpp_log_shutdown(void)` | Flush, stop the drain thread, close the sink. |

### C++ — `src/cpp_log_core.h`

`namespace cpplog` with `Level`, `Kind`, `Sink`, the control functions, and the
`CPPLOG(level, tag, fmt, ...)` macro (plus `CPPLOG_TRACE/DEBUG/INFO/WARN/ERROR`).
Compile-time gate: define `CPP_LOG_ENABLED=0` to strip all call sites.

### Dart — `lib/cpp_log.dart`

```dart
// In the app (native → Dart → app.log):
await CppLog.instance.start(
  minLevel: CppLogLevel.info,
  onLogBatch: FileLogSink.instance.add, // funnel into the shared app.log
);
CppLog.instance.emit(CppLogLevel.info, 'NATIVE', 'hello from C++');

// In a native-only process (write a rotating file directly):
CppLog.instance.useFileSink(r'C:\...\logs\app.log', maxBytes: 2 << 20, maxFiles: 5);
```

Incoming batches are routed by `CppLogKind`; only the `log` branch is wired
today.

## Layout

```
src/cpp_log_core.{h,cc}          # C++ core + CPPLOG macro
src/cpp_log_c.h                  # stable C ABI
src/third_party/dart_dl/         # vendored Dart SDK DL glue (6 files, BSD)
windows/CMakeLists.txt           # builds the cpp_log target from ../src
lib/cpp_log.dart                 # Dart FFI wrapper
test/cpp_log_file_sink_test.dart # native FileSink rotation test (via FFI)
example/                         # minimal app; also the Windows build harness
```

## Build notes (Windows)

`windows/CMakeLists.txt` builds the `cpp_log` shared library **directly** from
`../src` (rather than delegating to a `src/CMakeLists.txt`), keeping the native
sources in one reusable place:

* `project(... LANGUAGES CXX C)` — C is required so the vendored
  `dart_api_dl.c` is compiled and the `Dart_*_DL` symbols link.
* `dart_api_dl.c` is compiled with `/w` (third-party SDK code, exempt from our
  warning bar).
* `CPP_LOG_BUILDING_DLL` drives `__declspec(dllexport)` on the exported symbols.
* C++17 is required for `std::filesystem` (FileSink rotation).
* The output `cpp_log.dll` is bundled next to the app so
  `DynamicLibrary.open('cpp_log.dll')` resolves.

## Verify

```bash
flutter pub get
flutter analyze lib test
cd example && flutter build windows --debug   # compiles + links the native core
cd .. && flutter test                         # native FileSink rotation (FFI)
```

The rotation test drives the real `cpp_log.dll` through the C ABI; it skips
gracefully if the DLL has not been built yet.
