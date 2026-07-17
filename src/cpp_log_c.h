// cpp_log — stable C ABI for the native logging core.
//
// This is the surface consumed by `dart:ffi` (via DynamicLibrary.open +
// lookup) and by ObjC/Swift/C plugin code. It is intentionally `extern "C"`
// and free of C++ types so it stays ABI-stable across compilers.
//
// Symbol / level contract (mirrored in lib/cpp_log.dart):
//   level: 0=trace 1=debug 2=info 3=warn 4=error   (default minimum: info)
//
// A typical Dart wiring:
//   cpp_log_init_dart_api(NativeApi.initializeApiDLData);  // returns 0 on ok
//   cpp_log_set_min_level(0);                              // trace and up
//   cpp_log_set_port(receivePort.sendPort.nativePort);     // batches -> Dart
// A native-only process instead calls:
//   cpp_log_use_file_sink("C:\\...\\logs\\app.log");
#ifndef CPP_LOG_C_H_
#define CPP_LOG_C_H_

#include <stdint.h>

#if defined(_WIN32)
#if defined(CPP_LOG_STATIC)
#define CPP_LOG_C_API
#elif defined(CPP_LOG_BUILDING_DLL)
#define CPP_LOG_C_API __declspec(dllexport)
#else
#define CPP_LOG_C_API __declspec(dllimport)
#endif
#else
#define CPP_LOG_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the dynamically-linked Dart API. Pass the pointer from Dart's
// `NativeApi.initializeApiDLData`. Returns 0 on success. Must be called before
// cpp_log_set_port posts anything; not required for the file sink.
CPP_LOG_C_API intptr_t cpp_log_init_dart_api(void* dart_api_dl_data);

// Sets the Dart native port that batched lines are posted to, or 0 to detach.
CPP_LOG_C_API void cpp_log_set_port(int64_t port);

// Sets the minimum level (0=trace..4=error). Records below it are dropped.
CPP_LOG_C_API void cpp_log_set_min_level(int32_t level);

// Emits one record on the log channel. `tag` / `msg` may be null.
CPP_LOG_C_API void cpp_log_emit(int32_t level, const char* tag,
                                const char* msg);

// Switches the active sink to a rotating file at `path` (defaults: 2 MiB x 5).
CPP_LOG_C_API void cpp_log_use_file_sink(const char* path);

// Same, with explicit rotation bounds (bytes per file, number of archives).
CPP_LOG_C_API void cpp_log_use_file_sink_ex(const char* path, int64_t max_bytes,
                                            int32_t max_files);

// Flushes pending records and stops the drain thread. Best-effort.
CPP_LOG_C_API void cpp_log_shutdown(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CPP_LOG_C_H_
