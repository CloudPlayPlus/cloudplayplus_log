#ifndef FLUTTER_PLUGIN_CPP_LOG_PLUGIN_C_API_H_
#define FLUTTER_PLUGIN_CPP_LOG_PLUGIN_C_API_H_

#include <flutter_plugin_registrar.h>

#ifdef CPP_LOG_BUILDING_DLL
#define CPP_LOG_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CPP_LOG_PLUGIN_EXPORT __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

CPP_LOG_PLUGIN_EXPORT void CppLogPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // FLUTTER_PLUGIN_CPP_LOG_PLUGIN_C_API_H_
