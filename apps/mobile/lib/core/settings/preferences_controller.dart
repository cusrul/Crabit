import 'dart:async';

import 'package:flutter/foundation.dart';

import '../transport/ble_transport.dart';
import 'preferences_gateway.dart';
import 'preferences_protocol.dart';

class PreferencesController extends ChangeNotifier {
  PreferencesController(this.gateway) {
    _ready = gateway.ready;
    gateway.addListener(_linkChanged);
    if (_ready) unawaited(refresh());
  }

  final PreferencesGateway gateway;
  bool _ready = false;
  bool _disposed = false;
  int _epoch = 0;

  bool busy = false;
  WatchPreferences? applied;
  WatchPreferences? draft;
  bool? _deepSleepCapability;
  Map<String, int>? _retryPatch;
  String message = '连接并完成验证后读取手表偏好。';

  bool get ready => _ready;
  bool? get deepSleepCapability => _deepSleepCapability;
  bool get editable => ready && !busy && applied != null && draft != null;
  Map<String, int> get changes =>
      draft != null && applied != null ? draft!.changesFrom(applied!) : {};
  bool get canApply => editable && (changes.isNotEmpty || _retryPatch != null);

  void _linkChanged() {
    if (_disposed || _ready == gateway.ready) return;
    _ready = gateway.ready;
    ++_epoch;
    applied = draft = null;
    _retryPatch = null;
    _deepSleepCapability = null;
    busy = false;
    message = _ready ? '正在读取手表偏好…' : '设备离线，偏好草稿已清除。';
    notifyListeners();
    if (_ready) unawaited(refresh());
  }

  void edit(WatchPreferences value) {
    if (!editable) return;
    draft = value;
    message = '草稿尚未应用。';
    notifyListeners();
  }

  Future<void> refresh() => _run(false);
  Future<void> apply() => _run(true);

  Future<void> _run(bool write) async {
    if (!ready || busy || (write && !canApply)) return;
    // A failed persistence attempt may need the same fields written again,
    // but their values must always come from the CURRENT visible draft.
    final patch = write
        ? <String, int>{
            ...changes,
            for (final key in _retryPatch?.keys ?? const <String>[])
              key: draft!.fields[key]!,
          }
        : <String, int>{};
    final epoch = ++_epoch;
    busy = true;
    message = write ? '正在提交偏好并保存…' : '正在读取手表偏好…';
    if (!write) {
      applied = draft = null;
      _retryPatch = null;
    }
    notifyListeners();

    bool current() => !_disposed && ready && epoch == _epoch;
    try {
      final reply = write ? await gateway.apply(patch) : await gateway.read();
      if (!current()) return;
      if (!write) {
        _handleRead(reply);
      } else {
        _handleWrite(reply, patch);
      }
    } catch (error) {
      if (!current()) return;
      final reason = error is TransportException ? error.code.name : 'protocol';
      if (write) {
        // A timed-out write may have reached the watch. Keep the draft for an
        // explicit user decision, but never replay it automatically.
        message = '未取得保存确认（$reason），设备结果未知；未自动重发。请刷新核对后再手动应用。';
      } else if (error is TransportException &&
          error.code == TransportErrorCode.timeout) {
        message = '读取超时：手表可能使用不支持新偏好协议的旧版固件。偏好暂不可用，现有设置仍可使用。';
      } else {
        message = '偏好读取失败（$reason），新偏好暂不可用；现有设置仍可使用。';
      }
    } finally {
      if (current()) {
        busy = false;
        notifyListeners();
      }
    }
  }

  void _handleRead(PreferencesReply reply) {
    switch (reply.result) {
      case 'snapshot':
        applied = draft = reply.preferences;
        _deepSleepCapability = reply.preferences.deepSleep;
        message = '已读取手表当前偏好；尚未修改设备。';
      case 'busy':
        message = '手表当前正忙，偏好暂不可读；可以稍后手动刷新。';
      case 'invalid':
        message = '手表未接受偏好读取（payload）；新偏好暂不可用，可能是旧版固件。现有设置仍可使用。';
      default:
        message = '手表返回了无法识别的偏好读取结果。';
    }
  }

  void _handleWrite(PreferencesReply reply, Map<String, int> patch) {
    switch (reply.result) {
      case 'saved':
        // The complete reply is the device-authoritative readback.
        applied = draft = reply.preferences;
        _deepSleepCapability = reply.preferences.deepSleep;
        _retryPatch = null;
        message = '手表已确认保存；当前值来自设备回读。';
      case 'save_failed':
        applied = reply.preferences;
        _deepSleepCapability = reply.preferences.deepSleep;
        _retryPatch = patch;
        message = '手表报告保存失败（${reply.error}）；显示值为设备回读，未确认持久化。可手动重试，未自动重发。';
      case 'busy':
        _retryPatch = null;
        message = '手表当前正忙，未确认保存；空闲后可手动重试。';
      case 'invalid':
        _retryPatch = null;
        message = '手表拒绝了偏好草稿（payload），未显示为已保存。';
      default:
        message = '手表返回了无法识别的偏好保存结果。请刷新确认。';
    }
  }

  @override
  void dispose() {
    _disposed = true;
    ++_epoch;
    gateway.removeListener(_linkChanged);
    super.dispose();
  }
}
