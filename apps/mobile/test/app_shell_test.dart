import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/main.dart';
import 'package:crabit_watch_app/presentation/widgets/app_shell.dart';

const _labels = ['设备', 'USB 工具', '无线记录', '设置', '调试', '时间与天气'];

void main() {
  testWidgets('MyApp defaults to a readable light theme', (tester) async {
    await tester.pumpWidget(const MyApp(home: Scaffold(body: Text('theme'))));

    final context = tester.element(find.text('theme'));
    final theme = Theme.of(context);
    expect(theme.brightness, Brightness.light);
    expect(theme.colorScheme.brightness, Brightness.light);
    expect(theme.colorScheme.surface, AppPalette.surface);
    expect(theme.colorScheme.primary, AppPalette.blue);
    expect(_contrast(theme.colorScheme.onSurface, theme.colorScheme.surface),
        greaterThanOrEqualTo(4.5));
    expect(_contrast(theme.colorScheme.onPrimary, theme.colorScheme.primary),
        greaterThanOrEqualTo(4.5));
    expect(theme.colorScheme.surface, isNot(theme.colorScheme.primary));
  });

  testWidgets('switches layout across sizes and retains each page state',
      (tester) async {
    await _setSize(tester, const Size(360, 760));
    await tester.pumpWidget(_shell());

    expect(find.byKey(const ValueKey('mobile-navigation')), findsOneWidget);
    expect(find.byKey(const ValueKey('desktop-sidebar')), findsNothing);
    await _editPage(tester, 0);
    await _selectMobile(tester, 1);
    await _editPage(tester, 1);

    await _setSize(tester, const Size(1000, 480));
    await tester.pump();
    expect(find.byKey(const ValueKey('desktop-sidebar')), findsOneWidget);
    expect(find.byKey(const ValueKey('mobile-navigation')), findsNothing);
    expect(find.text('page 1 counter: 1'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'page 1 draft'), findsOneWidget);
    await _selectDesktop(tester, 4);
    await _editPage(tester, 4);

    await _setSize(tester, const Size(360, 480));
    await tester.pump();
    expect(find.byKey(const ValueKey('mobile-navigation')), findsOneWidget);
    expect(find.text('page 4 counter: 1'), findsOneWidget);
    await _selectMobile(tester, 0);
    expect(find.text('page 0 counter: 1'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'page 0 draft'), findsOneWidget);

    await _setSize(tester, const Size(1000, 760));
    await tester.pump();
    expect(find.byKey(const ValueKey('desktop-sidebar')), findsOneWidget);
    expect(find.text('page 0 counter: 1'), findsOneWidget);
  });

  testWidgets('all six destinations navigate and returning preserves state',
      (tester) async {
    await _setSize(tester, const Size(360, 760));
    await tester.pumpWidget(_shell());
    await _editPage(tester, 0);

    for (var index = 1; index < _labels.length; index++) {
      await _selectMobile(tester, index);
      expect(find.text('page $index'), findsOneWidget);
    }
    await _selectMobile(tester, 0);
    expect(find.text('page 0 counter: 1'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'page 0 draft'), findsOneWidget);

    final nav = tester
        .widget<NavigationBar>(find.byKey(const ValueKey('mobile-navigation')));
    expect(
        nav.destinations
            .map((destination) => (destination as NavigationDestination).label),
        _labels);
    expect(nav.selectedIndex, 0);
  });

  testWidgets('desktop sidebar exposes all named destinations', (tester) async {
    await _setSize(tester, const Size(1000, 760));
    await tester.pumpWidget(_shell());

    expect(find.byKey(const ValueKey('desktop-sidebar')), findsOneWidget);
    for (var index = 0; index < _labels.length; index++) {
      final tile =
          tester.widget<ListTile>(find.byKey(ValueKey('app-nav-$index')));
      expect(tile.title, isA<Text>());
      expect((tile.title! as Text).data, _labels[index]);
    }
    await _selectDesktop(tester, 3);
    expect(
        tester
            .widget<ListTile>(find.byKey(const ValueKey('app-nav-3')))
            .selected,
        isTrue);
    expect(find.text('page 3'), findsOneWidget);
  });

  testWidgets('text scaling remains usable at mobile and desktop sizes',
      (tester) async {
    tester.platformDispatcher.textScaleFactorTestValue = 1.5;
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    for (final size in [const Size(360, 760), const Size(1000, 760)]) {
      await _setSize(tester, size);
      await tester.pumpWidget(_shell());
      expect(find.text('page 0'), findsOneWidget);
      expect(tester.takeException(), isNull);
    }
  });

  testWidgets('keyboard insets hide and restore the mobile navigation bar',
      (tester) async {
    await _setSize(tester, const Size(360, 760));
    await tester.pumpWidget(_shell());
    expect(find.byKey(const ValueKey('mobile-navigation')), findsOneWidget);

    tester.view.viewInsets = const FakeViewPadding(bottom: 280);
    await tester.pump();
    expect(find.byKey(const ValueKey('mobile-navigation')), findsNothing);
    expect(find.byKey(const ValueKey('desktop-sidebar')), findsNothing);

    tester.view.viewInsets = FakeViewPadding.zero;
    await tester.pump();
    expect(find.byKey(const ValueKey('mobile-navigation')), findsOneWidget);
  });
}

Widget _shell() => MaterialApp(
      theme: AppTheme.lightTheme,
      home: const AppShell(pages: [
        _StatefulPage(index: 0),
        _StatefulPage(index: 1),
        _StatefulPage(index: 2),
        _StatefulPage(index: 3),
        _StatefulPage(index: 4),
        _StatefulPage(index: 5),
      ]),
    );

Future<void> _setSize(WidgetTester tester, Size size) async {
  tester.view.devicePixelRatio = 1;
  tester.view.physicalSize = size;
  await tester.pump();
}

Future<void> _selectMobile(WidgetTester tester, int index) async {
  await tester.tap(find.text(_labels[index]).last);
  await tester.pumpAndSettle();
}

Future<void> _selectDesktop(WidgetTester tester, int index) async {
  await tester.tap(find.byKey(ValueKey('app-nav-$index')));
  await tester.pumpAndSettle();
}

Future<void> _editPage(WidgetTester tester, int index) async {
  await tester.tap(find.byKey(ValueKey('counter-$index')));
  await tester.pump();
  await tester.enterText(
      find.byKey(ValueKey('field-$index')), 'page $index draft');
  await tester.pump();
}

double _contrast(Color foreground, Color background) {
  final a = foreground.computeLuminance();
  final b = background.computeLuminance();
  final lighter = a > b ? a : b;
  final darker = a > b ? b : a;
  return (lighter + 0.05) / (darker + 0.05);
}

class _StatefulPage extends StatefulWidget {
  const _StatefulPage({required this.index});
  final int index;

  @override
  State<_StatefulPage> createState() => _StatefulPageState();
}

class _StatefulPageState extends State<_StatefulPage> {
  int _count = 0;
  final _controller = TextEditingController();

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        body: ListView(padding: const EdgeInsets.all(24), children: [
          Text('page ${widget.index}'),
          Text('page ${widget.index} counter: $_count'),
          FilledButton(
            key: ValueKey('counter-${widget.index}'),
            onPressed: () => setState(() => _count++),
            child: const Text('increment'),
          ),
          TextField(
            key: ValueKey('field-${widget.index}'),
            controller: _controller,
            decoration: const InputDecoration(labelText: 'draft'),
          ),
        ]),
      );
}
