// Manual visual check: flutter test test/usb_workbench_preview.dart --update-goldens
// Outputs stay in ignored build/, not a machine-specific golden in the repository.
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/scripts/usb_script.dart';
import 'package:crabit_watch_app/core/scripts/usb_script_repository.dart';
import 'package:crabit_watch_app/core/usb/usb_device_gateway.dart';
import 'package:crabit_watch_app/presentation/pages/tools/badusb_page.dart';

void main() {
  testWidgets('workbench wide and compact preview', (tester) async {
    await tester.runAsync(() async {
      final font = FontLoader('WorkbenchPreview')
        ..addFont(File('C:/Windows/Fonts/msyh.ttc')
            .readAsBytes()
            .then((b) => ByteData.sublistView(b)));
      await font.load();
      final mono = FontLoader('monospace')
        ..addFont(File('C:/Windows/Fonts/consola.ttf')
            .readAsBytes()
            .then((b) => ByteData.sublistView(b)));
      await mono.load();
      final icons = FontLoader('MaterialIcons')
        ..addFont(File(
                'build/windows/x64/runner/Release/data/flutter_assets/fonts/MaterialIcons-Regular.otf')
            .readAsBytes()
            .then((b) => ByteData.sublistView(b)));
      await icons.load();
    });
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    for (final size in [const Size(1280, 850), const Size(430, 850)]) {
      tester.view.physicalSize = size;
      await tester.pumpWidget(MaterialApp(
        theme: ThemeData(
            useMaterial3: true,
            fontFamily: 'WorkbenchPreview',
            colorSchemeSeed: const Color(0xff345f9b)),
        home: BadUsbPage(repository: _Repository(), gateway: _Gateway()),
      ));
      await tester.pumpAndSettle();
      await tester.enterText(find.byKey(const ValueKey('usb-script-editor')),
          'STRING Hello from Crabit\nDELAY 100\nENTER\n');
      await tester.pumpAndSettle();
      await expectLater(
          find.byType(BadUsbPage),
          matchesGoldenFile(
              '../build/usb-workbench-${size.width.toInt()}.png'));
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    }
  });
}

class _Repository implements UsbScriptRepository {
  @override
  Future<List<UsbScript>> load() async => [
        const UsbScript(
            id: 'greeting', title: '问候并回车', body: 'STRING Hello\nENTER')
      ];
  @override
  Future<void> save(UsbScript script) async {}
  @override
  Future<void> delete(String id) async {}
}

class _Gateway implements UsbDeviceGateway {
  @override
  bool get isProductReady => false;
  @override
  Future<UsbDeviceReply> request(String command) => throw UnimplementedError();
}
