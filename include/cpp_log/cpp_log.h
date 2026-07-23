// C++ convenience API for the process-wide CloudPlayPlus native logger.
#ifndef CPP_LOG_INCLUDE_CPP_LOG_CPP_LOG_H_
#define CPP_LOG_INCLUDE_CPP_LOG_CPP_LOG_H_

#include <cstdint>
#include <string>

#include "cpp_log_c.h"

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

enum class Level : int {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
  kCritical = 5,
};

enum class Kind : int {
  kLog = 0,
  kMetric = 1,
  kEvent = 2,
};

CPP_LOG_API bool InitializeFile(const std::string& path,
                                int64_t max_bytes = 5 * 1024 * 1024,
                                int max_files = 3,
                                int queue_capacity = 8192);
CPP_LOG_API intptr_t InitDartApi(void* dart_api_dl_data);
CPP_LOG_API void SetPort(int64_t port);
CPP_LOG_API void SetMinLevel(Level level);
CPP_LOG_API bool ShouldLog(Level level);

CPP_LOG_API void Log(Level level, const char* tag,
                     const std::string& message);
CPP_LOG_API void LogSource(Level level, const char* tag,
                           const std::string& message, const char* file,
                           int line, const char* function);
CPP_LOG_API void LogKind(Kind kind, Level level, const char* tag,
                         const std::string& message);
CPP_LOG_API void Logf(Level level, const char* tag, const char* format, ...);
CPP_LOG_API void LogfSource(Level level, const char* tag, const char* file,
                            int line, const char* function,
                            const char* format, ...);

CPP_LOG_API void UseFileSink(const std::string& path);
CPP_LOG_API void UseFileSink(const std::string& path, int64_t max_bytes,
                             int max_files);
CPP_LOG_API void Flush();
CPP_LOG_API uint64_t DroppedCount();
CPP_LOG_API void Shutdown();

}  // namespace cpplog

#ifndef CPP_LOG_ENABLED
#define CPP_LOG_ENABLED 1
#endif

#if CPP_LOG_ENABLED
#define CPPLOG(level, tag, ...)                                            \
  ::cpplog::LogfSource((level), (tag), __FILE__, __LINE__, __func__,       \
                       __VA_ARGS__)
#else
#define CPPLOG(level, tag, ...) ((void)0)
#endif

#define CPPLOG_TRACE(tag, ...) \
  CPPLOG(::cpplog::Level::kTrace, (tag), __VA_ARGS__)
#define CPPLOG_DEBUG(tag, ...) \
  CPPLOG(::cpplog::Level::kDebug, (tag), __VA_ARGS__)
#define CPPLOG_INFO(tag, ...) \
  CPPLOG(::cpplog::Level::kInfo, (tag), __VA_ARGS__)
#define CPPLOG_WARN(tag, ...) \
  CPPLOG(::cpplog::Level::kWarn, (tag), __VA_ARGS__)
#define CPPLOG_ERROR(tag, ...) \
  CPPLOG(::cpplog::Level::kError, (tag), __VA_ARGS__)
#define CPPLOG_CRITICAL(tag, ...) \
  CPPLOG(::cpplog::Level::kCritical, (tag), __VA_ARGS__)

#endif  // CPP_LOG_INCLUDE_CPP_LOG_CPP_LOG_H_
