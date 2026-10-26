import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/settings/settings_protocol.dart';

void main() {
  test('encodes only the four device settings as bounded ASCII fields', () {
    expect(
        encodeSettingsPatch({
          'brightness': 745,
          'invert': 1,
          'screen_off_ms': 15000,
          'deep_sleep_ms': 0,
        }),
        'brightness=745;invert=1;screen_off_ms=15000;deep_sleep_ms=0');
  });

  test('rejects empty, unknown, out-of-range, and unsupported settings', () {
    for (final patch in <Map<String, int>>[
      {},
      {'always_on': 1},
      {'brightness': 1001},
      {'invert': 2},
      {'screen_off_ms': 0},
      {'deep_sleep_ms': 86400001},
    ]) {
      expect(() => encodeSettingsPatch(patch), throwsFormatException);
    }
  });

  test('parses correlated firmware snapshots and persisted-save outcomes', () {
    final snapshot = SettingsReply.parse(
        'result=snapshot;error=none;display=unchanged;brightness=745;invert=1;screen_off_ms=15000;deep_sleep_ms=0');
    expect(snapshot.settings.brightness, 745);
    expect(snapshot.settings.invert, isTrue);
    expect(snapshot.settings.screenOffMs, 15000);
    expect(snapshot.settings.deepSleepMs, 0);

    final saved = SettingsReply.parse(
        'result=saved;error=none;display=deferred;brightness=745;invert=1;screen_off_ms=15000;deep_sleep_ms=0');
    expect(saved.result, 'saved');
    expect(saved.display, 'deferred');
    final failed = SettingsReply.parse(
        'result=save_failed;error=program;display=unchanged;brightness=745;invert=1;screen_off_ms=15000;deep_sleep_ms=0');
    expect(failed.error, 'program');
    expect(
        () => SettingsReply.parse(
            'result=saved;error=none;display=unchanged;brightness=745;invert=1;screen_off_ms=15000;deep_sleep_ms=0;always_on=1'),
        throwsFormatException);
  });
}
