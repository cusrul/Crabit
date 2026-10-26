import 'dart:math' as math;

/// QMI8658 gravity sample returned by the read-only diag/imu response.
class ImuSample {
  const ImuSample(
      {required this.ax,
      required this.ay,
      required this.az,
      required this.at,
      required this.age});
  final double ax, ay, az; // g
  final int at, age; // milliseconds

  static ImuSample parse(String input) {
    final fields = <String, String>{};
    for (final part in input.split(';')) {
      final split = part.indexOf('=');
      if (split <= 0 || fields.containsKey(part.substring(0, split))) {
        throw const FormatException('invalid IMU fields');
      }
      fields[part.substring(0, split)] = part.substring(split + 1);
    }
    const required = ['ax', 'ay', 'az', 'gx', 'gy', 'gz', 'at', 'age', 'valid'];
    if ((fields.length != required.length &&
            fields.length != required.length + 1) ||
        required.any((key) => !fields.containsKey(key)) ||
        (fields.length == required.length + 1 && fields['mode'] != 'tilt') ||
        fields['valid'] != '1' ||
        ['gx', 'gy', 'gz'].any((key) => double.tryParse(fields[key]!) != 0)) {
      throw const FormatException('invalid or unsupported IMU mode');
    }
    final ax = double.tryParse(fields['ax']!);
    final ay = double.tryParse(fields['ay']!);
    final az = double.tryParse(fields['az']!);
    final at = int.tryParse(fields['at']!);
    final age = int.tryParse(fields['age']!);
    if (ax == null ||
        ay == null ||
        az == null ||
        !ax.isFinite ||
        !ay.isFinite ||
        !az.isFinite ||
        at == null ||
        age == null ||
        at < 0 ||
        age < 0 ||
        age > 1000) {
      throw const FormatException('invalid or stale IMU sample');
    }
    final magnitude = math.sqrt(ax * ax + ay * ay + az * az);
    if (magnitude < 0.65 || magnitude > 1.35) {
      throw const FormatException('acceleration is not gravity-dominant');
    }
    return ImuSample(ax: ax, ay: ay, az: az, at: at, age: age);
  }
}
