import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../core/battery/battery_history_controller.dart';
import '../../core/battery/battery_history_segments.dart';
import '../../core/battery/battery_sample.dart';

enum BatteryHistoryWindow { day, week }

enum BatteryHistoryMetric { percent, voltage }

class BatteryHistoryPanel extends StatefulWidget {
  const BatteryHistoryPanel({
    super.key,
    required this.controller,
    this.now,
    this.onExportRequested,
  });

  final BatteryHistoryController controller;
  final DateTime Function()? now;

  /// Receives copyable CSV text. No clipboard operation is performed here.
  final ValueChanged<String>? onExportRequested;

  @override
  State<BatteryHistoryPanel> createState() => _BatteryHistoryPanelState();
}

class _BatteryHistoryPanelState extends State<BatteryHistoryPanel> {
  BatteryHistoryWindow _window = BatteryHistoryWindow.day;
  BatteryHistoryMetric _metric = BatteryHistoryMetric.percent;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return AnimatedBuilder(
      animation: widget.controller,
      builder: (context, _) {
        final controller = widget.controller;
        final latest = controller.latestSample;
        final samples = controller.chartSamples;
        final now = (widget.now?.call() ?? DateTime.now()).toUtc();
        final validInRange = samples.any((sample) {
          final time = sample.receivedAtUTC.toUtc();
          final range = _window == BatteryHistoryWindow.day
              ? const Duration(hours: 24)
              : const Duration(days: 7);
          return sample.isValid &&
              !time.isBefore(now.subtract(range)) &&
              !time.isAfter(now);
        });

        return Card(
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(Icons.battery_std_rounded, color: colors.primary),
                    const SizedBox(width: 10),
                    Expanded(
                      child: Text('电池历史',
                          style: Theme.of(context).textTheme.titleMedium),
                    ),
                    IconButton(
                      key: const ValueKey('battery-history-export'),
                      tooltip: '导出数据',
                      onPressed: () => _export(context, controller.exportCsv()),
                      icon: const Icon(Icons.ios_share_rounded),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                _LatestReading(
                  sample: latest,
                  stale: controller.latestIsStale,
                  colors: colors,
                ),
                const SizedBox(height: 10),
                Text(
                  controller.statusText,
                  key: const ValueKey('battery-history-status'),
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: controller.errorMessage == null
                            ? colors.onSurfaceVariant
                            : colors.error,
                      ),
                ),
                const SizedBox(height: 14),
                Wrap(
                  spacing: 8,
                  runSpacing: 6,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    _selector(
                      key: const ValueKey('battery-history-window'),
                      selected: _window,
                      labels: const {
                        BatteryHistoryWindow.day: '24 小时',
                        BatteryHistoryWindow.week: '7 天',
                      },
                      onSelected: (value) => setState(() => _window = value),
                    ),
                    _selector(
                      key: const ValueKey('battery-history-metric'),
                      selected: _metric,
                      labels: const {
                        BatteryHistoryMetric.percent: '电量',
                        BatteryHistoryMetric.voltage: '电压',
                      },
                      onSelected: (value) => setState(() => _metric = value),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                SizedBox(
                  height: 174,
                  width: double.infinity,
                  child: Stack(
                    fit: StackFit.expand,
                    children: [
                      CustomPaint(
                        key: const ValueKey('battery-history-chart'),
                        painter: _BatteryHistoryPainter(
                          samples: samples,
                          historyWindow: _window,
                          metric: _metric,
                          now: now,
                          colors: colors,
                          labelStyle: Theme.of(context).textTheme.bodySmall ??
                              const TextStyle(),
                        ),
                      ),
                      if (!validInRange)
                        Center(
                          child: DecoratedBox(
                            decoration: BoxDecoration(
                              color: colors.surface.withValues(alpha: 0.9),
                              borderRadius: BorderRadius.circular(8),
                            ),
                            child: Padding(
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 10, vertical: 6),
                              child: Text(
                                '暂无可绘制的有效读数',
                                style: Theme.of(context)
                                    .textTheme
                                    .bodySmall
                                    ?.copyWith(color: colors.onSurfaceVariant),
                              ),
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
                const SizedBox(height: 10),
                Row(
                  children: [
                    Expanded(
                      child: Text(
                        '仅连接期间采集；断开期间没有数据，不代表全天历史。',
                        style: Theme.of(context)
                            .textTheme
                            .bodySmall
                            ?.copyWith(color: colors.onSurfaceVariant),
                      ),
                    ),
                    const SizedBox(width: 8),
                    OutlinedButton.icon(
                      key: const ValueKey('battery-history-read-now'),
                      onPressed: controller.canRequestNow
                          ? () => controller.sampleNow()
                          : null,
                      icon: const Icon(Icons.refresh_rounded, size: 18),
                      label: const Text('立即读取'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _selector<T extends Enum>({
    required Key key,
    required T selected,
    required Map<T, String> labels,
    required ValueChanged<T> onSelected,
  }) {
    return SegmentedButton<T>(
      key: key,
      showSelectedIcon: false,
      segments: labels.entries
          .map((entry) => ButtonSegment<T>(
                value: entry.key,
                label: Text(entry.value),
              ))
          .toList(growable: false),
      selected: {selected},
      onSelectionChanged: (selection) => onSelected(selection.first),
    );
  }

  Future<void> _export(BuildContext context, String csv) async {
    final callback = widget.onExportRequested;
    if (callback != null) {
      callback(csv);
      return;
    }
    await showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('导出电池历史'),
        content: SizedBox(
          width: 560,
          child: SingleChildScrollView(
            child: SelectionArea(child: SelectableText(csv)),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('关闭'),
          ),
        ],
      ),
    );
  }
}

class _LatestReading extends StatelessWidget {
  const _LatestReading({
    required this.sample,
    required this.stale,
    required this.colors,
  });

  final BatterySample? sample;
  final bool stale;
  final ColorScheme colors;

  @override
  Widget build(BuildContext context) {
    final reading = sample;
    final usable = reading?.isValid ?? false;
    final headline = reading == null
        ? '尚无电量读数'
        : usable
            ? '${reading.percent}%'
            : '电量数据暂不可用';
    final subline = usable
        ? '${(reading!.voltageMv / 1000).toStringAsFixed(2)} V'
        : reading == null
            ? '等待设备返回有效样本'
            : !reading.deviceValid
                ? '设备标记此样本无效'
                : '设备样本已超过 5 秒';
    return Row(
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                headline,
                key: const ValueKey('battery-history-latest'),
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                      fontWeight: FontWeight.w600,
                      color:
                          usable ? colors.onSurface : colors.onSurfaceVariant,
                    ),
              ),
              const SizedBox(height: 3),
              Text(subline, style: Theme.of(context).textTheme.bodyMedium),
              if (reading != null) ...[
                const SizedBox(height: 3),
                Text(
                  '读于 ${_formatTime(reading.receivedAtUTC.toLocal())}',
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                ),
              ],
            ],
          ),
        ),
        if (usable && reading!.charging)
          Padding(
            padding: const EdgeInsets.only(right: 10),
            child: Icon(Icons.bolt_rounded, color: colors.tertiary, size: 22),
          ),
        Chip(
          avatar: Icon(
            reading == null
                ? Icons.hourglass_empty_rounded
                : stale
                    ? Icons.schedule_rounded
                    : Icons.check_circle_outline,
            size: 16,
          ),
          label: Text(reading == null
              ? '等待数据'
              : stale
                  ? '陈旧 / 无效'
                  : '有效'),
          visualDensity: VisualDensity.compact,
          backgroundColor:
              stale ? colors.surfaceContainerHighest : colors.primaryContainer,
        ),
      ],
    );
  }
}

class BatteryHistoryChart extends StatelessWidget {
  const BatteryHistoryChart({
    super.key,
    required this.samples,
    required this.window,
    required this.metric,
    required this.now,
  });

  final List<BatterySample> samples;
  final BatteryHistoryWindow window;
  final BatteryHistoryMetric metric;
  final DateTime now;

  @override
  Widget build(BuildContext context) => CustomPaint(
        painter: _BatteryHistoryPainter(
          samples: samples,
          historyWindow: window,
          metric: metric,
          now: now.toUtc(),
          colors: Theme.of(context).colorScheme,
          labelStyle:
              Theme.of(context).textTheme.bodySmall ?? const TextStyle(),
        ),
        child: const SizedBox.expand(),
      );
}

class _BatteryHistoryPainter extends CustomPainter {
  const _BatteryHistoryPainter({
    required this.samples,
    required this.historyWindow,
    required this.metric,
    required this.now,
    required this.colors,
    required this.labelStyle,
  });

  final List<BatterySample> samples;
  final BatteryHistoryWindow historyWindow;
  final BatteryHistoryMetric metric;
  final DateTime now;
  final ColorScheme colors;
  final TextStyle labelStyle;

  @override
  void paint(Canvas canvas, Size size) {
    if (size.width <= 64 || size.height <= 36) return;
    final start = now.subtract(historyWindow == BatteryHistoryWindow.day
        ? const Duration(hours: 24)
        : const Duration(days: 7));
    final visible = samples.where((sample) {
      final time = sample.receivedAtUTC.toUtc();
      return !time.isBefore(start) && !time.isAfter(now);
    }).toList()
      ..sort((a, b) => a.receivedAtUTC.compareTo(b.receivedAtUTC));
    final valid = visible.where((sample) => sample.isValid).toList();
    final values = valid.map(_valueFor).toList(growable: false);

    const left = 44.0;
    const right = 10.0;
    const top = 8.0;
    const bottom = 25.0;
    final plot = Rect.fromLTRB(
      left,
      top,
      size.width - right,
      size.height - bottom,
    );
    if (plot.width <= 1 || plot.height <= 1) return;

    final minSample =
        values.isEmpty ? 0.0 : values.reduce((a, b) => a < b ? a : b);
    final maxSample =
        values.isEmpty ? 0.0 : values.reduce((a, b) => a > b ? a : b);
    final minValue = metric == BatteryHistoryMetric.percent || values.isEmpty
        ? 0.0
        : math.max(0.0, minSample - _voltagePadding(values)).toDouble();
    final maxValue = metric == BatteryHistoryMetric.percent
        ? 100.0
        : values.isEmpty
            ? 1.0
            : maxSample + _voltagePadding(values);
    final valueSpan = math.max(1.0, maxValue - minValue).toDouble();
    final gridPaint = Paint()
      ..color = colors.outlineVariant.withValues(alpha: 0.65)
      ..strokeWidth = 1;
    final axisStyle =
        labelStyle.copyWith(color: colors.onSurfaceVariant, fontSize: 10);
    for (var index = 0; index <= 2; index++) {
      final fraction = index / 2;
      final y = plot.bottom - plot.height * fraction;
      canvas.drawLine(Offset(plot.left, y), Offset(plot.right, y), gridPaint);
      final value = minValue + valueSpan * fraction;
      _drawLabel(canvas, _formatAxis(value), Offset(0, y - 7), axisStyle,
          maxWidth: left - 5, alignRight: true);
    }

    _drawLabel(canvas, _formatWindowTime(start.toLocal(), historyWindow),
        Offset(plot.left, plot.bottom + 5), axisStyle);
    _drawLabel(canvas, _formatWindowTime(now.toLocal(), historyWindow),
        Offset(plot.right - 42, plot.bottom + 5), axisStyle);

    Offset chartPoint(BatterySample sample) {
      final timeFraction =
          sample.receivedAtUTC.toUtc().difference(start).inMicroseconds /
              rangeDuration.inMicroseconds;
      final valueFraction = (_valueFor(sample) - minValue) / valueSpan;
      return Offset(
        plot.left + plot.width * timeFraction.clamp(0.0, 1.0).toDouble(),
        plot.bottom - plot.height * valueFraction.clamp(0.0, 1.0).toDouble(),
      );
    }

    final segments = splitBatteryHistorySegments(
      visible,
      maxGap: BatteryHistoryController.disconnectGap,
    ).map((samples) => samples.map(chartPoint).toList()).toList();

    final linePaint = Paint()
      ..color = colors.primary
      ..strokeWidth = 2.2
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..style = PaintingStyle.stroke;
    final pointPaint = Paint()
      ..color = colors.primary
      ..style = PaintingStyle.fill;
    for (final points in segments) {
      if (points.length > 1) {
        final path = Path()..moveTo(points.first.dx, points.first.dy);
        for (final point in points.skip(1)) {
          path.lineTo(point.dx, point.dy);
        }
        canvas.drawPath(path, linePaint);
      }
      for (final point in points) {
        canvas.drawCircle(point, 3.2, pointPaint);
      }
    }

    for (final sample in visible) {
      if (!sample.isValid || !sample.charging) continue;
      final timeFraction =
          sample.receivedAtUTC.toUtc().difference(start).inMicroseconds /
              rangeDuration.inMicroseconds;
      final valueFraction = (_valueFor(sample) - minValue) / valueSpan;
      final center = Offset(
        plot.left + plot.width * timeFraction.clamp(0.0, 1.0).toDouble(),
        plot.bottom -
            plot.height * valueFraction.clamp(0.0, 1.0).toDouble() -
            9,
      );
      final bolt = Path()
        ..moveTo(center.dx + 1, center.dy - 5)
        ..lineTo(center.dx - 3, center.dy + 1)
        ..lineTo(center.dx, center.dy + 1)
        ..lineTo(center.dx - 1, center.dy + 6)
        ..lineTo(center.dx + 4, center.dy - 1)
        ..lineTo(center.dx + 1, center.dy - 1)
        ..close();
      canvas.drawPath(bolt, Paint()..color = colors.tertiary);
    }
  }

  Duration get rangeDuration => historyWindow == BatteryHistoryWindow.day
      ? const Duration(hours: 24)
      : const Duration(days: 7);

  double _valueFor(BatterySample sample) =>
      metric == BatteryHistoryMetric.percent
          ? sample.percent.toDouble()
          : sample.voltageMv.toDouble();

  static double _voltagePadding(List<double> values) {
    final minimum = values.reduce((a, b) => a < b ? a : b);
    final maximum = values.reduce((a, b) => a > b ? a : b);
    final span = maximum - minimum;
    return math.max(50.0, span * 0.1).toDouble();
  }

  String _formatAxis(double value) => metric == BatteryHistoryMetric.percent
      ? '${value.round()}%'
      : '${(value / 1000).toStringAsFixed(1)}V';

  @override
  bool shouldRepaint(covariant _BatteryHistoryPainter oldDelegate) =>
      oldDelegate.samples != samples ||
      oldDelegate.historyWindow != historyWindow ||
      oldDelegate.metric != metric ||
      oldDelegate.now != now ||
      oldDelegate.colors != colors ||
      oldDelegate.labelStyle != labelStyle;
}

void _drawLabel(
  Canvas canvas,
  String text,
  Offset offset,
  TextStyle style, {
  double maxWidth = 60,
  bool alignRight = false,
}) {
  final painter = TextPainter(
    text: TextSpan(text: text, style: style),
    textDirection: TextDirection.ltr,
    maxLines: 1,
  )..layout(maxWidth: maxWidth);
  painter.paint(
    canvas,
    Offset(alignRight ? offset.dx + maxWidth - painter.width : offset.dx,
        offset.dy),
  );
}

String _formatTime(DateTime time) {
  String two(int value) => value.toString().padLeft(2, '0');
  return '${two(time.hour)}:${two(time.minute)}';
}

String _formatWindowTime(DateTime time, BatteryHistoryWindow window) {
  String two(int value) => value.toString().padLeft(2, '0');
  return window == BatteryHistoryWindow.day
      ? '${two(time.hour)}:${two(time.minute)}'
      : '${two(time.month)}-${two(time.day)}';
}
