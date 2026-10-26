import 'package:flutter/material.dart';
import 'package:crabit_watch_app/core/device/device_center_controller.dart';

// Tutorial model: the legacy snapshot has no per-sensor validity bit.
class ReportedTemperature {
  const ReportedTemperature(this.celsius, this.ageMs);
  final double celsius;
  final int ageMs;

  static ReportedTemperature? parse(String value) {
    if (value.startsWith('unavailable')) return null;
    final fields = <String, String>{};
    for (final entry in value.split(';')) {
      final separator = entry.indexOf('=');
      if (separator <= 0) return null;
      final key = entry.substring(0, separator);
      if (fields.containsKey(key)) return null;
      fields[key] = entry.substring(separator + 1);
    }
    final bmp = fields['bmp']?.split(',');
    final age = int.tryParse(fields['age'] ?? '');
    if (bmp == null || bmp.length != 2 || age == null || age < 0) return null;
    final pressure = double.tryParse(bmp[0]);
    final temperature = double.tryParse(bmp[1]);
    if (pressure == null ||
        !pressure.isFinite ||
        temperature == null ||
        !temperature.isFinite)
      return null;
    return ReportedTemperature(temperature, age);
  }
}

class TutorialTemperatureCard extends StatelessWidget {
  const TutorialTemperatureCard({super.key, required this.controller});
  final DeviceCenterController controller;
  @override
  Widget build(BuildContext context) => ListenableBuilder(
    listenable: controller,
    builder: (context, _) {
      final value = controller.resultFor('diag/snapshot')?.event.value;
      final report = value == null ? null : ReportedTemperature.parse(value);
      final text = !controller.isReady
          ? '请先连接并验证手表'
          : controller.error != null
          ? '读取失败，请手动重试'
          : value == null
          ? '尚未读取'
          : report == null
          ? '快照不可用或格式错误'
          : report.ageMs > 5000
          ? '快照已过期，请重新读取'
          : '报告温度 ${report.celsius.toStringAsFixed(2)} °C';
      return Card(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text('传感器温度 · 教程扩展'),
              const SizedBox(height: 12),
              Text(text),
              if (report != null) Text('设备快照年龄 ${report.ageMs} ms'),
              const Text('仅展示设备报告：旧快照没有单个传感器有效位，不作为可靠温度测量。'),
              OutlinedButton(
                key: const ValueKey('tutorial-read-temperature'),
                onPressed: controller.canRead
                    ? () => controller.readDiagnostic('diag/snapshot')
                    : null,
                child: const Text('读取快照'),
              ),
            ],
          ),
        ),
      );
    },
  );
}
