import '../managers/ble_manager.dart';
import 'transport_regression.dart';

/// Exercise the actual product manager, not a test-specific wire implementation.
class BleRegressionAdapter implements RegressionTransport {
  BleRegressionAdapter(this.ble);
  final BleManager ble;
  @override
  bool get isReady => ble.isReady && ble.diagnosticMode;
  @override
  Future<void> verifyBleEcho() => ble.verifyBleEchoChecked();
  @override
  Future<RegressionReply> request(String topic, String value) async {
    final event = await ble.requestDiagnosticChecked(topic, value);
    return RegressionReply(
        topic: event.topic,
        value: event.value,
        requestId: event.requestId ?? '');
  }

  @override
  Future<void> reconnect() => ble.reconnectForDiagnostics();
}
