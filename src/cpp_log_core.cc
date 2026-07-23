// CloudPlayPlus logging runtime backed by one process-wide spdlog async queue.

#include "cpp_log_core.h"

#include "cpp_log_c.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_FILENAMES
#endif
#include <windows.h>
#endif

#include "spdlog/async_logger.h"
#include "spdlog/details/thread_pool.h"
#include "spdlog/details/periodic_worker.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/dist_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#if defined(_WIN32)
#include "spdlog/sinks/msvc_sink.h"
#elif defined(__ANDROID__)
#include "spdlog/sinks/android_sink.h"
#endif

#ifndef CPP_LOG_WITH_DART_PORT
#define CPP_LOG_WITH_DART_PORT 1
#endif

#if CPP_LOG_WITH_DART_PORT
#include "third_party/dart_dl/dart_api_dl.h"
#endif

namespace cpplog {
namespace {

constexpr int64_t kDefaultMaxBytes = 5 * 1024 * 1024;
constexpr int kDefaultMaxFiles = 3;
constexpr int kDefaultQueueCapacity = 8192;
constexpr int kMinimumQueueCapacity = 256;
constexpr char kLogPattern[] = "%Y-%m-%d %H:%M:%S.%e %v";

using DistributionSink = spdlog::sinks::dist_sink_mt;

std::mutex g_runtime_mutex;
std::shared_ptr<spdlog::details::thread_pool> g_thread_pool;
std::shared_ptr<DistributionSink> g_distribution_sink;
std::shared_ptr<spdlog::sinks::sink> g_file_sink;
std::shared_ptr<spdlog::async_logger> g_logger;
std::unique_ptr<spdlog::details::periodic_worker> g_periodic_flusher;
std::atomic<int> g_min_level{static_cast<int>(Level::kInfo)};
std::atomic<uint64_t> g_last_dropped{0};

#if CPP_LOG_WITH_DART_PORT
class DartPortSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  void SetPort(int64_t port) {
    port_.store(port, std::memory_order_release);
  }

 protected:
  void sink_it_(const spdlog::details::log_msg& message) override {
    const int64_t port = port_.load(std::memory_order_acquire);
    if (port == 0) {
      return;
    }

    spdlog::memory_buf_t formatted;
    formatter_->format(message, formatted);
    std::string text(formatted.data(), formatted.size());
    Dart_CObject object;
    object.type = Dart_CObject_kString;
    object.value.as_string = text.data();
    Dart_PostCObject_DL(port, &object);
  }

  void flush_() override {}

