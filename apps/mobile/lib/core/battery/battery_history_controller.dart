import 'dart:async';

import 'package:flutter/foundation.dart';

import 'battery_sample.dart';
import 'battery_history_store.dart';

abstract interface class BatteryHistoryGateway implements Listenable {
  bool get ready;
  String? get deviceId;

  /// Sends one `diag/battery` request over an already-owned connection.
  Future<String> requestBattery();
}

typedef BatteryPeriodicTimerFactory = Timer Function(
  Duration interval,
  void Function() onTick,
);

class BatteryHistoryController extends ChangeNotifier {
  BatteryHistoryController({
    required BatteryHistoryGateway gateway,
    required BatteryHistoryStore store,
    DateTime Function()? now,
    bool Function()? canSample,
    BatteryPeriodicTimerFactory? periodicTimerFactory,
    this.pollInterval = const Duration(seconds: 60),
  })  : _gateway = gateway,
        _store = store,
        _now = now ?? DateTime.now,
        _canSample = canSample,
        _timerFactory = periodicTimerFactory ?? _createPeriodicTimer {
    if (pollInterval <= Duration.zero) {
      throw ArgumentError.value(
          pollInterval, 'pollInterval', 'must be positive');
    }
    _gateway.addListener(_gatewayChanged);
    final id = _normalizedDeviceId(_gateway.deviceId);
    initialized = id == null ? Future<void>.value() : _activateDevice(id);
  }

  static const maxPersistInterval = Duration(minutes: 1);
  static const disconnectGap = Duration(minutes: 2);

  final BatteryHistoryGateway _gateway;
  final BatteryHistoryStore _store;
  final DateTime Function() _now;
  final bool Function()? _canSample;
  final BatteryPeriodicTimerFactory _timerFactory;
  final Duration pollInterval;

  late final Future<void> initialized;
  Timer? _timer;
  int _epoch = 0;
  bool _disposed = false;
  bool _loading = false;
  bool _ready = false;
  bool _requestInFlight = false;
  bool _breakBeforeNextSample = false;
  int? _pendingReadyEpoch;
  String? _deviceId;
  String? _storageError;
  String? _requestError;
  bool _storageErrorFromLoad = false;
  DateTime? _lastPersistedAt;
  BatterySample? _latestSample;
  List<BatterySample> _samples = <BatterySample>[];
  Future<void> _lastRequest = Future<void>.value();

  String? get deviceId => _deviceId;
  bool get isReady => _ready;
  bool get isLoading => _loading;
  bool get isBusy => _requestInFlight;
  String? get errorMessage => _storageError ?? _requestError;
  BatterySample? get latestSample => _latestSample;
  List<BatterySample> get samples => List.unmodifiable(_samples);
  Future<void> get lastRequest => _lastRequest;

  bool get canRequestNow {
    if (_disposed || !_ready || _loading || _requestInFlight) return false;
    try {
      return _canSample?.call() ?? true;
    } catch (_) {
      return false;
    }
  }

  bool get latestIsStale {
    final sample = _latestSample;
    if (sample == null || !sample.isValid) return true;
    return !_ready ||
        _now().toUtc().difference(sample.receivedAtUTC) > disconnectGap;
  }

  String get statusText {
    if (_loading) return '正在读取本地历史';
    if (errorMessage != null) return errorMessage!;
    if (_deviceId == null) return '等待已连接设备';
    if (_ready) return _requestInFlight ? '正在读取电量' : '已连接 · 每分钟读取一次';
    return '已断开 · 显示此前保存的记录';
  }

  /// Persisted history plus the newest live reading when it is a new point.
  List<BatterySample> get chartSamples {
    final latest = _latestSample;
    if (latest == null || _samples.isEmpty) return samples;
    final previous = _samples.last;
    if (previous.uptimeMs == latest.uptimeMs &&
        !isBatteryDeviceReset(previous, latest)) {
      return samples;
    }
    return List.unmodifiable(
      _pruneSamples(<BatterySample>[..._samples, latest], _now().toUtc()),
    );
  }

