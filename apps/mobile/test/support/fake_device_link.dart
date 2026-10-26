import 'package:crabit_watch_app/core/device/device_link.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:flutter/foundation.dart';

/// Deterministic injected transport fake. It intentionally performs no BLE I/O.
class FakeDeviceLink extends ChangeNotifier implements DeviceLink {
  bool connectedValue = false;
  bool authenticatedValue = false;
  bool scanningValue = false;
  bool readyValue = false;
  PairTransferStatus pairingValue =
      const PairTransferStatus(stage: PairTransferStage.idle);
  String? accessCodeValue;

  Future<void> Function()? onConnect;
  Future<void> Function()? onDisconnect;
  Future<void> Function()? onVerify;
  Future<DiagnosticEvent> Function(String topic, String value)? onRead;

  int connectCalls = 0;
  int disconnectCalls = 0;
  int verifyCalls = 0;
  final List<(String, String)> reads = [];

  @override
  String? get bleAccessCode => accessCodeValue;

  @override
  bool get connected => connectedValue;
  @override
  bool get authenticated => authenticatedValue;
  @override
  bool get scanning => scanningValue;
  @override
  bool get ready => readyValue;
  @override
  PairTransferStatus get pairing => pairingValue;

  void setState({
    bool? connected,
    bool? authenticated,
    bool? scanning,
    bool? ready,
    PairTransferStatus? pairing,
  }) {
    connectedValue = connected ?? connectedValue;
    authenticatedValue = authenticated ?? authenticatedValue;
    scanningValue = scanning ?? scanningValue;
    readyValue = ready ?? readyValue;
    pairingValue = pairing ?? pairingValue;
    notifyListeners();
  }

  @override
  Future<void> connect() async {
    connectCalls++;
    await onConnect?.call();
  }

  @override
  Future<void> saveBleAccessCode(String code) async {
    accessCodeValue = code;
    notifyListeners();
  }

  @override
  Future<void> disconnect() async {
    disconnectCalls++;
    await onDisconnect?.call();
  }

  @override
  Future<void> verify() async {
    verifyCalls++;
    await onVerify?.call();
  }

  @override
  Future<DiagnosticEvent> read(String topic, String value) async {
    reads.add((topic, value));
    final handler = onRead;
    if (handler != null) return handler(topic, value);
    return DiagnosticEvent(topic: topic, value: value);
  }
}
