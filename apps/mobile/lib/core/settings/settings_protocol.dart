import 'dart:convert';

const settingsReadTopic = 'sys/settings';
const settingsSetTopic = 'sys/settings/set';
const settingsMaxDelayMs = 86400000;

/// Matches firmware/shared/settings_contract.h. Brightness is thousandths;
/// all wire values are ASCII safe through the existing BW16 JSON bridge.
class WatchSettings {
  const WatchSettings(
      {required this.brightness,
      required this.invert,
      required this.screenOffMs,
      required this.deepSleepMs});
  final int brightness;
  final bool invert;
  final int screenOffMs;
  final int deepSleepMs;
  WatchSettings copyWith(
          {int? brightness,
          bool? invert,
          int? screenOffMs,
          int? deepSleepMs}) =>
      WatchSettings(
          brightness: brightness ?? this.brightness,
          invert: invert ?? this.invert,
          screenOffMs: screenOffMs ?? this.screenOffMs,
          deepSleepMs: deepSleepMs ?? this.deepSleepMs);
  Map<String, int> get fields => {
        'brightness': brightness,
        'invert': invert ? 1 : 0,
        'screen_off_ms': screenOffMs,
        'deep_sleep_ms': deepSleepMs
      };
  Map<String, int> changesFrom(WatchSettings applied) => {
        for (final entry in fields.entries)
          if (entry.value != applied.fields[entry.key]) entry.key: entry.value,
      };
}

String encodeSettingsPatch(Map<String, int> fields) {
  if (fields.isEmpty) throw const FormatException('Empty settings patch');
  for (final entry in fields.entries) {
    final valid = switch (entry.key) {
      'brightness' => entry.value >= 0 && entry.value <= 1000,
      'invert' => entry.value == 0 || entry.value == 1,
      'screen_off_ms' => entry.value > 0 && entry.value <= settingsMaxDelayMs,
      'deep_sleep_ms' => entry.value >= 0 && entry.value <= settingsMaxDelayMs,
      _ => false,
    };
    if (!valid) throw const FormatException('Invalid settings patch');
  }
  final value = fields.entries.map((e) => '${e.key}=${e.value}').join(';');
  if (utf8.encode(value).length > 191) {
    throw const FormatException('Oversized settings patch');
  }
  return value;
}

class SettingsReply {
  const SettingsReply(this.result, this.error, this.display, this.settings);
  final String result;
  final String error;
  final String display;
  final WatchSettings settings;
  factory SettingsReply.parse(String value) {
    if (utf8.encode(value).length > 191 ||
        !RegExp(r'^[a-z0-9_=;\-]+$').hasMatch(value)) {
      throw const FormatException('Invalid settings response');
    }
    final fields = <String, String>{};
    for (final part in value.split(';')) {
      final pair = part.split('=');
      if (pair.length != 2 || pair[1].isEmpty || fields.containsKey(pair[0])) {
        throw const FormatException('Invalid settings fields');
      }
      fields[pair[0]] = pair[1];
    }
    const keys = {
      'result',
      'error',
      'display',
      'brightness',
      'invert',
      'screen_off_ms',
      'deep_sleep_ms'
    };
    if (fields.length != keys.length || !fields.keys.every(keys.contains)) {
      throw const FormatException('Incomplete settings response');
    }
    final result = fields['result']!;
    final error = fields['error']!;
    final display = fields['display']!;
    if (!const {'unchanged', 'deferred'}.contains(display)) {
      throw const FormatException('Invalid display application status');
    }
    final validResult = switch (result) {
      'snapshot' || 'saved' => error == 'none',
      'save_failed' =>
        const {'unlock', 'erase', 'program', 'lock'}.contains(error),
      'invalid' => error == 'payload',
      'busy' => error == 'owner',
      _ => false,
    };
    if (!validResult) throw const FormatException('Invalid settings result');
    int number(String key, int min, int max) {
      final raw = fields[key]!;
      if (!RegExp(r'^-?[0-9]+$').hasMatch(raw)) {
        throw const FormatException('Invalid number');
      }
      final parsed = int.tryParse(raw);
      if (parsed == null || parsed < min || parsed > max) {
        throw const FormatException('Out of range');
      }
      return parsed;
    }

    return SettingsReply(
        result,
        error,
        display,
        WatchSettings(
            brightness: number('brightness', -1, 1000),
            invert: number('invert', 0, 1) == 1,
            screenOffMs: number('screen_off_ms', 0, 0xffffffff),
            deepSleepMs: number('deep_sleep_ms', 0, 0xffffffff)));
  }
}