 private:
  std::atomic<int64_t> port_{0};
};

std::shared_ptr<DartPortSink> g_dart_sink;
#endif

Level ClampLevel(int level) {
  return static_cast<Level>(
      std::clamp(level, static_cast<int>(Level::kTrace),
                 static_cast<int>(Level::kCritical)));
}

spdlog::level::level_enum ToSpdLevel(Level level) {
  switch (level) {
    case Level::kTrace:
      return spdlog::level::trace;
    case Level::kDebug:
      return spdlog::level::debug;
    case Level::kInfo:
      return spdlog::level::info;
    case Level::kWarn:
      return spdlog::level::warn;
    case Level::kError:
      return spdlog::level::err;
    case Level::kCritical:
      return spdlog::level::critical;
  }
  return spdlog::level::info;
}

const char* LevelLabel(Level level) {
  switch (level) {
    case Level::kTrace:
      return "[TRACE]";
    case Level::kDebug:
      return "[DEBUG]";
    case Level::kInfo:
      return "[INFO ]";
    case Level::kWarn:
      return "[WARN ]";
    case Level::kError:
      return "[ERROR]";
    case Level::kCritical:
      return "[FATAL]";
  }
  return "[INFO ]";
}

#if defined(_WIN32)
spdlog::filename_t FileNameFromUtf8(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                          static_cast<int>(path.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring result(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                      static_cast<int>(path.size()), result.data(), length);
  return result;
}
#else
spdlog::filename_t FileNameFromUtf8(const std::string& path) {
  return path;
}
#endif

void SetSinkPattern(const std::shared_ptr<spdlog::sinks::sink>& sink) {
  sink->set_pattern(kLogPattern);
}

bool EnsureRuntimeLocked(int queue_capacity) {
  if (g_logger) {
    return true;
  }

  try {
    const size_t capacity = static_cast<size_t>(
        std::max(queue_capacity, kMinimumQueueCapacity));
    auto thread_pool =
        std::make_shared<spdlog::details::thread_pool>(capacity, 1);
    auto distribution = std::make_shared<DistributionSink>();

#if CPP_LOG_WITH_DART_PORT
    auto dart_sink = std::make_shared<DartPortSink>();
    SetSinkPattern(dart_sink);
    distribution->add_sink(dart_sink);
#endif

#if defined(_WIN32)
    auto platform_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    SetSinkPattern(platform_sink);
    distribution->add_sink(platform_sink);
#elif defined(__ANDROID__)
    auto platform_sink =
        std::make_shared<spdlog::sinks::android_sink_mt>("CloudPlayPlus");
    SetSinkPattern(platform_sink);
    distribution->add_sink(platform_sink);
#endif

    auto logger = std::make_shared<spdlog::async_logger>(
        "cloudplayplus", distribution, thread_pool,
        spdlog::async_overflow_policy::overrun_oldest);
    logger->set_level(
        ToSpdLevel(ClampLevel(g_min_level.load(std::memory_order_relaxed))));
    logger->flush_on(spdlog::level::warn);
    std::weak_ptr<spdlog::async_logger> weak_logger = logger;

    g_thread_pool = std::move(thread_pool);
    g_distribution_sink = std::move(distribution);
#if CPP_LOG_WITH_DART_PORT
    g_dart_sink = std::move(dart_sink);
#endif
    std::atomic_store_explicit(&g_logger, std::move(logger),
                               std::memory_order_release);
    g_periodic_flusher =
        std::make_unique<spdlog::details::periodic_worker>(
            [weak_logger] {
              if (auto current = weak_logger.lock()) {
                current->flush();
              }
            },
            std::chrono::seconds(2));
    g_last_dropped.store(0, std::memory_order_relaxed);
    return true;
  } catch (...) {
    return false;
  }
}

bool InstallFileSinkLocked(const std::string& path, int64_t max_bytes,
                           int max_files) {
  if (path.empty()) {
    return false;
  }
  try {
    const spdlog::filename_t filename = FileNameFromUtf8(path);
    if (filename.empty()) {
      return false;
    }
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        filename, static_cast<size_t>(max_bytes > 0 ? max_bytes
                                                    : kDefaultMaxBytes),
        static_cast<size_t>(max_files > 0 ? max_files : kDefaultMaxFiles),
        false);
    SetSinkPattern(sink);
    if (g_file_sink) {
      g_file_sink->flush();
      g_distribution_sink->remove_sink(g_file_sink);
    }
    g_distribution_sink->add_sink(sink);
    g_file_sink = std::move(sink);
    return true;
  } catch (...) {
    return false;
  }
}

std::shared_ptr<spdlog::async_logger> Logger() {
  return std::atomic_load_explicit(&g_logger, std::memory_order_acquire);
}

void VLogfSource(Level level, const char* tag, const char* file, int line,
                 const char* function, const char* format, va_list args) {
  if (!format || !ShouldLog(level)) {
    return;
  }

  char stack_buffer[512];
  va_list size_args;
  va_copy(size_args, args);
  const int needed =
      std::vsnprintf(stack_buffer, sizeof(stack_buffer), format, size_args);
  va_end(size_args);
  if (needed < 0) {
    return;
  }
  if (static_cast<size_t>(needed) < sizeof(stack_buffer)) {
    LogSource(level, tag,
              std::string(stack_buffer, static_cast<size_t>(needed)), file,
              line, function);
    return;
  }

  std::string buffer(static_cast<size_t>(needed) + 1, '\0');
  std::vsnprintf(buffer.data(), buffer.size(), format, args);
  buffer.resize(static_cast<size_t>(needed));
  LogSource(level, tag, buffer, file, line, function);
}

}  // namespace

bool InitializeFile(const std::string& path, int64_t max_bytes, int max_files,
                    int queue_capacity) {
  std::lock_guard<std::mutex> lock(g_runtime_mutex);
  return EnsureRuntimeLocked(queue_capacity) &&
         InstallFileSinkLocked(path, max_bytes, max_files);
}

intptr_t InitDartApi(void* dart_api_dl_data) {
#if CPP_LOG_WITH_DART_PORT
  return dart_api_dl_data ? Dart_InitializeApiDL(dart_api_dl_data) : -1;
#else
  (void)dart_api_dl_data;
  return -1;
#endif
}

void SetPort(int64_t port) {
#if CPP_LOG_WITH_DART_PORT
  std::lock_guard<std::mutex> lock(g_runtime_mutex);
  if (port != 0 && !EnsureRuntimeLocked(kDefaultQueueCapacity)) {
    return;
  }
  if (g_dart_sink) {
    g_dart_sink->SetPort(port);
  }
#else
  (void)port;
#endif
}

void SetMinLevel(Level level) {
  level = ClampLevel(static_cast<int>(level));
  g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
  if (auto logger = Logger()) {
    logger->set_level(ToSpdLevel(level));
  }
}

bool ShouldLog(Level level) {
  level = ClampLevel(static_cast<int>(level));
  auto logger = Logger();
  return logger &&
         static_cast<int>(level) >=
             g_min_level.load(std::memory_order_relaxed) &&
         logger->should_log(ToSpdLevel(level));
}

