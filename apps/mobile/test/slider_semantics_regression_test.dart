import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:crabit_watch_app/presentation/widgets/app_shell.dart';
import 'package:crabit_watch_app/presentation/widgets/slider_overlay_scope.dart';

// Observe exactly what the framework submits, not just the render-object tree.
// Flutter's native accessibility bridge receives these incremental updates.
final _binding = _RecordingBinding();

class _RecordingBinding extends AutomatedTestWidgetsFlutterBinding {
  final tree = <int, List<int>>{};
  final orphans = <int>{};
  int updates = 0;

  @override
  ui.SemanticsUpdateBuilder createSemanticsUpdateBuilder() =>
      _RecordingBuilder(super.createSemanticsUpdateBuilder(), this);

  void commit(Map<int, List<int>> nodes) {
    updates++;
    tree.addAll(nodes);
    final reachable = <int>{};
    void visit(int id) {
      if (!reachable.add(id)) return;
      for (final child in tree[id] ?? const <int>[]) {
        visit(child);
      }
    }

    visit(0);
    orphans.addAll(nodes.keys.where((id) => !reachable.contains(id)));
    tree.removeWhere((id, _) => !reachable.contains(id));
  }

  void clearObservations() {
    tree.clear();
    orphans.clear();
    updates = 0;
  }
}

class _RecordingBuilder implements ui.SemanticsUpdateBuilder {
  _RecordingBuilder(this.delegate, this.binding);
  final ui.SemanticsUpdateBuilder delegate;
  final _RecordingBinding binding;
  final nodes = <int, List<int>>{};

  @override
  void updateNode({
    required int id,
    required ui.SemanticsFlags flags,
    required int actions,
    required int maxValueLength,
    required int currentValueLength,
    required int textSelectionBase,
    required int textSelectionExtent,
    required int platformViewId,
    required int scrollChildren,
    required int scrollIndex,
    required int traversalParent,
    required double scrollPosition,
    required double scrollExtentMax,
    required double scrollExtentMin,
    required ui.Rect rect,
    required String identifier,
    required String label,
    required List<ui.StringAttribute> labelAttributes,
    required String value,
    required List<ui.StringAttribute> valueAttributes,
    required String increasedValue,
    required List<ui.StringAttribute> increasedValueAttributes,
    required String decreasedValue,
    required List<ui.StringAttribute> decreasedValueAttributes,
    required String hint,
    required List<ui.StringAttribute> hintAttributes,
    required String tooltip,
    required ui.TextDirection? textDirection,
    required Float64List transform,
    required Float64List hitTestTransform,
    required Int32List childrenInTraversalOrder,
    required Int32List childrenInHitTestOrder,
    required Int32List additionalActions,
    int headingLevel = 0,
    String linkUrl = '',
    ui.SemanticsRole role = ui.SemanticsRole.none,
    required List<String>? controlsNodes,
    ui.SemanticsValidationResult validationResult =
        ui.SemanticsValidationResult.none,
    ui.SemanticsHitTestBehavior hitTestBehavior =
        ui.SemanticsHitTestBehavior.defer,
    required ui.SemanticsInputType inputType,
    required ui.Locale? locale,
    required String minValue,
    required String maxValue,
  }) {
    nodes[id] = childrenInTraversalOrder.toList();
    delegate.updateNode(
      id: id,
      flags: flags,
      actions: actions,
      maxValueLength: maxValueLength,
      currentValueLength: currentValueLength,
      textSelectionBase: textSelectionBase,
      textSelectionExtent: textSelectionExtent,
      platformViewId: platformViewId,
      scrollChildren: scrollChildren,
      scrollIndex: scrollIndex,
      traversalParent: traversalParent,
      scrollPosition: scrollPosition,
      scrollExtentMax: scrollExtentMax,
      scrollExtentMin: scrollExtentMin,
      rect: rect,
      identifier: identifier,
      label: label,
      labelAttributes: labelAttributes,
      value: value,
      valueAttributes: valueAttributes,
      increasedValue: increasedValue,
      increasedValueAttributes: increasedValueAttributes,
      decreasedValue: decreasedValue,
      decreasedValueAttributes: decreasedValueAttributes,
      hint: hint,
      hintAttributes: hintAttributes,
      tooltip: tooltip,
      textDirection: textDirection,
      transform: transform,
      hitTestTransform: hitTestTransform,
      childrenInTraversalOrder: childrenInTraversalOrder,
      childrenInHitTestOrder: childrenInHitTestOrder,
      additionalActions: additionalActions,
      headingLevel: headingLevel,
      linkUrl: linkUrl,
      role: role,
      controlsNodes: controlsNodes,
      validationResult: validationResult,
      hitTestBehavior: hitTestBehavior,
      inputType: inputType,
      locale: locale,
      minValue: minValue,
      maxValue: maxValue,
    );
  }

