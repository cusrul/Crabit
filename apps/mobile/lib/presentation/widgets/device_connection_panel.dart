import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../core/device/device_center_controller.dart';
import '../../core/transport/ble_auth.dart';
import '../../core/transport/ble_transport.dart';

/// A compact, honest summary of the current watch connection state.
class DeviceConnectionPanel extends StatelessWidget {
  const DeviceConnectionPanel({super.key, required this.controller});

  final DeviceCenterController controller;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final link = controller.link;
    final canStop = link.connected || link.scanning || controller.busy;
    final (title, hint) = controller.disconnecting
        ? ('正在断开连接', '正在释放通信操作，请稍候。')
        : switch (controller.phase) {
            DevicePhase.disconnected => (
                '尚未连接',
                '在手表正常界面单独长按左上键 1 秒，或从系统设置中开启蓝牙。'
              ),
            DevicePhase.scanning => (
                '正在寻找手表',
                '请确认手表蓝牙已开启，状态为 ADVERTISING。'
              ),
            DevicePhase.connecting => ('正在建立连接', '正在准备蓝牙通道。'),
            DevicePhase.authenticating => (
                '正在验证设备码',
                'BLE 通道已建立，正在由 BW16 校验验证码。'
              ),
            DevicePhase.waitingForWatch => ('等待验证结果', '设备正在返回应用层验证结果。'),
            DevicePhase.verifying => ('正在验证传输', '正在等待 BLE 回显，尚不能读取设备。'),
            DevicePhase.ready => ('通信已验证', '验证码已通过，STM32 回显正常，可以读取设备状态。'),
            DevicePhase.failed => (
                '连接或验证未完成',
                link.pairing.detail ?? '请检查手表状态后手动重试。'
              ),
          };

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Crabit-Watch', style: Theme.of(context).textTheme.headlineSmall),
        const SizedBox(height: 12),
        Semantics(
          liveRegion: true,
          child: Row(
            children: [
              Icon(
                controller.isReady ? Icons.check_circle : Icons.circle_outlined,
                size: 16,
                color: colors.primary,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(title,
                    style: const TextStyle(fontWeight: FontWeight.w600)),
              ),
            ],
          ),
        ),
        const SizedBox(height: 8),
        Text(hint),
        const SizedBox(height: 8),
        Text(
          '首次连接输入 NanoUI 固件目录 ble_auth.local.h 中的 6 位码；'
          '保存后，手动重连会自动发送。',
          style: Theme.of(context).textTheme.bodySmall,
        ),
        if (controller.error case final error?) ...[
          const SizedBox(height: 8),
          Semantics(
            liveRegion: true,
            child: Text(error, style: TextStyle(color: colors.error)),
          ),
        ],
        const SizedBox(height: 16),
        Wrap(
          spacing: 12,
          runSpacing: 8,
          children: [
            FilledButton.icon(
              key: const ValueKey('device-connect'),
              onPressed: controller.disconnecting
                  ? null
                  : canStop
                      ? controller.disconnect
                      : () => _connect(context),
              icon: Icon(canStop ? Icons.bluetooth_disabled : Icons.bluetooth),
              label: Text(controller.disconnecting
                  ? '正在断开'
                  : canStop
                      ? '断开连接'
                      : '连接手表'),
            ),
          ],
        ),
        Align(
          alignment: Alignment.centerLeft,
          child: TextButton.icon(
            key: const ValueKey('device-auth-code'),
            onPressed: controller.disconnecting || controller.busy
                ? null
                : () => _changeAccessCode(context),
            icon: const Icon(Icons.password_outlined),
            label:
                Text(controller.bleAccessCode == null ? '设置设备验证码' : '更改已保存验证码'),
          ),
        ),
        if (controller.busy) ...[
          const SizedBox(height: 12),
          const LinearProgressIndicator(semanticsLabel: '正在处理请求'),
        ],
        const SizedBox(height: 8),
        ExpansionTile(
          key: const ValueKey('connection-details'),
          tilePadding: EdgeInsets.zero,
          title: const Text('连接检查'),
          children: [
            Align(
              alignment: Alignment.centerLeft,
              child: OutlinedButton(
                key: const ValueKey('device-verify'),
                onPressed: controller.canVerify ? controller.verify : null,
                child: const Text('验证传输'),
              ),
            ),
            Wrap(
              spacing: 16,
              runSpacing: 8,
              children: [
                _ConnectionStep(label: 'BLE 连接', done: link.connected),
                _ConnectionStep(
                  label: '设备码验证',
                  done: link.connected && link.authenticated,
                ),
                _ConnectionStep(label: '传输验证', done: controller.isReady),
              ],
            ),
            const SizedBox(height: 12),
          ],
        ),
      ],
    );
  }

  Future<void> _connect(BuildContext context) async {
    var code = controller.bleAccessCode;
    if (code == null) {
      code = await _editAccessCode(context);
      if (code == null || !context.mounted) return;
      try {
        await controller.saveBleAccessCode(code);
      } on TransportException {
        return;
      }
    }
    await controller.connect();
  }

  Future<void> _changeAccessCode(BuildContext context) async {
    final code =
        await _editAccessCode(context, initialValue: controller.bleAccessCode);
    if (code == null || !context.mounted) return;
    try {
      await controller.saveBleAccessCode(code);
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('验证码已保存在此应用的本地设置中')),
      );
    } on TransportException {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('验证码格式无效，请输入 6 位数字')),
      );
    }
  }

  Future<String?> _editAccessCode(BuildContext context,
      {String? initialValue}) async {
    var value = initialValue ?? '';
    var invalid = false;
    return showDialog<String>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setDialogState) => AlertDialog(
          title: Text(initialValue == null ? '输入设备验证码' : '更改已保存验证码'),
          content: TextFormField(
            initialValue: initialValue,
            autofocus: true,
            obscureText: true,
            keyboardType: TextInputType.number,
            maxLength: bleAccessCodeDigits,
            inputFormatters: [FilteringTextInputFormatter.digitsOnly],
            onChanged: (text) {
              value = text;
              if (invalid) setDialogState(() => invalid = false);
            },
            decoration: InputDecoration(
              labelText: '6 位数字',
              errorText: invalid ? '请输入 6 位数字' : null,
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () {
                final code = value.trim();
                if (!isValidBleAccessCode(code)) {
                  setDialogState(() => invalid = true);
                  return;
                }
                Navigator.pop(context, code);
              },
              child: const Text('保存'),
            ),
          ],
        ),
      ),
    );
  }
}

class _ConnectionStep extends StatelessWidget {
  const _ConnectionStep({required this.label, required this.done});

  final String label;
  final bool done;

  @override
  Widget build(BuildContext context) => Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            done ? Icons.check_circle : Icons.radio_button_unchecked,
            size: 14,
            color: Theme.of(context).colorScheme.primary,
          ),
          const SizedBox(width: 6),
          Text(label, style: Theme.of(context).textTheme.bodySmall),
        ],
      );
}
