import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/battery/battery_history_controller.dart';
import 'package:crabit_watch_app/core/battery/battery_history_store.dart';
import 'package:crabit_watch_app/core/battery/battery_sample.dart';

void main() {
  group('BatteryHistoryController', () {
    test('samples immediately on ready and then on the 60-second timer',
        () async {
      final gateway = _Gateway();
      final store = _MemoryStore();
      final timers = <_ManualTimer>[];
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        periodicTimerFactory: (interval, callback) {
          expect(interval, const Duration(seconds: 60));
          final timer = _ManualTimer(callback);
          timers.add(timer);
          return timer;
        },
      );
      await controller.initialized;

      gateway.setReady(true);
      await controller.lastRequest;
      expect(gateway.requests, 1);
      expect(store.writes, 1);
      expect(timers, hasLength(1));

      timers.single.fire();
      await controller.lastRequest;
      expect(gateway.requests, 2);
      controller.dispose();
      expect(timers.single.isActive, isFalse);
    });

    test('skips a busy sampling opportunity without overlap or immediate retry',
        () async {
      final gateway = _Gateway();
      final response = Completer<String>();
      gateway.pendingResponse = response;
      final store = _MemoryStore();
      final timers = <_ManualTimer>[];
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        periodicTimerFactory: (interval, callback) {
          final timer = _ManualTimer(callback);
          timers.add(timer);
          return timer;
        },
      );
      await controller.initialized;

      gateway.setReady(true);
      final originalRequest = controller.lastRequest;
      expect(controller.isBusy, isTrue);
      await controller.sampleNow();
      expect(gateway.requests, 1);

      gateway.setReady(false);
      response.complete(_payload(at: 100));
      await originalRequest;
      expect(store.writes, 0);
      expect(controller.latestSample, isNull);
      expect(gateway.requests, 1);
      expect(timers.single.isActive, isFalse);
      controller.dispose();
    });

    test(
        'manual readings display immediately but persist at most once per minute',
        () async {
      var now = DateTime.utc(2026, 10, 22, 12);
      final gateway = _Gateway()..ready = true;
      gateway.payload = _payload(at: 100);
      final store = _MemoryStore();
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        now: () => now,
        periodicTimerFactory: (_, callback) => _ManualTimer(callback),
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(store.writes, 1);

      now = now.add(const Duration(seconds: 30));
      gateway.payload = _payload(at: 200, percent: 68);
      await controller.sampleNow();
      expect(controller.latestSample!.percent, 68);
      expect(controller.chartSamples, hasLength(2));
      expect(store.writes, 1);

      now = now.add(const Duration(seconds: 30));
      gateway.payload = _payload(at: 300, percent: 67);
      await controller.sampleNow();
      expect(store.writes, 2);
      expect(controller.samples.last.percent, 67);
      controller.dispose();
    });

    test('deduplicates same-boot uptime but records a device reset', () async {
      var now = DateTime.utc(2026, 10, 22, 12);
      final gateway = _Gateway()..ready = true;
      gateway.payload = _payload(at: 100);
      final store = _MemoryStore();
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        now: () => now,
        periodicTimerFactory: (_, callback) => _ManualTimer(callback),
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(store.writes, 1);

      now = now.add(const Duration(minutes: 1));
      gateway.payload = _payload(at: 100, percent: 69);
      await controller.sampleNow();
      expect(store.writes, 1);

      now = now.add(const Duration(minutes: 1));
      gateway.payload = _payload(at: 5, percent: 69);
      await controller.sampleNow();
      expect(store.writes, 2);
      expect(controller.samples.last.uptimeMs, 5);
      controller.dispose();
    });

    test('marks a reconnect gap explicitly even when it is under two minutes',
        () async {
      var now = DateTime.utc(2026, 10, 22, 12);
      final gateway = _Gateway()..ready = true;
      final store = _MemoryStore();
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        now: () => now,
        periodicTimerFactory: (_, callback) => _ManualTimer(callback),
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(controller.samples.single.breakBefore, isTrue);

      gateway.setReady(false);
      now = now.add(const Duration(seconds: 30));
      gateway.payload = _payload(at: 20);
      gateway.setReady(true);
      await controller.lastRequest;

      expect(controller.latestSample!.breakBefore, isTrue);
      expect(controller.chartSamples.last.breakBefore, isTrue);
      expect(controller.samples, hasLength(1)); // cadence still holds

      now = now.add(const Duration(seconds: 30));
      gateway.payload = _payload(at: 30);
      await controller.sampleNow();
      expect(controller.samples.last.breakBefore, isTrue);
      controller.dispose();
    });

    test('bounds in-memory history by retention and maximum sample count',
        () async {
      var now = DateTime.utc(2026, 10, 22, 12);
      final seed = <BatterySample>[
        _sample('watch-a', now.subtract(const Duration(days: 8)), 0),
        for (var index = 1;
            index <= BatteryHistoryStore.maxSamplesPerDevice + 1;
            index++)
          _sample(
            'watch-a',
            now.subtract(Duration(
                seconds: BatteryHistoryStore.maxSamplesPerDevice + 1 - index)),
            index,
          ),
      ];
      final store = _MemoryStore()..data.addAll(seed);
      final gateway = _Gateway()..ready = true;
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        now: () => now,
        periodicTimerFactory: (_, callback) => _ManualTimer(callback),
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(controller.samples, hasLength(10080));
      expect(controller.samples.first.uptimeMs, 2);

      now = now.add(const Duration(minutes: 1));
      gateway.payload = _payload(at: 10082);
      await controller.sampleNow();
      expect(controller.samples, hasLength(10080));
      expect(controller.samples.first.uptimeMs, 3);
      expect(controller.samples.last.uptimeMs, 10082);
      expect(
        controller.samples.every((sample) => !sample.receivedAtUTC
            .isBefore(now.subtract(BatteryHistoryStore.retention))),
        isTrue,
      );
      controller.dispose();
    });

    test('invalid or aged data remains unavailable and never plots as zero',
        () async {
      final gateway = _Gateway()..ready = true;
      gateway.payload = _payload(at: 10, percent: 0, mv: 0, age: 5001);
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: _MemoryStore(),
        periodicTimerFactory: (_, callback) => _ManualTimer(callback),
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(controller.latestSample!.isValid, isFalse);
      expect(
          controller.chartSamples.where((sample) => sample.isValid), isEmpty);
      expect(
          controller.chartSamples.single.percent, 0); // retained, never plotted
      expect(controller.latestIsStale, isTrue);
      controller.dispose();
    });

    test('surfaces parse and storage errors and retries only on a later tick',
        () async {
      final gateway = _Gateway()..ready = true;
      gateway.payload = 'not-a-battery-response';
      final timers = <_ManualTimer>[];
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: _MemoryStore(),
        periodicTimerFactory: (_, callback) {
          final timer = _ManualTimer(callback);
          timers.add(timer);
          return timer;
        },
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(controller.errorMessage, '设备返回的电量数据无效');
      expect(gateway.requests, 1);

      gateway.payload = _payload(at: 2);
      timers.single.fire();
      await controller.lastRequest;
      expect(gateway.requests, 2);
      expect(controller.errorMessage, isNull);
      controller.dispose();

      final writeGateway = _Gateway()..ready = true;
      final failingStore = _MemoryStore()..failSave = true;
      final writeController = BatteryHistoryController(
        gateway: writeGateway,
        store: failingStore,
      );
      await writeController.initialized;
      await writeController.lastRequest;
      expect(writeController.errorMessage, '本地电池历史保存失败');
      expect(writeController.latestSample, isNotNull);
      writeController.dispose();
    });

    test('loads saved cadence and disposes without driving the gateway',
        () async {
      final now = DateTime.utc(2026, 10, 22, 12);
      final store = _MemoryStore()
        ..data.add(
            _sample('watch-a', now.subtract(const Duration(seconds: 20)), 10));
      final gateway = _Gateway()..ready = true;
      final controller = BatteryHistoryController(
        gateway: gateway,
        store: store,
        now: () => now,
        periodicTimerFactory: (_, callback) => _ManualTimer(callback),
      );
      await controller.initialized;
      await controller.lastRequest;
      expect(gateway.requests, 1);
      expect(store.writes, 0);
      expect(controller.samples, hasLength(1));
      controller.dispose();
      gateway.setReady(false);
      expect(gateway.requests, 1);
    });
  });
}

