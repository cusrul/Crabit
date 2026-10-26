import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/battery/battery_sample.dart';

void main() {
  group('BatteryWireReading', () {
    test('parses the six typed fields and permits field order independence',
        () {
      final reading = BatteryWireReading.parse(
        'age=80;charging=1;mv=4075;valid=1;at=4294967295;pct=82',
      );
      expect(reading.voltageMv, 4075);
      expect(reading.percent, 82);
      expect(reading.charging, isTrue);
      expect(reading.valid, isTrue);
      expect(reading.uptimeMs, 0xffffffff);
      expect(reading.ageMs, 80);
    });

    test('rejects unknown, duplicate, missing, and malformed fields', () {
      const valid = 'mv=4000;pct=50;charging=0;valid=1;at=10;age=0';
      for (final payload in [
        valid.replaceFirst('age=0', 'age=0;extra=1'),
        valid.replaceFirst('age=0', 'at=10'),
        valid.replaceFirst('mv=4000', 'mv=4=000'),
        valid.replaceFirst('pct=50', 'pct= 50'),
        valid.replaceFirst('mv=4000', 'mv=-1'),
      ]) {
        expect(() => BatteryWireReading.parse(payload), throwsFormatException,
            reason: payload);
      }
    });

    test(
        'rejects invalid ranges, flags, uint32 overflow, and non-ASCII/long data',
        () {
      const valid = 'mv=4000;pct=50;charging=0;valid=1;at=10;age=0';
      for (final payload in [
        valid.replaceFirst('pct=50', 'pct=101'),
        valid.replaceFirst('charging=0', 'charging=2'),
        valid.replaceFirst('valid=1', 'valid=9'),
        valid.replaceFirst('at=10', 'at=4294967296'),
        valid.replaceFirst('age=0', 'age=4294967296'),
        valid.replaceFirst('pct=50', 'pct=五十'),
        '$valid${List.filled(200, "x").join()}',
      ]) {
        expect(() => BatteryWireReading.parse(payload), throwsFormatException,
            reason: payload);
      }
    });

    test('freshness and device validity remain explicit on the sample', () {
      final received = DateTime(2026, 10, 22, 12);
      final stale = BatterySample.fromWire(
        deviceId: 'watch-a',
        receivedAtUTC: received,
        reading: BatteryWireReading.parse(
          'mv=0;pct=0;charging=0;valid=1;at=15;age=5001',
        ),
      );
      expect(stale.receivedAtUTC.isUtc, isTrue);
      expect(stale.isValid, isFalse);
      expect(stale.ageMs, 5001);

      final explicitlyInvalid = BatterySample.fromWire(
        deviceId: 'watch-a',
        receivedAtUTC: received,
        reading: BatteryWireReading.parse(
          'mv=0;pct=0;charging=0;valid=0;at=16;age=0',
        ),
      );
      expect(explicitlyInvalid.isValid, isFalse);
      expect(explicitlyInvalid.percent, 0); // retained as raw invalid data
    });

    test('physical voltage bounds are required for a plottable sample', () {
      BatterySample sampleAt(int voltageMv) => BatterySample.fromWire(
            deviceId: 'watch-a',
            receivedAtUTC: DateTime.utc(2026, 10, 22, 12),
            reading: BatteryWireReading.parse(
              'mv=$voltageMv;pct=50;charging=0;valid=1;at=10;age=0',
            ),
          );

      expect(sampleAt(501).isValid, isTrue);
      expect(sampleAt(5999).isValid, isTrue);
      expect(sampleAt(500).isValid, isFalse);
      expect(sampleAt(6000).isValid, isFalse);
    });

    test('round-trips versioned history maps and detects uptime reset not wrap',
        () {
      final first = BatterySample.fromWire(
        deviceId: 'watch-a',
        receivedAtUTC: DateTime.utc(2026, 10, 22, 12),
        reading: BatteryWireReading.parse(
          'mv=4000;pct=75;charging=0;valid=1;at=4294967290;age=0',
        ),
        breakBefore: true,
      );
      final wrapped = BatterySample.fromWire(
        deviceId: 'watch-a',
        receivedAtUTC: DateTime.utc(2026, 10, 22, 12, 1),
        reading: BatteryWireReading.parse(
          'mv=3990;pct=74;charging=0;valid=1;at=8;age=0',
        ),
      );
      final reset = BatterySample.fromWire(
        deviceId: 'watch-a',
        receivedAtUTC: DateTime.utc(2026, 10, 22, 12, 2),
        reading: BatteryWireReading.parse(
          'mv=3980;pct=73;charging=0;valid=1;at=20;age=0',
        ),
      );
      final restored = BatterySample.fromMap(first.toMap());
      expect(first.toMap()['version'], 1);
      expect(restored.receivedAtUTC, first.receivedAtUTC);
      expect(restored.breakBefore, isTrue);
      final oldMap = Map<String, Object?>.from(first.toMap())
        ..remove('breakBefore');
      expect(BatterySample.fromMap(oldMap).breakBefore, isFalse);
      expect(isBatteryDeviceReset(first, wrapped), isFalse);
      expect(isBatteryDeviceReset(wrapped, reset), isFalse);

      final rebooted = BatterySample.fromWire(
        deviceId: 'watch-a',
        receivedAtUTC: DateTime.utc(2026, 10, 22, 12, 3),
        reading: BatteryWireReading.parse(
          'mv=3980;pct=73;charging=0;valid=1;at=2;age=0',
        ),
      );
      expect(isBatteryDeviceReset(reset, rebooted), isTrue);
    });
  });
}
