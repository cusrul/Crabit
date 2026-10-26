// Opt-in rendering of the actual product page with a fake link, not hardware data.
import 'dart:io';
import 'dart:ui' as ui;

import 'package:crabit_watch_app/core/device/device_center_controller.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/presentation/pages/home/dashboard_page.dart';
import 'package:crabit_watch_app/presentation/widgets/app_shell.dart';
import 'package:crabit_watch_app/presentation/pages/tools/badusb_page.dart';
import 'package:crabit_watch_app/presentation/pages/ble/sniffer_page.dart';
import 'package:crabit_watch_app/presentation/pages/settings/settings_page.dart';
import 'package:crabit_watch_app/presentation/pages/misc/debug_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'support/fake_device_link.dart';
import 'support/watch_preview_stub.dart';

void main() {
  final output = Platform.environment['CRABIT_DEVICE_CENTER_SNAPSHOTS'];
  testWidgets('render actual device center with labelled simulated states',
      (tester) async {
    final fontPath = Platform.environment['CRABIT_DEVICE_CENTER_FONT'];
    expect(fontPath, isNotNull,
        reason: 'Provide an existing CJK font for render QA');
    await tester.runAsync(() async {
      final font = FontLoader('DeviceCenterRender')
        ..addFont(File(fontPath!).readAsBytes().then(ByteData.sublistView));
      await font.load();
      final roboto = FontLoader('Roboto')
        ..addFont(File(fontPath).readAsBytes().then(ByteData.sublistView));
      await roboto.load();
      final mono = FontLoader('monospace')
        ..addFont(File('C:/Windows/Fonts/consola.ttf')
            .readAsBytes()
            .then(ByteData.sublistView))
        ..addFont(File(fontPath).readAsBytes().then(ByteData.sublistView));
      await mono.load();
      final icons = FontLoader('MaterialIcons')
        ..addFont(File('build/unit_test_assets/fonts/MaterialIcons-Regular.otf')
            .readAsBytes()
            .then(ByteData.sublistView));
      await icons.load();
    });
    final directory = Directory(output!)..createSync(recursive: true);
    tester.view.physicalSize = const Size(1440, 960);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final link = FakeDeviceLink();
    link.onRead = (topic, value) async => DiagnosticEvent(
        topic: topic,
        value: 'unavailable;at=12000;age=0',
        requestId: 'sample-1');
    final controller = DeviceCenterController(link: link);
    addTearDown(controller.dispose);
    final boundary = GlobalKey();
    final baseTheme = AppTheme.lightTheme;
    await tester.pumpWidget(MaterialApp(
      theme: baseTheme.copyWith(
          textTheme:
              baseTheme.textTheme.apply(fontFamily: 'DeviceCenterRender')),
      home: RepaintBoundary(
        key: boundary,
        child: Column(children: [
          const Material(
            child: SafeArea(
              bottom: false,
              child: Padding(
                padding: EdgeInsets.all(8),
                child: Text('界面验收 · 模拟连接状态，非实机数据'),
              ),
            ),
          ),
          Expanded(
              child: AppShell(pages: [
            DashboardPage(
                controller: controller, previewBuilder: buildPreviewStub),
            const BadUsbPage(),
            const SnifferPage(),
            const SettingsPage(),
            const DebugPage()
          ])),
        ]),
      ),
    ));

    Future<void> capture(String name) async {
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      final render =
          boundary.currentContext!.findRenderObject()! as RenderRepaintBoundary;
      await tester.runAsync(() async {
        final image = await render.toImage();
        final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
        expect(bytes, isNotNull);
        await File('${directory.path}/$name.png')
            .writeAsBytes(bytes!.buffer.asUint8List());
        image.dispose();
      });
    }

    await capture('disconnected');
    link.setState(
      connected: true,
      pairing:
          const PairTransferStatus(stage: PairTransferStage.waitingForWatch),
    );
    await capture('waiting-for-watch');
    link.setState(
      authenticated: true,
      ready: true,
      pairing: const PairTransferStatus(
          stage: PairTransferStage.verified,
          latency: Duration(milliseconds: 180)),
    );
    await controller.readDiagnostic('diag/snapshot');
    await capture('verified-with-snapshot');
    for (var index = 1; index < 5; index++) {
      await tester.tap(find.byKey(ValueKey('app-nav-$index')));
      await capture(
          ['', 'usb-editor', 'wireless-records', 'settings', 'debug'][index]);
    }
    await tester.tap(find.byKey(const ValueKey('app-nav-0')));
    tester.view.physicalSize = const Size(360, 800);
    await capture('phone-verified');
    await tester.scrollUntilVisible(
        find.byKey(const ValueKey('diag-echo')), 200,
        scrollable: find.byType(Scrollable).first);
    await capture('phone-diagnostics');
    await tester.pumpWidget(const SizedBox.shrink());
  }, skip: output == null);
}
