// Stable C ABI for the process-wide CloudPlayPlus native logger.
#ifndef CPP_LOG_INCLUDE_CPP_LOG_CPP_LOG_C_H_
#define CPP_LOG_INCLUDE_CPP_LOG_CPP_LOG_C_H_

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

// Level ABI: 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical.

// Creates the asynchronous runtime and installs a rotating file sink.
// Returns 0 on success and a negative value on invalid arguments or setup
// failure.
CPP_LOG_C_API int32_t cpp_log_initialize(const char* path, int64_t max_bytes,
                                         int32_t max_files,
                                         int32_t queue_capacity);

// Initializes Dart's dynamically-linked native API. This is needed only when a
// Dart receive port is attached for an optional live-log view.
CPP_LOG_C_API intptr_t cpp_log_init_dart_api(void* dart_api_dl_data);

// Attaches an optional Dart native port, or detaches it when port is zero.
// Before attaching a non-zero port, cpp_log_init_dart_api() must have returned
// zero for this Dart process. The rotating file sink remains active.
CPP_LOG_C_API void cpp_log_set_port(int64_t port);

CPP_LOG_C_API void cpp_log_set_min_level(int32_t level);
CPP_LOG_C_API int32_t cpp_log_should_log(int32_t level);

// Emits UTF-8 text. Inputs are copied before this function returns.
CPP_LOG_C_API void cpp_log_emit(int32_t level, const char* tag,
                                const char* message);
CPP_LOG_C_API void cpp_log_emit_source(int32_t level, const char* tag,
                                       const char* message, const char* file,
                                       int32_t line, const char* function);

// Compatibility configuration entry points.
CPP_LOG_C_API void cpp_log_use_file_sink(const char* path);
CPP_LOG_C_API void cpp_log_use_file_sink_ex(const char* path,
                                            int64_t max_bytes,
                                            int32_t max_files);

// flush() schedules an asynchronous flush. shutdown() stops producers, drains
// the queue, flushes sinks, and releases the worker thread.
CPP_LOG_C_API void cpp_log_flush(void);
CPP_LOG_C_API uint64_t cpp_log_dropped_count(void);
CPP_LOG_C_API void cpp_log_shutdown(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CPP_LOG_INCLUDE_CPP_LOG_CPP_LOG_C_H_