  Future<void> sampleNow() {
    final operation = _poll(_epoch);
    _lastRequest = operation;
    return operation;
  }

  /// Copyable CSV text for an explicit export action; this writes no clipboard.
  String exportCsv() {
    final rows = <String>[
      'device_id,received_at_utc,uptime_ms,voltage_mv,percent,charging,valid,age_ms,break_before',
    ];
    for (final sample in chartSamples) {
      final escapedId = '"${sample.deviceId.replaceAll('"', '""')}"';
      rows.add([
        escapedId,
        sample.receivedAtUTC.toUtc().toIso8601String(),
        sample.uptimeMs,
        sample.voltageMv,
        sample.percent,
        sample.charging ? 1 : 0,
        sample.isValid ? 1 : 0,
        sample.ageMs,
        sample.breakBefore ? 1 : 0,
      ].join(','));
    }
    return rows.join('\n');
  }

  Future<void> _activateDevice(String deviceId) async {
    final epoch = ++_epoch;
    _timer?.cancel();
    _timer = null;
    _pendingReadyEpoch = null;
    _ready = false;
    _loading = true;
    _deviceId = deviceId;
    _samples = <BatterySample>[];
    _latestSample = null;
    _breakBeforeNextSample = false;
    _lastPersistedAt = null;
    _storageError = null;
    _requestError = null;
    _storageErrorFromLoad = false;
    _notify();

    try {
      final loaded = await _store.loadDevice(deviceId, now: _now().toUtc());
      if (!_isCurrentDevice(epoch, deviceId)) return;
      _samples = _pruneSamples(loaded, _now().toUtc());
      if (_samples.isNotEmpty) {
        _latestSample = _samples.last;
        _lastPersistedAt = _samples.last.receivedAtUTC;
      }
    } catch (error) {
      if (!_isCurrentDevice(epoch, deviceId)) return;
      _storageError = _describeError(error, operation: 'load');
      _storageErrorFromLoad = true;
    }
    if (!_isCurrentDevice(epoch, deviceId)) return;
    _loading = false;
    if (_gateway.ready && _normalizedDeviceId(_gateway.deviceId) == deviceId) {
      _beginSession(epoch);
    } else {
      _notify();
    }
  }

  void _gatewayChanged() {
    if (_disposed) return;
    final id = _normalizedDeviceId(_gateway.deviceId);
    if (id != null && id != _deviceId) {
      unawaited(_activateDevice(id));
      return;
    }
    if (_loading) return;
    final shouldBeReady = _gateway.ready && id != null && id == _deviceId;
    if (shouldBeReady == _ready) return;
    final epoch = ++_epoch;
    _timer?.cancel();
    _timer = null;
    if (shouldBeReady) {
      _beginSession(epoch);
    } else {
      _ready = false;
      _pendingReadyEpoch = null;
      _notify();
    }
  }

  void _beginSession(int epoch) {
    if (_disposed || epoch != _epoch || _loading) return;
    _ready = true;
    _breakBeforeNextSample = true;
    _timer?.cancel();
    _timer = _timerFactory(pollInterval, () {
      _launchPoll(epoch);
    });
    _notify();
    if (_requestInFlight) {
      // A readiness transition is a new sampling trigger, not a retry of the
      // response that is still unwinding from the previous session.
      _pendingReadyEpoch = epoch;
    } else {
      _launchPoll(epoch);
    }
  }

  void _launchPoll(int epoch) {
    final operation = _poll(epoch);
    _lastRequest = operation;
    unawaited(operation);
  }

