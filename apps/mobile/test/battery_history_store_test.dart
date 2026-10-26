import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:hive/hive.dart';
import 'package:crabit_watch_app/core/battery/battery_sample.dart';
import 'package:crabit_watch_app/core/battery/battery_history_store.dart';

void main() {
  group('HiveBatteryHistoryStore', () {
    late Directory directory;
    late Box<dynamic> box;
    late HiveBatteryHistoryStore store;

    setUp(() async {
      directory = await Directory.systemTemp.createTemp('battery-history-');
      Hive.init(directory.path);
      box = await Hive.openBox<dynamic>(HiveBatteryHistoryStore.boxName);
      store = HiveBatteryHistoryStore(box);
    });

    tearDown(() async {
      if (box.isOpen) await box.close();
      await Hive.deleteBoxFromDisk(HiveBatteryHistoryStore.boxName);
      await directory.delete(recursive: true);
    });

    test('awaited writes round-trip versioned samples by device', () async {
      final now = DateTime.utc(2026, 10, 22, 12);
      final sample = _sample('watch-a', now, 10, breakBefore: true);
      await store.save(sample, now: now);

      final loaded = await store.loadDevice('watch-a', now: now);
      expect(loaded.map((entry) => entry.toMap()).toList(), [sample.toMap()]);
      expect(loaded.single.breakBefore, isTrue);
      expect(await store.loadDevice('watch-b', now: now), isEmpty);
      final raw = box.get(HiveBatteryHistoryStore.storageKeyFor('watch-a'));
      expect(raw, hasLength(1));
      expect(raw.single['version'], 1);
    });

    test('prunes records older than seven days and caps each device at 10,080',
        () async {
      final now = DateTime.utc(2026, 10, 22, 12);
      final key = HiveBatteryHistoryStore.storageKeyFor('watch-a');
      await box.put(key, [
        _sample('watch-a', now.subtract(const Duration(days: 8)), 1).toMap(),
        _sample('watch-a', now.subtract(const Duration(days: 6)), 2).toMap(),
      ]);

      final retainedByAge = await store.loadDevice('watch-a', now: now);
      expect(retainedByAge.map((sample) => sample.uptimeMs), [2]);
      expect(box.get(key), hasLength(1));

      final dense = List<Map<String, Object?>>.generate(
        HiveBatteryHistoryStore.maxSamplesPerDevice + 1,
        (index) => _sample(
          'watch-a',
          now.subtract(Duration(seconds: 10080 - index)),
          index,
        ).toMap(),
      );
      await box.put(key, dense);
      final retainedByCount = await store.loadDevice('watch-a', now: now);
      expect(retainedByCount, hasLength(10080));
      expect(retainedByCount.first.uptimeMs, 1);
      expect(retainedByCount.last.uptimeMs, 10080);
    });

    test('surfaces corrupt reads and closed-box write failures', () async {
      final now = DateTime.utc(2026, 10, 22, 12);
      await box.put(
        HiveBatteryHistoryStore.storageKeyFor('watch-a'),
        ['not-a-map'],
      );
      await expectLater(
        store.loadDevice('watch-a', now: now),
        throwsA(isA<BatteryHistoryStorageException>()),
      );

      await box.close();
      await expectLater(
        store.save(_sample('watch-a', now, 20), now: now),
        throwsA(isA<BatteryHistoryStorageException>()),
      );
    });
  });
}

BatterySample _sample(
  String deviceId,
  DateTime receivedAt,
  int uptimeMs, {
  bool breakBefore = false,
}) =>
    BatterySample(
      deviceId: deviceId,
      receivedAtUTC: receivedAt.toUtc(),
      uptimeMs: uptimeMs,
      voltageMv: 4000,
      percent: 70,
      charging: false,
      deviceValid: true,
      ageMs: 0,
      breakBefore: breakBefore,
    );
