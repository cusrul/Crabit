import 'package:flutter/foundation.dart';
import '../managers/ble_manager.dart';
import 'settings_protocol.dart';

/// Injectable boundary; no scan, connect, token, retry or transport ownership.
abstract class SettingsGateway implements Listenable {
  bool get ready;
  Future<SettingsReply> read();
  Future<SettingsReply> apply(Map<String, int> patch);
}

class BleSettingsGateway extends ChangeNotifier implements SettingsGateway {
  BleSettingsGateway(this._ble) {
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
  Future<SettingsReply> read() => _request(settingsReadTopic, 'read');
  @override
  Future<SettingsReply> apply(Map<String, int> patch) =>
      _request(settingsSetTopic, encodeSettingsPatch(patch));
  Future<SettingsReply> _request(String topic, String value) async {
    if (!ready) throw StateError('Settings offline');
    // Device-scoped checked requests preserve the business reply correlation;
    // settings are not diagnostic traffic and must not use the diag allowlist.
    final event = await _ble.requestDeviceChecked(topic, value);
    final id = int.tryParse(event.requestId ?? '');
    if (event.topic != topic || id == null || id < 1 || id > 65535) {
      throw const FormatException('Uncorrelated settings reply');
    }
    final reply = SettingsReply.parse(event.value);
    if ((topic == settingsReadTopic &&
            reply.result != 'snapshot' &&
            reply.result != 'invalid') ||
        (topic == settingsSetTopic && reply.result == 'snapshot')) {
      throw const FormatException('Unexpected settings result');
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
