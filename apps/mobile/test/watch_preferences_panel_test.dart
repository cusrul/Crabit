import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/settings/preferences_gateway.dart';
import 'package:crabit_watch_app/core/settings/preferences_protocol.dart';
import 'package:crabit_watch_app/core/settings/settings_gateway.dart';
import 'package:crabit_watch_app/core/settings/settings_protocol.dart';
import 'package:crabit_watch_app/presentation/pages/settings/settings_page.dart';
import 'package:crabit_watch_app/presentation/widgets/watch_preferences_panel.dart';
import 'package:crabit_watch_app/presentation/widgets/slider_overlay_scope.dart';

WatchPreferences _preferences({
  bool pat = false,
  bool qmi = true,
  bool lift = false,
  bool tap = true,
  bool sound = true,
  bool led = false,
  int volume = 25,
  int ledLevel = 70,
  int face = 1,
  bool deepSleep = true,
}) =>
    WatchPreferences(
      pat: pat,
      qmi: qmi,
      lift: lift,
      tap: tap,
      sound: sound,
      led: led,
      volume: volume,
      ledLevel: ledLevel,
      face: face,
      deepSleep: deepSleep,
    );

class _PreferencesGateway extends ChangeNotifier implements PreferencesGateway {
  @override
  bool ready = false;
  int reads = 0;
  final writes = <Map<String, int>>[];
  WatchPreferences value = _preferences();

  @override
  Future<PreferencesReply> read() async {
    reads++;
    return PreferencesReply('snapshot', 'none', value);
  }

  @override
  Future<PreferencesReply> apply(Map<String, int> patch) async {
    writes.add(Map.of(patch));
    final fields = {...value.fields, ...patch};
    value = _preferences(
      pat: fields['pat'] == 1,
      qmi: fields['qmi'] == 1,
      lift: fields['lift'] == 1,
      tap: fields['tap'] == 1,
      sound: fields['sound'] == 1,
      led: fields['led'] == 1,
      volume: fields['volume']!,
      ledLevel: fields['led_level']!,
      face: fields['face']!,
      deepSleep: value.deepSleep,
    );
    return PreferencesReply('saved', 'none', value);
  }

  void setReady(bool value) {
    ready = value;
    notifyListeners();
  }
}

class _SettingsGateway extends ChangeNotifier implements SettingsGateway {
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
          deepSleepMs: 15000,
        ),
      );

  @override
  Future<SettingsReply> apply(Map<String, int> patch) => read();
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
  @override
  Future<void> setEnabled(bool value) async {}
}

