import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/settings/settings_gateway.dart';
import 'package:crabit_watch_app/core/settings/settings_protocol.dart';
import 'package:crabit_watch_app/presentation/pages/settings/settings_page.dart';
import 'package:crabit_watch_app/presentation/widgets/slider_overlay_scope.dart';

class _Gateway extends ChangeNotifier implements SettingsGateway {
  @override
  bool ready = true;
  @override
  Future<SettingsReply> read() async => SettingsReply(
      'snapshot',
      'none',
      'unchanged',
      const WatchSettings(
          brightness: 500,
          invert: false,
          screenOffMs: 3000,
          deepSleepMs: 10000));
  @override
  Future<SettingsReply> apply(Map<String, int> patch) async => read();
}

class _Remembered extends ChangeNotifier
    implements RememberedConnectionPreference {
  @override
  bool enabled = false;
  @override
  bool saving = false;
  @override
  String status = '连接偏好已关闭';
  @override
  bool hasRememberedDevice = false;
  bool? lastSetValue;
  @override
  Future<void> setEnabled(bool value) async {
    lastSetValue = value;
    enabled = value;
    notifyListeners();
  }
}

void main() {
  testWidgets('shows only supported watch fields and gates remembered opt-in',
      (tester) async {
    final remembered = _Remembered();
    await tester.pumpWidget(MaterialApp(
        home: SettingsPage(
            gateway: _Gateway(), rememberedConnection: remembered)));
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('settings-brightness')), findsOneWidget);
    expect(
        find.ancestor(
            of: find.byKey(const Key('settings-brightness')),
            matching: find.byType(SliderOverlayScope)),
        findsOneWidget);
    expect(find.text('反色显示'), findsOneWidget);
    expect(find.text('息屏时间'), findsOneWidget);
    expect(find.text('息屏后深度休眠延迟'), findsOneWidget);
    expect(find.textContaining('始终供电'), findsNothing);
    await tester.scrollUntilVisible(
        find.byKey(const Key('settings-remembered-connection')), 200);
    final preference = tester.widget<SwitchListTile>(
        find.byKey(const Key('settings-remembered-connection')));
    expect(preference.onChanged, isNull);

    remembered.hasRememberedDevice = true;
    remembered.notifyListeners();
    await tester.pump();
    await tester.tap(find.byKey(const Key('settings-remembered-connection')));
    await tester.pump();
    expect(remembered.lastSetValue, isTrue);
    expect(remembered.enabled, isTrue);
  });

  testWidgets(
      'time and weather opens the existing page without sending commands',
      (tester) async {
    final remembered = _Remembered();
    var opens = 0;
    var sends = 0;
    await tester.pumpWidget(MaterialApp(
        home: SettingsPage(
            gateway: _Gateway(),
            rememberedConnection: remembered,
            onOpenTimeWeather: () => opens++,
            sendTimeSync: (topic, value) async {
              sends++;
              return true;
            })));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('settings-time-weather')));
    await tester.pumpAndSettle();
    expect(opens, 1);
    expect(sends, 0);
    expect(find.byKey(const Key('settings-sync-time')), findsNothing);
  });
}
