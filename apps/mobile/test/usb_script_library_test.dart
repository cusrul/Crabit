import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:hive/hive.dart';
import 'package:crabit_watch_app/core/scripts/usb_script.dart';
import 'package:crabit_watch_app/core/scripts/usb_script_repository.dart';
import 'package:crabit_watch_app/presentation/pages/tools/badusb_page.dart';

void main() {
  group('HiveUsbScriptRepository', () {
    late Directory directory;
    late Box box;
    late HiveUsbScriptRepository repository;

    setUp(() async {
      directory = await Directory.systemTemp.createTemp('usb-scripts-');
      Hive.init(directory.path);
      box = await Hive.openBox('settings-test');
      repository = HiveUsbScriptRepository(box);
    });

    tearDown(() async {
      await box.close();
      await Hive.deleteBoxFromDisk('settings-test');
      await directory.delete(recursive: true);
    });

    test('persists scripts across repository instances and deletes by id',
        () async {
      const script =
          UsbScript(id: 'one', title: 'Manual sample', body: 'STRING hello');
      await repository.save(script);
      final reopened = HiveUsbScriptRepository(box);
      expect(await reopened.load(), [script]);
      await reopened.delete(script.id);
      expect(await repository.load(), isEmpty);
    });

    test('replacing an id updates instead of duplicating', () async {
      await repository
          .save(const UsbScript(id: 'one', title: 'old', body: 'GUI r'));
      await repository
          .save(const UsbScript(id: 'one', title: 'new', body: 'GUI l'));
      expect(await repository.load(), [
        const UsbScript(id: 'one', title: 'new', body: 'GUI l'),
      ]);
    });

    test('concurrent mutations across repository instances do not lose updates',
        () async {
      await repository.save(const UsbScript(
          id: 'remove', title: 'Remove me', body: 'STRING old'));
      final secondRepository = HiveUsbScriptRepository(box);
      await Future.wait([
        repository
            .save(const UsbScript(id: 'one', title: 'One', body: 'STRING one')),
        secondRepository
            .save(const UsbScript(id: 'two', title: 'Two', body: 'STRING two')),
        repository.delete('remove'),
      ]);

      expect(await repository.load(), [
        const UsbScript(id: 'one', title: 'One', body: 'STRING one'),
        const UsbScript(id: 'two', title: 'Two', body: 'STRING two'),
      ]);
    });
  });

  testWidgets('loads local scripts and keeps device controls in their own tab',
      (tester) async {
    _wide(tester);
    final repository = _MemoryRepository([
      const UsbScript(id: 'saved', title: 'Local draft', body: 'STRING hello'),
    ]);
    await tester
        .pumpWidget(MaterialApp(home: BadUsbPage(repository: repository)));
    await tester.pumpAndSettle();

    expect(find.textContaining('本地脚本可直接运行到 Crabit'), findsOneWidget);
    expect(find.text('本地脚本'), findsOneWidget);
    await tester.pumpAndSettle();
    expect(find.text('Local draft'), findsOneWidget);
    await _reveal(tester, find.text('Local draft'));
    await tester.tap(find.text('Local draft'));
    await tester.pumpAndSettle();
    expect(find.text('编辑原脚本'), findsOneWidget);
    expect(find.text('运行一次'), findsNothing);
    await _reveal(tester, find.text('编辑原脚本'));
    await tester.tap(find.text('编辑原脚本'));
    await tester.pumpAndSettle();
    expect(
        tester
            .widget<TextField>(find.byKey(const ValueKey('usb-script-editor')))
            .controller!
            .text,
        'STRING hello');
  });

  testWidgets(
      'updates and deletes a saved script and reports local receipt only',
      (tester) async {
    _wide(tester);
    final repository = _MemoryRepository([
      const UsbScript(id: 'saved', title: 'Original', body: 'STRING before'),
    ]);
    await tester
        .pumpWidget(MaterialApp(home: BadUsbPage(repository: repository)));
    await tester.pumpAndSettle();
    await tester.pumpAndSettle();
    await _reveal(tester, find.text('Original'));
    await tester.tap(find.text('Original'));
    await tester.pumpAndSettle();
    await _reveal(tester, find.text('编辑原脚本'));
    await tester.tap(find.text('编辑原脚本'));
    await tester.pumpAndSettle();
    await tester.enterText(
        find.byKey(const ValueKey('usb-script-editor')), 'STRING after');
    await tester.tap(find.text('更新本地脚本'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'Renamed');
    await tester.tap(find.widgetWithText(FilledButton, '保存'));
    await tester.pumpAndSettle();

    expect(repository.scripts, [
      const UsbScript(id: 'saved', title: 'Renamed', body: 'STRING after'),
    ]);
    expect(find.text('已保存到手机本地脚本库；未发送或执行。'), findsOneWidget);
    expect(find.text('运行'), findsNothing);

    await tester.pumpAndSettle();
    await _reveal(tester, find.text('Renamed'));
    await tester.tap(find.text('Renamed'));
    await tester.pumpAndSettle();
    await _reveal(tester, find.byTooltip('删除本地脚本'));
    await tester.tap(find.byTooltip('删除本地脚本'));
    await tester.pumpAndSettle();
    expect(repository.scripts, isEmpty);
  });

  testWidgets('repeated load composes actions and saves a new reusable script',
      (tester) async {
    tester.view.physicalSize = const Size(1280, 850);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final repository = _MemoryRepository([
      const UsbScript(id: 'saved', title: 'My enter', body: 'ENTER'),
    ]);
    await tester
        .pumpWidget(MaterialApp(home: BadUsbPage(repository: repository)));
    await tester.pumpAndSettle();
    for (final id in ['example-text', 'example-delay', 'saved']) {
      final tile = find.byKey(ValueKey('script-$id'));
      await _reveal(tester, tile);
      await tester.pumpAndSettle();
      final insert = find.byKey(ValueKey('insert-local-$id'));
      await _reveal(tester, insert);
      await tester.tap(insert);
      await tester.pumpAndSettle();
    }
    final editor = tester
        .widget<TextField>(find.byKey(const ValueKey('usb-script-editor')));
    expect(editor.controller!.text,
        'STRING Hello from Crabit\nDELAY 100\nENTER\n');
    await tester.tap(find.text('保存到本地'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'My composition');
    await tester.tap(find.widgetWithText(FilledButton, '保存'));
    await tester.pumpAndSettle();
    expect(repository.scripts.length, 2);
    expect(repository.scripts.first.body, 'ENTER');
    expect(repository.scripts.last.body, editor.controller!.text);
    expect(find.text('脚本工作台'), findsOneWidget);
  });

  testWidgets('built-in examples are non-disruptive', (tester) async {
    _wide(tester);
    await tester.pumpWidget(
        MaterialApp(home: BadUsbPage(repository: _MemoryRepository([]))));
    await tester.pumpAndSettle();
    await tester.pumpAndSettle();
    expect(find.text('输入字符'), findsOneWidget);
    expect(find.text('等待 100ms'), findsOneWidget);
    expect(find.textContaining('curl'), findsNothing);
    expect(find.textContaining('GUI r'), findsNothing);
  });

  testWidgets('persistence errors remain visible and editor draft survives',
      (tester) async {
    final repository = _MemoryRepository([], failSave: true);
    await tester
        .pumpWidget(MaterialApp(home: BadUsbPage(repository: repository)));
    await tester.pumpAndSettle();
    await tester.enterText(
        find.byKey(const ValueKey('usb-script-editor')), 'GUI r');
    await tester.tap(find.text('保存到本地'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'Draft title');
    await tester.tap(find.widgetWithText(FilledButton, '保存'));
    await tester.pumpAndSettle();
    expect(find.textContaining('本地保存失败'), findsOneWidget);
    expect(
        tester
            .widget<TextField>(find.byKey(const ValueKey('usb-script-editor')))
            .controller!
            .text,
        'GUI r');
  });
}

void _wide(WidgetTester tester) {
  tester.view.physicalSize = const Size(1280, 1000);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
}

Future<void> _reveal(WidgetTester tester, Finder target) async {
  // Scroll only the library, not the horizontal TabBarView ancestor.
  for (var i = 0; target.evaluate().isEmpty && i < 20; i++) {
    await tester.drag(find.byKey(const ValueKey('usb-script-library')),
        const Offset(0, -180));
    await tester.pumpAndSettle();
  }
  final context = tester.element(target);
  await Scrollable.of(context)
      .position
      .ensureVisible(tester.renderObject(target));
  await tester.pumpAndSettle();
}

class _MemoryRepository implements UsbScriptRepository {
  _MemoryRepository(this.scripts, {this.failSave = false});
  final List<UsbScript> scripts;
  final bool failSave;

  @override
  Future<List<UsbScript>> load() async => List.of(scripts);

  @override
  Future<void> save(UsbScript script) async {
    if (failSave) throw StateError('injected write failure');
    final index = scripts.indexWhere((item) => item.id == script.id);
    if (index < 0) {
      scripts.add(script);
    } else {
      scripts[index] = script;
    }
  }

  @override
  Future<void> delete(String id) async =>
      scripts.removeWhere((item) => item.id == id);
}
