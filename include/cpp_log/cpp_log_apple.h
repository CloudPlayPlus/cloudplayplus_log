// cpp_log — Objective-C(++) convenience adapter over the stable C ABI.
//
// Exposes an NSLog-style `CPPLOGM(...)` macro (and per-level shorthands) so
// Apple-platform plugin/app code (macOS / iOS) can funnel records into the same
// cpp_log core — and therefore the same ordered `app.log` — as the C++ plugins
// and the Dart FFI bridge. It is a thin inline wrapper over `cpp_log_emit` from
// cpp_log_c.h; no Objective-C runtime state is added.
//
// The macOS framework/link path and shared C ABI file sink are validated through
// the CloudPlayPlus host app. The iOS build path and this Objective-C convenience
// adapter still require their respective platform verification.
//
// Level ordinals match the C ABI (mirrored in cpp_log_c.h / lib/cpp_log.dart):
//   0=trace 1=debug 2=info 3=warn 4=error 5=critical
#ifndef CPP_LOG_APPLE_H_
#define CPP_LOG_APPLE_H_

#import <Foundation/Foundation.h>

// Resolved via the podspec HEADER_SEARCH_PATHS (../src is on the search path).
#include "cpp_log_c.h"

#ifdef __cplusplus
extern "C" {
#endif

// Emits one record built from an NSString message. `tag` may be null; a nil
// message is treated as empty. Kept tiny and header-inline so callers don't need
// to link an extra translation unit.
static inline void CppLogEmitNS(int level, const char* tag, NSString* message) {
  if (message == nil) {
    message = @"";
  }
  cpp_log_emit(level, tag, message.UTF8String);
}

#ifdef __cplusplus
}  // extern "C"
#endif

// NSLog-style formatting into the cpp_log core.
//   CPPLOGM(2 /*info*/, "AUDIO", @"route changed to %@", name);
#define CPPLOGM(level, tag, fmt, ...) \
  CppLogEmitNS((level), (tag), [NSString stringWithFormat:(fmt), ##__VA_ARGS__])

// Per-level shorthands (map to the C ABI ordinals above).
#define CPPLOGM_TRACE(tag, fmt, ...) CPPLOGM(0, (tag), (fmt), ##__VA_ARGS__)
#define CPPLOGM_DEBUG(tag, fmt, ...) CPPLOGM(1, (tag), (fmt), ##__VA_ARGS__)
#define CPPLOGM_INFO(tag, fmt, ...) CPPLOGM(2, (tag), (fmt), ##__VA_ARGS__)
#define CPPLOGM_WARN(tag, fmt, ...) CPPLOGM(3, (tag), (fmt), ##__VA_ARGS__)
#define CPPLOGM_ERROR(tag, fmt, ...) CPPLOGM(4, (tag), (fmt), ##__VA_ARGS__)
#define CPPLOGM_CRITICAL(tag, fmt, ...) CPPLOGM(5, (tag), (fmt), ##__VA_ARGS__)

#endif  // CPP_LOG_APPLE_H_
