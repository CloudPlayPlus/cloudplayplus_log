// cpp_log core implementation. See cpp_log_core.h / cpp_log_c.h for the API and
// the design rationale.

#include "cpp_log_core.h"

#include "cpp_log_c.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// The DartPortSink (and the vendored dynamically-linked Dart API it posts
// through) is only meaningful inside a process that hosts a Dart isolate. A
// native-only consumer — e.g. a Windows service that compiles this core
// straight into its binary and uses only the FileSink — defines
// CPP_LOG_WITH_DART_PORT=0 to strip the Dart port sink and the dart_dl
// dependency entirely. Defaults to on so the shared-library build is unchanged.
#ifndef CPP_LOG_WITH_DART_PORT
#define CPP_LOG_WITH_DART_PORT 1
#endif

#if CPP_LOG_WITH_DART_PORT
#include "third_party/dart_dl/dart_api_dl.h"
#endif

namespace cpplog {
namespace {

// Ring-buffer bound. On overrun we drop the *oldest* record (never block the
// producer) and count the loss so the drain can surface it.
constexpr size_t kMaxQueued = 8192;
// Drain cadence and batch cap. Batching collapses N sink writes into one per
// interval; the interval bounds worst-case latency.
constexpr auto kFlushInterval = std::chrono::milliseconds(30);
constexpr size_t kMaxBatch = 512;

// FileSink defaults — chosen to match the Dart FileLogSink (2 MiB x 5).
constexpr int64_t kDefaultMaxBytes = 2 * 1024 * 1024;
constexpr int kDefaultMaxFiles = 5;

// One buffered record. `line` is the fully-formatted, ready-to-emit text; the
// timestamp/level/tag were baked in at production time. `kind` selects the
// output channel (only kLog is emitted today).
struct Record {
  Kind kind;
  std::string line;
};

std::mutex g_mutex;  // guards g_queue, g_dropped
std::condition_variable g_cv;
std::deque<Record> g_queue;
uint64_t g_dropped = 0;  // guarded by g_mutex

std::mutex g_sink_mutex;  // guards g_sink
std::unique_ptr<Sink> g_sink;
std::atomic<bool> g_has_sink{false};  // fast, lock-free predicate for the drain

std::atomic<int> g_min_level{static_cast<int>(Level::kInfo)};
std::atomic<bool> g_running{false};
std::thread g_worker;

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
  }
  return "[INFO ]";
}

// "yyyy-MM-dd HH:mm:ss.mmm" — matches the Dart-side on-disk format exactly so
// native- and Dart-origin records interleave cleanly in one app.log.
std::string NowTimestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
  return std::string(buf);
}

std::string FormatLine(Level level, const char* tag,
                       const std::string& message) {
  std::string line = NowTimestamp();
  line += ' ';
  line += LevelLabel(level);
  line += " [";
  line += (tag && *tag) ? tag : "NATIVE";
  line += "] ";
  line += message;
  return line;
}

// ---------------------------------------------------------------------------
// Sinks
// ---------------------------------------------------------------------------

// Posts each batch to a Dart isolate over a native port. `Dart_PostCObject` is
// callable from any thread, so the drain thread can post directly. Compiled out
// for native-only (FileSink-only) builds via CPP_LOG_WITH_DART_PORT=0.
#if CPP_LOG_WITH_DART_PORT
class DartPortSink : public Sink {
 public:
  explicit DartPortSink(int64_t port) : port_(port) {}
  void Write(const std::string& batch) override {
    // Dart copies the string during the post, so a stack CObject is safe.
    Dart_CObject obj;
    obj.type = Dart_CObject_kString;
    obj.value.as_string = const_cast<char*>(batch.c_str());
    Dart_PostCObject_DL(port_, &obj);
  }

 private:
  int64_t port_;
};
#endif  // CPP_LOG_WITH_DART_PORT

// Append-only file with size-based rotation. Layout mirrors the Dart
// FileLogSink: `path` is active; archives are `path.1` (newest) .. `path.N`
// (oldest). On rollover the oldest is deleted and the rest shift up by one.
class FileSink : public Sink {
 public:
  FileSink(std::string path, int64_t max_bytes, int max_files)
      : path_(std::move(path)),
        max_bytes_(max_bytes > 0 ? max_bytes : kDefaultMaxBytes),
        max_files_(max_files > 0 ? max_files : kDefaultMaxFiles) {
    std::error_code ec;
    const std::filesystem::path p(path_);
    if (p.has_parent_path()) {
      std::filesystem::create_directories(p.parent_path(), ec);
    }
    Open();
  }

  ~FileSink() override { Close(); }

