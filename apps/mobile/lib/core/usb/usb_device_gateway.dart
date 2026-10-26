import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:flutter/foundation.dart';

const String usbDeviceTopic = 'app/usb';

enum UsbDeviceResult {
  snapshot,
  accepted,
  ready,
  queued,
  stopped,
  cleared,
  busy,
  invalid,
  unavailable,
  failed,
}

enum UsbRamState {
  empty,
  uploading,
  ready,
  running,
  done,
  failed,
  stopping,
  stopped,
}

class UsbDeviceReply {
  const UsbDeviceReply({
    required this.requestId,
    required this.result,
    required this.state,
    required this.id,
    required this.received,
    required this.length,
    required this.error,
    required this.usbConnected,
  });

  final String requestId;
  final UsbDeviceResult result;
  final UsbRamState state;
  final String id;
  final int received;
  final int length;
  final String error;
  final bool usbConnected;

  bool get hasScript => id != '00000000';

  static UsbDeviceReply fromEvent(DiagnosticEvent event) {
    if (event.topic != usbDeviceTopic) {
      throw const FormatException('USB 设备回复主题不匹配。');
    }
    final requestId = event.requestId;
    if (requestId == null || !RegExp(r'^[0-9]+$').hasMatch(requestId)) {
      throw const FormatException('USB 设备回复 request_id 必须是十进制数字。');
    }
    final numericRequestId = int.tryParse(requestId);
    if (numericRequestId == null ||
        numericRequestId < 1 ||
        numericRequestId > 65535) {
      throw const FormatException('USB 设备回复 request_id 超出范围。');
    }

    final fields = <String, String>{};
    for (final part in event.value.split(';')) {
      final separator = part.indexOf('=');
      if (separator <= 0 || separator == part.length - 1) {
        throw const FormatException('USB 设备回复字段格式无效。');
      }
      final key = part.substring(0, separator);
      final value = part.substring(separator + 1);
      if (fields.containsKey(key)) {
        throw const FormatException('USB 设备回复包含重复字段。');
      }
      fields[key] = value;
    }

    const required = {
      'result',
      'state',
      'id',
      'received',
      'length',
      'error',
      'usb',
    };
    if (fields.length != required.length ||
        !fields.keys.toSet().containsAll(required)) {
      throw const FormatException('USB 设备回复字段不完整或包含未知字段。');
    }

    final result =
        UsbDeviceResult.values.where((item) => item.name == fields['result']);
    final state =
        UsbRamState.values.where((item) => item.name == fields['state']);
    if (result.length != 1 || state.length != 1) {
      throw const FormatException('USB 设备回复 result/state 无效。');
    }
    final id = fields['id']!;
    if (!RegExp(r'^(?:[0-9a-f]{8}|00000000)$').hasMatch(id)) {
      throw const FormatException('USB 设备回复脚本 id 无效。');
    }
    final received = _parseUnsigned(fields['received']!);
    final length = _parseUnsigned(fields['length']!);
    if (received > 512 || length > 512 || received > length) {
      throw const FormatException('USB 设备回复长度超出范围。');
    }
    final error = fields['error']!;
    if (error != 'none' && !RegExp(r'^[A-Za-z0-9_-]{1,32}$').hasMatch(error)) {
      throw const FormatException('USB 设备回复 error 字段无效。');
    }
    if (fields['usb'] != '0' && fields['usb'] != '1') {
      throw const FormatException('USB 设备回复 usb 字段无效。');
    }

    return UsbDeviceReply(
      requestId: requestId,
      result: result.single,
      state: state.single,
      id: id,
      received: received,
      length: length,
      error: error,
      usbConnected: fields['usb'] == '1',
    );
  }

  static int _parseUnsigned(String value) {
    if (!RegExp(r'^\d{1,3}$').hasMatch(value)) {
      throw const FormatException('USB 设备回复数字字段无效。');
    }
    return int.parse(value);
  }
}

abstract interface class UsbDeviceGateway {
  bool get isProductReady;

  Future<UsbDeviceReply> request(String command);
}

class BleUsbDeviceGateway implements UsbDeviceGateway {
  BleUsbDeviceGateway({BleManager? manager})
      : _manager = manager ?? BleManager();

  final BleManager _manager;

  @override
  bool get isProductReady => _manager.isProductReady;

  @override
  Future<UsbDeviceReply> request(String command) async {
    if (!isProductReady) {
      throw StateError('需要已验证的 Crabit 产品连接。');
    }
    final watch = bleTimingEnabled ? (Stopwatch()..start()) : null;
    var completed = false;
    try {
      final event =
          await _manager.requestDeviceChecked(usbDeviceTopic, command);
      final reply = UsbDeviceReply.fromEvent(event);
      completed = true;
      return reply;
    } finally {
      if (watch != null) {
        // Never log command contents: upload chunks may contain private text.
        debugPrint(
            '[USB_TIMING] phase=${command.split(':').first} us=${watch.elapsedMicroseconds} replied=$completed');
      }
    }
  }
}
