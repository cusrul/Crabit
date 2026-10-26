import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/presentation/pages/misc/transport_diagnostics_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  for (final size in [const Size(360, 640), const Size(960, 700)]) {
    testWidgets('diagnostic page is passive and scrollable at $size',
        (tester) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      await tester
          .pumpWidget(const MaterialApp(home: TransportDiagnosticsPage()));
      expect(find.text('链路诊断'), findsOneWidget);
      expect(find.textContaining('UART PASS / RAM'), findsOneWidget);
      expect(find.textContaining('保存设备验证码'), findsOneWidget);
      expect(find.textContaining('连接时会自动发送'), findsOneWidget);
      expect(find.textContaining('BLE → PAIR DIAG'), findsNothing);
      expect(BleManager().isScanning.value, isFalse);
      expect(BleManager().isConnected.value, isFalse);
      final quick = tester.widget<FilledButton>(
          find.widgetWithText(FilledButton, '运行基础回归 · 42 项'));
      expect(quick.onPressed, isNull);
      await tester.drag(find.byType(ListView), const Offset(0, -1000));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    });
  }
}
