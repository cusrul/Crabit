import 'package:flutter/material.dart';
import 'dart:async';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/device/device_center_controller.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/presentation/pages/home/dashboard_page.dart';
import 'support/fake_device_link.dart';
import 'support/watch_preview_stub.dart';

class _Preview extends StatefulWidget {
  const _Preview({required this.active, required this.inspect});
  final bool active, inspect;
  @override
  State<_Preview> createState() => _PreviewState();
}

class _PreviewState extends State<_Preview> {
  int page = 0;
  @override
  Widget build(BuildContext context) => Center(
          child: TextButton(
        key: const ValueKey('preview-input'),
        onPressed: () => setState(() => page++),
        child: Text(
            'page $page · ${widget.inspect ? "inspect" : "front"} · ${widget.active ? "running" : "paused"}'),
      ));
}

void main() {
  testWidgets('diagnostics cannot interrupt a pending home connection',
      (tester) async {
    final pending = Completer<void>();
    final link = FakeDeviceLink()..onConnect = () => pending.future;
    final controller = DeviceCenterController(link: link);
    addTearDown(controller.dispose);
    await tester.pumpWidget(MaterialApp(
        home: DashboardPage(
            controller: controller, previewBuilder: buildPreviewStub)));
    final request = controller.connect();
    await tester.pump();
    await tester.tap(find.byTooltip('更多'));
    await tester.pump(const Duration(milliseconds: 300));
    final item = tester.widget<PopupMenuItem<String>>(
        find.widgetWithText(PopupMenuItem<String>, '链路诊断'));
    expect(item.enabled, isFalse);
    expect(link.connectCalls, 1);
    expect(link.disconnectCalls, 0);
    pending.complete();
    await request;
    await tester.pumpWidget(const SizedBox.shrink());
  });
  testWidgets(
      'home preview survives expand, collapse, resize, and device notifications',
      (tester) async {
    tester.view.physicalSize = const Size(1100, 800);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final link = FakeDeviceLink();
    final controller = DeviceCenterController(link: link);
    addTearDown(controller.dispose);
    await tester.pumpWidget(MaterialApp(
        theme: AppTheme.lightTheme,
        home: DashboardPage(
            controller: controller,
            previewBuilder: (_, active, inspect) =>
                _Preview(active: active, inspect: inspect))));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('preview-input')));
    await tester.tap(find.byKey(const ValueKey('watch-expand')));
    await tester.pumpAndSettle();
    expect(find.text('page 1 · inspect · running'), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('watch-expand')));
    await tester.pumpAndSettle();
    expect(find.text('page 1 · front · running'), findsOneWidget);
    tester.view.physicalSize = const Size(360, 760);
    await tester.pumpAndSettle();
    expect(find.text('page 1 · front · running'), findsOneWidget);
    link.setState(connected: true);
    await tester.pumpAndSettle();
    expect(find.text('page 1 · front · running'), findsOneWidget);
    expect(tester.takeException(), isNull);
    expect(link.reads, isEmpty);
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets(
      'scrolling out pauses the preview and shortcuts request existing destinations',
      (tester) async {
    tester.view.physicalSize = const Size(800, 700);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final controller = DeviceCenterController(link: FakeDeviceLink());
    addTearDown(controller.dispose);
    final destinations = <int>[];
    await tester.pumpWidget(MaterialApp(
        home: DashboardPage(
            controller: controller,
            onNavigate: destinations.add,
            previewBuilder: (_, active, inspect) =>
                _Preview(active: active, inspect: inspect))));
    await tester.pumpAndSettle();
    await tester.drag(
        find.byKey(const ValueKey('device-scroll')), const Offset(0, -900));
    await tester.pumpAndSettle();
    expect(find.textContaining('paused'), findsOneWidget);
    for (final index in [1, 2, 3]) {
      final target = find.byKey(ValueKey('device-shortcut-$index'));
      await tester.ensureVisible(target);
      await tester.tap(target);
      await tester.pump();
    }
    expect(destinations, [1, 2, 3]);
    await tester.drag(
        find.byKey(const ValueKey('device-scroll')), const Offset(0, 2000));
    await tester.pumpAndSettle();
    expect(find.textContaining('running'), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
  });
}
