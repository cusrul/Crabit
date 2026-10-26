import 'dart:math' as math;
import 'imu_sample.dart';

/// Relative gravity-derived tilt. Yaw is intentionally always zero: no
/// magnetometer or enabled gyroscope is present in this firmware data path.
class GravityPose {
  const GravityPose(this.roll, this.pitch, this.yaw);
  final double roll, pitch, yaw;

  List<double> get quaternion {
    final cr = math.cos(roll / 2), sr = math.sin(roll / 2);
    final cp = math.cos(pitch / 2), sp = math.sin(pitch / 2);
    return [sr * cp, cr * sp, -sr * sp, cr * cp];
  }
}

class GravityPoseFilter {
  double _zeroRoll = 0, _zeroPitch = 0;
  double? _roll, _pitch;
  int? _lastAt;

  GravityPose update(ImuSample sample) {
    final roll = math.atan2(sample.ay, sample.az);
    final pitch = math.atan2(
      -sample.ax,
      math.sqrt(sample.ay * sample.ay + sample.az * sample.az),
    );
    final previousAt = _lastAt;
    final dt = previousAt == null ? 0.2 : (sample.at - previousAt) / 1000.0;
    _lastAt = sample.at;
    if (_roll == null || dt <= 0 || dt > 1) {
      _roll = roll;
      _pitch = pitch;
    } else {
      final alpha = 1 - math.exp(-dt.clamp(0.02, 0.5) / 0.18);
      _roll = _roll! + _wrap(roll - _roll!) * alpha;
      _pitch = _pitch! + _wrap(pitch - _pitch!) * alpha;
    }
    return GravityPose(
      _wrap(_roll! - _zeroRoll),
      _wrap(_pitch! - _zeroPitch),
      0,
    );
  }

  GravityPose calibrate() {
    _zeroRoll = _roll ?? 0;
    _zeroPitch = _pitch ?? 0;
    return GravityPose(0, 0, 0);
  }

  void reset() {
    _roll = null;
    _pitch = null;
    _lastAt = null;
    _zeroRoll = 0;
    _zeroPitch = 0;
  }

  static double _wrap(double angle) =>
      math.atan2(math.sin(angle), math.cos(angle));
}
