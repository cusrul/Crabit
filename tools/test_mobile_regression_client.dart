import 'dart:async';
import 'dart:convert';
import 'dart:io';

// Standalone local mock VM-service tests. Never scans or accesses Bluetooth.
Future<void> main() async {
  final temp = await Directory.systemTemp.createTemp('crabit-vm-client-test-');
  final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
  final uriFile = File('${temp.path}/service-uri');
  final sockets = <WebSocket>[];
  var extensionCalls = 0;
  server.listen((request) async {
    final socket = await WebSocketTransformer.upgrade(request);
    sockets.add(socket);
    socket.listen((raw) {
      final rpc = jsonDecode(raw as String) as Map<String, dynamic>;
      Object result;
      switch (rpc['method']) {
        case 'getVM':
          result = {
            'isolates': [
              {'id': 'isolates/1'},
            ],
          };
        case 'getIsolate':
          result = {
            'extensionRPCs': ['ext.crabit.transport'],
          };
        case 'ext.crabit.transport':
          extensionCalls++;
          result = {'ready': false, 'command': rpc['params']['command']};
        default:
          result = {};
      }
      socket.add(
        jsonEncode({'jsonrpc': '2.0', 'id': rpc['id'], 'result': result}),
      );
    });
  });
  Future<ProcessResult> run() => Process.run(Platform.resolvedExecutable, [
    File.fromUri(Platform.script.resolve('mobile_regression_client.dart')).path,
    uriFile.path,
    'status',
  ]).timeout(const Duration(seconds: 20));
  void check(bool condition, String message) {
    if (!condition) throw StateError(message);
  }

  try {
    await uriFile.writeAsString(
      'http://127.0.0.1:${server.port}/private-debug-auth/',
    );
    final success = await run();
    check(
      success.exitCode == 0 && extensionCalls == 1,
      'mock RPC must succeed',
    );
    check(
      jsonDecode(success.stdout as String)['ready'] == false,
      'actual result must survive',
    );
    check(
      !success.stdout.toString().contains('private-debug-auth'),
      'do not print auth URI',
    );
    await uriFile.writeAsString('http://example.invalid/private-debug-auth/');
    final remote = await run();
    check(
      remote.exitCode == 1 && extensionCalls == 1,
      'non-loopback must be rejected before connecting',
    );
    await uriFile.writeAsString('not a service URI');
    final invalid = await run();
    check(
      invalid.exitCode == 1 &&
          !invalid.stderr.toString().contains('private-debug-auth'),
      'invalid URI must fail sanitized',
    );
    stdout.writeln(
      'Mobile regression client: 3/3 local mock checks passed (no Bluetooth).',
    );
  } finally {
    for (final socket in sockets) {
      await socket.close();
    }
    await server.close(force: true);
    if (temp.absolute.parent.path == Directory.systemTemp.absolute.path &&
        temp.uri.pathSegments
            .where((s) => s.isNotEmpty)
            .last
            .startsWith('crabit-vm-client-test-')) {
      await temp.delete(recursive: true);
    }
  }
}
