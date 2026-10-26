import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/settings/preferences_protocol.dart';

String _reply({String result = 'snapshot', String error = 'none'}) =>
    'result=$result;error=$error;pat=1;qmi=0;lift=1;tap=0;'
    'sound=1;led=0;volume=37;led_level=82;face=4;deep_sleep=0';

void main() {
  test('encodes partial ASCII patches and excludes read-only capability', () {
    expect(
      encodePreferencesPatch({'pat': 1, 'volume': 37, 'face': 4}),
      'pat=1;volume=37;face=4',
    );
    expect(
      encodePreferencesPatch({'led_level': 0, 'lift': 0}),
      'led_level=0;lift=0',
    );
    expect(
      () => encodePreferencesPatch({'deep_sleep': 1}),
      throwsFormatException,
    );
  });

  test('rejects empty, unknown, and out-of-range patches', () {
    for (final patch in <Map<String, int>>[
      {},
      {'unknown': 1},
      {'pat': 2},
      {'volume': 101},
      {'led_level': -1},
      {'face': 5},
      {'qmi': -1},
    ]) {
      expect(() => encodePreferencesPatch(patch), throwsFormatException);
    }
  });

  test('parses the exact snapshot shape and all declared result outcomes', () {
    final snapshot = PreferencesReply.parse(_reply());
    expect(snapshot.result, 'snapshot');
    expect(snapshot.preferences.pat, isTrue);
    expect(snapshot.preferences.qmi, isFalse);
    expect(snapshot.preferences.lift, isTrue);
    expect(snapshot.preferences.tap, isFalse);
    expect(snapshot.preferences.sound, isTrue);
    expect(snapshot.preferences.led, isFalse);
    expect(snapshot.preferences.volume, 37);
    expect(snapshot.preferences.ledLevel, 82);
    expect(snapshot.preferences.face, 4);
    expect(snapshot.preferences.deepSleep, isFalse);
    expect(snapshot.preferences.fields.keys, isNot(contains('deep_sleep')));

    expect(PreferencesReply.parse(_reply(result: 'saved')).result, 'saved');
    expect(
      PreferencesReply.parse(_reply(result: 'invalid', error: 'payload')).error,
      'payload',
    );
    expect(
      PreferencesReply.parse(_reply(result: 'busy', error: 'owner')).result,
      'busy',
    );
    for (final error in ['unlock', 'erase', 'program', 'lock']) {
      expect(
        PreferencesReply.parse(
          _reply(result: 'save_failed', error: error),
        ).error,
        error,
      );
    }
  });

  test('rejects duplicate, unknown, incomplete, non-ASCII, and oversized data',
      () {
    final valid = _reply();
    for (final value in [
      '$valid;pat=0',
      '$valid;extra=1',
      valid.replaceFirst(';deep_sleep=0', ''),
      valid.replaceFirst('volume=37', 'volume=37;note=é'),
      '$valid;${List.filled(100, 'x').join()}',
    ]) {
      expect(() => PreferencesReply.parse(value), throwsFormatException);
    }
  });

  test('rejects invalid result/error pairs and every value range violation',
      () {
    for (final value in [
      _reply(result: 'snapshot', error: 'program'),
      _reply(result: 'invalid', error: 'none'),
      _reply(result: 'busy', error: 'payload'),
      _reply(result: 'save_failed', error: 'none'),
      _reply().replaceFirst('pat=1', 'pat=2'),
      _reply().replaceFirst('volume=37', 'volume=101'),
      _reply().replaceFirst('led_level=82', 'led_level=999'),
      _reply().replaceFirst('face=4', 'face=5'),
      _reply().replaceFirst('deep_sleep=0', 'deep_sleep=2'),
      _reply().replaceFirst('volume=37', 'volume=-1'),
    ]) {
      expect(() => PreferencesReply.parse(value), throwsFormatException);
    }
  });
}
