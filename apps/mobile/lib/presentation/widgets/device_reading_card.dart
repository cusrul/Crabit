import 'package:flutter/material.dart';

import '../../core/device/device_center_controller.dart';

/// Displays one on-demand diagnostic result supplied by the real controller.
class DeviceReadingCard extends StatelessWidget {
  const DeviceReadingCard({
    super.key,
    required this.controller,
    required this.topic,
    required this.title,
    required this.description,
    required this.action,
    required this.icon,
    required this.buttonKey,
  });

  final DeviceCenterController controller;
  final String topic, title, description, action, buttonKey;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    final reading = controller.resultFor(topic);
    final colors = Theme.of(context).colorScheme;
    final style = Theme.of(context).textTheme;
    final unavailable = reading?.event.value.startsWith('unavailable') ?? false;
    final summary = reading == null
        ? '尚未读取'
        : unavailable
            ? '快照暂不可用'
            : topic == 'diag/echo'
                ? '回显一致'
                : '已收到状态回复';

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: colors.surfaceContainerLow,
        borderRadius: BorderRadius.circular(20),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: colors.primary, size: 24),
          const SizedBox(height: 16),
          Text(title, style: style.titleMedium),
          const SizedBox(height: 8),
          Text(description, style: TextStyle(color: colors.onSurfaceVariant)),
          const SizedBox(height: 20),
          Text(
            summary,
            style: TextStyle(
              fontWeight: FontWeight.w500,
              color: unavailable ? colors.error : colors.onSurface,
            ),
          ),
          if (reading != null) ...[
            const SizedBox(height: 6),
            Text('读回于 ${_receivedAt(reading.event.timestamp)}',
                style: style.bodySmall),
            Theme(
              data:
                  Theme.of(context).copyWith(dividerColor: Colors.transparent),
              child: ExpansionTile(
                key: PageStorageKey('raw-$topic'),
                tilePadding: EdgeInsets.zero,
                childrenPadding: const EdgeInsets.only(bottom: 12),
                title: Text(
                  '查看原始返回',
                  style: TextStyle(fontSize: 12, color: colors.primary),
                ),
                children: [
                  SelectableText(
                    reading.event.value.isEmpty
                        ? '返回内容为空'
                        : reading.event.value,
                  ),
                  const SizedBox(height: 6),
                  Text(
                    '${reading.elapsed.inMilliseconds} ms · 请求 ${reading.event.requestId ?? "未提供"}',
                    style: style.bodySmall,
                  ),
                  if (unavailable) const Text('这不是传感器读取成功；请稍后手动重试。'),
                ],
              ),
            ),
          ],
          const SizedBox(height: 16),
          OutlinedButton(
            key: ValueKey(buttonKey),
            onPressed: controller.canRead
                ? () => controller.readDiagnostic(topic)
                : null,
            child: Text(action),
          ),
        ],
      ),
    );
  }
}

String _receivedAt(DateTime value) {
  String two(int number) => number.toString().padLeft(2, '0');
  return '${two(value.hour)}:${two(value.minute)}:${two(value.second)}';
}
