import 'dart:async';
import 'package:crabit_watch_app/core/pose/gravity_pose.dart';
import 'package:crabit_watch_app/core/pose/imu_pose_poller.dart';
import 'package:crabit_watch_app/core/pose/imu_sample.dart';
import 'package:flutter_test/flutter_test.dart';

String sample({
  String ax = '0',
  String ay = '0',
  String az = '1',
  String at = '1000',
}) =>
    'ax=$ax;ay=$ay;az=$az;gx=0;gy=0;gz=0;at=$at;age=0;valid=1;mode=tilt';

void main() {
  test(
    'parses gravity-only samples and rejects unsupported or unsafe data',
    () {
      expect(ImuSample.parse(sample()).az, 1);
      expect(
        () => ImuSample.parse(sample().replaceFirst('mode=tilt', 'mode=gyro')),
        throwsFormatException,
      );
      expect(
        () => ImuSample.parse(sample().replaceFirst('gz=0', 'gz=1')),
        throwsFormatException,
      );
      expect(() => ImuSample.parse(sample(ax: 'NaN')), throwsFormatException);
      expect(() => ImuSample.parse(sample(az: '0.1')), throwsFormatException);
      expect(
        () => ImuSample.parse(sample().replaceFirst('age=0', 'age=3000')),
        throwsFormatException,
      );
    },
  );

  test('gravity pose calibrates current tilt and never invents yaw', () {
    final filter = GravityPoseFilter();
    final tilted = filter.update(
      ImuSample.parse(sample(ax: '0.5', az: '0.866', at: '1000')),
    );
    expect(tilted.pitch, lessThan(-0.2));
    final zero = filter.calibrate();
    expect(zero.pitch, 0);
    expect(
      filter
          .update(ImuSample.parse(sample(ax: '0.5', az: '0.866', at: '1200')))
          .pitch,
      closeTo(0, 1e-9),
    );
    expect(tilted.yaw, 0);
    expect(tilted.quaternion[3], closeTo(1, 1));
  });

  test(
      'poller expires stale samples and clears pose when background reads stop',
      () async {
    var now = DateTime.utc(2026);
    var readable = true;
    final poller = ImuPosePoller(
      read: () async => sample(ax: '0.5', az: '0.866'),
      canReadBackground: () => readable,
      clock: () => now,
      period: const Duration(milliseconds: 15),
    );
    addTearDown(poller.dispose);
    poller.setActive(true);
    poller.setEnabled(true);
    await Future<void>.delayed(const Duration(milliseconds: 5));
    expect(poller.pose, isNotNull);
    expect(poller.calibrate()?.pitch, 0);
    await Future<void>.delayed(const Duration(milliseconds: 25));
    expect(poller.pose?.pitch, closeTo(0, 1e-9));
    readable = false;
    now = now.add(const Duration(seconds: 2));
    await Future<void>.delayed(const Duration(milliseconds: 20));
    expect(poller.pose, isNull);
    expect(poller.stale, isFalse);
  });

  test(
    'poller is opt-in, serialized, and ignores late hidden replies',
    () async {
      final pending = Completer<String>();
      var reads = 0;
      final poller = ImuPosePoller(
        read: () {
          reads++;
          return pending.future;
        },
        canReadBackground: () => true,
        period: const Duration(milliseconds: 20),
      );
      addTearDown(poller.dispose);
      poller.setActive(true);
      await Future<void>.delayed(const Duration(milliseconds: 45));
      expect(reads, 0);
      poller.setEnabled(true);
      await Future<void>.delayed(const Duration(milliseconds: 45));
      expect(reads, 1);
      poller.setActive(false);
      pending.complete(sample());
      await Future<void>.delayed(const Duration(milliseconds: 5));
      expect(poller.pose, isNull);
      expect(poller.status, '预览不可见');
    },
  );
}
