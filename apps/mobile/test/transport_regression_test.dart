import 'dart:async';
import 'dart:convert';

import 'package:crabit_watch_app/core/diagnostics/transport_regression.dart';
import 'package:flutter_test/flutter_test.dart';

class _FakeTransport implements RegressionTransport {
  bool ready = true;
  int bleCalls = 0;
  int requestCalls = 0;
  int reconnectCalls = 0;
  String? failAt;
  String? mismatchAt;
  String? duplicateId;
  String? emptyReplyAt;
  String? wrongTopicAt;
  Completer<void>? bleGate;
  Completer<void>? reconnectGate;
  void Function()? onRequest;
  final List<String> topics = [];
  final List<String> values = [];

  @override
  bool get isReady => ready;

  @override
  Future<void> verifyBleEcho() async {
    bleCalls++;
    if (failAt == 'ble:$bleCalls') throw StateError('sensitive token data');
    if (bleGate != null) await bleGate!.future;
  }

  @override
  Future<RegressionReply> request(String topic, String value) async {
    requestCalls++;
    topics.add(topic);
    values.add(value);
    onRequest?.call();
    if (failAt == 'request:$requestCalls') {
      throw StateError('private-address-and-token');
    }
    return RegressionReply(
      topic: wrongTopicAt == 'request:$requestCalls' ? 'diag/wrong' : topic,
      value: emptyReplyAt == 'request:$requestCalls'
          ? ''
          : topic == 'diag/echo'
              ? (mismatchAt == 'request:$requestCalls' ? '$value!' : value)
              : topic == 'diag/snapshot'
                  ? '{"temperature":24}'
                  : 'disconnected',
      requestId:
          duplicateId == 'request:$requestCalls' ? 'id-1' : 'id-$requestCalls',
    );
  }

  @override
  Future<void> reconnect() async {
    reconnectCalls++;
    if (failAt == 'reconnect:$reconnectCalls') {
      throw StateError('private transport detail');
    }
    if (reconnectGate != null) await reconnectGate!.future;
    ready = true;
  }
}

TransportRegressionRunner _runner(_FakeTransport fake,
        {RegressionProgress? onResult}) =>
    TransportRegressionRunner(
      transport: fake,
      runKind: RegressionRunKind.hostMock,
      onResult: onResult,
    );

