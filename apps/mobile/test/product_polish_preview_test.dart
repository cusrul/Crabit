import 'dart:io';
import 'dart:ui' as ui;

import 'package:crabit_watch_app/core/battery/battery_history_controller.dart';
import 'package:crabit_watch_app/core/battery/battery_history_store.dart';
import 'package:crabit_watch_app/core/battery/battery_sample.dart';
import 'package:crabit_watch_app/core/settings/preferences_gateway.dart';
import 'package:crabit_watch_app/core/settings/preferences_protocol.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/presentation/widgets/battery_history_panel.dart';
import 'package:crabit_watch_app/presentation/widgets/watch_preferences_panel.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

/// Deterministic simulated readings, never presented as physical-device proof.
void main() {
  testWidgets('product panels remain readable at phone and desktop widths',
      (tester) async {
    final output = Platform.environment['CRABIT_PRODUCT_PREVIEWS'];
    if (output != null) {
      await tester.runAsync(() async {
        for (final family in ['ProductPreview', 'Roboto']) {
          await (FontLoader(family)
                ..addFont(File('C:/Windows/Fonts/simhei.ttf')
                    .readAsBytes()
                    .then(ByteData.sublistView)))
              .load();
        }
        await (FontLoader('MaterialIcons')
              ..addFont(
                  File('build/unit_test_assets/fonts/MaterialIcons-Regular.otf')
                      .readAsBytes()
                      .then(ByteData.sublistView)))
            .load();
      });
    }
    final now = DateTime.utc(2026, 10, 22, 12);
    final batteryGateway = _BatteryGateway();
    final battery = BatteryHistoryController(
      gateway: batteryGateway,
      store: _BatteryStore(now),
      now: () => now,
    );
    final preferences = _PreferencesGateway();
    await battery.initialized;
    final boundary = GlobalKey();
    Future<void> render(String name, Widget child, Size size) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      await tester.pumpWidget(MaterialApp(
        theme: output == null
            ? AppTheme.lightTheme
            : AppTheme.lightTheme.copyWith(
                textTheme: AppTheme.lightTheme.textTheme
                    .apply(fontFamily: 'ProductPreview'),
              ),
        home: RepaintBoundary(
            key: boundary,
            child: Scaffold(
              body: SingleChildScrollView(
                  key: ValueKey(name),
                  padding: const EdgeInsets.all(16),
                  child: child),
            )),
      ));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      if (output != null) {
        final render = boundary.currentContext!.findRenderObject()!
            as RenderRepaintBoundary;
        await tester.runAsync(() async {
          final directory = Directory(output)..createSync(recursive: true);
          final image = await render.toImage();
          final png = await image.toByteData(format: ui.ImageByteFormat.png);
          await File('${directory.path}/$name.png')
              .writeAsBytes(png!.buffer.asUint8List());
          image.dispose();
        });
      }
    }

    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    for (final size in [const Size(390, 1100), const Size(1000, 1100)]) {
      await render('simulated-preferences-${size.width.toInt()}',
          WatchPreferencesPanel(gateway: preferences), size);
      expect(find.text('输入与反馈'), findsOneWidget);
      await render('simulated-battery-${size.width.toInt()}',
          BatteryHistoryPanel(controller: battery, now: () => now), size);
      expect(
          find.byKey(const ValueKey('battery-history-chart')), findsOneWidget);
    }
    await tester.pumpWidget(const SizedBox.shrink());
    battery.dispose();
    batteryGateway.dispose();
    preferences.dispose();
  });
}

class _PreferencesGateway extends ChangeNotifier implements PreferencesGateway {
  @override
  bool get ready => true;
  @override
  Future<PreferencesReply> read() async => const PreferencesReply(
      'snapshot',
      'none',
      WatchPreferences(
          pat: true,
          qmi: false,
          lift: true,
          tap: true,
          sound: true,
          led: true,
          volume: 35,
          ledLevel: 8,
          face: 0,
          deepSleep: false));
  @override
  Future<PreferencesReply> apply(Map<String, int> patch) async => read();
}

class _BatteryGateway extends ChangeNotifier implements BatteryHistoryGateway {
  @override
  bool get ready => false;
  @override
  String get deviceId => 'simulated-watch';
  @override
  Future<String> requestBattery() => throw StateError('No physical device');
}

class _BatteryStore implements BatteryHistoryStore {
  _BatteryStore(this.now);
  final DateTime now;
  @override
  Future<List<BatterySample>> loadDevice(String deviceId,
          {DateTime? now}) async =>
      [
        for (var i = 0; i < 1440; i++)
          BatterySample(
            deviceId: deviceId,
            receivedAtUTC: this.now.subtract(Duration(minutes: 1439 - i)),
            uptimeMs: (i + 1) * 60000,
            voltageMv: 4200 - i ~/ 5,
            percent: 95 - i ~/ 60,
            charging: false,
            deviceValid: true,
            ageMs: 0,
          ),
      ];
  @override
  Future<void> save(BatterySample sample, {DateTime? now}) async {}
}
