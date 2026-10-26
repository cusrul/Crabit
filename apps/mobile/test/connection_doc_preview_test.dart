// Documentation captures of actual widgets; fake link, never hardware evidence.
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/device/device_center_controller.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/presentation/widgets/device_connection_panel.dart';
import 'support/fake_device_link.dart';

void main() {
  testWidgets('connection documentation uses real controls with simulated link',
      (tester) async {
    final output = Platform.environment['CRABIT_CONNECTION_PREVIEWS'];
    if (output == null) return;
    await tester.runAsync(() async {
      for (final family in ['DocPreview', 'Roboto']) {
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
    tester.view.physicalSize = const Size(560, 850);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final link = FakeDeviceLink();
    final controller = DeviceCenterController(link: link);
    addTearDown(controller.dispose);
    final boundary = GlobalKey();
    await tester.pumpWidget(RepaintBoundary(
        key: boundary,
        child: MaterialApp(
          debugShowCheckedModeBanner: false,
          theme: AppTheme.lightTheme.copyWith(
              textTheme: AppTheme.lightTheme.textTheme
                  .apply(fontFamily: 'DocPreview')),
          home: Scaffold(
              body: SingleChildScrollView(
                  child: Padding(
                      padding: const EdgeInsets.all(24),
                      child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text('连接操作示例 · 模拟状态，非实机连接'),
                            const SizedBox(height: 24),
                            ListenableBuilder(
                                listenable: controller,
                                builder: (context, _) => DeviceConnectionPanel(
                                    controller: controller)),
                          ])))),
        )));
    Future<void> capture(String name) async {
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      await tester.runAsync(() async {
        final image = await (boundary.currentContext!.findRenderObject()!
                as RenderRepaintBoundary)
            .toImage();
        final png = await image.toByteData(format: ui.ImageByteFormat.png);
        await Directory(output).create(recursive: true);
        await File('$output/$name.png').writeAsBytes(png!.buffer.asUint8List());
        image.dispose();
      });
    }

    await capture('connection-start');
    await tester.tap(find.byKey(const ValueKey('device-auth-code')));
    await capture(
        'connection-code'); // Empty field: no real or example password exposed.
    await tester.tap(find.text('取消'));
    await tester.pumpAndSettle();
    await link.saveBleAccessCode(
        '000000'); // Fake local setting; not a device password.
    link.setState(
        connected: true,
        authenticated: true,
        ready: true,
        pairing: const PairTransferStatus(stage: PairTransferStage.verified));
    await tester.pumpAndSettle();
    await tester.tap(find.text('连接检查'));
    await capture('connection-ready');
    expect(find.text('通信已验证'), findsOneWidget);
  });
}
