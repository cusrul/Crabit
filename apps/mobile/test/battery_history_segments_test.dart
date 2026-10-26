import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/battery/battery_history_segments.dart';
import 'package:crabit_watch_app/core/battery/battery_sample.dart';

void main() {
  test('splits explicit reconnect, stale, implausible, and long-gap history',
      () {
    final start = DateTime.utc(2026, 10, 22, 12);
    final samples = [
      _sample(start.subtract(const Duration(seconds: 40)), 100),
      _sample(start.subtract(const Duration(seconds: 30)), 110),
      _sample(
        start.subtract(const Duration(seconds: 20)),
        120,
        breakBefore: true,
      ),
      _sample(start.subtract(const Duration(seconds: 10)), 130, ageMs: 5001),
      _sample(start, 140),
      _sample(start.add(const Duration(seconds: 121)), 150),
      _sample(start.add(const Duration(seconds: 122)), 160, voltageMv: 500),
      _sample(start.add(const Duration(seconds: 123)), 170),
    ];

    final segments = splitBatteryHistorySegments(samples);
    expect(
      segments
          .map((segment) => segment.map((sample) => sample.uptimeMs).toList())
          .toList(),
      [
        [100, 110],
        [120],
        [140],
        [150],
        [170],
      ],
    );
  });
}

BatterySample _sample(
  DateTime at,
  int uptimeMs, {
  int ageMs = 0,
  int voltageMv = 4000,
  bool breakBefore = false,
}) =>
    BatterySample(
      deviceId: 'watch-a',
      receivedAtUTC: at,
      uptimeMs: uptimeMs,
      voltageMv: voltageMv,
      percent: 60,
      charging: false,
      deviceValid: true,
      ageMs: ageMs,
      breakBefore: breakBefore,
    );
