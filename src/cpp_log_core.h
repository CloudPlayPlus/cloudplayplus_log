// cpp_log — cross-plugin native logging core (C++ surface).
//
// Purpose: give C++ plugin code a structured, leveled log macro whose records
// are timestamped *at the call site*, batched on a background thread, and
// handed to a pluggable sink. The two shipped sinks are:
//   * DartPortSink — posts batches to a Dart isolate over a native port
//     (`Dart_PostCObject`), so the Dart layer can funnel them into the same
//     on-disk `app.log` as Dart-origin logs.
//   * FileSink     — writes batches straight to a rotating local file, for
//     native-only processes (services, headless workers) that have no Dart
//     isolate to post to.
//
// Why native ports (not a Flutter MethodChannel): channels must be invoked on
// the platform main thread, but plugin logs originate on arbitrary worker
// threads. `Dart_PostCObject` is documented as callable from *any* thread, so
// the log path never touches the UI thread.
//
// The core is deliberately self-contained (no other plugin deps) so it can be
// linked into any plugin or built as the standalone `cpp_log` shared library.
//
// On-disk / on-wire line format is byte-for-byte identical to the Dart side:
//   "yyyy-MM-dd HH:mm:ss.mmm [LEVEL] [TAG] message"
// with fixed-width labels [TRACE] [DEBUG] [INFO ] [WARN ] [ERROR].
#ifndef CPP_LOG_CORE_H_
#define CPP_LOG_CORE_H_

#include <cstdint>
#include <memory>
#include <string>

// Export/import decoration. The build defines CPP_LOG_BUILDING_DLL for the
// shared library; consumers that link the import library get dllimport. Callers
// that reach the library purely through `dart:ffi` / GetProcAddress ignore this.
// Native-only consumers (services, headless workers) that compile the core
// straight into their own binary define CPP_LOG_STATIC so the symbols carry no
// dll linkage at all.
#if defined(_WIN32)
#if defined(CPP_LOG_STATIC)
#define CPP_LOG_API
#elif defined(CPP_LOG_BUILDING_DLL)
#define CPP_LOG_API __declspec(dllexport)
#else
#define CPP_LOG_API __declspec(dllimport)
#endif
#else
#define CPP_LOG_API __attribute__((visibility("default")))
#endif

namespace cpplog {

// Severity, low → high. Records below the configured minimum are dropped before
// any formatting. Numeric values are part of the ABI (mirrored on the Dart side
// and in the C header) — do not renumber.
enum class Level : int {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

// Record channel. Only kLog is wired end-to-end today; kMetric / kEvent are
// reserved so producers and sinks can branch on channel without an ABI break.
enum class Kind : int {
  kLog = 0,
  kMetric = 1,
  kEvent = 2,
};

// Pluggable output. The drain thread hands each sink a newline-joined batch of
// already-formatted records. Implementations must be cheap to destroy; a sink
// is only ever touched from the drain thread (installation is serialized).
struct Sink {
  virtual ~Sink() = default;
  virtual void Write(const std::string& batch) = 0;
};

// Initializes the dynamically-linked Dart API from the pointer returned by
// Dart's `NativeApi.initializeApiDLData`. Required before a DartPortSink can
// post; a no-op for FileSink-only use. Returns 0 on success (matches
// `Dart_InitializeApiDL`). Also brings up the drain thread.
CPP_LOG_API intptr_t InitDartApi(void* dart_api_dl_data);

// Installs a DartPortSink posting batches to `port` (a Dart SendPort's
// `nativePort`), or detaches/removes the active sink when `port == 0`. Records
// enqueued before any sink is installed are retained in the ring buffer and
// flushed once one arrives (up to the buffer bound).
CPP_LOG_API void SetPort(int64_t port);

// Installs a rotating FileSink at `path`. The two-arg overload uses defaults
// (2 MiB per file, 5 archives) that match the Dart FileLogSink.
CPP_LOG_API void UseFileSink(const std::string& path);
CPP_LOG_API void UseFileSink(const std::string& path, int64_t max_bytes,
                             int max_files);

// Installs a caller-provided sink, taking ownership. Passing nullptr detaches.
CPP_LOG_API void SetSink(std::unique_ptr<Sink> sink);

// Minimum emitted level. Atomic; default kInfo.
CPP_LOG_API void SetMinLevel(Level level);

// Enqueues one record on the log channel. The timestamp is captured *here*, at
// the production moment — never on the sink side, which sees records only after
// async batching.
CPP_LOG_API void Log(Level level, const char* tag, const std::string& message);

// Enqueues one record on an explicit channel (currently only kLog is emitted;
// other kinds are accepted and reserved).
CPP_LOG_API void LogKind(Kind kind, Level level, const char* tag,
                         const std::string& message);

// printf-style convenience wrapper around Log().
CPP_LOG_API void Logf(Level level, const char* tag, const char* fmt, ...);

// Flushes pending records and stops the drain thread. Best-effort; mainly for
// clean shutdown and tests. Safe to call more than once.
CPP_LOG_API void Shutdown();

}  // namespace cpplog

// Compile-time gate: define CPP_LOG_ENABLED=0 to strip all CPPLOG sites.
#ifndef CPP_LOG_ENABLED
#define CPP_LOG_ENABLED 1
#endif

#if CPP_LOG_ENABLED
#define CPPLOG(level, tag, ...) ::cpplog::Logf((level), (tag), __VA_ARGS__)
#else
#define CPPLOG(level, tag, ...) ((void)0)
#endif

// Per-level shorthands.
#define CPPLOG_TRACE(tag, ...) CPPLOG(::cpplog::Level::kTrace, (tag), __VA_ARGS__)
#define CPPLOG_DEBUG(tag, ...) CPPLOG(::cpplog::Level::kDebug, (tag), __VA_ARGS__)
#define CPPLOG_INFO(tag, ...) CPPLOG(::cpplog::Level::kInfo, (tag), __VA_ARGS__)
#define CPPLOG_WARN(tag, ...) CPPLOG(::cpplog::Level::kWarn, (tag), __VA_ARGS__)
#define CPPLOG_ERROR(tag, ...) CPPLOG(::cpplog::Level::kError, (tag), __VA_ARGS__)

#endif  // CPP_LOG_CORE_H_
