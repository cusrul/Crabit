import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/battery/battery_history_controller.dart';
import 'package:crabit_watch_app/core/battery/battery_history_store.dart';
import 'package:crabit_watch_app/core/battery/battery_sample.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/presentation/widgets/battery_history_panel.dart';

void main() {
  testWidgets('shows deterministic history, selectors, stale state, and export',
      (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    final now = DateTime.utc(2026, 10, 22, 12);
    final store = _Store([
      _sample(now.subtract(const Duration(hours: 22)), 80, 20),
      _sample(now.subtract(const Duration(hours: 2)), 76, 72020,
          charging: true),
      _sample(now.subtract(const Duration(hours: 1)), 75, 3),
      _sample(now.subtract(const Duration(minutes: 5)), 0, 4, ageMs: 7000),
    ]);
    final gateway = _Gateway();
    final controller = BatteryHistoryController(
      gateway: gateway,
      store: store,
      now: () => now,
    );
    await controller.initialized;
    String? exported;

    await tester.pumpWidget(MaterialApp(
      theme: AppTheme.lightTheme,
      home: Scaffold(
        body: SingleChildScrollView(
          padding: const EdgeInsets.all(12),
          child: BatteryHistoryPanel(
            controller: controller,
            now: () => now,
            onExportRequested: (value) => exported = value,
          ),
        ),
      ),
    ));
    await tester.pumpAndSettle();

    expect(find.text('电池历史'), findsOneWidget);
    expect(find.text('仅连接期间采集；断开期间没有数据，不代表全天历史。'), findsOneWidget);
    expect(find.text('已断开 · 显示此前保存的记录'), findsOneWidget);
    expect(find.text('陈旧 / 无效'), findsOneWidget);
    expect(find.byKey(const ValueKey('battery-history-chart')), findsOneWidget);
    final latest = tester.widget<Text>(
      find.byKey(const ValueKey('battery-history-latest')),
    );
    expect(latest.data, '电量数据暂不可用');
    expect(latest.data, isNot('0%'));

    var windowControl = tester.widget<SegmentedButton<BatteryHistoryWindow>>(
      find.byKey(const ValueKey('battery-history-window')),
    );
    expect(windowControl.selected, {BatteryHistoryWindow.day});
    await tester.tap(find.text('7 天'));
    await tester.pumpAndSettle();
    windowControl = tester.widget<SegmentedButton<BatteryHistoryWindow>>(
      find.byKey(const ValueKey('battery-history-window')),
    );
    expect(windowControl.selected, {BatteryHistoryWindow.week});

    await tester.tap(find.text('电压'));
    await tester.pumpAndSettle();
    final metricControl = tester.widget<SegmentedButton<BatteryHistoryMetric>>(
      find.byKey(const ValueKey('battery-history-metric')),
    );
    expect(metricControl.selected, {BatteryHistoryMetric.voltage});

    await tester.tap(find.byKey(const ValueKey('battery-history-export')));
    expect(exported, contains('received_at_utc,uptime_ms,voltage_mv'));
    expect(exported, contains('watch-a'));
    expect(exported, contains(',0,0,0,7000'));

    await tester.pumpWidget(const SizedBox.shrink());
    controller.dispose();
  });
}

class _Gateway extends ChangeNotifier implements BatteryHistoryGateway {
  @override
  bool get ready => false;

  @override
  String get deviceId => 'watch-a';

  @override
  Future<String> requestBattery() async =>
      throw StateError('disconnected gateway must not be called');
}

class _Store implements BatteryHistoryStore {
  _Store(this.data);

  final List<BatterySample> data;

  @override
  Future<List<BatterySample>> loadDevice(String deviceId,
          {DateTime? now}) async =>
      data.where((sample) => sample.deviceId == deviceId).toList();

  @override
  Future<void> save(BatterySample sample, {DateTime? now}) async {
    data.add(sample);
  }
}

BatterySample _sample(
  DateTime at,
  int percent,
  int uptimeMs, {
  bool charging = false,
  int ageMs = 0,
}) =>
    BatterySample(
      deviceId: 'watch-a',
      receivedAtUTC: at,
      uptimeMs: uptimeMs,
      voltageMv: 3900 + percent * 2,
      percent: percent,
      charging: charging,
      deviceValid: true,
      ageMs: ageMs,
    );
