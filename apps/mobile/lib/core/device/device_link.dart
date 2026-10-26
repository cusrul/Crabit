import 'package:flutter/foundation.dart';

import '../managers/ble_manager.dart';
import '../transport/ble_transport.dart';

/// Device-center boundary. Opening or disposing a view never operates the radio.
abstract class DeviceLink implements Listenable {
  bool get connected;
  bool get authenticated;
  bool get scanning;
  bool get ready;
  PairTransferStatus get pairing;
  String? get bleAccessCode;

  Future<void> connect();
  Future<void> saveBleAccessCode(String code);
  Future<void> disconnect();
  Future<void> verify();
  Future<DiagnosticEvent> read(String topic, String value);
}

class BleDeviceLink extends ChangeNotifier implements DeviceLink {
  BleDeviceLink(this._ble) {
    for (final source in _sources) {
      source.addListener(notifyListeners);
    }
  }

  final BleManager _ble;
  Future<void>? _connection;
  Iterable<Listenable> get _sources => [
        _ble.isConnected,
        _ble.isAuthenticated,
        _ble.isScanning,
        _ble.pairTransferStatus,
      ];

  @override
  bool get connected => _ble.isConnected.value;
  @override
  bool get authenticated => _ble.isAuthenticated.value;
  @override
  bool get scanning => _ble.isScanning.value;
  @override
  bool get ready => _ble.isReady;
  @override
  PairTransferStatus get pairing => _ble.pairTransferStatus.value;
  @override
  String? get bleAccessCode => _ble.bleAccessCode;

  @override
  Future<void> connect() async {
    // Do not schedule a new scan after waiting for a cancelled operation. The
    // user can retry explicitly once the previous connection call has settled.
    if (_connection != null) {
      throw const TransportException(TransportErrorCode.busy);
    }
    final operation = _connect();
    _connection = operation;
    try {
      await operation;
    } finally {
      if (identical(_connection, operation)) _connection = null;
    }
  }

  @override
  Future<void> saveBleAccessCode(String code) => _ble.saveBleAccessCode(code);

  Future<void> _connect() async {
    // Start immediately under the manager's existing epoch/teardown guards.
    // No preparation await is allowed before this cancellable manager call.
    await _ble.connectProduct();
    // Native connection kickoff finishes before device-code auth and STM32 echo.
    // Require verified readiness rather than treating kickoff as success.
    await _ble.waitForReady(timeout: const Duration(seconds: 20));
  }

  @override
  Future<void> disconnect() => _ble.disconnect(checked: true);
  @override
  Future<void> verify() => _ble.verifyPairTransfer();
  @override
  Future<DiagnosticEvent> read(String topic, String value) =>
      _ble.requestDiagnosticChecked(topic, value);

  @override
  void dispose() {
    for (final source in _sources) {
      source.removeListener(notifyListeners);
    }
    super.dispose();
  }
}