  Future<void> _poll(int epoch) async {
    final deviceId = _deviceId;
    if (deviceId == null ||
        !_isCurrentSession(epoch, deviceId) ||
        !canRequestNow) {
      return;
    }
    _requestInFlight = true;
    _requestError = null;
    _notify();
    try {
      final raw = await _gateway.requestBattery();
      if (!_isCurrentSession(epoch, deviceId)) return;
      final receivedAt = _now().toUtc();
      final sample = BatterySample.parse(
        raw,
        deviceId: deviceId,
        receivedAtUTC: receivedAt,
        breakBefore: _breakBeforeNextSample,
      );
      _latestSample = sample;
      _requestError = null;

      final previous = _samples.isEmpty ? null : _samples.last;
      final duplicateUptime = previous != null &&
          previous.uptimeMs == sample.uptimeMs &&
          !isBatteryDeviceReset(previous, sample);
      final elapsedSinceSave = _lastPersistedAt == null
          ? null
          : receivedAt.difference(_lastPersistedAt!);
      final canPersistNow =
          elapsedSinceSave == null || elapsedSinceSave >= maxPersistInterval;
      if (!duplicateUptime && canPersistNow) {
        await _store.save(sample, now: receivedAt);
        if (!_isCurrentSession(epoch, deviceId)) return;
        _samples =
            _pruneSamples(<BatterySample>[..._samples, sample], receivedAt);
        _lastPersistedAt = receivedAt;
        _breakBeforeNextSample = false;
        if (!_storageErrorFromLoad) _storageError = null;
      }
    } catch (error) {
      if (_isCurrentSession(epoch, deviceId)) {
        if (error is BatteryHistoryStorageException) {
          _storageError = _describeError(error, operation: 'sample');
          _storageErrorFromLoad = false;
        } else {
          _requestError = _describeError(error, operation: 'sample');
        }
      }
    } finally {
      _requestInFlight = false;
      final pendingEpoch = _pendingReadyEpoch;
      _pendingReadyEpoch = null;
      _notify();
      final currentDeviceId = _deviceId;
      if (pendingEpoch != null &&
          currentDeviceId != null &&
          _isCurrentSession(pendingEpoch, currentDeviceId)) {
        _launchPoll(pendingEpoch);
      }
    }
  }

  bool _isCurrentDevice(int epoch, String deviceId) =>
      !_disposed && epoch == _epoch && _deviceId == deviceId;

  bool _isCurrentSession(int epoch, String deviceId) =>
      _isCurrentDevice(epoch, deviceId) &&
      _ready &&
      _gateway.ready &&
      _normalizedDeviceId(_gateway.deviceId) == deviceId;

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  static Timer _createPeriodicTimer(
          Duration interval, void Function() onTick) =>
      Timer.periodic(interval, (_) => onTick());

  static String? _normalizedDeviceId(String? deviceId) {
    if (deviceId == null || deviceId.trim().isEmpty) return null;
    return deviceId;
  }

  static int _compareSamples(BatterySample a, BatterySample b) =>
      a.receivedAtUTC.compareTo(b.receivedAtUTC);

  static List<BatterySample> _pruneSamples(
    Iterable<BatterySample> source,
    DateTime nowUtc,
  ) {
    final cutoff = nowUtc.toUtc().subtract(BatteryHistoryStore.retention);
    final kept = source
        .where((sample) => !sample.receivedAtUTC.isBefore(cutoff))
        .toList()
      ..sort(_compareSamples);
    if (kept.length > BatteryHistoryStore.maxSamplesPerDevice) {
      return kept
          .sublist(kept.length - BatteryHistoryStore.maxSamplesPerDevice);
    }
    return kept;
  }

  static String _describeError(Object error, {required String operation}) {
    if (error is BatteryHistoryStorageException) {
      return operation == 'load' ? '本地电池历史读取失败' : '本地电池历史保存失败';
    }
    if (error is FormatException) return '设备返回的电量数据无效';
    return '电量读取失败，请检查连接后手动重试';
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    ++_epoch;
    _timer?.cancel();
    _timer = null;
    _gateway.removeListener(_gatewayChanged);
    super.dispose();
  }
}
