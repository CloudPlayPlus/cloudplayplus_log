// Runtime bridge from a consumer's CPPLOG macros to cpp_log. See
// cpp_log_client.h for the rationale (no link-time coupling; GetProcAddress
// against the bundled cpp_log.dll). Shared, single implementation — consumers
// add this .cc to their build rather than copying it.

#include "cpp_log_client.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

namespace cpplog_client {
namespace {

// cpp_log's stable C ABI:
//   void cpp_log_emit(int32_t level, const char* tag, const char* msg);
// Declared locally (not via cpp_log_c.h) so this client is fully self-contained:
// a consumer only needs this directory on its include path, nothing from
// cpp_log/src. The signature is a frozen ABI and must match cpp_log_c.h.
using CppLogEmitFn = void (*)(int32_t, const char*, const char*);

// Cached once resolved. Stays null (retrying each call) until cpp_log.dll is
// available, then pinned for the process lifetime.
std::atomic<CppLogEmitFn> g_emit{nullptr};

CppLogEmitFn ResolveEmit() {
  CppLogEmitFn fn = g_emit.load(std::memory_order_acquire);
  if (fn != nullptr) {
    return fn;
  }
  // cpp_log.dll is bundled next to the consumer's DLL in the app directory.
  // Prefer the already-loaded module (opened by Dart's DynamicLibrary.open or a
  // prior resolve); otherwise LoadLibrary finds it via the app's DLL search
  // path. GetModuleHandleW does not bump the refcount; the one-time LoadLibraryW
  // does, intentionally pinning cpp_log.dll for the process lifetime.
  HMODULE mod = ::GetModuleHandleW(L"cpp_log.dll");
  if (mod == nullptr) {
    mod = ::LoadLibraryW(L"cpp_log.dll");
  }
  if (mod == nullptr) {
    return nullptr;
  }
  fn = reinterpret_cast<CppLogEmitFn>(::GetProcAddress(mod, "cpp_log_emit"));
  if (fn != nullptr) {
    g_emit.store(fn, std::memory_order_release);
  }
  return fn;
}

}  // namespace

void Emit(Level level, const char* tag, const char* fmt, ...) {
  const CppLogEmitFn emit = ResolveEmit();
  if (emit == nullptr) {
    return;  // cpp_log.dll not present → silently no-op.
  }

  // printf-style formatting on a stack buffer, growing to the heap only for the
  // rare oversized record. Mirrors cpplog::Logf so behaviour is consistent.
  char stackbuf[512];
  va_list args;
  va_start(args, fmt);
  const int needed = std::vsnprintf(stackbuf, sizeof(stackbuf), fmt, args);
  va_end(args);
  if (needed < 0) {
    return;
  }

  const int32_t lvl = static_cast<int32_t>(level);
  if (static_cast<size_t>(needed) < sizeof(stackbuf)) {
    emit(lvl, tag, stackbuf);
    return;
  }

  std::string big(static_cast<size_t>(needed) + 1, '\0');
  va_start(args, fmt);
  std::vsnprintf(&big[0], big.size(), fmt, args);
  va_end(args);
  big.resize(static_cast<size_t>(needed));
  emit(lvl, tag, big.c_str());
}

}  // namespace cpplog_client

#else  // !_WIN32

// The runtime bridge is only wired to cpp_log.dll on Windows today. On other
// platforms the client compiles to a no-op so consumers build unchanged; add a
// dlopen/dlsym branch here when cpp_log ships on those platforms.
namespace cpplog_client {
void Emit(Level, const char*, const char*, ...) {}
}  // namespace cpplog_client

#endif  // _WIN32
