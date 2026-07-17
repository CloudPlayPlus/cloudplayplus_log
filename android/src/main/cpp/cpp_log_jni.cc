// cpp_log — JNI shim bridging the Kotlin CppLog adapter to the stable C ABI.
//
// Compiled into libcpp_log.so alongside the shared core (see the sibling
// CMakeLists.txt). NOT verified on Android — structural glue only.
//
// JNI name mangling note: the package segment `cpp_log` contains an underscore,
// which JNI encodes as `_1`. So `com.cloudplayplus.cpp_log.CppLog#nativeEmit`
// maps to `Java_com_cloudplayplus_cpp_1log_CppLog_nativeEmit`. The methods are
// declared `@JvmStatic external` in a Kotlin `object`, hence the `jclass`
// receiver (static), not `jobject`.
#include <jni.h>

#include <cstdint>

#include "cpp_log_c.h"

extern "C" {

JNIEXPORT void JNICALL
Java_com_cloudplayplus_cpp_1log_CppLog_nativeEmit(JNIEnv* env, jclass /*clazz*/,
                                                  jint level, jstring tag,
                                                  jstring msg) {
  const char* tag_c = tag ? env->GetStringUTFChars(tag, nullptr) : nullptr;
  const char* msg_c = msg ? env->GetStringUTFChars(msg, nullptr) : nullptr;
  cpp_log_emit(static_cast<int32_t>(level), tag_c, msg_c);
  if (tag && tag_c) env->ReleaseStringUTFChars(tag, tag_c);
  if (msg && msg_c) env->ReleaseStringUTFChars(msg, msg_c);
}

JNIEXPORT void JNICALL
Java_com_cloudplayplus_cpp_1log_CppLog_nativeSetMinLevel(JNIEnv* /*env*/,
                                                         jclass /*clazz*/,
                                                         jint level) {
  cpp_log_set_min_level(static_cast<int32_t>(level));
}

JNIEXPORT void JNICALL
Java_com_cloudplayplus_cpp_1log_CppLog_nativeUseFileSink(JNIEnv* env,
                                                         jclass /*clazz*/,
                                                         jstring path) {
  if (!path) return;
  const char* path_c = env->GetStringUTFChars(path, nullptr);
  cpp_log_use_file_sink(path_c);
  env->ReleaseStringUTFChars(path, path_c);
}

}  // extern "C"
