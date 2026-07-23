#include "include/cpp_log/cpp_log_plugin_c_api.h"

void CppLogPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  // The logger is reached through its C ABI/FFI and needs no method channel.
  (void)registrar;
}
