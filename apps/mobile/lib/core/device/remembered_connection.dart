import 'dart:async';
import 'package:flutter/foundation.dart';
import 'reconnect_policy.dart';

/// Timer/storage adapter shared by the manager and host tests; no radio on init
/// until explicitly enabled AND a foreground owner is present.
class RememberedConnection extends ChangeNotifier {
  RememberedConnection({
    required this.readSetting,
    required this.putSetting,
    required this.connectRemembered,
    required this.cancelAutomatic,
    required this.isReady,
    required this.isOccupied,
  }) {
    _store = RememberedDeviceStore(
      read: () => _readId(),
      write: (id) => putSetting(deviceKey, id),
    );
  }

  static const deviceKey = 'ble_last_verified_remote_id_v1';
  static const enabledKey = 'ble_foreground_reconnect_v1';
  final dynamic Function(String) readSetting;
  final Future<void> Function(String, dynamic) putSetting;
  final Future<void> Function(String) connectRemembered;
  final Future<void> Function() cancelAutomatic;
  final bool Function() isReady;
  final bool Function() isOccupied;
  final ReconnectPolicy _policy = ReconnectPolicy();
  late final RememberedDeviceStore _store;
  Timer? _timer;
  bool _initialized = false;
  bool _disposed = false;
  bool _running = false;
  bool _cancelling = false;
  ReconnectAttempt? _activeAttempt;
  bool saving = false;
  String status = '自动重连默认关闭';
  bool get enabled => _policy.enabled;
  bool get hasRememberedDevice => _policy.rememberedId != null;
  bool get retryPending => _timer != null || _running;
  bool get automaticRunning => _running;
  bool get automaticAttemptCurrent =>
      _running && _activeAttempt != null && _policy.accepts(_activeAttempt!);
  int get attempts => _policy.attempts;

  String? _readId() {
    final value = readSetting(deviceKey);
    return value is String && value.isNotEmpty ? value : null;
  }

  void initialize() {
    if (_initialized || _disposed) return;
    try {
      _policy.rememberedId = _readId();
      _policy.enabled = readSetting(enabledKey) == true;
      _initialized = true;
      status = enabled ? '仅前台重连已记住的手表' : '自动重连已关闭';
    } catch (_) {
      status = '本地连接设置不可用';
    }
    _notify();
  }

  Future<void> setEnabled(bool value) async {
    if (saving || _disposed) return;
    initialize();
    saving = true;
    // Disabling revokes timers immediately, even if durable storage fails.
    if (!value) {
      _policy.enabled = false;
      _cancel();
    }
    _notify();
    try {
      await putSetting(enabledKey, value);
      if (_disposed) return;
      _policy.enabled = value;
      _policy.invalidate(resetBudget: true);
      status = value ? '仅前台重连已记住的手表' : '自动重连已关闭';
      if (value) _schedule();
    } catch (_) {
      status = '连接设置保存失败；未启用新的自动重连';
    } finally {
      saving = false;
      _notify();
    }
  }

  void setForeground(bool value) {
    initialize();
    if (_policy.foreground == value || _disposed) return;
    _policy.foreground = value;
    _cancel();
    if (value) {
      _policy.invalidate(resetBudget: true);
      _schedule();
    }
  }

  void explicitConnect({required bool diagnostic}) {
    _cancel();
    _policy.diagnostic = diagnostic;
    _policy.userStopped = false;
    _policy.faulted = false;
    _policy.invalidate(resetBudget: true);
    _notify();
  }

  void stopByUser() {
    _policy.userStopped = true;
    _cancel();
    status = '已手动断开；自动重连已暂停';
    _notify();
  }

  void enterDiagnostics() {
    _policy.diagnostic = true;
    // Do not tear down a diagnostic operation or hide its failing device state.
    _cancel(cancelNative: false);
    status = '诊断会话不自动重连';
    _notify();
  }

  void fault() {
    _policy.faulted = true;
    _cancel(cancelNative: false);
    status = '连接异常；请检查后手动重试';
    _notify();
  }

  void connectionLost({required bool wasVerified}) {
    if (!wasVerified && !_running) return;
    if (!_running) _policy.invalidate(resetBudget: true);
    _schedule();
  }

  Future<void> rememberVerified(String id, bool Function() isCurrent) async {
    try {
      if (await _store.saveVerified(id, () => !_disposed && isCurrent())) {
        _policy.rememberedId = id;
        status = '已记住已验证的手表（仅本机）';
        _notify();
      }
    } catch (_) {
      status = '手表已验证，但记住设备失败';
      _notify();
    }
  }

  void _cancel({bool cancelNative = true}) {
    _timer?.cancel();
    _timer = null;
    _policy.invalidate();
    if (_running && cancelNative && !_cancelling) {
      unawaited(_cancelAndReconcile());
    }
    _notify();
  }

  Future<void> _cancelAndReconcile() async {
    _cancelling = true;
    try {
      await cancelAutomatic();
    } catch (_) {
      // Manager status owns teardown errors; policy still gates any retry.
    } finally {
      _cancelling = false;
      if (!_disposed) _schedule();
    }
  }

  void _schedule() {
    if (_disposed || _running || _timer != null || isReady() || isOccupied()) {
      return;
    }
    final attempt = _policy.next();
    if (attempt == null) {
      if (_policy.eligible && attempts == ReconnectPolicy.backoff.length) {
        status = '自动重连已达 3 次；请手动连接';
        _notify();
      }
      return;
    }
    status = '前台重连 ${attempt.number}/3，${attempt.delay.inSeconds} 秒后尝试';
    _timer = Timer(attempt.delay, () => unawaited(_attempt(attempt)));
    _notify();
  }

  Future<void> _attempt(ReconnectAttempt attempt) async {
    _timer = null;
    if (_disposed || !_policy.accepts(attempt) || isReady() || isOccupied()) {
      _notify();
      return;
    }
    _running = true;
    _activeAttempt = attempt;
    status = '正在前台重连 ${attempt.number}/3';
    _notify();
    try {
      await connectRemembered(attempt.remoteId);
    } catch (_) {
      // The manager retains sanitized failure details; no payload/native logging.
    } finally {
      _running = false;
      if (identical(_activeAttempt, attempt)) _activeAttempt = null;
      if (!_disposed) {
        if (_policy.accepts(attempt) && isReady()) {
          status = '已重新连接已记住的手表';
        } else {
          // A revoked attempt may be the event that clears manager occupancy for
          // a newer eligible foreground epoch.
          _schedule();
        }
      }
      _notify();
    }
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _cancel();
    _disposed = true;
    super.dispose();
  }
}
