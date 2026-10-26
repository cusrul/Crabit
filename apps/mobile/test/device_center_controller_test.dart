import 'dart:async';

import 'package:crabit_watch_app/core/device/device_center_controller.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';
import 'package:flutter_test/flutter_test.dart';

import 'support/fake_device_link.dart';

void main() {
  late FakeDeviceLink link;
  late DeviceCenterController controller;
  var controllerDisposed = false;

  setUp(() {
    link = FakeDeviceLink();
    controller = DeviceCenterController(link: link);
    controllerDisposed = false;
  });
  tearDown(() {
    if (!controllerDisposed) controller.dispose();
  });

  test('ready requires connection, authentication, and verified transfer', () {
    link.setState(ready: true, authenticated: true);
    expect(controller.phase, DevicePhase.disconnected);
    link.setState(connected: true, ready: true);
    expect(controller.phase, isNot(DevicePhase.ready));
    link.setState(
      authenticated: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    expect(controller.phase, DevicePhase.ready);
  });

  test('independent inconsistent link flags never imply false readiness', () {
    for (final state in [
      {'connected': true},
      {'authenticated': true},
      {'ready': true},
      {'connected': true, 'authenticated': true},
      {'connected': true, 'ready': true},
      {
        'connected': true,
        'authenticated': true,
        'ready': true,
        'scanning': true
      },
    ]) {
      final isolated = FakeDeviceLink();
      isolated.setState(
        connected: state['connected'],
        authenticated: state['authenticated'],
        ready: state['ready'],
        scanning: state['scanning'],
      );
      final subject = DeviceCenterController(link: isolated);
      expect(subject.phase, isNot(DevicePhase.ready), reason: '$state');
      subject.dispose();
    }
  });

  test('connect calls link once and rejects duplicate action while pending',
      () async {
    final gate = Completer<void>();
    link.onConnect = () => gate.future;
    final first = controller.connect();
    await controller.connect();
    expect(link.connectCalls, 1);
    gate.complete();
    await first;
  });

  test('diagnostics are manual, whitelisted, and use exact request values',
      () async {
    expect(link.connectCalls, 0);
    expect(link.reads, isEmpty);
    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    for (final topic in ['diag/echo', 'diag/snapshot', 'diag/wifi']) {
      await controller.readDiagnostic(topic);
    }
    expect(link.reads, [
      ('diag/echo', 'crabit-check'),
      ('diag/snapshot', ''),
      ('diag/wifi', ''),
    ]);
    await controller.readDiagnostic('sys/time');
    expect(link.reads, hasLength(3));
    expect(controller.error, isNotNull);
  });

  test('transport timeout/failure is surfaced as controller error', () async {
    link.setState(connected: true, authenticated: true);
    link.onVerify = () async {
      link.setState(
        pairing: const PairTransferStatus(stage: PairTransferStage.failed),
      );
      throw const TransportException(TransportErrorCode.timeout);
    };
    await controller.verify();
    expect(controller.error, contains('超时'));
    expect(controller.phase, DevicePhase.failed);
    expect(controller.busy, isFalse);
  });

  test('disconnect invalidates pending read across a fast reconnect', () async {
    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    final response = Completer<DiagnosticEvent>();
    final teardown = Completer<void>();
    link.onRead = (_, __) => response.future;
    link.onDisconnect = () => teardown.future;
    final read = controller.readDiagnostic('diag/snapshot');
    final disconnect = controller.disconnect();
    expect(controller.disconnecting, isTrue);
    expect(controller.busy, isTrue);
    link.setState(
      connected: false,
      authenticated: false,
      ready: false,
      pairing: const PairTransferStatus(stage: PairTransferStage.idle),
    );
    response.complete(DiagnosticEvent(topic: 'diag/snapshot', value: 'stale'));
    await read;
    expect(controller.resultFor('diag/snapshot'), isNull);
    expect(controller.busy, isTrue,
        reason: 'old operation must not clear teardown busy state');
    teardown.complete();
    await disconnect;
    expect(controller.disconnecting, isFalse);
    expect(controller.busy, isFalse);

    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    link.onRead = (topic, value) async =>
        DiagnosticEvent(topic: topic, value: 'fresh:$value');
    await controller.readDiagnostic('diag/snapshot');
    expect(controller.resultFor('diag/snapshot')!.event.value, 'fresh:');
  });

  test('authentication loss discards result and a new session starts clear',
      () async {
    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    final response = Completer<DiagnosticEvent>();
    link.onRead = (_, __) => response.future;
    final read = controller.readDiagnostic('diag/snapshot');
    link.setState(authenticated: false);
    response.complete(DiagnosticEvent(topic: 'diag/snapshot', value: 'late'));
    await read;
    expect(controller.resultFor('diag/snapshot'), isNull);
    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    link.onRead = (topic, value) async =>
        DiagnosticEvent(topic: topic, value: 'fresh:$value');
    await controller.readDiagnostic('diag/snapshot');
    expect(controller.resultFor('diag/snapshot')!.event.value, 'fresh:');
  });

  test('dispose while action pending is safe and does not own injected link',
      () async {
    final gate = Completer<void>();
    link.onConnect = () => gate.future;
    final pending = controller.connect();
    controller.dispose();
    controllerDisposed = true;
    gate.complete();
    await pending;
    expect(link.connectCalls, 1);
    expect(link.disconnectCalls, 0);
    link.setState(
        connected: true); // still usable; controller did not dispose it
  });

  test('old failure after cancellation cannot release a newer read', () async {
    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    final oldReply = Completer<DiagnosticEvent>();
    link.onRead = (_, __) => oldReply.future;
    final oldRead = controller.readDiagnostic('diag/snapshot');
    await controller.disconnect();
    final newReply = Completer<DiagnosticEvent>();
    link.onRead = (_, __) => newReply.future;
    final newRead = controller.readDiagnostic('diag/snapshot');
    oldReply
        .completeError(const TransportException(TransportErrorCode.timeout));
    await oldRead;
    expect(controller.busy, isTrue);
    expect(controller.error, isNull);
    newReply
        .complete(DiagnosticEvent(topic: 'diag/snapshot', value: 'current'));
    await newRead;
    expect(controller.resultFor('diag/snapshot')!.event.value, 'current');
    expect(controller.busy, isFalse);
  });

  test('timed out manual refresh cannot retain an earlier success', () async {
    link.setState(
      connected: true,
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
    );
    await controller.readDiagnostic('diag/snapshot');
    expect(controller.resultFor('diag/snapshot'), isNotNull);
    link.onRead = (_, __) async =>
        throw const TransportException(TransportErrorCode.timeout);
    await controller.readDiagnostic('diag/snapshot');
    expect(controller.resultFor('diag/snapshot'), isNull);
    expect(controller.error, contains('超时'));
    expect(link.reads, hasLength(2), reason: 'no automatic retransmission');
  });
}
