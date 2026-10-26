import 'dart:async';
import 'dart:convert';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';
import 'package:flutter_test/flutter_test.dart';

Matcher failure(TransportErrorCode code) =>
    isA<TransportException>().having((error) => error.code, 'code', code);

void main() {
  test('teardown timeout returns BUSY but fence remains until native settles',
      () async {
    final fence = NativeTeardownFence();
    final native = Completer<void>();
    var calls = 0;
    Future<void> teardown() {
      calls++;
      return native.future;
    }

    await expectLater(
        fence.run(teardown, timeout: const Duration(milliseconds: 10)),
        throwsA(failure(TransportErrorCode.busy)));
    expect(fence.busy, isTrue);
    await expectLater(
        fence.run(teardown, timeout: const Duration(milliseconds: 10)),
        throwsA(failure(TransportErrorCode.busy)));
    expect(calls, 1);
    native.complete();
    await Future<void>.delayed(Duration.zero);
    expect(fence.busy, isFalse);
    await fence.run(() async {
      calls++;
    });
    expect(calls, 2);
  });
  test('late native teardown failure is handled after caller timeout',
      () async {
    final fence = NativeTeardownFence();
    final native = Completer<void>();
    await expectLater(
        fence.run(() => native.future,
            timeout: const Duration(milliseconds: 10)),
        throwsA(failure(TransportErrorCode.busy)));
    native.completeError(
        const TransportException(TransportErrorCode.disconnected));
    await Future<void>.delayed(Duration.zero);
    expect(fence.busy, isFalse);
  });
  test('UTF-8 split at every byte; escaped quotes and braces survive', () {
    final decoder = BleJsonDecoder();
    final original = {'t': 'diag/echo', 'v': '中文 } { " \\ \n', 'i': 7};
    final frames = <Map<String, dynamic>>[];
    for (final byte in utf8.encode(jsonEncode(original))) {
      frames.addAll(decoder.add([byte]));
    }
    expect(frames, [original]);
  });
  test('coalesced/nested objects and reset', () {
    final decoder = BleJsonDecoder();
    expect(decoder.add([]), isEmpty);
    expect(decoder.add(utf8.encode(' \r\n{"v":{"x":1}}{"v":2}')), [
      {
        'v': {'x': 1}
      },
      {'v': 2}
    ]);
    decoder.add(utf8.encode('{"v":"partial'));
    decoder.reset();
    expect(decoder.add(utf8.encode('{"v":3}')), [
      {'v': 3}
    ]);
  });
  test('malformed and oversized frames are rejected; decoder recovers', () {
    final decoder = BleJsonDecoder();
    expect(() => decoder.add(utf8.encode('{bad}')),
        throwsA(failure(TransportErrorCode.protocolError)));
    expect(() => decoder.add([123, ...List.filled(4096, 65)]),
        throwsA(failure(TransportErrorCode.protocolError)));
    expect(decoder.add(utf8.encode('{}')), [{}]);
  });

  group('production message queue and requests', () {
    late BleTransport transport;
    setUp(() => transport = BleTransport());
    tearDown(() => transport.close());

    test('not connected and not authenticated are distinct', () async {
      await expectLater(transport.request('diag/echo', 'x'),
          throwsA(failure(TransportErrorCode.notConnected)));
      transport.open((_) async {});
      await expectLater(transport.request('diag/echo', 'x'),
          throwsA(failure(TransportErrorCode.notAuthenticated)));
    });
    test('pending-work signal covers correlated requests and stalled writes',
        () async {
      final gate = Completer<void>();
      transport.open((_) => gate.future);
      transport.authenticated = true;
      final request = transport.request('sys/settings', '');
      await Future<void>.delayed(Duration.zero);
      expect(transport.hasPendingWork, isTrue);
      transport.close();
      await expectLater(
          request, throwsA(failure(TransportErrorCode.disconnected)));
      gate.complete();
      await transport.waitForIdle();
      expect(transport.hasPendingWork, isFalse);
    });
    test('concurrent messages keep contiguous 20-byte chunks', () async {
      final chunks = <String>[];
      transport.open((bytes) async {
        chunks.add(utf8.decode(bytes));
        await Future<void>.delayed(Duration.zero);
      });
      transport.authenticated = true;
      await Future.wait(
          [transport.sendRaw('A' * 41), transport.sendRaw('B' * 21)]);
      expect(chunks, ['A' * 20, 'A' * 20, 'A', 'B' * 20, 'B']);
    });
    test('reply may arrive before the final write future completes', () async {
      final decoder = BleJsonDecoder();
      transport.open((bytes) async {
        for (final message in decoder.add(bytes)) {
          transport.accept(message);
        }
      });
      transport.authenticated = true;
      final reply = await transport.request('diag/echo', '中文');
      expect(reply.value, '中文');
      expect(reply.requestId, '1');
    });
    test('ACK and wrong topic cannot complete a diagnostic', () async {
      final decoder = BleJsonDecoder();
      transport.open((bytes) async {
        for (final message in decoder.add(bytes)) {
          transport.accept({'t': 'ack', 'v': message['i']});
          transport.accept({'t': 'diag/wifi', 'v': 'wrong', 'i': message['i']});
        }
      });
      transport.authenticated = true;
      await expectLater(
          transport.request('diag/echo', 'x',
              timeout: const Duration(milliseconds: 15)),
          throwsA(failure(TransportErrorCode.timeout)));
    });
    test('duplicate reply and late old-session ID cannot satisfy a new request',
        () async {
      final decoder = BleJsonDecoder();
      void open() {
        transport.open((bytes) async {
          for (final message in decoder.add(bytes)) {
            if (message['i'] != 1) {
              transport.accept({'t': 'diag/echo', 'v': 'old', 'i': 1});
            }
            transport.accept(message);
            transport.accept(message);
          }
        });
        transport.authenticated = true;
      }

      open();
      expect((await transport.request('diag/echo', 'first')).requestId, '1');
      transport.close();
      open();
      final reply = await transport.request('diag/echo', 'new');
      expect(reply.requestId, '2');
      expect(reply.value, 'new');
    });
    test('disconnect immediately terminates requests, probes and queued writes',
        () async {
      final gate = Completer<void>();
      transport.open((_) => gate.future);
      transport.authenticated = true;
      final requestCheck = expectLater(transport.request('diag/echo', 'x'),
          throwsA(failure(TransportErrorCode.disconnected)));
      final probeCheck = expectLater(transport.verifyEcho('TEST:P:123456'),
          throwsA(failure(TransportErrorCode.disconnected)));
      transport.close();
      await Future.wait([requestCheck, probeCheck]);
      gate.complete();
    });
    test('partial write failure poisons session and stops remaining chunks',
        () async {
      var writes = 0;
      transport.open((_) async {
        writes++;
        throw StateError('native secret');
      });
      transport.authenticated = true;
      await expectLater(transport.request('diag/echo', 'A' * 41),
          throwsA(failure(TransportErrorCode.writeFailed)));
      expect(transport.connected, isFalse);
      expect(writes, 1);
    });
    test('deadline includes write time; stalled write blocks further messages',
        () async {
      final gate = Completer<void>();
      transport.open((_) => gate.future);
      transport.authenticated = true;
      await expectLater(
          transport.request('diag/echo', 'x',
              timeout: const Duration(milliseconds: 20)),
          throwsA(failure(TransportErrorCode.timeout)));
      await Future<void>.delayed(const Duration(milliseconds: 5));
      expect(transport.connected, isFalse);
      gate.complete();
    });
    test('wire limits count UTF-8 bytes; invalid input never writes', () async {
      var writes = 0;
      transport.open((_) async {
        writes++;
      });
      transport.authenticated = true;
      for (final pair in [
        ['t' * 48, 'x'],
        ['diag/echo', '中' * 64],
        ['diag/echo', 'a\u0000b']
      ]) {
        await expectLater(transport.request(pair[0], pair[1]),
            throwsA(failure(TransportErrorCode.invalidPayload)));
      }
      expect(writes, 0);
    });
    test('new session cannot silently queue behind an old stuck native write',
        () async {
      final oldNative = Completer<void>();
      transport.open((_) => oldNative.future);
      transport.authenticated = true;
      await expectLater(
          transport.sendRaw('x', timeout: const Duration(milliseconds: 10)),
          throwsA(failure(TransportErrorCode.timeout)));
      expect(() => transport.open((_) async {}),
          throwsA(failure(TransportErrorCode.busy)));
      await expectLater(
          transport.waitForIdle(timeout: const Duration(milliseconds: 10)),
          throwsA(failure(TransportErrorCode.busy)));
      oldNative.complete();
      await transport.waitForIdle();
      transport.open((_) async {});
      transport.authenticated = true;
      await transport.sendRaw('new');
    });
    test('only exact probe echo matches, at most one probe is pending',
        () async {
      transport.open((_) async {
        transport.accept({'t': 'sys/notify', 'v': 'TEST:P:wrong'});
      });
      transport.authenticated = true;
      final probe = transport.verifyEcho('TEST:P:123456',
          timeout: const Duration(milliseconds: 15));
      await expectLater(transport.verifyEcho('TEST:P:654321'),
          throwsA(failure(TransportErrorCode.busy)));
      await expectLater(probe, throwsA(failure(TransportErrorCode.timeout)));
    });
    test('queue saturation rejects caller without poisoning active writes',
        () async {
      final gate = Completer<void>();
      transport.open((_) => gate.future);
      transport.authenticated = true;
      final writes = List.generate(16, (_) => transport.sendRaw('x'));
      await expectLater(transport.sendRaw('overflow'),
          throwsA(failure(TransportErrorCode.busy)));
      gate.complete();
      await Future.wait(writes);
      expect(transport.connected, isTrue);
    });
  });
}
