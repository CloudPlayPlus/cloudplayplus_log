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

#include <ostream>  // std::ostream manipulators (std::endl, std::hex, ...)
#include <sstream>  // std::ostringstream backing the CPPLOG_STREAM_* adapter
#include <string>

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

// Stream-style adapter over Emit(), for the classic `LOG(INFO) << ...` idiom
// (glog / Abseil). Accumulates operands into an ostringstream and flushes
// exactly one record on destruction — i.e. at the end of the full expression
// (the `;`), because the macro yields a temporary. Prefer this over the
// printf-style CPPLOG_* when migrating existing std::cout / std::cerr code or
// when a call site reads more naturally as a stream: every operand formats via
// its own operator<< exactly as it did with the stream, so there is no printf
// conversion specifier to pick (and get wrong — MSVC does not verify them).
//
// The accumulated text is passed to Emit as a single "%s" argument, so a stray
// '%' in the message is never interpreted as a conversion. A trailing newline
// (from a leftover `<< std::endl` or `<< "\n"`) is trimmed, since cpp_log adds
// its own line separator. These are cold-path diagnostics; the per-call
// ostringstream allocation is irrelevant here (keep hot, per-frame logging on
// the printf-style CPPLOG_* macros).
class LogStream {
 public:
  LogStream(Level level, const char* tag) : level_(level), tag_(tag) {}

  ~LogStream() {
    std::string s = oss_.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
      s.pop_back();
    }
    if (!s.empty()) {
      Emit(level_, tag_, "%s", s.c_str());
    }
  }

  LogStream(const LogStream&) = delete;
  LogStream& operator=(const LogStream&) = delete;

  // Values: forward to the backing stream.
  template <typename T>
  LogStream& operator<<(const T& value) {
    oss_ << value;
    return *this;
  }

  // Stream manipulators (std::endl, std::flush, std::hex, std::setw, ...).
  LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
    oss_ << manip;
    return *this;
  }
  LogStream& operator<<(std::ios_base& (*manip)(std::ios_base&)) {
    oss_ << manip;
    return *this;
  }

 private:
  Level level_;
  const char* tag_;
  std::ostringstream oss_;
};

// No-op counterpart used when logging is compiled out (CPP_LOG_CLIENT_ENABLED=0)
// so `CPPLOG_STREAM_*(tag) << a << b;` still parses and discards its operands.
class NullStream {
 public:
  template <typename T>
  NullStream& operator<<(const T&) {
    return *this;
  }
  NullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
  NullStream& operator<<(std::ios_base& (*)(std::ios_base&)) { return *this; }
};

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

// Stream-style variant: `CPPLOG_STREAM_INFO("TAG") << "x=" << x << ...;`
// Yields a per-statement temporary LogStream that emits one record when the
// full expression ends. See LogStream above for when to prefer this over the
// printf-style macros. When logging is compiled out it yields a NullStream so
// the same call sites still parse and cost nothing.
#if CPP_LOG_CLIENT_ENABLED
#define CPPLOG_STREAM(level, tag) ::cpplog_client::LogStream((level), (tag))
#else
#define CPPLOG_STREAM(level, tag) ::cpplog_client::NullStream()
#endif

#define CPPLOG_STREAM_TRACE(tag) \
  CPPLOG_STREAM(::cpplog_client::Level::kTrace, (tag))
#define CPPLOG_STREAM_DEBUG(tag) \
  CPPLOG_STREAM(::cpplog_client::Level::kDebug, (tag))
#define CPPLOG_STREAM_INFO(tag) \
  CPPLOG_STREAM(::cpplog_client::Level::kInfo, (tag))
#define CPPLOG_STREAM_WARN(tag) \
  CPPLOG_STREAM(::cpplog_client::Level::kWarn, (tag))
#define CPPLOG_STREAM_ERROR(tag) \
  CPPLOG_STREAM(::cpplog_client::Level::kError, (tag))

#endif  // CPP_LOG_CLIENT_H_
