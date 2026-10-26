import 'dart:convert';

import 'ble_transport.dart';

const bleAccessCodeDigits = 6;
const bleAuthenticationTimeout = Duration(seconds: 15);

bool isValidBleAccessCode(String value) =>
    RegExp('^[0-9]{$bleAccessCodeDigits}\$').hasMatch(value);

String bleAuthenticationCommand(String code) {
  if (!isValidBleAccessCode(code) ||
      utf8.encode('AUTH:$code').length > BleTransport.chunkBytes) {
    throw const TransportException(TransportErrorCode.invalidPayload);
  }
  return 'AUTH:$code';
}
