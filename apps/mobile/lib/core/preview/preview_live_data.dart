import 'dart:async';

import 'package:hive_flutter/hive_flutter.dart';

import '../managers/storage_manager.dart';
import '../weather/weather_controller.dart';
import '../weather/weather_service.dart';

/// Read-only data source for the local NanoUI preview. It never sends data to
/// a watch and only contacts Open-Meteo when the preview is active.
abstract interface class PreviewWeatherSource {
  String get label;
  PreviewWeather? cached(DateTime now);
  Future<PreviewWeather?> load(DateTime now);
  void close();
}

class PreviewWeather {
  const PreviewWeather(this.day, this.fetchedAt);
  final WeatherDay day;
  final DateTime fetchedAt;
}

class HostPreviewWeatherSource implements PreviewWeatherSource {
  HostPreviewWeatherSource({
    WeatherService? service,
    WeatherStore? store,
    this.defaultLocation = const WeatherLocation(
      name: '重庆',
      latitude: 29.5630,
      longitude: 106.5516,
    ),
    DateTime Function()? now,
  })  : _service = service ?? OpenMeteoWeatherService(),
        _providedStore = store,
        _now = now ?? DateTime.now;

  final WeatherService _service;
  final WeatherLocation defaultLocation;
  final DateTime Function() _now;
  bool _closed = false;

  @override
  String get label {
    final selected = _store?.location;
    final location = _store?.cachedLocation ?? defaultLocation;
    if (selected != null || _store?.cachedLocation != null) {
      return 'Open-Meteo 日预报 · 已保存城市 ${selected?.name ?? location.name}';
    }
    return 'Open-Meteo 日预报 · ${location.name} 示例城市';
  }

  WeatherStore? get _store =>
      _providedStore ??
      (Hive.isBoxOpen(StorageManager.boxSettings)
          ? HiveWeatherStore(StorageManager())
          : null);
  final WeatherStore? _providedStore;

  @override
  PreviewWeather? cached(DateTime now) {
    if (_closed) return null;
    final store = _store;
    final cachedAt = store?.fetchedAt;
    final cachedDay = _dayFor(store?.forecast, now);
    if (cachedDay != null &&
        cachedAt != null &&
        !cachedAt.isAfter(now.toUtc()) &&
        now.toUtc().difference(cachedAt.toUtc()) <= const Duration(hours: 6) &&
        _sameLocation(
            _store?.location ?? _store?.cachedLocation ?? defaultLocation,
            _store?.cachedLocation)) {
      return PreviewWeather(cachedDay, cachedAt);
    }
    return null;
  }

  @override
  Future<PreviewWeather?> load(DateTime now) async {
    if (_closed) return null;
    final store = _store;
    final location =
        store?.location ?? store?.cachedLocation ?? defaultLocation;
    try {
      final forecast = await _service
          .forecast(location, days: 3)
          .timeout(const Duration(seconds: 10));
      if (_closed) return null;
      final day = _dayFor(forecast, _now());
      if (day == null) return null;
      return PreviewWeather(day, _now().toUtc());
    } catch (_) {
      return cached(_now());
    }
  }

  static WeatherDay? _dayFor(WeatherForecast? forecast, DateTime now) {
    if (forecast == null) return null;
    for (final day in forecast.days) {
      if (day.date.year == now.year &&
          day.date.month == now.month &&
          day.date.day == now.day) {
        return day;
      }
    }
    return null;
  }

  static bool _sameLocation(
          WeatherLocation selected, WeatherLocation? cached) =>
      cached != null &&
      selected.latitude == cached.latitude &&
      selected.longitude == cached.longitude;

  @override
  void close() {
    if (_closed) return;
    _closed = true;
    _service.close();
  }
}

/// Session-owned refresher with an injectable clock/source and no background
/// work while paused. Failed refreshes are throttled exactly like successes.
class PreviewLiveData {
  PreviewLiveData({
    required this.source,
    required this.apply,
    DateTime Function()? now,
  }) : _now = now ?? DateTime.now;

  final PreviewWeatherSource source;
  final void Function(PreviewWeather?) apply;
  final DateTime Function() _now;
  static const refreshInterval = Duration(minutes: 15);
  bool _closed = false;
  bool _active = false;
  bool _retryWhenIdle = false;
  int _generation = 0;
  DateTime? _lastAttempt;
  Future<void>? _pending;
  Timer? _expiryTimer;
  PreviewWeather? _current;
  static const maxAge = Duration(hours: 6);

  void setActive(bool active) {
    if (_closed || _active == active) return;
    _active = active;
    if (active) {
      expireIfDue();
      refreshIfDue();
    }
    if (!active) {
      _generation++;
      if (_pending != null) {
        _lastAttempt = null;
      }
      _expiryTimer?.cancel();
      _expiryTimer = null;
    } else {
      _expiryTimer = Timer.periodic(const Duration(seconds: 30), (_) {
        expireIfDue();
      });
      if (_pending != null) _retryWhenIdle = true;
    }
  }

  void refreshIfDue() {
    if (_closed || !_active || _pending != null) return;
    final now = _now();
    final last = _lastAttempt;
    if (last != null &&
        !now.isBefore(last) &&
        now.difference(last) < refreshInterval) {
      return;
    }
    _lastAttempt = now;
    final generation = ++_generation;
    final cached = source.cached(now);
    if (cached != null) {
      _current = cached;
      apply(cached);
    }
    final request = source.load(now).then((value) {
      if (!_closed && _active && generation == _generation) {
        if (value != null) {
          _current = value;
          apply(value);
        } else if (cached == null && _current == null) {
          apply(null);
        }
      }
    }).catchError((Object _) {
      if (!_closed &&
          _active &&
          generation == _generation &&
          cached == null &&
          _current == null) {
        apply(null);
      }
    }).whenComplete(() {
      _pending = null;
      if (_retryWhenIdle && !_closed && _active) {
        _retryWhenIdle = false;
        refreshIfDue();
      }
    });
    _pending = request;
  }

  /// Expires the value actually shown by the session, including a fresh
  /// network response that has not been persisted in the app's Hive cache.
  void expireIfDue() {
    if (_closed || !_active) return;
    final current = _current;
    if (current == null) return;
    final now = _now();
    final age = now.toUtc().difference(current.fetchedAt.toUtc());
    if (age.isNegative ||
        age > maxAge ||
        current.day.date.year != now.year ||
        current.day.date.month != now.month ||
        current.day.date.day != now.day) {
      _current = null;
      apply(null);
    }
  }

  void close() {
    if (_closed) return;
    _closed = true;
    _active = false;
    _expiryTimer?.cancel();
    _expiryTimer = null;
    _generation++;
    source.close();
  }
}