void LogSource(Level level, const char* tag, const std::string& message,
               const char* file, int line, const char* function) {
  level = ClampLevel(static_cast<int>(level));
  auto logger = Logger();
  if (!logger ||
      static_cast<int>(level) <
          g_min_level.load(std::memory_order_relaxed)) {
    return;
  }
  const char* resolved_tag = tag && *tag ? tag : "NATIVE";
  logger->log(spdlog::source_loc{file ? file : "", line,
                                 function ? function : ""},
              ToSpdLevel(level), "{} [{}] {}", LevelLabel(level), resolved_tag,
              message);
}

void Log(Level level, const char* tag, const std::string& message) {
  LogSource(level, tag, message, nullptr, 0, nullptr);
}

void LogKind(Kind kind, Level level, const char* tag,
             const std::string& message) {
  if (kind == Kind::kLog) {
    Log(level, tag, message);
  }
}

void Logf(Level level, const char* tag, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VLogfSource(level, tag, nullptr, 0, nullptr, format, args);
  va_end(args);
}

void LogfSource(Level level, const char* tag, const char* file, int line,
                const char* function, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VLogfSource(level, tag, file, line, function, format, args);
  va_end(args);
}

void UseFileSink(const std::string& path) {
  UseFileSink(path, kDefaultMaxBytes, kDefaultMaxFiles);
}

void UseFileSink(const std::string& path, int64_t max_bytes, int max_files) {
  (void)InitializeFile(path, max_bytes, max_files, kDefaultQueueCapacity);
}

void Flush() {
  if (auto logger = Logger()) {
    logger->flush();
  }
}

uint64_t DroppedCount() {
  std::lock_guard<std::mutex> lock(g_runtime_mutex);
  if (g_thread_pool) {
    return static_cast<uint64_t>(g_thread_pool->overrun_counter());
  }
  return g_last_dropped.load(std::memory_order_relaxed);
}

void Shutdown() {
  std::shared_ptr<spdlog::async_logger> logger;
  std::shared_ptr<spdlog::details::thread_pool> thread_pool;
  std::unique_ptr<spdlog::details::periodic_worker> periodic_flusher;
  {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    logger = std::atomic_exchange_explicit(
        &g_logger, std::shared_ptr<spdlog::async_logger>(),
        std::memory_order_acq_rel);
    thread_pool = std::move(g_thread_pool);
    periodic_flusher = std::move(g_periodic_flusher);
    if (thread_pool) {
      g_last_dropped.store(
          static_cast<uint64_t>(thread_pool->overrun_counter()),
          std::memory_order_relaxed);
    }
#if CPP_LOG_WITH_DART_PORT
    if (g_dart_sink) {
      g_dart_sink->SetPort(0);
    }
    g_dart_sink.reset();
#endif
    g_file_sink.reset();
    g_distribution_sink.reset();
  }

  periodic_flusher.reset();
  if (logger) {
    logger->flush();
  }
  logger.reset();
  // The thread-pool destructor posts a blocking terminate message and joins its
  // worker after all earlier log and flush messages have been processed.
  thread_pool.reset();
}

}  // namespace cpplog

extern "C" {

int32_t cpp_log_initialize(const char* path, int64_t max_bytes,
                           int32_t max_files, int32_t queue_capacity) {
  if (!path || !*path || max_bytes <= 0 || max_files <= 0 ||
      queue_capacity <= 0) {
    return -1;
  }
  return cpplog::InitializeFile(path, max_bytes, max_files, queue_capacity)
             ? 0
             : -3;
}

intptr_t cpp_log_init_dart_api(void* dart_api_dl_data) {
  return cpplog::InitDartApi(dart_api_dl_data);
}

void cpp_log_set_port(int64_t port) {
  cpplog::SetPort(port);
}

void cpp_log_set_min_level(int32_t level) {
  cpplog::SetMinLevel(static_cast<cpplog::Level>(level));
}

int32_t cpp_log_should_log(int32_t level) {
  return cpplog::ShouldLog(static_cast<cpplog::Level>(level)) ? 1 : 0;
}

void cpp_log_emit(int32_t level, const char* tag, const char* message) {
  cpplog::Log(static_cast<cpplog::Level>(level), tag,
              message ? std::string(message) : std::string());
}

void cpp_log_emit_source(int32_t level, const char* tag, const char* message,
                         const char* file, int32_t line,
                         const char* function) {
  cpplog::LogSource(static_cast<cpplog::Level>(level), tag,
                    message ? std::string(message) : std::string(), file,
                    static_cast<int>(line), function);
}

void cpp_log_use_file_sink(const char* path) {
  if (path) {
    cpplog::UseFileSink(path);
  }
}

void cpp_log_use_file_sink_ex(const char* path, int64_t max_bytes,
                              int32_t max_files) {
  if (path) {
    cpplog::UseFileSink(path, max_bytes, static_cast<int>(max_files));
  }
}

void cpp_log_flush(void) {
  cpplog::Flush();
}

uint64_t cpp_log_dropped_count(void) {
  return cpplog::DroppedCount();
}

void cpp_log_shutdown(void) {
  cpplog::Shutdown();
}

}  // extern "C"