  void Write(const std::string& batch) override {
    if (!out_.is_open()) return;
    // `batch` carries no trailing newline; append one so records stay separated
    // and the file always ends on a newline (as the Dart sink does).
    const int64_t bytes = static_cast<int64_t>(batch.size()) + 1;
    // Rotate *before* writing so the active file always holds the newest lines
    // (better for `tail`). Never rotate an empty file — an oversized single
    // batch is written as-is rather than lost.
    if (size_ > 0 && size_ + bytes > max_bytes_) {
      Rotate();
    }
    if (!out_.is_open()) return;
    out_.write(batch.data(), static_cast<std::streamsize>(batch.size()));
    out_.put('\n');
    out_.flush();
    size_ += bytes;
  }

 private:
  void Open() {
    std::error_code ec;
    size_ = 0;
    if (std::filesystem::exists(path_, ec)) {
      const auto s = std::filesystem::file_size(path_, ec);
      if (!ec) size_ = static_cast<int64_t>(s);
    }
    out_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
  }

  void Close() {
    if (out_.is_open()) {
      out_.flush();
      out_.close();
    }
  }

  std::string Rot(int i) const { return path_ + "." + std::to_string(i); }

  void Rotate() {
    std::error_code ec;
    Close();
    std::filesystem::remove(Rot(max_files_), ec);  // drop oldest (ok if absent)
    for (int i = max_files_ - 1; i >= 1; --i) {
      if (std::filesystem::exists(Rot(i), ec)) {
        std::filesystem::rename(Rot(i), Rot(i + 1), ec);
      }
    }
    if (std::filesystem::exists(path_, ec)) {
      std::filesystem::rename(path_, Rot(1), ec);
    }
    Open();  // fresh, empty active file
  }

  std::string path_;
  int64_t max_bytes_;
  int max_files_;
  std::ofstream out_;
  int64_t size_ = 0;
};

// ---------------------------------------------------------------------------
// Drain thread
// ---------------------------------------------------------------------------

void WorkerMain() {
  std::vector<Record> batch;
  batch.reserve(kMaxBatch);
  for (;;) {
    uint64_t dropped_snapshot = 0;
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_cv.wait_for(lock, kFlushInterval, [] {
        return (!g_queue.empty() && g_has_sink.load(std::memory_order_relaxed)) ||
               !g_running.load(std::memory_order_relaxed);
      });
      size_t n = 0;
      while (!g_queue.empty() && n < kMaxBatch) {
        batch.push_back(std::move(g_queue.front()));
        g_queue.pop_front();
        ++n;
      }
      dropped_snapshot = g_dropped;
      g_dropped = 0;
    }

    // Partition by channel. Only kLog is emitted today; kMetric / kEvent are
    // drained (so they don't accumulate) but not yet forwarded to any sink.
    std::string log_batch;
    for (const Record& rec : batch) {
      if (rec.kind != Kind::kLog) continue;
      if (!log_batch.empty()) log_batch.push_back('\n');
      log_batch += rec.line;
    }

    bool delivered = false;
    {
      std::lock_guard<std::mutex> sl(g_sink_mutex);
      if (g_sink) {
        if (dropped_snapshot > 0) {
          g_sink->Write(NowTimestamp() + " [WARN ] [NATIVE] cpp_log dropped " +
                        std::to_string(dropped_snapshot) + " records (overrun)");
        }
        if (!log_batch.empty()) {
          g_sink->Write(log_batch);
        }
        delivered = true;
      }
    }

    const bool running = g_running.load(std::memory_order_relaxed);
    if (!delivered && running && !batch.empty()) {
      // Sink vanished between drain and write (a rare SetPort(0) race). Retain
      // records + dropped count so a later sink still sees them. On shutdown we
      // deliberately drop instead of retaining — there is nowhere left to send.
      std::lock_guard<std::mutex> lock(g_mutex);
      for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
        if (g_queue.size() >= kMaxQueued) break;
        g_queue.push_front(std::move(*it));
      }
      g_dropped += dropped_snapshot;
    }
    batch.clear();

    if (!running) {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_queue.empty()) break;  // fully drained → exit
    }
  }
}

void EnsureWorker() {
  bool expected = false;
  if (g_running.compare_exchange_strong(expected, true)) {
    g_worker = std::thread(WorkerMain);
  }
}

