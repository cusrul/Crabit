import 'package:crabit_watch_app/core/transport/ble_auth.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('accepts exactly six digits and encodes one short GATT write', () {
    expect(isValidBleAccessCode('123456'), isTrue);
    expect(bleAuthenticationCommand('123456'), 'AUTH:123456');
    expect('AUTH:123456'.length, lessThanOrEqualTo(BleTransport.chunkBytes));
  });

  test('rejects malformed codes before transport writes', () {
    for (final code in ['', '12345', '1234567', '12a456', '１２３４５６']) {
      expect(isValidBleAccessCode(code), isFalse);
      expect(
          () => bleAuthenticationCommand(code),
          throwsA(isA<TransportException>().having(
            (error) => error.code,
            'code',
            TransportErrorCode.invalidPayload,
          )));
    }
  });
}
