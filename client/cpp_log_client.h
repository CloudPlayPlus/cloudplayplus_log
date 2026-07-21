// Shared cpp_log *client* adapter.
//
// Any native module running inside the Flutter process (a plugin DLL, etc.)
// that wants its C++ diagnostics in the shared on-disk app.log includes THIS
// header and adds `cpp_log_client.cc` to its build. It does NOT reimplement
// logging and does NOT link against cpp_log: the C entrypoint `cpp_log_emit` is
// resolved at runtime with GetProcAddress against the `cpp_log.dll` that Flutter
// already bundles next to every plugin DLL. That keeps each consumer fully
// decoupled at link time (no import lib, no build-order coupling) while sharing
// one native logging channel at runtime.
//
// This is the single, shared implementation — consumers reference it, they do
// not copy it. The macro surface mirrors cpp_log_core.h's CPPLOG / CPPLOG_* so a
// call site reads identically whether the module compiles the core directly
// (e.g. the standalone desktop_service) or bridges via this client (in-process
// plugins). The on-disk line format is owned by cpp_log and byte-for-byte
// identical to the Dart side: "yyyy-MM-dd HH:mm:ss.mmm [LEVEL] [pid] [TAG] msg".
#ifndef CPP_LOG_CLIENT_H_
#define CPP_LOG_CLIENT_H_

namespace cpplog_client {

// Mirrors cpplog::Level. The numeric values ARE the cpp_log ABI
// (0=trace .. 4=error) and are passed straight through to cpp_log_emit; do not
// renumber.
enum class Level : int {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

// Formats (printf-style) and forwards one record to cpp_log. The native sink is
// resolved lazily on first use and cached; a no-op if cpp_log.dll is absent.
// Safe to call from any thread and at any time — records emitted before the Dart
// side installs its port sink are retained in cpp_log's ring buffer and flushed
// once the sink comes up.
void Emit(Level level, const char* tag, const char* fmt, ...);

}  // namespace cpplog_client

// Compile-time gate, mirroring cpp_log_core.h. Define CPP_LOG_CLIENT_ENABLED=0 to
// strip all CPPLOG sites in the including module.
#ifndef CPP_LOG_CLIENT_ENABLED
#define CPP_LOG_CLIENT_ENABLED 1
#endif

#if CPP_LOG_CLIENT_ENABLED
#define CPPLOG(level, tag, ...) \
  ::cpplog_client::Emit((level), (tag), __VA_ARGS__)
#else
#define CPPLOG(level, tag, ...) ((void)0)
#endif

// Per-level shorthands (match cpp_log_core.h).
#define CPPLOG_TRACE(tag, ...) \
  CPPLOG(::cpplog_client::Level::kTrace, (tag), __VA_ARGS__)
#define CPPLOG_DEBUG(tag, ...) \
  CPPLOG(::cpplog_client::Level::kDebug, (tag), __VA_ARGS__)
#define CPPLOG_INFO(tag, ...) \
  CPPLOG(::cpplog_client::Level::kInfo, (tag), __VA_ARGS__)
#define CPPLOG_WARN(tag, ...) \
  CPPLOG(::cpplog_client::Level::kWarn, (tag), __VA_ARGS__)
#define CPPLOG_ERROR(tag, ...) \
  CPPLOG(::cpplog_client::Level::kError, (tag), __VA_ARGS__)

#endif  // CPP_LOG_CLIENT_H_
