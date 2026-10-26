import 'package:flutter/material.dart';

import '../../core/managers/ble_manager.dart';
import '../../core/settings/preferences_controller.dart';
import '../../core/settings/preferences_gateway.dart';
import '../../core/settings/preferences_protocol.dart';
import 'slider_overlay_scope.dart';

class WatchPreferencesPanel extends StatefulWidget {
  const WatchPreferencesPanel({
    super.key,
    this.gateway,
    this.onDeepSleepCapabilityChanged,
  });

  final PreferencesGateway? gateway;
  final ValueChanged<bool?>? onDeepSleepCapabilityChanged;

  @override
  State<WatchPreferencesPanel> createState() => _WatchPreferencesPanelState();
}

class _WatchPreferencesPanelState extends State<WatchPreferencesPanel> {
  late final PreferencesGateway _gateway;
  late final bool _ownsGateway;
  late final PreferencesController _controller;
  bool? _lastCapability;

  @override
  void initState() {
    super.initState();
    _ownsGateway = widget.gateway == null;
    _gateway = widget.gateway ?? BlePreferencesGateway(BleManager());
    _controller = PreferencesController(_gateway)
      ..addListener(_forwardDeepSleepCapability);
    _lastCapability = _controller.deepSleepCapability;
  }

  void _forwardDeepSleepCapability() {
    final capability = _controller.deepSleepCapability;
    if (capability == _lastCapability) return;
    _lastCapability = capability;
    widget.onDeepSleepCapabilityChanged?.call(capability);
  }

  @override
  void didUpdateWidget(covariant WatchPreferencesPanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.onDeepSleepCapabilityChanged !=
        widget.onDeepSleepCapabilityChanged) {
      widget.onDeepSleepCapabilityChanged?.call(
        _controller.deepSleepCapability,
      );
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    if (_ownsGateway) (_gateway as BlePreferencesGateway).dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: _controller,
        builder: (context, _) {
          final controller = _controller;
          final draft = controller.draft;
          final editable = controller.editable;
          return Card(
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('输入与反馈', style: Theme.of(context).textTheme.titleLarge),
                  const SizedBox(height: 4),
                  const Text('修改先保存在草稿中；设备回读是当前值的依据。'),
                  const SizedBox(height: 12),
                  Text(controller.message, key: const Key('prefs-result')),
                  if (controller.busy) ...[
                    const SizedBox(height: 8),
                    const LinearProgressIndicator(),
                  ],
                  if (draft == null) ...[
                    const SizedBox(height: 8),
                    const Text(
                      '尚未取得设备偏好；不会用默认开关状态代替设备值。',
                      key: Key('prefs-unavailable'),
                    ),
                  ] else ...[
                    const SizedBox(height: 12),
                    _switch(
                      '触摸操作（PAT）',
                      'prefs-pat',
                      draft.pat,
                      editable,
                      (value) => controller.edit(draft.copyWith(pat: value)),
                    ),
                    _switch(
                      '倾斜操作（QMI）',
                      'prefs-qmi',
                      draft.qmi,
                      editable,
                      (value) => controller.edit(draft.copyWith(qmi: value)),
                    ),
                    _switch(
                      '抬腕唤醒',
                      'prefs-lift',
                      draft.lift,
                      editable,
                      (value) => controller.edit(draft.copyWith(lift: value)),
                    ),
                    _switch(
                      '敲击唤醒',
                      'prefs-tap',
                      draft.tap,
                      editable,
                      (value) => controller.edit(draft.copyWith(tap: value)),
                    ),
                    _switch(
                      '声音反馈',
                      'prefs-sound',
                      draft.sound,
                      editable,
                      (value) => controller.edit(draft.copyWith(sound: value)),
                    ),
                    _switch(
                      '灯光反馈',
                      'prefs-led',
                      draft.led,
                      editable,
                      (value) => controller.edit(draft.copyWith(led: value)),
                    ),
                    const SizedBox(height: 8),
                    Text('音量 · ${draft.volume}%'),
                    SliderOverlayScope(
                        child: Slider(
                      key: const Key('prefs-volume'),
                      min: 0,
                      max: 100,
                      divisions: 100,
                      value: draft.volume.toDouble(),
                      onChanged: editable
                          ? (value) => controller.edit(
                                draft.copyWith(volume: value.round()),
                              )
                          : null,
                    )),
                    Text('灯光强度 · ${draft.ledLevel}%'),
                    SliderOverlayScope(
                        child: Slider(
                      key: const Key('prefs-led-level'),
                      min: 0,
                      max: 100,
                      divisions: 100,
                      value: draft.ledLevel.toDouble(),
                      onChanged: editable
                          ? (value) => controller.edit(
                                draft.copyWith(ledLevel: value.round()),
                              )
                          : null,
                    )),
                    const SizedBox(height: 8),
                    Text('表盘', style: Theme.of(context).textTheme.titleMedium),
                    const SizedBox(height: 8),
                    DropdownButtonFormField<int>(
                      key: const Key('prefs-face'),
                      initialValue: draft.face,
                      isExpanded: true,
                      decoration: const InputDecoration(labelText: '表盘样式'),
                      items: [
                        for (final entry in watchFaceNames.entries)
                          DropdownMenuItem(
                            value: entry.key,
                            child: Text(entry.value),
                          ),
                      ],
                      onChanged: editable
                          ? (value) {
                              if (value != null) {
                                controller.edit(draft.copyWith(face: value));
                              }
                            }
                          : null,
                    ),
                    if (controller.deepSleepCapability case final bool value)
                      Padding(
                        padding: const EdgeInsets.only(top: 12),
                        child: Text(value
                            ? '设备报告支持深度休眠。'
                            : '设备报告当前仅支持屏幕息屏；深度休眠延迟值会保留。'),
                      ),
                  ],
                  const SizedBox(height: 12),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      OutlinedButton.icon(
                        key: const Key('prefs-refresh'),
                        onPressed: controller.ready && !controller.busy
                            ? controller.refresh
                            : null,
                        icon: const Icon(Icons.refresh),
                        label: const Text('刷新偏好'),
                      ),
                      FilledButton(
                        key: const Key('prefs-apply'),
                        onPressed:
                            controller.canApply ? controller.apply : null,
                        child: const Text('应用并保存偏好'),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          );
        },
      );

  Widget _switch(
    String title,
    String key,
    bool value,
    bool enabled,
    ValueChanged<bool> onChanged,
  ) =>
      SwitchListTile(
        key: Key(key),
        contentPadding: EdgeInsets.zero,
        title: Text(title),
        value: value,
        onChanged: enabled ? onChanged : null,
      );
}
