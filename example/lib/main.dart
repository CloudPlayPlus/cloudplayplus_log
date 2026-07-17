import 'dart:async';

import 'package:cpp_log/cpp_log.dart';
import 'package:flutter/material.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final List<String> _received = <String>[];
  bool _started = false;
  int _counter = 0;

  @override
  void initState() {
    super.initState();
    // Bring up the native→Dart bridge. Each batch of native log lines arrives
    // on `onLogBatch`; here we just show them. A real app forwards them to its
    // on-disk log sink so native and Dart logs share one file.
    unawaited(
      CppLog.instance
          .start(
            minLevel: CppLogLevel.trace,
            onLogBatch: (String batch) {
              if (!mounted) return;
              setState(() => _received.addAll(batch.split('\n')));
            },
          )
          .then((_) {
            if (!mounted) return;
            setState(() => _started = CppLog.instance.started);
            CppLog.instance.emit(CppLogLevel.info, 'EXAMPLE', 'bridge started');
          }),
    );
  }

  @override
  void dispose() {
    CppLog.instance.stop();
    super.dispose();
  }

  void _emit() {
    _counter++;
    CppLog.instance.emit(CppLogLevel.info, 'EXAMPLE', 'button press #$_counter');
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('cpp_log example')),
        body: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              Text(
                _started
                    ? 'Native log bridge: STARTED'
                    : 'Native log bridge: disabled (platform without cpp_log)',
                style: Theme.of(context).textTheme.titleMedium,
              ),
              const SizedBox(height: 8),
              FilledButton(
                onPressed: _started ? _emit : null,
                child: const Text('Emit a native log line'),
              ),
              const SizedBox(height: 8),
              const Text('Received batches (newest at bottom):'),
              const SizedBox(height: 4),
              Expanded(
                child: Container(
                  color: const Color(0xFF101418),
                  padding: const EdgeInsets.all(8),
                  child: ListView.builder(
                    itemCount: _received.length,
                    itemBuilder: (BuildContext _, int i) => Text(
                      _received[i],
                      style: const TextStyle(
                        color: Color(0xFFB8C0CC),
                        fontFamily: 'monospace',
                        fontSize: 12,
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
