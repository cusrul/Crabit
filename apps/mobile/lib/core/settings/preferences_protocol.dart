import 'dart:convert';

const preferencesReadTopic = 'sys/prefs';
const preferencesSetTopic = 'sys/prefs/set';
const preferencesMaxValueBytes = 191;

const watchFaceNames = <int, String>{
  0: '反白底栏',
  1: '点阵',
  2: '极简',
  3: '左右分区',
  4: '双行时刻',
};

/// Product preferences exposed by the watch. [deepSleep] is a read-only
/// capability bit; it is deliberately absent from [fields] and write patches.
class WatchPreferences {
  const WatchPreferences({
    required this.pat,
    required this.qmi,
    required this.lift,
    required this.tap,
    required this.sound,
    required this.led,
    required this.volume,
    required this.ledLevel,
    required this.face,
    required this.deepSleep,
  });

  final bool pat;
  final bool qmi;
  final bool lift;
  final bool tap;
  final bool sound;
  final bool led;
  final int volume;
  final int ledLevel;
  final int face;
  final bool deepSleep;

  Map<String, int> get fields => {
        'pat': pat ? 1 : 0,
        'qmi': qmi ? 1 : 0,
        'lift': lift ? 1 : 0,
        'tap': tap ? 1 : 0,
        'sound': sound ? 1 : 0,
        'led': led ? 1 : 0,
        'volume': volume,
        'led_level': ledLevel,
        'face': face,
      };

  WatchPreferences copyWith({
    bool? pat,
    bool? qmi,
    bool? lift,
    bool? tap,
    bool? sound,
    bool? led,
    int? volume,
    int? ledLevel,
    int? face,
  }) =>
      WatchPreferences(
        pat: pat ?? this.pat,
        qmi: qmi ?? this.qmi,
        lift: lift ?? this.lift,
        tap: tap ?? this.tap,
        sound: sound ?? this.sound,
        led: led ?? this.led,
        volume: volume ?? this.volume,
        ledLevel: ledLevel ?? this.ledLevel,
        face: face ?? this.face,
        deepSleep: deepSleep,
      );

  Map<String, int> changesFrom(WatchPreferences applied) => {
        for (final entry in fields.entries)
          if (entry.value != applied.fields[entry.key]) entry.key: entry.value,
      };
}

class PreferencesReply {
  const PreferencesReply(this.result, this.error, this.preferences);

  final String result;
  final String error;
  final WatchPreferences preferences;

  factory PreferencesReply.parse(String value) {
    if (utf8.encode(value).length > preferencesMaxValueBytes ||
        !RegExp(r'^[a-z0-9_=;]+$').hasMatch(value)) {
      throw const FormatException('Invalid preferences response');
    }

    final fields = <String, String>{};
    for (final part in value.split(';')) {
      final pair = part.split('=');
      if (pair.length != 2 || pair[1].isEmpty || fields.containsKey(pair[0])) {
        throw const FormatException('Invalid preferences fields');
      }
      fields[pair[0]] = pair[1];
    }

    const keys = {
      'result',
      'error',
      'pat',
      'qmi',
      'lift',
      'tap',
      'sound',
      'led',
      'volume',
      'led_level',
      'face',
      'deep_sleep',
    };
    if (fields.length != keys.length || !fields.keys.every(keys.contains)) {
      throw const FormatException('Incomplete preferences response');
    }

    final result = fields['result']!;
    final error = fields['error']!;
    final validResult = switch (result) {
      'snapshot' || 'saved' => error == 'none',
      'invalid' => error == 'payload',
      'busy' => error == 'owner',
      'save_failed' =>
        const {'unlock', 'erase', 'program', 'lock'}.contains(error),
      _ => false,
    };
    if (!validResult) throw const FormatException('Invalid preferences result');

    int number(String key, int min, int max) {
      final raw = fields[key]!;
      if (!RegExp(r'^(0|[1-9][0-9]*)$').hasMatch(raw)) {
        throw const FormatException('Invalid preferences number');
      }
      final parsed = int.tryParse(raw);
      if (parsed == null || parsed < min || parsed > max) {
        throw const FormatException('Preferences value out of range');
      }
      return parsed;
    }

    bool flag(String key) => number(key, 0, 1) == 1;

    return PreferencesReply(
      result,
      error,
      WatchPreferences(
        pat: flag('pat'),
        qmi: flag('qmi'),
        lift: flag('lift'),
        tap: flag('tap'),
        sound: flag('sound'),
        led: flag('led'),
        volume: number('volume', 0, 100),
        ledLevel: number('led_level', 0, 100),
        face: number('face', 0, 4),
        deepSleep: flag('deep_sleep'),
      ),
    );
  }
}

/// Encode only user-editable fields. A Map cannot contain duplicate keys;
/// duplicate wire fields are rejected by [PreferencesReply.parse].
String encodePreferencesPatch(Map<String, int> fields) {
  if (fields.isEmpty) throw const FormatException('Empty preferences patch');
  for (final entry in fields.entries) {
    final valid = switch (entry.key) {
      'pat' ||
      'qmi' ||
      'lift' ||
      'tap' ||
      'sound' ||
      'led' =>
        entry.value == 0 || entry.value == 1,
      'volume' || 'led_level' => entry.value >= 0 && entry.value <= 100,
      'face' => entry.value >= 0 && entry.value <= 4,
      _ => false,
    };
    if (!valid) throw const FormatException('Invalid preferences patch');
  }

  final value = fields.entries.map((e) => '${e.key}=${e.value}').join(';');
  if (utf8.encode(value).length > preferencesMaxValueBytes) {
    throw const FormatException('Oversized preferences patch');
  }
  return value;
}