void main() {
  test('quick has exact bounded counts and allowlisted diagnostic topics',
      () async {
    final fake = _FakeTransport();
    final summary = await _runner(fake).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.pass);
    expect(summary.completed, isTrue);
    expect(summary.runKind, RegressionRunKind.hostMock);
    expect(summary.passed, 42);
    expect(fake.bleCalls, 20);
    expect(fake.requestCalls, 22);
    expect(fake.topics.toSet(), {'diag/echo', 'diag/snapshot', 'diag/wifi'});
    expect(summary.steps.every((step) => step.elapsedMs >= 0), isTrue);
  });

  test('stops on first mismatch and never retries', () async {
    final fake = _FakeTransport()..mismatchAt = 'request:1';
    final summary = await _runner(fake).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.fail);
    expect(summary.failed, 1);
    expect(summary.steps, hasLength(2));
    expect(summary.steps[0].status, RegressionStatus.pass);
    expect(summary.steps[0].name, 'ble_echo_1');
    expect(summary.steps[1].status, RegressionStatus.fail);
    expect(summary.steps[1].errorCode, 'reply_mismatch');
    expect(fake.requestCalls, 1);
    expect(fake.bleCalls, 1);
  });

  test('exceptions are classified without exporting arbitrary error text',
      () async {
    final fake = _FakeTransport()..failAt = 'request:1';
    final summary = await TransportRegressionRunner(
      transport: fake,
      runKind: RegressionRunKind.hardware,
      classifyError: (_) => 'ble_timeout',
    ).run(RegressionProfile.quick);
    final json = summary.encodeJson();

    expect(summary.status, RegressionStatus.fail);
    expect(summary.steps, hasLength(2));
    expect(summary.steps[0].status, RegressionStatus.pass);
    expect(summary.steps[1].status, RegressionStatus.fail);
    expect(summary.steps[1].errorCode, 'ble_timeout');
    expect(json, contains('"runKind":"hardware"'));
    expect(json, isNot(contains('private-address')));
    expect(json, isNot(contains('token')));
    expect(json, isNot(contains('reg-1')));
    expect(jsonDecode(json), isA<Map<String, dynamic>>());
  });

  test('not-ready fails before any transport operation', () async {
    final fake = _FakeTransport()..ready = false;
    final summary = await _runner(fake).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.fail);
    expect(summary.steps.single.errorCode, 'not_ready');
    expect(fake.bleCalls + fake.requestCalls + fake.reconnectCalls, 0);
  });

  test('concurrent run is rejected and first run remains active', () async {
    final fake = _FakeTransport()..bleGate = Completer<void>();
    final runner = _runner(fake);
    final first = runner.run(RegressionProfile.quick);
    await Future<void>.delayed(Duration.zero);

    expect(runner.running, isTrue);
    await expectLater(runner.run(RegressionProfile.soak), throwsStateError);
    fake.bleGate!.complete();
    runner.cancel();
    final summary = await first;
    expect(summary.status, RegressionStatus.cancelled);
    expect(runner.running, isFalse);
  });

  test('cancellation during final awaited request is not reported as pass',
      () async {
    final fake = _FakeTransport();
    TransportRegressionRunner? runner;
    fake.onRequest = () {
      if (fake.requestCalls == 22) runner!.cancel();
    };
    runner = _runner(fake);
    final summary = await runner.run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.cancelled);
    expect(summary.completed, isFalse);
    expect(summary.cancelled, 1);
    expect(summary.steps.last.status, RegressionStatus.cancelled);
  });

  test('cancellation during reconnect stops before BLE probe and request',
      () async {
    final fake = _FakeTransport()..reconnectGate = Completer<void>();
    final runner = _runner(fake);
    final pending = runner.run(RegressionProfile.reconnect);
    await Future<void>.delayed(Duration.zero);
    runner.cancel();
    fake.reconnectGate!.complete();
    final summary = await pending;

    expect(summary.status, RegressionStatus.cancelled);
    expect(summary.steps, hasLength(1));
    expect(summary.steps.single.name, 'reconnect_1');
    expect(summary.steps.single.status, RegressionStatus.cancelled);
    expect(fake.bleCalls, 0);
    expect(fake.requestCalls, 0);
  });

  test('cancellation between operations starts no next transport operation',
      () async {
    late TransportRegressionRunner runner;
    runner = _runner(_FakeTransport(), onResult: (result) {
      if (result.name == 'ble_echo_1') runner.cancel();
    });
    final summary = await runner.run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.cancelled);
    expect(summary.steps, hasLength(2));
    expect(summary.steps[0].status, RegressionStatus.pass);
    expect(summary.steps[1].status, RegressionStatus.cancelled);
    expect(summary.steps[1].name, 'echo_1');
  });

  test('reconnect profile stops immediately on reconnect failure', () async {
    final fake = _FakeTransport()..failAt = 'reconnect:2';
    final summary = await _runner(fake).run(RegressionProfile.reconnect);

    expect(summary.status, RegressionStatus.fail);
    expect(fake.reconnectCalls, 2);
    expect(fake.bleCalls, 1);
    expect(fake.requestCalls, 1);
    expect(summary.failed, 1);
  });

  test('reconnect has its own successful step before probe and echo', () async {
    final fake = _FakeTransport();
    final summary = await _runner(fake).run(RegressionProfile.reconnect);

    expect(summary.status, RegressionStatus.pass);
    expect(summary.passed, 15);
    expect(fake.reconnectCalls, 5);
    expect(fake.bleCalls, 5);
    expect(fake.requestCalls, 5);
    expect(
        summary.steps.where((step) =>
            step.name.startsWith('reconnect_') &&
            !step.name.contains('ble') &&
            !step.name.contains('echo')),
        hasLength(5));
  });

  test('snapshot and Wi-Fi accept realistic nonempty diagnostic values',
      () async {
    final summary =
        await _runner(_FakeTransport()).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.pass);
    expect(summary.steps.firstWhere((step) => step.name == 'snapshot').status,
        RegressionStatus.pass);
    expect(
        summary.steps.firstWhere((step) => step.name == 'wifi_status').status,
        RegressionStatus.pass);
  });

  test('duplicate response request IDs stop the run', () async {
    final fake = _FakeTransport()..duplicateId = 'request:2';
    final summary = await _runner(fake).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.fail);
    expect(summary.steps.last.errorCode, 'duplicate_request_id');
    expect(fake.requestCalls, 2);
  });

  test('response topic mismatch stops the run', () async {
    final fake = _FakeTransport()..wrongTopicAt = 'request:1';
    final summary = await _runner(fake).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.fail);
    expect(summary.steps, hasLength(2));
    expect(summary.steps.last.errorCode, 'reply_mismatch');
    expect(fake.requestCalls, 1);
    expect(fake.bleCalls, 1);
  });

  test('empty non-echo diagnostic response fails', () async {
    final fake = _FakeTransport()..emptyReplyAt = 'request:21';
    final summary = await _runner(fake).run(RegressionProfile.quick);

    expect(summary.status, RegressionStatus.fail);
    expect(summary.steps.last.errorCode, 'empty_diagnostic_reply');
    expect(fake.requestCalls, 21);
  });

  test('boundary inputs are exact UTF-8 sizes, including structured samples',
      () async {
    final fake = _FakeTransport();
    final summary = await _runner(fake).run(RegressionProfile.boundaries);

    expect(summary.status, RegressionStatus.pass);
    expect(summary.passed, 11);
    expect(fake.topics.toSet(), {'diag/echo'});
    expect(fake.values.take(8).map(utf8.encode).map((b) => b.length),
        [19, 20, 21, 39, 40, 41, 128, 191]);
    expect(fake.values[8], contains('中文'));
    expect(fake.values[9], contains('{}'));
    expect(fake.values[10], contains(r'\'));
    expect(
        summary.steps.every((step) => (step.payloadBytes ?? 0) <= 191), isTrue);
  });

  test('soak is 100 serial diagnostic echo then snapshot rounds', () async {
    final fake = _FakeTransport();
    final summary = await _runner(fake).run(RegressionProfile.soak);

    expect(summary.status, RegressionStatus.pass);
    expect(summary.passed, 200);
    expect(fake.bleCalls, 0);
    expect(fake.requestCalls, 200);
    for (var i = 0; i < 100; i++) {
      expect(summary.steps[i * 2].name, 'soak_echo_${i + 1}');
      expect(summary.steps[i * 2 + 1].name, 'soak_snapshot_${i + 1}');
    }
  });
}
