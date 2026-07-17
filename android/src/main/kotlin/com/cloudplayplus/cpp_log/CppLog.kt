package com.cloudplayplus.cpp_log

/**
 * Kotlin adapter over the cpp_log C ABI (via the JNI shim in
 * src/main/cpp/cpp_log_jni.cc).
 *
 * Lets native Android (Kotlin/Java) callers funnel log records into the same
 * cpp_log core that the C++ plugins and the Dart FFI bridge use, so everything
 * lands in one ordered `app.log`. This is a standalone adapter — it is NOT a
 * FlutterPlugin and is not auto-registered; callers use [CppLog] directly.
 *
 * Level ordinals match the C ABI (mirrored in cpp_log_c.h / lib/cpp_log.dart):
 *   0=trace 1=debug 2=info 3=warn 4=error
 *
 * NOT verified on Android — structural glue only. The C ABI it calls is the same
 * one validated on Windows; only the Android build/link path is unvalidated.
 */
object CppLog {
    init {
        // Loads libcpp_log.so (built by src/main/cpp/CMakeLists.txt).
        System.loadLibrary("cpp_log")
    }

    const val TRACE: Int = 0
    const val DEBUG: Int = 1
    const val INFO: Int = 2
    const val WARN: Int = 3
    const val ERROR: Int = 4

    /** Emits one record at [level] (see the level constants above). */
    fun emit(level: Int, tag: String, message: String) = nativeEmit(level, tag, message)

    fun trace(tag: String, message: String) = nativeEmit(TRACE, tag, message)
    fun debug(tag: String, message: String) = nativeEmit(DEBUG, tag, message)
    fun info(tag: String, message: String) = nativeEmit(INFO, tag, message)
    fun warn(tag: String, message: String) = nativeEmit(WARN, tag, message)
    fun error(tag: String, message: String) = nativeEmit(ERROR, tag, message)

    /** Sets the native minimum severity (records below it are dropped). */
    fun setMinLevel(level: Int) = nativeSetMinLevel(level)

    /**
     * Switches the native active sink to a rotating file at [path] — for
     * native-only Android contexts with no Dart isolate to post to.
     */
    fun useFileSink(path: String) = nativeUseFileSink(path)

    @JvmStatic private external fun nativeEmit(level: Int, tag: String, message: String)
    @JvmStatic private external fun nativeSetMinLevel(level: Int)
    @JvmStatic private external fun nativeUseFileSink(path: String)
}
