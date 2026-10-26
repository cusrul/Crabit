import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/settings/preferences_controller.dart';
import 'package:crabit_watch_app/core/settings/preferences_gateway.dart';
import 'package:crabit_watch_app/core/settings/preferences_protocol.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';

WatchPreferences _preferences({
  bool pat = false,
  bool qmi = false,
  bool lift = false,
  bool tap = false,
  bool sound = false,
  bool led = false,
  int volume = 30,
  int ledLevel = 60,
  int face = 2,
  bool deepSleep = true,
}) =>
    WatchPreferences(
      pat: pat,
      qmi: qmi,
      lift: lift,
      tap: tap,
      sound: sound,
      led: led,
      volume: volume,
      ledLevel: ledLevel,
      face: face,
      deepSleep: deepSleep,
    );

PreferencesReply _snapshot(WatchPreferences value) =>
    PreferencesReply('snapshot', 'none', value);

class _FakeGateway extends ChangeNotifier implements PreferencesGateway {
  @override
  bool ready = false;
  int readCalls = 0;
  final writes = <Map<String, int>>[];
  Future<PreferencesReply> Function()? readHandler;
  Future<PreferencesReply> Function(Map<String, int> patch)? applyHandler;

  @override
  Future<PreferencesReply> read() {
    readCalls++;
    return readHandler?.call() ?? Future.value(_snapshot(_preferences()));
  }

  @override
  Future<PreferencesReply> apply(Map<String, int> patch) {
    writes.add(Map.of(patch));
    return applyHandler?.call(patch) ??
        Future.value(PreferencesReply('saved', 'none', _preferences()));
  }

  void setReady(bool value) {
    ready = value;
    notifyListeners();
  }
}

Future<void> _flush() async {
  await Future<void>.delayed(Duration.zero);
  await Future<void>.delayed(Duration.zero);
}

