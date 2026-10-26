import 'dart:convert';
import 'dart:developer' as developer;
import 'package:flutter/foundation.dart';

/// A debug VM-service extension, not a new network server or watch protocol.
/// The regular product entry never enables it. UI and automation share actions.
class TransportAutomation {
  static bool _enabled = false;
  static Object? _owner;
  static Map<String, Object?> Function()? _snapshot;
  static Map<String, Object?> Function(String command)? _action;

  static void enable() {
    if (!kDebugMode || _enabled) return;
    _enabled = true;
    developer.registerExtension('ext.crabit.transport', (_, parameters) async {
      final command = parameters['command'] ?? 'status';
      final result = command == 'status'
          ? (_snapshot?.call() ?? {'available': false})
          : (_action?.call(command) ??
              {'accepted': false, 'reason': 'page_closed'});
      return developer.ServiceExtensionResponse.result(jsonEncode(result));
    });
  }

  static void attach(
    Object owner, {
    required Map<String, Object?> Function() snapshot,
    required Map<String, Object?> Function(String command) action,
  }) {
    if (!_enabled) return;
    _owner = owner;
    _snapshot = snapshot;
    _action = action;
  }

  static void detach(Object owner) {
    if (!identical(_owner, owner)) return;
    _owner = null;
    _snapshot = null;
    _action = null;
  }
}
