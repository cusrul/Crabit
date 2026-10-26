// Standard-library-only client for the opt-in Flutter diagnostic VM extension.
// Read the local service URI from a file, never print its authentication path.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

Future<void> main(List<String> args) async {
  if (args.length != 2 ||
      !const {
        'status',
        'connect',
        'quick',
        'reconnect',
        'boundaries',
        'soak',
        'cancel',
      }.contains(args[1])) {
    stderr.writeln(
      'Usage: dart tools/mobile_regression_client.dart <local-uri-file> <command>',
    );
    exitCode = 64;
    return;
  }
  WebSocket? socket;
  try {
    final uri = Uri.parse((await File(args[0]).readAsString()).trim());
    if (!const {'127.0.0.1', 'localhost', '::1'}.contains(uri.host) ||
        !const {'http', 'ws'}.contains(uri.scheme)) {
      throw const FormatException('loopback_only');
    }
    final path = uri.path.endsWith('/ws')
        ? uri.path
        : '${uri.path.endsWith('/') ? uri.path : '${uri.path}/'}ws';
    socket = await WebSocket.connect(
      uri.replace(scheme: 'ws', path: path).toString(),
    ).timeout(const Duration(seconds: 10));
    final incoming = socket
        .map((raw) => jsonDecode(raw as String) as Map<String, dynamic>)
        .asBroadcastStream();
    int nextId = 1;
    Future<Map<String, dynamic>> rpc(
      String method, [
      Map<String, Object?> params = const {},
    ]) async {
      final id = '${nextId++}';
      final response = incoming
          .firstWhere((m) => m['id'] == id)
          .timeout(const Duration(seconds: 10));
      socket!.add(
        jsonEncode({
          'jsonrpc': '2.0',
          'id': id,
          'method': method,
          'params': params,
        }),
      );
      final message = await response;
      if (message.containsKey('error'))
        throw const FormatException('rpc_error');
      return message['result'] as Map<String, dynamic>;
    }

    final vm = await rpc('getVM');
    for (final isolate in vm['isolates'] as List<dynamic>) {
      final info = await rpc('getIsolate', {'isolateId': isolate['id']});
      if (!(info['extensionRPCs'] as List<dynamic>? ?? []).contains(
        'ext.crabit.transport',
      ))
        continue;
      final result = await rpc('ext.crabit.transport', {
        'isolateId': isolate['id'],
        'command': args[1],
      });
      stdout.writeln(jsonEncode(result));
      return;
    }
    stderr.writeln(
      'Diagnostic extension unavailable; use transport_diagnostics_main.dart in debug mode.',
    );
    exitCode = 2;
  } catch (_) {
    // Native/RPC exception text can contain the VM service authentication URI.
    stderr.writeln(
      'Diagnostic command failed; check the local Flutter process and URI file.',
    );
    exitCode = 1;
  } finally {
    await socket?.close();
  }
}
