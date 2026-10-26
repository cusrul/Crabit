import 'package:flutter/foundation.dart';

import '../managers/ble_manager.dart';
import 'battery_history_controller.dart';

/// Adapts the existing authenticated transport; never opens a BLE session.
class BleBatteryGateway extends ChangeNotifier
    implements BatteryHistoryGateway {
  BleBatteryGateway(this.ble) {
    for (final source in _sources) {
      source.addListener(notifyListeners);
    }
  }

  final BleManager ble;
  bool _foreground = true;
  List<Listenable> get _sources => [
        ble.isConnected,
        ble.isAuthenticated,
        ble.pairTransferStatus,
      ];

  void setForeground(bool value) {
    if (_foreground == value) return;
    _foreground = value;
    notifyListeners();
  }

  @override
  bool get ready => _foreground && ble.isProductReady;
  @override
  String? get deviceId => ble.connectedDeviceId;

  @override
  Future<String> requestBattery() async {
    if (!ready) throw StateError('Battery read requires a ready connection');
    final reply = await ble.requestDeviceChecked('diag/battery', 'read');
    final id = int.tryParse(reply.requestId ?? '');
    if (reply.topic != 'diag/battery' || id == null || id < 1 || id > 65535) {
      throw const FormatException('Uncorrelated battery reply');
    }
    return reply.value;
  }

  @override
  void dispose() {
    for (final source in _sources) {
      source.removeListener(notifyListeners);
    }
    super.dispose();
  }
}