void main() {
  testWidgets('panel waits for ready, edits a draft, and applies on request',
      (tester) async {
    final gateway = _PreferencesGateway();
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SingleChildScrollView(
          child: WatchPreferencesPanel(gateway: gateway),
        ),
      ),
    ));

    expect(gateway.reads, 0);
    expect(find.byKey(const Key('prefs-unavailable')), findsOneWidget);
    expect(find.text('触摸操作（PAT）'), findsNothing,
        reason: 'unread preferences must not render default switch values');
    expect(
      tester
          .widget<FilledButton>(find.byKey(const Key('prefs-apply')))
          .onPressed,
      isNull,
    );

    gateway.setReady(true);
    await tester.pumpAndSettle();
    expect(gateway.reads, 1);
    expect(find.text('触摸操作（PAT）'), findsOneWidget);
    expect(find.text('倾斜操作（QMI）'), findsOneWidget);
    expect(find.text('抬腕唤醒'), findsOneWidget);
    expect(find.text('敲击唤醒'), findsOneWidget);
    expect(find.text('声音反馈'), findsOneWidget);
    expect(find.text('灯光反馈'), findsOneWidget);
    expect(find.text('表盘'), findsOneWidget);
    expect(find.text('点阵'), findsOneWidget);
    for (final key in ['prefs-volume', 'prefs-led-level']) {
      expect(
          find.ancestor(
              of: find.byKey(Key(key)),
              matching: find.byType(SliderOverlayScope)),
          findsOneWidget);
    }
    expect(gateway.writes, isEmpty);

    final patSwitch = find.byKey(const Key('prefs-pat'));
    await tester.ensureVisible(patSwitch);
    await tester.pumpAndSettle();
    await tester.tap(patSwitch);
    await tester.pump();
    expect(gateway.writes, isEmpty,
        reason: 'switch changes remain a local draft until apply');
    final applyButton = find.byKey(const Key('prefs-apply'));
    await tester.ensureVisible(applyButton);
    await tester.pumpAndSettle();
    await tester.tap(applyButton);
    await tester.pumpAndSettle();
    expect(gateway.writes, [
      {'pat': 1}
    ]);
    expect(
      tester.widget<Text>(find.byKey(const Key('prefs-result'))).data,
      '手表已确认保存；当前值来自设备回读。',
    );

    await tester.pumpWidget(const SizedBox.shrink());
    gateway.dispose();
  });

  testWidgets(
      'deep-sleep editing requires confirmed capability and retains its value otherwise',
      (tester) async {
    final preferences = _PreferencesGateway()
      ..value = _preferences(deepSleep: false);
    final settings = _SettingsGateway();
    final remembered = _Remembered();
    await tester.pumpWidget(MaterialApp(
      home: SettingsPage(
        gateway: settings,
        rememberedConnection: remembered,
        preferencesGateway: preferences,
      ),
    ));
    await tester.pumpAndSettle();

    final sleepField = find.byKey(const Key('settings-sleep-15000'));
    await tester.scrollUntilVisible(sleepField, 180);
    await tester.pumpAndSettle();
    var dropdown = tester.widget<DropdownButtonFormField<int>>(sleepField);
    expect(dropdown.initialValue, 15000);
    expect(dropdown.onChanged, isNull);
    expect(find.textContaining('未确认深睡支持，暂不可改'), findsOneWidget);
    expect(find.textContaining('15000 ms 保留'), findsOneWidget);

    final preferencesRefresh = find.byKey(const Key('prefs-refresh'));
    final settingsList = find.byType(ListView).first;
    for (var scroll = 0;
        scroll < 20 && preferencesRefresh.evaluate().isEmpty;
        scroll++) {
      await tester.drag(settingsList, const Offset(0, -240));
      await tester.pumpAndSettle();
    }
    expect(preferencesRefresh, findsOneWidget);
    await tester.ensureVisible(preferencesRefresh);
    await tester.pumpAndSettle();
    preferences.setReady(true);
    await tester.pumpAndSettle();
    expect(preferences.reads, 1);

    await tester.scrollUntilVisible(sleepField, 180);
    await tester.pumpAndSettle();
    dropdown = tester.widget<DropdownButtonFormField<int>>(sleepField);
    expect(dropdown.initialValue, 15000);
    expect(dropdown.onChanged, isNull);
    expect(
      find.textContaining('设备报告当前固件仅支持屏幕息屏'),
      findsOneWidget,
    );
    expect(find.textContaining('15000 ms 保留'), findsOneWidget);
    final timeWeatherTile = find.byKey(const Key('settings-time-weather'));
    for (var scroll = 0;
        scroll < 20 && timeWeatherTile.evaluate().isEmpty;
        scroll++) {
      await tester.drag(settingsList, const Offset(0, 240));
      await tester.pumpAndSettle();
    }
    expect(timeWeatherTile, findsOneWidget);
    await tester.ensureVisible(timeWeatherTile);
    await tester.pumpAndSettle();
    expect(
      find.descendant(
        of: timeWeatherTile,
        matching: find.text('时间与天气'),
      ),
      findsOneWidget,
    );

    await tester.pumpWidget(const SizedBox.shrink());
    settings.dispose();
    remembered.dispose();
    preferences.dispose();
  });
}
