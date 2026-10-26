import 'dart:io';

import 'package:crabit_watch_app/core/device/device_center_controller.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/presentation/pages/home/dashboard_page.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'support/fake_device_link.dart';
import 'support/watch_preview_stub.dart';

void main() {
  test('default viewport receives the resolved connection controller', () {
    // Native WebView is replaced in widget tests. Guard the production wiring
    // separately, including the default path with no injected controller.
    final source = File('lib/presentation/pages/home/dashboard_page.dart')
        .readAsStringSync();
    expect(source, contains('deviceController: _controller'));
    expect(source, isNot(contains('deviceController: widget.controller')));
  });

  Future<void> revealControl(WidgetTester tester, String key) async {
    final target = find.byKey(ValueKey(key));
    final list = find.byKey(const ValueKey('device-scroll'));
    final height = tester.view.physicalSize.height;
    // Lazy cards above the current viewport may be unmounted after a reply.
    // Start at the top before the bounded downward search in either read order.
    await tester.drag(list, const Offset(0, 3000));
    await tester.pumpAndSettle();
    if (target.evaluate().isEmpty &&
        (key == 'device-verify' || key == 'diag-echo')) {
      final label = find.text(key == 'device-verify' ? '连接检查' : '连接诊断');
      await tester.scrollUntilVisible(label, 120,
          scrollable: find
              .descendant(of: list, matching: find.byType(Scrollable))
              .first);
      await tester.tap(label);
      await tester.pumpAndSettle();
    }
    await tester.scrollUntilVisible(
      target,
      120,
      scrollable:
          find.descendant(of: list, matching: find.byType(Scrollable)).first,
      maxScrolls: 30,
    );
    await tester.pumpAndSettle();
    for (var attempt = 0; attempt < 6; attempt++) {
      final rect = tester.getRect(target);
      if (rect.top >= 96 && rect.bottom <= height - 24) return;
      await tester.drag(
        list,
        Offset(0, rect.top < 96 ? 120 : -160),
      );
      await tester.pumpAndSettle();
    }
    expect(tester.getRect(target).top, greaterThanOrEqualTo(96));
    expect(tester.getRect(target).bottom, lessThanOrEqualTo(height - 24));
  }

  for (final size in [const Size(360, 640), const Size(1000, 760)]) {
    testWidgets('device center renders and operates at $size with large text',
        (tester) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);

      final link = FakeDeviceLink();
      link.accessCodeValue = '123456';
      final controller = DeviceCenterController(link: link);
      addTearDown(controller.dispose);
      await tester.pumpWidget(
        MaterialApp(
          theme: AppTheme.lightTheme,
          builder: (context, child) => MediaQuery(
            data: MediaQuery.of(context)
                .copyWith(textScaler: const TextScaler.linear(1.5)),
            child: child!,
          ),
          home: DashboardPage(
              controller: controller, previewBuilder: buildPreviewStub),
        ),
      );

      expect(find.text('设备'), findsOneWidget);
      expect(find.text('Crabit-Watch'), findsOneWidget);
      expect(find.text('尚未连接'), findsOneWidget);
      expect(link.connectCalls, 0);
      expect(link.reads, isEmpty);
      expect(tester.takeException(), isNull);

      await tester.ensureVisible(find.byKey(const ValueKey('device-connect')));
      await tester.tap(find.byKey(const ValueKey('device-connect')));
      await tester.pump();
      expect(link.connectCalls, 1);
      expect(link.reads, isEmpty);

      link.setState(connected: true, authenticated: true);
      await tester.pump();
      await revealControl(tester, 'device-verify');
      await tester.tap(find.byKey(const ValueKey('device-verify')));
      await tester.pump();
      expect(link.verifyCalls, 1);

      link.setState(
        ready: true,
        pairing: const PairTransferStatus(stage: PairTransferStage.verified),
      );
      await tester.pump();

      for (final key in ['diag-echo', 'diag-snapshot', 'diag-wifi']) {
        await revealControl(tester, key);
        await tester.tap(find.byKey(ValueKey(key)));
        await tester.pump();
      }
      expect(link.reads, [
        ('diag/echo', 'crabit-check'),
        ('diag/snapshot', ''),
        ('diag/wifi', ''),
      ]);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    });
  }

  testWidgets('status distinguishes waiting, verifying and verified states',
      (tester) async {
    final link = FakeDeviceLink();
    final controller = DeviceCenterController(link: link);
    await tester.pumpWidget(MaterialApp(
      theme: AppTheme.lightTheme,
      home: DashboardPage(
          controller: controller, previewBuilder: buildPreviewStub),
    ));
    link.setState(
      connected: true,
      pairing:
          const PairTransferStatus(stage: PairTransferStage.waitingForWatch),
    );
    await tester.pump();
    expect(find.text('等待验证结果'), findsOneWidget);
    link.setState(
      authenticated: true,
      pairing: const PairTransferStatus(stage: PairTransferStage.verifying),
    );
    await tester.pump();
    expect(find.text('正在验证传输'), findsOneWidget);
    link.setState(
      pairing: const PairTransferStatus(stage: PairTransferStage.verified),
      ready: true,
    );
    await tester.pump();
    expect(find.text('通信已验证'), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
    controller.dispose();
  });

  testWidgets('first connection saves the device code before connecting',
      (tester) async {
    final link = FakeDeviceLink();
    final controller = DeviceCenterController(link: link);
    await tester.pumpWidget(MaterialApp(
      theme: AppTheme.lightTheme,
      home: DashboardPage(
          controller: controller, previewBuilder: buildPreviewStub),
    ));

    await tester.ensureVisible(find.byKey(const ValueKey('device-connect')));
    await tester.tap(find.byKey(const ValueKey('device-connect')));
    await tester.pumpAndSettle();
    expect(find.text('输入设备验证码'), findsOneWidget);

    await tester.enterText(find.byType(TextField), '123456');
    await tester.tap(find.text('保存'));
    await tester.pumpAndSettle();

    expect(link.accessCodeValue, '123456');
    expect(link.connectCalls, 1);
    await tester.pumpWidget(const SizedBox.shrink());
    controller.dispose();
  });
}
