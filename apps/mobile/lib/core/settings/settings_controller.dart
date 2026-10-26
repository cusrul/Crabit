import 'dart:async';
import 'package:flutter/foundation.dart';
import '../transport/ble_transport.dart';
import 'settings_gateway.dart';
import 'settings_protocol.dart';

class SettingsController extends ChangeNotifier {
  SettingsController(this.gateway) {
    _ready = gateway.ready;
    gateway.addListener(_linkChanged);
    if (_ready) unawaited(refresh());
  }
  final SettingsGateway gateway;
  bool _ready = false, _disposed = false;
  int _epoch = 0;
  bool busy = false;
  WatchSettings? applied, draft;
  Map<String, int>? _unsavedPatch;
  String message = '连接并完成验证后读取手表设置。';
  bool get ready => _ready;
  bool get editable => ready && !busy && applied != null;
  Map<String, int> get changes =>
      draft != null && applied != null ? draft!.changesFrom(applied!) : {};
  bool get canApply =>
      editable && (changes.isNotEmpty || _unsavedPatch != null);
  void _linkChanged() {
    if (_disposed || _ready == gateway.ready) return;
    _ready = gateway.ready;
    ++_epoch;
    applied = draft = null;
    _unsavedPatch = null;
    busy = false;
    message = _ready ? '正在读取手表设置…' : '设备离线，设置不可用。';
    notifyListeners();
    if (_ready) unawaited(refresh());
  }

  void edit(WatchSettings value) {
    if (!editable) return;
    draft = value;
    message = '草稿尚未应用。';
    notifyListeners();
  }

  Future<void> refresh() => _run(false);
  Future<void> apply() => _run(true);
  Future<void> _run(bool write) async {
    if (!ready || busy || (write && !canApply)) return;
    final patch = write ? {...?_unsavedPatch, ...changes} : <String, int>{};
    final epoch = ++_epoch;
    busy = true;
    message = write ? '正在应用并保存…' : '正在读取手表设置…';
    if (!write) {
      applied = draft = null;
      _unsavedPatch = null;
    }
    notifyListeners();
    bool current() => !_disposed && ready && epoch == _epoch;
    try {
      final reply = write ? await gateway.apply(patch) : await gateway.read();
      if (!current()) return;
      switch (reply.result) {
        case 'snapshot':
          applied = draft = reply.settings;
          message = '已读取手表当前配置（不是保存操作）。';
        case 'saved':
          applied = draft = reply.settings;
          _unsavedPatch = null;
          message = reply.display == 'deferred'
              ? '配置已保存到 Flash；屏幕亮度/反色将在手表下次正常应用时生效。'
              : '配置已应用并保存到 Flash。';
        case 'save_failed':
          applied = draft = reply.settings;
          _unsavedPatch = patch;
          message = reply.display == 'deferred'
              ? '配置已应用到 RAM，但保存失败（${reply.error}）；屏幕效果待下次正常应用。可重试，断电可能丢失。'
              : '已应用到 RAM，但保存失败（${reply.error}）。可重试保存；断电可能丢失配置。';
        case 'busy':
          message = 'AMEBA 正在启动或退出切换，设置未应用；切换完成后重试。';
        default:
          message = '手表拒绝了设置值，未应用。请刷新后重试。';
      }
    } catch (error) {
      if (!current()) return;
      // A timed-out write may have executed; no replay and no false "saved".
      applied = draft = null;
      _unsavedPatch = null;
      final reason = error is TransportException ? error.code.name : 'protocol';
      message =
          write ? '未取得应用/保存确认（$reason）。结果未知，请刷新。' : '读取失败（$reason），请手动刷新。';
    } finally {
      if (current()) {
        busy = false;
        notifyListeners();
      }
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
