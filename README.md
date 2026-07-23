# cpp_log

CloudPlayPlus 的进程级原生日志运行时。它把 spdlog 封装在一个 Flutter
插件内，同时提供 Dart FFI、稳定 C ABI 和 C++ 宏，避免每个插件各自创建
日志队列和写文件线程。

## 数据路径

```text
Dart CppLog.emit ─┐
C cpp_log_emit ───┼─> spdlog async queue ─> rotating app.log
C++ CPPLOG_* ─────┘                         └> optional Dart live view
```

- 单进程一个 `cpp_log` 动态库、一个 8192 槽有界队列、一个写线程。
- 队列满时覆盖最旧记录，不阻塞媒体线程；丢弃总数可通过
  `cpp_log_dropped_count()` 查询。
- 文件默认 5 MiB × 3 个归档，每 2 秒异步 flush，`warn` 以上立即安排
  flush。
- Windows 同时输出到 `OutputDebugString`，Android 同时输出到 logcat。
- Dart native port 只是可选的实时日志视图，文件写入不绕行 Dart。

日志格式：

```text
yyyy-MM-dd HH:mm:ss.mmm [LEVEL] [TAG] message
```

## API

公共头文件：

- `include/cpp_log/cpp_log_c.h`：稳定 C ABI，供 FFI/C/ObjC 调用。
- `include/cpp_log/cpp_log.h`：C++ `CPPLOG_TRACE/DEBUG/INFO/WARN/ERROR`
  宏，自动携带源码位置。

Dart：

```dart
final ok = CppLog.instance.initialize(
  filePath: logPath,
  minLevel: CppLogLevel.info,
);
CppLog.instance.emit(CppLogLevel.info, 'APP', 'started');
```

`CppLog` 的 Dart facade 只应在 root isolate 使用；后台 isolate 应把记录转发
到 root isolate。C/C++ API 可继续由原生工作线程直接调用。重复调用
`initialize()` 会切换文件 sink 和最低级别，但沿用现有异步队列及其初始
容量，直到 `stop()` 重建 runtime。

C++：

```cpp
#include <cpp_log/cpp_log.h>

CPPLOG_INFO("VIDEO", "Selected encoder: %s", encoder_name);
CPPLOG_ERROR("NETWORK", "Connection failed: %d", error_code);
```

宿主提供 `cpp_log_plugin` 时，其他 Flutter C++ 插件可链接该 target 共享同一
runtime，不能自行编译 spdlog。插件独立构建时应保留无日志 fallback，避免把
`cpp_log` 变成其 Dart package 的强制依赖。

## 第三方依赖

spdlog 固定为 `v1.17.0`，位于 `third_party/spdlog`，使用其 MIT
许可证。当前采用私有 header-only 集成：spdlog 头文件只由
`cpp_log_core.cc` 包含，因此实现仍只编进一个动态库，不会散落到调用方。

## 验证

```powershell
flutter pub get
flutter analyze lib test
cd example
flutter build windows --debug
cd ..
flutter test test/cpp_log_file_sink_test.dart
```

Windows 已完成构建、FFI 文件轮转和主应用运行验证。Linux、Android、
macOS、iOS 已有构建胶水，但仍需要对应平台 CI/真机验证。

若要测量 Dart 生产端路径，可把已构建的 `cpp_log.dll` 目录放到
`PATH` 最前面后运行：

```powershell
$env:PATH="<runner-output-directory>;$env:PATH"
dart run benchmark/emit_benchmark.dart
```

结果只统计 Dart → FFI → 有界异步队列的入队时间；随后由 shutdown
排空队列，因此生产端数据不包含文件系统吞吐耗时。
