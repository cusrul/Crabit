import 'dart:async';

import 'package:crabit_watch_app/core/preview/preview_live_data.dart';
import 'package:crabit_watch_app/core/weather/weather_service.dart';
import 'package:crabit_watch_app/core/weather/weather_controller.dart';
import 'package:flutter_test/flutter_test.dart';

class _Source implements PreviewWeatherSource {
  _Source(this.result);
  PreviewWeather? result;
  final requests = <Completer<PreviewWeather?>>[];
  int closed = 0;
  @override
  String get label => 'test';
  @override
  PreviewWeather? cached(DateTime now) => result;
  @override
  Future<PreviewWeather?> load(DateTime now) {
    final request = Completer<PreviewWeather?>();
    requests.add(request);
    return request.future;
  }

  @override
  void close() => closed++;
}

class _Store implements WeatherStore {
  @override
  WeatherLocation? location;
  @override
  WeatherLocation? cachedLocation;
  @override
  WeatherForecast? forecast;
  @override
  DateTime? fetchedAt;
  @override
  Future<void> saveForecast(WeatherForecast forecast, WeatherLocation location,
          DateTime fetchedAt) async =>
      throw UnimplementedError();
  @override
  Future<void> saveLocation(WeatherLocation location) async =>
      throw UnimplementedError();
}

class _WeatherService implements WeatherService {
  @override
  Future<WeatherForecast> forecast(WeatherLocation location, {int days = 3}) =>
      throw UnimplementedError();
  @override
  Future<List<WeatherLocation>> search(String query) =>
      throw UnimplementedError();
  @override
  void close() {}
}

void main() {
  test('inactive does nothing; active uses cache then throttles refresh',
      () async {
    var now = DateTime(2026, 10, 25, 12);
    final day = WeatherDay(
      date: DateTime(2026, 10, 25),
      code: 3,
      lowC: 10,
      highC: 18,
      rainProbability: 20,
    );
    final cached = PreviewWeather(day, now.toUtc());
    final source = _Source(cached);
    final applied = <PreviewWeather?>[];
    final live =
        PreviewLiveData(source: source, now: () => now, apply: applied.add);
    live.refreshIfDue();
    expect(source.requests, isEmpty);
    live.setActive(true);
    expect(applied, [cached]);
    expect(source.requests, hasLength(1));
    source.requests.single.complete(null);
    await Future<void>.delayed(Duration.zero);
    expect(applied.last, cached);
    live.refreshIfDue();
    expect(source.requests, hasLength(1));
    now = now.add(const Duration(minutes: 15));
    live.refreshIfDue();
    expect(source.requests, hasLength(2));
    live.close();
    expect(source.closed, 1);
  });

  test('pause suppresses late response and close suppresses later results',
      () async {
    var now = DateTime(2026, 10, 25);
    final source = _Source(null);
    final applied = <PreviewWeather?>[];
    final live =
        PreviewLiveData(source: source, now: () => now, apply: applied.add);
    live.setActive(true);
    live.setActive(false);
    source.requests.single.complete(null);
    await Future<void>.delayed(Duration.zero);
    expect(applied, isEmpty);
    now = now.add(const Duration(minutes: 15));
    live.setActive(true);
    live.close();
    source.requests.last.complete(null);
    await Future<void>.delayed(Duration.zero);
    expect(applied, isEmpty);
  });

  test('resume retries immediately after a paused request settles', () async {
    final now = DateTime(2026, 10, 25);
    final source = _Source(null);
    final applied = <PreviewWeather?>[];
    final live =
        PreviewLiveData(source: source, now: () => now, apply: applied.add);
    live.setActive(true);
    live.setActive(false);
    live.setActive(true);
    expect(source.requests, hasLength(1));
    source.requests.first.complete(null);
    await Future<void>.delayed(Duration.zero);
    expect(source.requests, hasLength(2));
    expect(applied, isEmpty);
    live.close();
  });

  test('host cache is fresh for six hours and must match selected city', () {
    final now = DateTime(2026, 10, 25, 12);
    const city = WeatherLocation(name: 'Saved city', latitude: 1, longitude: 2);
    final day = WeatherDay(
      date: DateTime(2026, 10, 25),
      code: 63,
      lowC: 19,
      highC: 22.9,
      rainProbability: 30,
    );
    final store = _Store()
      ..location = city
      ..cachedLocation = city
      ..forecast = WeatherForecast(
        timezone: 'test',
        utcOffsetSeconds: 0,
        days: [day],
      )
      ..fetchedAt = now.toUtc().subtract(const Duration(hours: 5));
    final source = HostPreviewWeatherSource(
      service: _WeatherService(),
      store: store,
      now: () => now,
    );
    expect(source.cached(now)?.day, day);
    expect(source.label, contains('已保存城市 Saved city'));
    store.cachedLocation = const WeatherLocation(
      name: 'Other',
      latitude: 3,
      longitude: 4,
    );
    expect(source.cached(now), isNull);
    store.cachedLocation = city;
    store.fetchedAt = now.toUtc().subtract(const Duration(hours: 7));
    expect(source.cached(now), isNull);
    expect(source.label, contains('已保存城市 Saved city'));
    source.close();
  });

  test('network-only forecast remains visible then expires after six hours',
      () async {
    var now = DateTime(2026, 10, 25, 12);
    final source = _Source(null);
    final applied = <PreviewWeather?>[];
    final live =
        PreviewLiveData(source: source, now: () => now, apply: applied.add);
    live.setActive(true);
    final day = WeatherDay(
      date: DateTime(2026, 10, 25),
      code: 63,
      lowC: 19,
      highC: 22.9,
      rainProbability: 40,
    );
    final fetched = PreviewWeather(day, now.toUtc());
    source.requests.single.complete(fetched);
    await Future<void>.delayed(Duration.zero);
    expect(applied, [fetched]);
    now = now.add(const Duration(seconds: 30));
    live.expireIfDue();
    expect(applied, [fetched]);
    now = now.add(const Duration(hours: 6));
    live.expireIfDue();
    expect(applied, [fetched, null]);
    live.close();
  });
}
