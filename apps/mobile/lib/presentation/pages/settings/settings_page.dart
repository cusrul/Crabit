import 'package:flutter/material.dart';

import '../../../core/managers/ble_manager.dart';
import '../../../core/settings/preferences_gateway.dart';
import '../../../core/settings/settings_controller.dart';
import '../../../core/settings/settings_gateway.dart';
import '../../../core/settings/settings_protocol.dart';
import '../../widgets/app_page.dart';
import '../../widgets/watch_preferences_panel.dart';
import '../../widgets/slider_overlay_scope.dart';
import '../weather/weather_page.dart';

class SettingsPage extends StatefulWidget {
  const SettingsPage(
      {super.key,
      this.gateway,
      this.rememberedConnection,
      this.sendTimeSync,
      this.preferencesGateway,
      this.onOpenTimeWeather});
  final SettingsGateway? gateway;
  final RememberedConnectionPreference? rememberedConnection;

  /// Retained for source compatibility; time sync now lives in WeatherPage.
  final Future<bool> Function(String topic, String value)? sendTimeSync;
  final PreferencesGateway? preferencesGateway;
  final VoidCallback? onOpenTimeWeather;
  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  late final SettingsGateway _gateway;
  late final SettingsController _controller;
  late final RememberedConnectionPreference _remembered;
  bool? _deepSleepCapability;
  @override
  void initState() {
    super.initState();
    _gateway = widget.gateway ?? BleSettingsGateway(BleManager());
    _controller = SettingsController(_gateway);
    _remembered = widget.rememberedConnection ??
        BleRememberedConnectionPreference(BleManager());
  }

  @override
  void dispose() {
    _controller.dispose();
    if (widget.gateway == null) (_gateway as BleSettingsGateway).dispose();
    super.dispose();
  }

