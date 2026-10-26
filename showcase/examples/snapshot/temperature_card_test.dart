import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/device/device_center_controller.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'temperature_card.dart';
import 'fake_device_link.dart';

void main() {
  test(
    'reported temperature parser accepts zero and refuses malformed data',
    () {
      expect(ReportedTemperature.parse('bmp=101325,0;age=0')!.celsius, 0);
      for (final value in [
        'unavailable',
        'bmp=1,NaN;age=0',
        'bmp=1,Infinity;age=0',
        'bmp=1,20;age=-1',
        'bmp=1,20',
        'bmp=1,20;age=0;age=1',
        'bmp=1;age=0',
      ]) {
        expect(ReportedTemperature.parse(value), isNull, reason: value);
      }
    },
  );
  testWidgets(
    'card reads manually, clears failed refresh and hides on disconnect',
    (tester) async {
      final preview = Platform.environment['CRABIT_SENSOR_PREVIEW'];
      final boundary = GlobalKey();
      if (preview != null) {
        tester.view.physicalSize = const Size(600, 360);
        tester.view.devicePixelRatio = 1;
        addTearDown(tester.view.resetPhysicalSize);
        addTearDown(tester.view.resetDevicePixelRatio);
        await tester.runAsync(() async {
          await (FontLoader('TutorialPreview')..addFont(
                File(
                  'C:/Windows/Fonts/simhei.ttf',
                ).readAsBytes().then(ByteData.sublistView),
              ))
              .load();
        });
      }
      final link = FakeDeviceLink();
      final controller = DeviceCenterController(link: link);
      addTearDown(controller.dispose);
      addTearDown(link.dispose);
      await tester.pumpWidget(
        MaterialApp(
          debugShowCheckedModeBanner: false,
          theme: AppTheme.lightTheme.copyWith(
            textTheme: AppTheme.lightTheme.textTheme.apply(
              fontFamily: preview == null ? null : 'TutorialPreview',
            ),
          ),
          home: RepaintBoundary(
            key: boundary,
            child: Scaffold(
              body: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  if (preview != null) const Text('教程界面 · 模拟数据，非实机测量'),
                  TutorialTemperatureCard(controller: controller),
                ],
              ),
            ),
          ),
        ),
      );
      expect(
        tester
            .widget<OutlinedButton>(
              find.byKey(const ValueKey('tutorial-read-temperature')),
            )
            .onPressed,
        isNull,
      );
      link.setState(
        connected: true,
        authenticated: true,
        ready: true,
        pairing: const PairTransferStatus(stage: PairTransferStage.verified),
      );
      link.onRead = (topic, value) async =>
          DiagnosticEvent(topic: topic, value: 'bmp=101325,23.5;age=10');
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('tutorial-read-temperature')));
      await tester.pumpAndSettle();
      expect(find.text('报告温度 23.50 °C'), findsOneWidget);
      expect(link.reads, [('diag/snapshot', '')]);
      if (preview != null) {
        await tester.runAsync(() async {
          final image =
              await (boundary.currentContext!.findRenderObject()!
                      as RenderRepaintBoundary)
                  .toImage();
          final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
          await File(preview).writeAsBytes(bytes!.buffer.asUint8List());
          image.dispose();
        });
      }
      link.onRead = (_, __) async => throw TimeoutException('simulated');
      await tester.tap(find.byKey(const ValueKey('tutorial-read-temperature')));
      await tester.pumpAndSettle();
      expect(find.text('报告温度 23.50 °C'), findsNothing);
      expect(find.text('读取失败，请手动重试'), findsOneWidget);
      link.setState(connected: false);
      await tester.pumpAndSettle();
      expect(find.text('请先连接并验证手表'), findsOneWidget);
    },
  );
  testWidgets(
    'stale and unavailable snapshots are not displayed as a temperature',
    (tester) async {
      final link = FakeDeviceLink()
        ..setState(
          connected: true,
          authenticated: true,
          ready: true,
          pairing: const PairTransferStatus(stage: PairTransferStage.verified),
        );
      final controller = DeviceCenterController(link: link);
      addTearDown(controller.dispose);
      addTearDown(link.dispose);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: TutorialTemperatureCard(controller: controller)),
        ),
      );
      for (final value in ['bmp=101325,23.5;age=6000', 'unavailable']) {
        link.onRead = (topic, _) async =>
            DiagnosticEvent(topic: topic, value: value);
        await tester.tap(
          find.byKey(const ValueKey('tutorial-read-temperature')),
        );
        await tester.pumpAndSettle();
        expect(find.textContaining('报告温度 23.50'), findsNothing);
      }
    },
  );
}
