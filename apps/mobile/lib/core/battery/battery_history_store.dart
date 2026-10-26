import 'package:hive/hive.dart';

import 'battery_sample.dart';

abstract interface class BatteryHistoryStore {
  static const retention = Duration(days: 7);
  static const maxSamplesPerDevice = 10080;

  Future<List<BatterySample>> loadDevice(
    String deviceId, {
    DateTime? now,
  });

  Future<void> save(
    BatterySample sample, {
    DateTime? now,
  });
}

class BatteryHistoryStorageException implements Exception {
  const BatteryHistoryStorageException({
    required this.operation,
    required this.deviceId,
    required this.cause,
  });

  final String operation;
  final String deviceId;
  final Object cause;

  @override
  String toString() =>
      'Battery history $operation failed for $deviceId: $cause';
}

/// Hive implementation for the dedicated `battery_history` box.
///
/// The box itself is opened by the application storage owner. Entries are
/// versioned maps grouped by device, with seven-day and 10,080-sample limits.
class HiveBatteryHistoryStore implements BatteryHistoryStore {
  HiveBatteryHistoryStore(this.box);

  static const boxName = 'battery_history';
  static const retention = BatteryHistoryStore.retention;
  static const maxSamplesPerDevice = BatteryHistoryStore.maxSamplesPerDevice;

  final Box<dynamic> box;

  static String storageKeyFor(String deviceId) => 'device:$deviceId';

  @override
  Future<List<BatterySample>> loadDevice(
    String deviceId, {
    DateTime? now,
  }) async {
    _checkDeviceId(deviceId);
    try {
      final currentTime = (now ?? DateTime.now()).toUtc();
      final samples = _decode(deviceId);
      final kept = _prune(samples, currentTime);
      if (!_sameSamples(samples, kept)) {
        await box.put(storageKeyFor(deviceId), _encode(kept));
      }
      return List.unmodifiable(kept);
    } catch (error) {
      if (error is BatteryHistoryStorageException) rethrow;
      throw BatteryHistoryStorageException(
        operation: 'read/prune',
        deviceId: deviceId,
        cause: error,
      );
    }
  }

  @override
  Future<void> save(
    BatterySample sample, {
    DateTime? now,
  }) async {
    _checkDeviceId(sample.deviceId);
    try {
      final currentTime = (now ?? DateTime.now()).toUtc();
      final samples = _prune(_decode(sample.deviceId), currentTime);
      samples.sort(_compareSamples);
      if (samples.isNotEmpty && _isDuplicateUptime(samples.last, sample)) {
        // A repeated device timestamp is the same measurement in this boot.
        return;
      }
      samples.add(sample);
      samples.sort(_compareSamples);
      final kept = _prune(samples, currentTime);
      await box.put(storageKeyFor(sample.deviceId), _encode(kept));
    } catch (error) {
      if (error is BatteryHistoryStorageException) rethrow;
      throw BatteryHistoryStorageException(
        operation: 'write/prune',
        deviceId: sample.deviceId,
        cause: error,
      );
    }
  }

  List<BatterySample> _decode(String deviceId) {
    final raw = box.get(storageKeyFor(deviceId));
    if (raw == null) return <BatterySample>[];
    if (raw is! List) {
      throw const FormatException('Battery history must be a list.');
    }
    return raw.map((entry) {
      if (entry is! Map) {
        throw const FormatException('Battery history entry must be a map.');
      }
      final sample = BatterySample.fromMap(entry);
      if (sample.deviceId != deviceId) {
        throw const FormatException(
            'Battery history device id does not match its key.');
      }
      return sample;
    }).toList();
  }

  static List<BatterySample> _prune(
    List<BatterySample> source,
    DateTime nowUtc,
  ) {
    final cutoff = nowUtc.subtract(retention);
    final kept = source
        .where((sample) => !sample.receivedAtUTC.isBefore(cutoff))
        .toList()
      ..sort(_compareSamples);
    if (kept.length > maxSamplesPerDevice) {
      return kept.sublist(kept.length - maxSamplesPerDevice);
    }
    return kept;
  }

  static bool _isDuplicateUptime(BatterySample previous, BatterySample next) {
    return previous.deviceId == next.deviceId &&
        previous.uptimeMs == next.uptimeMs &&
        !isBatteryDeviceReset(previous, next);
  }

  static List<Map<String, Object?>> _encode(List<BatterySample> samples) =>
      samples.map((sample) => sample.toMap()).toList(growable: false);

  static bool _sameSamples(
    List<BatterySample> left,
    List<BatterySample> right,
  ) {
    if (left.length != right.length) return false;
    for (var index = 0; index < left.length; index++) {
      final a = left[index];
      final b = right[index];
      if (a.receivedAtUTC != b.receivedAtUTC ||
          a.uptimeMs != b.uptimeMs ||
          a.deviceId != b.deviceId) {
        return false;
      }
    }
    return true;
  }

  static int _compareSamples(BatterySample a, BatterySample b) =>
      a.receivedAtUTC.compareTo(b.receivedAtUTC);

  static void _checkDeviceId(String deviceId) {
    if (deviceId.trim().isEmpty) {
      throw ArgumentError.value(deviceId, 'deviceId', 'must not be empty');
    }
  }
}
