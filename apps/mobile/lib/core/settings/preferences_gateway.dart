import 'package:flutter/foundation.dart';

import '../managers/ble_manager.dart';
import 'preferences_protocol.dart';

/// Injectable boundary for watch preferences. It owns no connection lifecycle.
abstract interface class PreferencesGateway implements Listenable {
  bool get ready;
  Future<PreferencesReply> read();
  Future<PreferencesReply> apply(Map<String, int> patch);
}

class BlePreferencesGateway extends ChangeNotifier
    implements PreferencesGateway {
  BlePreferencesGateway(this._ble) {
    for (final source in _sources) {
      source.addListener(notifyListeners);
    }
  }

  final BleManager _ble;

  Iterable<Listenable> get _sources =>
      [_ble.isConnected, _ble.isAuthenticated, _ble.pairTransferStatus];

  @override
  bool get ready => _ble.isProductReady;

  @override
  Future<PreferencesReply> read() => _request(preferencesReadTopic, 'read');

  @override
  Future<PreferencesReply> apply(Map<String, int> patch) =>
      _request(preferencesSetTopic, encodePreferencesPatch(patch));

  Future<PreferencesReply> _request(String topic, String value) async {
    if (!ready) throw StateError('Preferences offline');

    // Use the product checked path so the response remains correlated and
    // authenticated; preference traffic is not diagnostic allowlist traffic.
    final event = await _ble.requestDeviceChecked(topic, value);
    final id = int.tryParse(event.requestId ?? '');
    if (event.topic != topic || id == null || id < 1 || id > 65535) {
      throw const FormatException('Uncorrelated preferences reply');
    }

    final reply = PreferencesReply.parse(event.value);
    final validResult = topic == preferencesReadTopic
        ? const {'snapshot', 'busy', 'invalid'}.contains(reply.result)
        : const {'saved', 'busy', 'invalid', 'save_failed'}
            .contains(reply.result);
    if (!validResult) {
      throw const FormatException('Unexpected preferences result');
    }
    return reply;
  }

  @override
  void dispose() {
    for (final source in _sources) {
      source.removeListener(notifyListeners);
    }
    super.dispose();
  }
}