void InstallSink(std::unique_ptr<Sink> sink) {
  {
    std::lock_guard<std::mutex> sl(g_sink_mutex);
    g_sink = std::move(sink);
    g_has_sink.store(g_sink != nullptr, std::memory_order_relaxed);
  }
  EnsureWorker();
  g_cv.notify_one();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public C++ API
// ---------------------------------------------------------------------------

intptr_t InitDartApi(void* dart_api_dl_data) {
#if CPP_LOG_WITH_DART_PORT
  const intptr_t rc = Dart_InitializeApiDL(dart_api_dl_data);
  if (rc == 0) {
    EnsureWorker();
  }
  return rc;
#else
  // No Dart isolate in this build; the port sink is unavailable.
  (void)dart_api_dl_data;
  return -1;
#endif
}

void SetPort(int64_t port) {
#if CPP_LOG_WITH_DART_PORT
  if (port == 0) {
    InstallSink(nullptr);
  } else {
    InstallSink(std::unique_ptr<Sink>(new DartPortSink(port)));
  }
#else
  // DartPortSink compiled out — nothing to install. FileSink-only builds use
  // UseFileSink()/SetSink() instead.
  (void)port;
#endif
}

void UseFileSink(const std::string& path) {
  UseFileSink(path, kDefaultMaxBytes, kDefaultMaxFiles);
}

void UseFileSink(const std::string& path, int64_t max_bytes, int max_files) {
  InstallSink(std::unique_ptr<Sink>(new FileSink(path, max_bytes, max_files)));
}

void SetSink(std::unique_ptr<Sink> sink) { InstallSink(std::move(sink)); }

void SetMinLevel(Level level) {
  g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

void LogKind(Kind kind, Level level, const char* tag,
             const std::string& message) {
  if (static_cast<int>(level) < g_min_level.load(std::memory_order_relaxed)) {
    return;
  }
  Record rec{kind, FormatLine(level, tag, message)};
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kMaxQueued) {
      g_queue.pop_front();  // drop oldest, never block the producer
      ++g_dropped;
    }
    g_queue.push_back(std::move(rec));
  }
  g_cv.notify_one();
}

void Log(Level level, const char* tag, const std::string& message) {
  LogKind(Kind::kLog, level, tag, message);
}

void Logf(Level level, const char* tag, const char* fmt, ...) {
  if (static_cast<int>(level) < g_min_level.load(std::memory_order_relaxed)) {
    return;
  }
  char stackbuf[512];
  va_list args;
  va_start(args, fmt);
  const int needed = std::vsnprintf(stackbuf, sizeof(stackbuf), fmt, args);
  va_end(args);
  if (needed < 0) return;
  if (static_cast<size_t>(needed) < sizeof(stackbuf)) {
    Log(level, tag, std::string(stackbuf, static_cast<size_t>(needed)));
    return;
  }
  std::string big(static_cast<size_t>(needed) + 1, '\0');
  va_start(args, fmt);
  std::vsnprintf(&big[0], big.size(), fmt, args);
  va_end(args);
  big.resize(static_cast<size_t>(needed));
  Log(level, tag, big);
}

void Shutdown() {
  const bool was_running = g_running.exchange(false);
  g_cv.notify_all();
  if (g_worker.joinable()) {
    g_worker.join();
  }
  // Drop the active sink after the drain thread has stopped, so any FileSink
  // flushes and closes its file (letting readers open it) and a later
  // InitDartApi/SetPort can bring the pipeline back up cleanly.
  {
    std::lock_guard<std::mutex> sl(g_sink_mutex);
    g_sink.reset();
    g_has_sink.store(false, std::memory_order_relaxed);
  }
  (void)was_running;
}

}  // namespace cpplog

// ---------------------------------------------------------------------------
// Stable C ABI (see cpp_log_c.h). Thin forwarders over the C++ API.
// ---------------------------------------------------------------------------
extern "C" {

intptr_t cpp_log_init_dart_api(void* dart_api_dl_data) {
  return cpplog::InitDartApi(dart_api_dl_data);
}

void cpp_log_set_port(int64_t port) { cpplog::SetPort(port); }

void cpp_log_set_min_level(int32_t level) {
  cpplog::SetMinLevel(static_cast<cpplog::Level>(level));
}

void cpp_log_emit(int32_t level, const char* tag, const char* msg) {
  cpplog::Log(static_cast<cpplog::Level>(level), tag ? tag : "NATIVE",
              msg ? std::string(msg) : std::string());
}

void cpp_log_use_file_sink(const char* path) {
  if (!path) return;
  cpplog::UseFileSink(std::string(path));
}

void cpp_log_use_file_sink_ex(const char* path, int64_t max_bytes,
                              int32_t max_files) {
  if (!path) return;
  cpplog::UseFileSink(std::string(path), max_bytes,
                      static_cast<int>(max_files));
}

void cpp_log_shutdown(void) { cpplog::Shutdown(); }

}  // extern "C"
