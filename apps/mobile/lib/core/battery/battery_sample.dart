/// Battery values returned by the `diag/battery` diagnostic.
///
/// The device timestamp is its uptime when the battery reading was captured.
/// The payload age is kept separately so old or invalid device data is never
/// mistaken for a genuine zero-percent reading.
class BatteryWireReading {
  const BatteryWireReading({
    required this.voltageMv,
    required this.percent,
    required this.charging,
    required this.valid,
    required this.uptimeMs,
    required this.ageMs,
  });

  static const maxPayloadLength = 191;
  static const maxUint32 = 0xffffffff;

  final int voltageMv;
  final int percent;
  final bool charging;
  final bool valid;
  final int uptimeMs;
  final int ageMs;

  static const _keys = <String>{'mv', 'pct', 'charging', 'valid', 'at', 'age'};

  factory BatteryWireReading.parse(String raw) {
    if (raw.length > maxPayloadLength ||
        raw.codeUnits.any((unit) => unit > 0x7f)) {
      throw const FormatException(
          'Battery payload must be at most 191 ASCII characters.');
    }

    final values = <String, String>{};
    for (final field in raw.split(';')) {
      final separator = field.indexOf('=');
      if (separator <= 0 || separator != field.lastIndexOf('=')) {
        throw const FormatException('Malformed battery field.');
      }
      final key = field.substring(0, separator);
      final value = field.substring(separator + 1);
      if (!_keys.contains(key)) {
        throw FormatException('Unknown battery field: $key');
      }
      if (values.containsKey(key)) {
        throw FormatException('Duplicate battery field: $key');
      }
      values[key] = value;
    }
    if (values.length != _keys.length ||
        !values.keys.toSet().containsAll(_keys)) {
      throw const FormatException(
          'Battery payload is missing a required field.');
    }

    int decimal(String key) {
      final value = values[key]!;
      if (!RegExp(r'^\d+$').hasMatch(value)) {
        throw FormatException('Battery field $key must be a decimal integer.');
      }
      final parsed = int.tryParse(value);
      if (parsed == null) {
        throw FormatException('Battery field $key is out of range.');
      }
      return parsed;
    }

    final voltageMv = decimal('mv');
    final percent = decimal('pct');
    final charging = decimal('charging');
    final valid = decimal('valid');
    final uptimeMs = decimal('at');
    final ageMs = decimal('age');
    if (percent > 100) {
      throw const FormatException(
          'Battery percentage must be between 0 and 100.');
    }
    if (charging > 1 || valid > 1) {
      throw const FormatException('Battery flags must be 0 or 1.');
    }
    if (uptimeMs > maxUint32 || ageMs > maxUint32) {
      throw const FormatException(
          'Battery timestamp and age must be uint32 values.');
    }

    return BatteryWireReading(
      voltageMv: voltageMv,
      percent: percent,
      charging: charging == 1,
      valid: valid == 1,
      uptimeMs: uptimeMs,
      ageMs: ageMs,
    );
  }
}

class BatterySample {
  const BatterySample({
    required this.deviceId,
    required this.receivedAtUTC,
    required this.uptimeMs,
    required this.voltageMv,
    required this.percent,
    required this.charging,
    required this.deviceValid,
    required this.ageMs,
    this.breakBefore = false,
  });

  static const maxFreshAgeMs = 5000;
  static const minPlausibleVoltageMv = 501;
  static const maxPlausibleVoltageMv = 5999;
  static const maxUint32 = BatteryWireReading.maxUint32;

  final String deviceId;
  final DateTime receivedAtUTC;
  final int uptimeMs;
  final int voltageMv;
  final int percent;
  final bool charging;
  final bool deviceValid;
  final int ageMs;

  /// Marks a known collection-session boundary before this sample.
  final bool breakBefore;

  /// True only when the device reading is fresh, flagged valid, and plausible.
  bool get isValid =>
      deviceValid &&
      ageMs <= maxFreshAgeMs &&
      voltageMv >= minPlausibleVoltageMv &&
      voltageMv <= maxPlausibleVoltageMv;

  factory BatterySample.parse(
    String raw, {
    required String deviceId,
    required DateTime receivedAtUTC,
    bool breakBefore = false,
  }) =>
      BatterySample.fromWire(
        deviceId: deviceId,
        receivedAtUTC: receivedAtUTC,
        reading: BatteryWireReading.parse(raw),
        breakBefore: breakBefore,
      );

  factory BatterySample.fromWire({
    required String deviceId,
    required DateTime receivedAtUTC,
    required BatteryWireReading reading,
    bool breakBefore = false,
  }) {
    if (deviceId.trim().isEmpty) {
      throw ArgumentError.value(deviceId, 'deviceId', 'must not be empty');
    }
    return BatterySample(
      deviceId: deviceId,
      receivedAtUTC: receivedAtUTC.toUtc(),
      uptimeMs: reading.uptimeMs,
      voltageMv: reading.voltageMv,
      percent: reading.percent,
      charging: reading.charging,
      deviceValid: reading.valid,
      ageMs: reading.ageMs,
      breakBefore: breakBefore,
    );
  }

  Map<String, Object?> toMap() => {
        'version': 1,
        'deviceId': deviceId,
        'receivedAtUtcMs': receivedAtUTC.toUtc().millisecondsSinceEpoch,
        'uptimeMs': uptimeMs,
        'voltageMv': voltageMv,
        'percent': percent,
        'charging': charging,
        'deviceValid': deviceValid,
        'ageMs': ageMs,
        'breakBefore': breakBefore,
      };

  factory BatterySample.fromMap(Map<dynamic, dynamic> map) {
    int integer(String key) {
      final value = map[key];
      if (value is! int) throw FormatException('Invalid battery field: $key');
      return value;
    }

    final version = integer('version');
    if (version != 1) {
      throw FormatException('Unsupported battery history version: $version');
    }
    final deviceId = map['deviceId'];
    final charging = map['charging'];
    final deviceValid = map['deviceValid'];
    final breakBefore = map['breakBefore'] ?? false;
    if (deviceId is! String || deviceId.trim().isEmpty) {
      throw const FormatException('Invalid battery device id.');
    }
    if (charging is! bool || deviceValid is! bool || breakBefore is! bool) {
      throw const FormatException('Invalid battery flags.');
    }
    final receivedAtMs = integer('receivedAtUtcMs');
    final uptimeMs = integer('uptimeMs');
    final voltageMv = integer('voltageMv');
    final percent = integer('percent');
    final ageMs = integer('ageMs');
    if (uptimeMs < 0 ||
        uptimeMs > maxUint32 ||
        ageMs < 0 ||
        ageMs > maxUint32 ||
        voltageMv < 0 ||
        percent < 0 ||
        percent > 100) {
      throw const FormatException('Battery history value is out of range.');
    }
    return BatterySample(
      deviceId: deviceId,
      receivedAtUTC:
          DateTime.fromMillisecondsSinceEpoch(receivedAtMs, isUtc: true),
      uptimeMs: uptimeMs,
      voltageMv: voltageMv,
      percent: percent,
      charging: charging,
      deviceValid: deviceValid,
      ageMs: ageMs,
      breakBefore: breakBefore,
    );
  }
}

/// A decrease is a reboot/reset unless it is the normal uint32 uptime wrap.
bool isBatteryDeviceReset(BatterySample previous, BatterySample next) {
  if (next.uptimeMs >= previous.uptimeMs) return false;
  final isUint32Wrap =
      previous.uptimeMs > 0xf0000000 && next.uptimeMs < 0x0fffffff;
  return !isUint32Wrap;
}
