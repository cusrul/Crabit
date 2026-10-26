import 'dart:async';

import 'package:crabit_watch_app/core/device/reconnect_policy.dart';
import 'package:crabit_watch_app/core/device/remembered_connection.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('ReconnectPolicy', () {
    test('requires opt-in, foreground, remembered ID and safe session', () {
      final policy = ReconnectPolicy()..rememberedId = 'watch-1';
      expect(policy.next(), isNull);
      policy.enabled = true;
      expect(policy.next(), isNull);
      policy.foreground = true;
      expect(policy.next(), isNotNull);
      policy.diagnostic = true;
      expect(policy.eligible, isFalse);
      policy.diagnostic = false;
      policy.userStopped = true;
      expect(policy.eligible, isFalse);
    });

    test('uses exactly three bounded backoffs and epochs revoke old work', () {
      final policy = ReconnectPolicy()
        ..enabled = true
        ..foreground = true
        ..rememberedId = 'watch-1';
      final attempts = [for (var i = 0; i < 3; i++) policy.next()!];
      expect(attempts.map((attempt) => attempt.delay), ReconnectPolicy.backoff);
      expect(policy.next(), isNull);
      policy.invalidate(resetBudget: true);
      expect(policy.accepts(attempts.last), isFalse);
      final replacement = policy.next()!;
      expect(replacement.number, 1);
      expect(replacement.remoteId, 'watch-1');
    });
  });

  group('RememberedDeviceStore', () {
    test(
      'serializes writes and rolls back if verification goes stale',
      () async {
        String? stored = 'old';
        final writes = <String?>[];
        final gate = Completer<void>();
        var current = true;
        final store = RememberedDeviceStore(
          read: () => stored,
          write: (value) async {
            writes.add(value);
            if (value == 'new') await gate.future;
            stored = value;
          },
        );

        final saving = store.saveVerified('new', () => current);
        await Future<void>.delayed(Duration.zero);
        current = false;
        gate.complete();
        expect(await saving, isFalse);
        expect(stored, 'old');
        expect(writes, ['new', 'old']);
        expect(await store.saveVerified('', () => true), isFalse);
      },
    );

    test(
      'does not persist a device unless caller confirms verification',
      () async {
        String? stored;
        final store = RememberedDeviceStore(
          read: () => stored,
          write: (value) async => stored = value,
        );
        expect(await store.saveVerified('watch-1', () => false), isFalse);
        expect(stored, isNull);
        expect(await store.saveVerified('watch-1', () => true), isTrue);
        expect(stored, 'watch-1');
      },
    );
  });

  group('RememberedConnection', () {
    late Map<String, Object?> settings;
    late List<String> reconnects;
    late int cancellations;
    late bool ready;
    late bool occupied;
    late bool holdFirstAttempt;
    late Completer<void> firstAttempt;
    late Completer<void> teardown;
    late Completer<void> attemptStarted;
    late RememberedConnection connection;

    setUp(() {
      settings = {
        RememberedConnection.deviceKey: 'watch-1',
        RememberedConnection.enabledKey: false,
      };
      reconnects = [];
      cancellations = 0;
      ready = false;
      occupied = false;
      holdFirstAttempt = false;
      firstAttempt = Completer<void>();
      teardown = Completer<void>();
      attemptStarted = Completer<void>();
      connection = RememberedConnection(
        readSetting: (key) => settings[key],
        putSetting: (key, value) async => settings[key] = value as Object?,
        connectRemembered: (id) async {
          reconnects.add(id);
          if (holdFirstAttempt &&
              reconnects.length == 1 &&
              !firstAttempt.isCompleted) {
            attemptStarted.complete();
            await firstAttempt.future;
          }
        },
        cancelAutomatic: () async {
          cancellations++;
          if (!teardown.isCompleted) await teardown.future;
        },
        isReady: () => ready,
        isOccupied: () => occupied,
      );
      connection.initialize();
    });

    tearDown(() => connection.dispose());

    test('is silent until explicitly enabled and foregrounded', () async {
      connection.setForeground(true);
      await Future<void>.delayed(const Duration(milliseconds: 1100));
      expect(reconnects, isEmpty);
      await connection.setEnabled(true);
      expect(connection.enabled, isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 1100));
      expect(reconnects, hasLength(1));
      expect(reconnects.single, 'watch-1');
    });

    test(
      'explicit disconnect cancels pending timer and suppresses retries',
      () async {
        await connection.setEnabled(true);
        connection
          ..setForeground(true)
          ..explicitConnect(diagnostic: false)
          ..connectionLost(wasVerified: true)
          ..stopByUser();
        await Future<void>.delayed(const Duration(milliseconds: 1100));
        expect(reconnects, isEmpty);
        expect(connection.retryPending, isFalse);
      },
    );

    test(
      'diagnostic session and background transition revoke pending retry',
      () async {
        await connection.setEnabled(true);
        connection.setForeground(true);
        connection.enterDiagnostics();
        await Future<void>.delayed(const Duration(milliseconds: 1100));
        expect(reconnects, isEmpty);
        connection
          ..explicitConnect(diagnostic: false)
          ..connectionLost(wasVerified: true);
        connection.setForeground(false);
        await Future<void>.delayed(const Duration(milliseconds: 1100));
        expect(reconnects, isEmpty);
      },
    );

    test('automatic attempts stop after three failures', () async {
      connection
        ..setForeground(true)
        ..explicitConnect(diagnostic: false)
        ..connectionLost(wasVerified: true);
      // Opt-in is persisted separately, exactly as in production.
      await connection.setEnabled(true);
      await Future<void>.delayed(const Duration(seconds: 12));
      expect(reconnects.length, 3);
      expect(connection.attempts, 3);
      expect(connection.status, contains('3 次'));
    });

    test(
        'foreground return reconciles after revoked attempt and teardown settle',
        () async {
      await connection.setEnabled(true);
      holdFirstAttempt = true;
      connection.setForeground(true);
      await attemptStarted.future.timeout(const Duration(seconds: 3));

      occupied = true;
      connection.setForeground(false);
      connection.setForeground(true);
      teardown.complete();
      await Future<void>.delayed(Duration.zero);
      occupied = false;
      firstAttempt.complete();
      await Future<void>.delayed(const Duration(milliseconds: 1100));

      expect(reconnects, hasLength(2));
      expect(connection.automaticRunning, isFalse);
      expect(cancellations, 1);
    });

    test('explicit stop during an in-flight attempt cannot resurrect retries',
        () async {
      await connection.setEnabled(true);
      holdFirstAttempt = true;
      connection.setForeground(true);
      await attemptStarted.future.timeout(const Duration(seconds: 3));
      connection.stopByUser();
      teardown.complete();
      firstAttempt.complete();
      await Future<void>.delayed(const Duration(milliseconds: 1100));

      expect(reconnects, hasLength(1));
      expect(connection.automaticRunning, isFalse);
      expect(cancellations, 1);
    });
  });
}
