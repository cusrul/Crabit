import 'dart:async';

import 'package:crabit_watch_app/core/device/device_link.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';

class FakeBleManager extends Fake implements BleManager {
  @override
  final isConnected = ValueNotifier<bool>(false);
  @override
  final isAuthenticated = ValueNotifier<bool>(false);
  @override
  final isScanning = ValueNotifier<bool>(false);
  @override
  final pairTransferStatus = ValueNotifier<PairTransferStatus>(
    const PairTransferStatus(stage: PairTransferStage.idle),
  );

  bool ready = false;
  String? accessCode;
  int startCalls = 0;
  int waitCalls = 0;
  int disconnectCalls = 0;
  bool? lastDiagnosticMode;
  bool? lastChecked;
  Future<void> Function()? onStartScanAndConnect;
  Future<void> Function()? onWaitForReady;
  Future<void> Function()? onDisconnect;
  Future<DiagnosticEvent> Function(String topic, String value)? onRead;

  @override
  bool get isReady => ready;

  @override
  String? get bleAccessCode => accessCode;

  @override
  Future<void> saveBleAccessCode(String code) async {
    accessCode = code;
  }

  @override
  Future<void> connectProduct() async {
    startCalls++;
    lastDiagnosticMode = false;
    await onStartScanAndConnect?.call();
  }

  @override
  Future<void> startScanAndConnect({bool diagnosticMode = false}) async {
    startCalls++;
    lastDiagnosticMode = diagnosticMode;
    await onStartScanAndConnect?.call();
  }

  @override
  Future<void> waitForReady(
      {Duration timeout = const Duration(seconds: 35)}) async {
    waitCalls++;
    await onWaitForReady?.call();
  }

  @override
  Future<void> disconnect({bool checked = false}) async {
    disconnectCalls++;
    lastChecked = checked;
    await onDisconnect?.call();
  }

  @override
  Future<void> verifyPairTransfer() async {}

  @override
  Future<DiagnosticEvent> requestDiagnosticChecked(
    String topic,
    String value, {
    Duration timeout = const Duration(seconds: 4),
  }) async {
    final handler = onRead;
    if (handler != null) return handler(topic, value);
    return DiagnosticEvent(topic: topic, value: value);
  }
}

Matcher transportFailure(TransportErrorCode code) =>
    isA<TransportException>().having((error) => error.code, 'code', code);

void main() {
  late FakeBleManager manager;
  late BleDeviceLink link;
  var linkDisposed = false;

  setUp(() {
    manager = FakeBleManager();
    link = BleDeviceLink(manager);
    linkDisposed = false;
  });

  tearDown(() {
    if (!linkDisposed) link.dispose();
  });

  test('construction and disposal do not scan, connect, read, or disconnect',
      () {
    expect(manager.startCalls, 0);
    expect(manager.waitCalls, 0);
    expect(manager.disconnectCalls, 0);
    link.dispose();
    linkDisposed = true;
    expect(manager.startCalls, 0);
    expect(manager.disconnectCalls, 0);
  });

  test('manager notifiers propagate and are detached on dispose', () {
    var notifications = 0;
    link.addListener(() => notifications++);
    manager.isConnected.value = true;
    manager.isAuthenticated.value = true;
    manager.isScanning.value = true;
    manager.pairTransferStatus.value = const PairTransferStatus(
      stage: PairTransferStage.verifying,
    );
    expect(link.connected, isTrue);
    expect(link.authenticated, isTrue);
    expect(link.scanning, isTrue);
    expect(link.pairing.stage, PairTransferStage.verifying);
    expect(notifications, 4);
    link.dispose();
    linkDisposed = true;
    manager.isAuthenticated.value = false;
    expect(notifications, 4);
    expect(manager.disconnectCalls, 0);
  });

  test('connect starts product mode immediately and waits for readiness',
      () async {
    await link.connect();
    expect(manager.startCalls, 1);
    expect(manager.lastDiagnosticMode, isFalse);
    expect(manager.waitCalls, 1);
  });

  test('overlapping connect is BUSY and does not start another scan', () async {
    final pendingScan = Completer<void>();
    manager.onStartScanAndConnect = () => pendingScan.future;
    final first = link.connect();
    expect(manager.startCalls, 1);
    expect(manager.waitCalls, 0);
    await expectLater(
        link.connect(), throwsA(transportFailure(TransportErrorCode.busy)));
    expect(manager.startCalls, 1);
    pendingScan.complete();
    await first;
    expect(manager.waitCalls, 1);
  });

  test('checked disconnect cancels pending connect without late rescan',
      () async {
    final pendingScan = Completer<void>();
    manager.onStartScanAndConnect = () => pendingScan.future;
    final first = link.connect();
    await link.disconnect();
    expect(manager.disconnectCalls, 1);
    expect(manager.lastChecked, isTrue);

    pendingScan.complete();
    await first;
    expect(manager.startCalls, 1,
        reason: 'settling the cancelled operation must not start a new scan');
    expect(manager.waitCalls, 1);

    manager.onStartScanAndConnect = null;
    await link.connect();
    expect(manager.startCalls, 2,
        reason:
            'a fresh explicit attempt is allowed after the old one settles');
    expect(manager.waitCalls, 2);
  });

  test('wait-for-ready failure propagates and releases the connection fence',
      () async {
    manager.onWaitForReady = () => Future<void>.error(
          const TransportException(TransportErrorCode.timeout),
        );
    await expectLater(
        link.connect(), throwsA(transportFailure(TransportErrorCode.timeout)));
    expect(manager.startCalls, 1);
    expect(manager.waitCalls, 1);

    manager.onWaitForReady = null;
    await link.connect();
    expect(manager.startCalls, 2);
  });

  test('diagnostic read passes topic and value through unchanged', () async {
    final expected = DiagnosticEvent(topic: 'diag/wifi', value: 'connected');
    manager.onRead = (topic, value) async {
      expect(topic, 'diag/wifi');
      expect(value, '');
      return expected;
    };
    expect(await link.read('diag/wifi', ''), same(expected));
  });
}