void main() {
  test('does not read before product readiness and reads on ready transition',
      () async {
    final gateway = _FakeGateway();
    final controller = PreferencesController(gateway);
    expect(gateway.readCalls, 0);
    await controller.refresh();
    expect(gateway.readCalls, 0);

    gateway.setReady(true);
    await _flush();
    expect(gateway.readCalls, 1);
    expect(controller.applied, isNotNull);
    expect(controller.draft, controller.applied);
    controller.dispose();
    gateway.dispose();
  });

  test('applies only draft differences and trusts the device readback',
      () async {
    final gateway = _FakeGateway()..ready = true;
    final controller = PreferencesController(gateway);
    await _flush();
    controller.edit(controller.draft!.copyWith(pat: true, volume: 41));
    gateway.applyHandler = (patch) async => PreferencesReply(
          'saved',
          'none',
          _preferences(pat: true, volume: 41, deepSleep: false),
        );

    await controller.apply();

    expect(gateway.writes, [
      {'pat': 1, 'volume': 41}
    ]);
    expect(controller.applied?.pat, isTrue);
    expect(controller.applied?.volume, 41);
    expect(controller.deepSleepCapability, isFalse);
    expect(controller.draft, controller.applied);
    expect(controller.message, contains('设备回读'));
    expect(controller.canApply, isFalse);
    controller.dispose();
    gateway.dispose();
  });

  test('save failure is not success and retries only after another user action',
      () async {
    final gateway = _FakeGateway()..ready = true;
    final controller = PreferencesController(gateway);
    await _flush();
    controller.edit(controller.draft!.copyWith(volume: 44));
    gateway.applyHandler = (patch) async => gateway.writes.length == 1
        ? PreferencesReply('save_failed', 'program', _preferences(volume: 44))
        : PreferencesReply('saved', 'none', _preferences(volume: 44));

    await controller.apply();
    await _flush();
    expect(gateway.writes, hasLength(1));
    expect(controller.message, contains('保存失败'));
    expect(controller.message, contains('未确认持久化'));
    expect(controller.canApply, isTrue,
        reason: 'the user may explicitly ask the watch to save again');

    await controller.apply();
    expect(gateway.writes, hasLength(2));
    expect(controller.message, contains('已确认保存'));
    expect(controller.canApply, isFalse);
    controller.dispose();
    gateway.dispose();
  });

  test('reverting a busy draft to the applied value cannot replay it',
      () async {
    final gateway = _FakeGateway()..ready = true;
    final controller = PreferencesController(gateway);
    await _flush();
    gateway.applyHandler =
        (_) async => PreferencesReply('busy', 'owner', _preferences());

    controller.edit(controller.draft!.copyWith(pat: true));
    await controller.apply();
    expect(gateway.writes, [
      {'pat': 1}
    ]);
    expect(controller.applied?.pat, isFalse);
    expect(controller.draft?.pat, isTrue);

    controller.edit(controller.draft!.copyWith(pat: false));
    expect(controller.changes, isEmpty);
    expect(controller.canApply, isFalse);
    await controller.apply();
    expect(
        gateway.writes,
        [
          {'pat': 1}
        ],
        reason: 'busy did not authorize retaining a stale retry patch');

    controller.dispose();
    gateway.dispose();
  });

  test('save-failed retry uses current draft values instead of stale values',
      () async {
    final gateway = _FakeGateway()..ready = true;
    final controller = PreferencesController(gateway);
    await _flush();
    controller.edit(controller.draft!.copyWith(pat: true));
    gateway.applyHandler = (patch) async => gateway.writes.length == 1
        ? PreferencesReply('save_failed', 'program', _preferences(pat: true))
        : PreferencesReply(
            'saved', 'none', _preferences(pat: patch['pat'] == 1));

    await controller.apply();
    expect(controller.applied?.pat, isTrue,
        reason: 'save_failed exposes the device readback as current');
    expect(controller.draft?.pat, isTrue);

    controller.edit(controller.draft!.copyWith(pat: false));
    expect(controller.canApply, isTrue);
    await controller.apply();

    expect(gateway.writes, [
      {'pat': 1},
      {'pat': 0},
    ]);
    expect(controller.applied?.pat, isFalse);
    expect(controller.canApply, isFalse);

    controller.dispose();
    gateway.dispose();
  });

  test(
      'old-firmware read timeout stays unavailable instead of showing defaults',
      () async {
    final gateway = _FakeGateway()..ready = true;
    gateway.readHandler = () async {
      throw const TransportException(TransportErrorCode.timeout);
    };
    final controller = PreferencesController(gateway);
    await _flush();

    expect(controller.applied, isNull);
    expect(controller.draft, isNull);
    expect(controller.message, contains('旧版固件'));
    expect(controller.canApply, isFalse);
    controller.dispose();
    gateway.dispose();
  });

  test(
      'disconnect revokes drafts and stale completions cannot overwrite a new session',
      () async {
    final gateway = _FakeGateway()..ready = true;
    final controller = PreferencesController(gateway);
    await _flush();
    controller.edit(controller.draft!.copyWith(pat: true));
    final oldWrite = Completer<PreferencesReply>();
    gateway.applyHandler = (_) => oldWrite.future;
    final applying = controller.apply();
    await _flush();

    gateway.setReady(false);
    expect(controller.applied, isNull);
    expect(controller.draft, isNull);
    expect(controller.deepSleepCapability, isNull);
    expect(controller.canApply, isFalse);

    gateway.readHandler = () async => _snapshot(_preferences(qmi: true));
    gateway.setReady(true);
    await _flush();
    expect(controller.applied?.qmi, isTrue);

    oldWrite.complete(
      PreferencesReply('saved', 'none', _preferences(pat: true)),
    );
    await applying;
    expect(controller.applied?.qmi, isTrue);
    expect(controller.applied?.pat, isFalse);
    expect(controller.busy, isFalse);
    controller.dispose();
    gateway.dispose();
  });

  test('write timeout is unknown and is never replayed automatically',
      () async {
    final gateway = _FakeGateway()..ready = true;
    final controller = PreferencesController(gateway);
    await _flush();
    controller.edit(controller.draft!.copyWith(led: true));
    gateway.applyHandler = (_) async {
      throw const TransportException(TransportErrorCode.timeout);
    };

    await controller.apply();
    await _flush();

    expect(gateway.writes, hasLength(1));
    expect(controller.message, contains('结果未知'));
    expect(controller.message, contains('未自动重发'));
    expect(controller.draft?.led, isTrue);
    controller.dispose();
    gateway.dispose();
  });
}