  void _updateDeepSleepCapability(bool? capability) {
    if (!mounted || _deepSleepCapability == capability) return;
    setState(() => _deepSleepCapability = capability);
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
      animation: _controller,
      builder: (context, _) {
        final c = _controller;
        final draft = c.draft;
        return AppPage(
            title: '设置',
            child: ListView(
                padding: const EdgeInsets.only(top: 20, bottom: 40),
                children: [
                  Text('手表设置',
                      style: Theme.of(context).textTheme.headlineLarge),
                  const SizedBox(height: 8),
                  const Text('以 STM32 当前配置为准。编辑只是草稿，点击应用后才写入手表。'),
                  const SizedBox(height: 20),
                  Card(
                      child: Padding(
                          padding: const EdgeInsets.all(20),
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(c.ready
                                    ? '设备已连接并验证'
                                    : '设备离线 · 请先在设备页连接并验证'),
                                const SizedBox(height: 12),
                                Text(c.message,
                                    key: const Key('settings-result')),
                                const SizedBox(height: 12),
                                OutlinedButton.icon(
                                    key: const Key('settings-refresh'),
                                    onPressed:
                                        c.ready && !c.busy ? c.refresh : null,
                                    icon: const Icon(Icons.refresh),
                                    label: const Text('刷新（丢弃草稿）')),
                                const SizedBox(height: 8),
                                if (c.busy) const LinearProgressIndicator(),
                              ]))),
                  const SizedBox(height: 16),
                  Card(
                    child: ListTile(
                      key: const Key('settings-time-weather'),
                      leading: const Icon(Icons.schedule),
                      title: const Text('时间与天气'),
                      subtitle: const Text('管理天气信息并同步手表时间'),
                      trailing: const Icon(Icons.chevron_right),
                      onTap: widget.onOpenTimeWeather ??
                          () => Navigator.of(context).push<void>(
                                MaterialPageRoute<void>(
                                  builder: (_) => const WeatherPage(),
                                ),
                              ),
                    ),
                  ),
                  const SizedBox(height: 16),
                  Card(
                      child: Padding(
                          padding: const EdgeInsets.all(20),
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                    '亮度 · ${draft == null || draft.brightness < 0 ? '未读取/无效' : '${draft.brightness / 10}%'}'),
                                SliderOverlayScope(
                                    child: Slider(
                                        key: const Key('settings-brightness'),
                                        min: 0,
                                        max: 1000,
                                        divisions: 100,
                                        value: (draft?.brightness ?? 0)
                                            .clamp(0, 1000)
                                            .toDouble(),
                                        onChanged: c.editable
                                            ? (v) => c.edit(draft!.copyWith(
                                                brightness: v.round()))
                                            : null)),
                                SwitchListTile(
                                    contentPadding: EdgeInsets.zero,
                                    title: const Text('反色显示'),
                                    value: draft?.invert ?? false,
                                    onChanged: c.editable
                                        ? (v) =>
                                            c.edit(draft!.copyWith(invert: v))
                                        : null),
                                _delay(
                                    '息屏时间',
                                    'settings-screen-off',
                                    draft?.screenOffMs,
                                    const [3000, 5000, 10000, 15000, 30000],
                                    c.editable
                                        ? (v) => c.edit(
                                            draft!.copyWith(screenOffMs: v))
                                        : null),
                                const SizedBox(height: 16),
                                _delay(
                                    '息屏后深度休眠延迟',
                                    'settings-sleep',
                                    draft?.deepSleepMs,
                                    const [5000, 10000, 15000, 30000, 60000, 0],
                                    c.editable && _deepSleepCapability == true
                                        ? (v) => c.edit(
                                            draft!.copyWith(deepSleepMs: v))
                                        : null),
                                const SizedBox(height: 20),
                                Text(_deepSleepCapability == false
                                    ? '设备报告当前固件仅支持屏幕息屏；已保存的深度休眠延迟 ${c.applied?.deepSleepMs ?? draft?.deepSleepMs ?? '未知'} ms 保留，不会因能力不可用而改写。'
                                    : _deepSleepCapability == true
                                        ? '深度休眠延迟 0 表示不进入深度休眠。自定义设备值保持不变，除非主动修改该项。'
                                        : '未确认深睡支持，暂不可改；已保存的深度休眠延迟 ${c.applied?.deepSleepMs ?? draft?.deepSleepMs ?? '未知'} ms 保留。'),
                                const SizedBox(height: 20),
                                FilledButton(
                                    key: const Key('settings-apply'),
                                    onPressed: c.canApply ? c.apply : null,
                                    child: const Text('应用并保存')),
                                if (c.applied
                                    case final WatchSettings applied) ...[
                                  const SizedBox(height: 16),
                                  Text(
                                      '手表已应用：亮度 ${applied.brightness < 0 ? '无效' : '${applied.brightness / 10}%'} · '
                                      '${applied.invert ? '反色' : '正常显示'}\n'
                                      '息屏 ${applied.screenOffMs} ms · 深度休眠延迟 ${applied.deepSleepMs} ms'),
                                ],
                              ]))),
                  const SizedBox(height: 16),
                  WatchPreferencesPanel(
                    gateway: widget.preferencesGateway,
                    onDeepSleepCapabilityChanged: _updateDeepSleepCapability,
                  ),
                  const SizedBox(height: 16),
                  AnimatedBuilder(
                      animation: _remembered,
                      builder: (context, _) => Card(
                            child: SwitchListTile(
                              key: const Key('settings-remembered-connection'),
                              title: const Text('记住设备并在前台恢复连接'),
                              subtitle: Text(!_remembered.hasRememberedDevice
                                  ? '尚无已记住的设备。请先在设备页连接并验证。'
                                  : _remembered.saving
                                      ? '正在保存连接偏好…'
                                      : _remembered.status),
                              value: _remembered.enabled,
                              onChanged: !_remembered.hasRememberedDevice ||
                                      _remembered.saving
                                  ? null
                                  : _remembered.setEnabled,
                            ),
                          )),
                ]));
      });

  Widget _delay(String label, String key, int? value, List<int> choices,
      ValueChanged<int>? change) {
    final options = {...choices, if (value != null) value};
    return DropdownButtonFormField<int>(
        key: Key('$key-${value ?? "none"}'),
        initialValue: value,
        isExpanded: true,
        decoration: InputDecoration(labelText: label),
        items: [
          for (final option in options)
            DropdownMenuItem(
                value: option,
                child: Text(option == 0 && key == 'settings-sleep'
                    ? '不进入深度休眠'
                    : '${option / 1000} 秒${choices.contains(option) ? '' : '（设备自定义）'}'))
        ],
        onChanged: change == null
            ? null
            : (v) {
                if (v != null) change(v);
              });
  }
}

/// UI-facing contract so the remembered-connection preference can be tested
/// without owning BLE lifecycle or transport behavior here.
abstract interface class RememberedConnectionPreference implements Listenable {
  bool get enabled;
  bool get saving;
  String get status;
  bool get hasRememberedDevice;
  Future<void> setEnabled(bool value);
}

class BleRememberedConnectionPreference
    implements RememberedConnectionPreference {
  BleRememberedConnectionPreference(this._ble);
  final BleManager _ble;
  Listenable get _state => _ble.rememberedConnection;
  @override
  bool get enabled => _ble.rememberedConnection.enabled;
  @override
  bool get saving => _ble.rememberedConnection.saving;
  @override
  String get status => _ble.rememberedConnection.status;
  @override
  bool get hasRememberedDevice => _ble.rememberedConnection.hasRememberedDevice;
  @override
  Future<void> setEnabled(bool value) =>
      _ble.rememberedConnection.setEnabled(value);
  @override
  void addListener(VoidCallback listener) => _state.addListener(listener);
  @override
  void removeListener(VoidCallback listener) => _state.removeListener(listener);
}
