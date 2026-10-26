import 'dart:async';
import 'package:flutter/foundation.dart';
import 'gravity_pose.dart';
import 'imu_sample.dart';

typedef ImuReader = Future<String> Function();
typedef PoseClock = DateTime Function();

/// Opt-in, one-request-at-a-time 5 Hz sampler. A stopped generation can never
/// publish a late reply into a hidden/disconnected preview.
class ImuPosePoller extends ChangeNotifier {
  ImuPosePoller({
    required this.read,
    required this.canReadBackground,
    PoseClock? clock,
    this.period = const Duration(milliseconds: 200),
  }) : _clock = clock ?? DateTime.now;
  final ImuReader read;
  final bool Function() canReadBackground;
  final PoseClock _clock;
  final Duration period;
  final GravityPoseFilter _filter = GravityPoseFilter();
  Timer? _timer;
  bool _enabled = false, _active = false, _pending = false, _disposed = false;
  int _generation = 0;
  DateTime? _receivedAt;
  GravityPose? _pose;
  String _status = '未启用';
  bool get enabled => _enabled;
  GravityPose? get pose => _pose;
  String get status => _status;
  bool get stale =>
      _pose != null &&
      _receivedAt != null &&
      _clock().difference(_receivedAt!) > const Duration(milliseconds: 1200);

  void setActive(bool active) {
    if (_active == active) return;
    _active = active;
    if (!active) {
      _stop();
      _status = _enabled ? '预览不可见' : '未启用';
      _pose = null;
    } else if (_enabled) {
      _start();
    }
    notifyListeners();
  }

  void setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
      _stop();
      _pose = null;
      _status = '未启用';
    } else if (_active) {
      _start();
    } else {
      _status = '等待预览';
    }
    notifyListeners();
  }

  GravityPose? calibrate() {
    if (_pose == null || stale) return null;
    _pose = _filter.calibrate();
    notifyListeners();
    return _pose;
  }

  void _start() {
    if (_disposed || _timer != null) return;
    _status = '等待 IMU';
    final generation = ++_generation;
    _timer = Timer.periodic(period, (_) => unawaited(_poll(generation)));
    unawaited(_poll(generation));
  }

  void _stop() {
    ++_generation;
    _timer?.cancel();
    _timer = null;
    _filter.reset();
    _receivedAt = null;
  }

  Future<void> _poll(int generation) async {
    if (_disposed ||
        !_enabled ||
        !_active ||
        _pending ||
        generation != _generation) {
      return;
    }
    bool readable;
    try {
      readable = canReadBackground();
    } catch (_) {
      readable = false;
    }
    if (!readable) {
      if (stale) {
        _pose = null;
        _filter.reset();
      }
      _status = '通信忙或链路未就绪';
      notifyListeners();
      return;
    }
    _pending = true;
    try {
      final sample = ImuSample.parse(await read());
      if (_disposed || generation != _generation || !_active || !_enabled) {
        return;
      }
      _pose = _filter.update(sample);
      _receivedAt = _clock();
      _status = '倾斜已更新 · 无航向';
      notifyListeners();
    } catch (_) {
      if (!_disposed && generation == _generation) {
        _pose = null;
        _status = 'IMU 无数据 / 已过期';
        notifyListeners();
      }
    } finally {
      _pending = false;
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _stop();
    super.dispose();
  }
}