  @override
  void updateCustomAction(
          {required int id,
          String? label,
          String? hint,
          int overrideId = -1}) =>
      delegate.updateCustomAction(
          id: id, label: label, hint: hint, overrideId: overrideId);

  @override
  ui.SemanticsUpdate build() {
    binding.commit(nodes);
    return delegate.build();
  }
}

void main() {
  _binding.clearObservations();
  testWidgets('probe raw and local-overlay sliders during route transitions',
      (tester) async {
    final semantics = tester.ensureSemantics();
    for (final isolated in [false, true]) {
      _binding.clearObservations();
      await tester.pumpWidget(MaterialApp(
          home: Builder(
        builder: (context) => Scaffold(
            body: TextButton(
                onPressed: () => Navigator.of(context).push(
                      MaterialPageRoute<void>(
                          builder: (_) => Scaffold(
                                  body: Column(children: [
                                _slider(isolated),
                                _slider(isolated),
                              ]))),
                    ),
                child: const Text('open'))),
      )));
      await tester.pumpAndSettle();
      await tester.tap(find.text('open'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 16));
      await tester.pumpAndSettle();
      // ignore: avoid_print
      print(
          'slider-isolated=$isolated commits=${_binding.updates} orphanIds=${_binding.orphans}');
      expect(_binding.updates, greaterThan(0));
      if (isolated) expect(_binding.orphans, isEmpty);
      await tester.pumpWidget(const SizedBox.shrink());
      await tester.pumpAndSettle();
    }
    semantics.dispose();
  }, variant: TargetPlatformVariant.only(TargetPlatform.windows));

  testWidgets(
      'hidden AppShell sliders stay attached across resize and tab changes',
      (tester) async {
    final semantics = tester.ensureSemantics();
    _binding.clearObservations();
    await tester.pumpWidget(MaterialApp(
        home: AppShell(pages: [
      Scaffold(body: Column(children: [_slider(true), _slider(true)])),
      Scaffold(body: _slider(true)),
    ])));
    await tester.pumpAndSettle();
    for (var i = 0; i < 8; i++) {
      tester.view.physicalSize =
          i.isEven ? const Size(1100, 800) : const Size(700, 900);
      tester.view.devicePixelRatio = 1;
      await tester.pumpAndSettle();
      final label = i.isEven
          ? find.byKey(const ValueKey('app-nav-1'))
          : find.text('设备').last;
      await tester.tap(label);
      await tester.pumpAndSettle();
    }
    expect(_binding.updates, greaterThan(8));
    expect(_binding.orphans, isEmpty);
    tester.view.resetPhysicalSize();
    tester.view.resetDevicePixelRatio();
    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pumpAndSettle();
    semantics.dispose();
  }, variant: TargetPlatformVariant.only(TargetPlatform.windows));

  testWidgets('isolated slider preserves size, pointer, keyboard and semantics',
      (tester) async {
    final semantics = tester.ensureSemantics();
    var value = .5;
    final focus = FocusNode();
    await tester.pumpWidget(MaterialApp(
        home: Scaffold(
            body: StatefulBuilder(
      builder: (context, setState) => Column(children: [
        SliderOverlayScope(
            child: Slider(
          key: const Key('interactive-slider'),
          value: value,
          min: 0,
          max: 1,
          divisions: 10,
          focusNode: focus,
          onChanged: (next) => setState(() => value = next),
        )),
      ]),
    ))));
    await tester.pumpAndSettle();
    final slider = find.byKey(const Key('interactive-slider'));
    expect(tester.getSize(slider).height, lessThanOrEqualTo(64));
    await tester.drag(slider, const Offset(100, 0));
    await tester.pumpAndSettle();
    expect(value, greaterThan(.5));
    focus.requestFocus();
    await tester.pumpAndSettle();
    final beforeKey = value;
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowLeft);
    await tester.pumpAndSettle();
    expect(value, lessThan(beforeKey));
    expect(
        tester.getSemantics(slider).getSemanticsData().flagsCollection.isSlider,
        isTrue);
    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pumpAndSettle();
    focus.dispose();
    semantics.dispose();
  }, variant: TargetPlatformVariant.only(TargetPlatform.windows));

  testWidgets('other platforms retain the original slider without an overlay',
      (tester) async {
    const key = Key('android-slider');
    await tester.pumpWidget(MaterialApp(
        home: Scaffold(
            body: SliderOverlayScope(
      child: Slider(key: key, value: .5, onChanged: (_) {}),
    ))));
    final scope = find.byType(SliderOverlayScope);
    expect(find.descendant(of: scope, matching: find.byType(Overlay)),
        findsNothing);
    expect(
        find.descendant(of: scope, matching: find.byKey(key)), findsOneWidget);
  }, variant: TargetPlatformVariant.only(TargetPlatform.android));
}

Widget _slider(bool isolated) {
  final slider = Slider(value: .5, onChanged: (_) {});
  return isolated ? SliderOverlayScope(child: slider) : slider;
}
