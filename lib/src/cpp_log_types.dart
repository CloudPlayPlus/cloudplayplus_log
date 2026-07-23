/// Severity values are part of the native ABI; do not reorder them.
enum CppLogLevel { trace, debug, info, warn, error, critical }

/// Reserved record channels. Only [log] is emitted today.
enum CppLogKind { log, metric, event }
