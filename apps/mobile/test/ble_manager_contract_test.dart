import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';
import 'package:flutter_test/flutter_test.dart';

Matcher failure(TransportErrorCode code) =>
    isA<TransportException>().having((error) => error.code, 'code', code);

void main() {
  final manager = BleManager();

  test('diag/imu is allowed without widening diagnostic topics', () async {
    await expectLater(
      manager.requestDiagnosticChecked('diag/imu', ''),
      throwsA(failure(TransportErrorCode.notConnected)),
    );
    await expectLater(
      manager.requestDiagnosticChecked('sys/settings', ''),
      throwsA(failure(TransportErrorCode.invalidPayload)),
    );
  });

  test('device requests allow only correlated settings and RAM USB topics',
      () async {
    for (final topic in ['sys/settings', 'sys/settings/set', 'app/usb']) {
      await expectLater(
        manager.requestDeviceChecked(topic, ''),
        throwsA(failure(TransportErrorCode.notConnected)),
      );
    }
    for (final topic in [
      'diag/imu',
      'sys/settings/get',
      'sys/time',
      'ack',
      'app/badusb'
    ]) {
      await expectLater(
        manager.requestDeviceChecked(topic, ''),
        throwsA(failure(TransportErrorCode.invalidPayload)),
      );
    }
  });
}