class _Gateway extends ChangeNotifier implements BatteryHistoryGateway {
  @override
  bool ready = false;
  @override
  String? deviceId = 'watch-a';
  String payload = _payload(at: 10);
  Completer<String>? pendingResponse;
  int requests = 0;

  void setReady(bool value) {
    ready = value;
    notifyListeners();
  }

  @override
  Future<String> requestBattery() {
    requests++;
    final pending = pendingResponse;
    if (pending != null) {
      pendingResponse = null;
      return pending.future;
    }
    return Future.value(payload);
  }
}

class _MemoryStore implements BatteryHistoryStore {
  final List<BatterySample> data = <BatterySample>[];
  int writes = 0;
  bool failSave = false;
  bool failLoad = false;

  @override
  Future<List<BatterySample>> loadDevice(String deviceId,
      {DateTime? now}) async {
    if (failLoad) {
      throw const BatteryHistoryStorageException(
        operation: 'read',
        deviceId: 'watch-a',
        cause: 'test failure',
      );
    }
    return data.where((sample) => sample.deviceId == deviceId).toList();
  }

  @override
  Future<void> save(BatterySample sample, {DateTime? now}) async {
    if (failSave) {
      throw BatteryHistoryStorageException(
        operation: 'write',
        deviceId: sample.deviceId,
        cause: 'test failure',
      );
    }
    writes++;
    data.add(sample);
  }
}

class _ManualTimer implements Timer {
  _ManualTimer(this.callback);

  final void Function() callback;
  bool _active = true;
  int _ticks = 0;

  void fire() {
    if (!_active) return;
    _ticks++;
    callback();
  }

  @override
  void cancel() => _active = false;

  @override
  bool get isActive => _active;

  @override
  int get tick => _ticks;
}

String _payload({
  required int at,
  int percent = 70,
  int mv = 4000,
  int age = 0,
}) =>
    'mv=$mv;pct=$percent;charging=0;valid=1;at=$at;age=$age';

BatterySample _sample(String deviceId, DateTime receivedAt, int uptimeMs) =>
    BatterySample(
      deviceId: deviceId,
      receivedAtUTC: receivedAt.toUtc(),
      uptimeMs: uptimeMs,
      voltageMv: 4000,
      percent: 70,
      charging: false,
      deviceValid: true,
      ageMs: 0,
    );
