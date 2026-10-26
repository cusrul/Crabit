import 'dart:io';
import 'dart:convert';

import 'package:crabit_watch_app/core/preview/nanoui_runtime.dart';
import 'package:crabit_watch_app/core/preview/watch_preview_session.dart';
import 'package:crabit_watch_app/core/preview/preview_live_data.dart';
import 'package:crabit_watch_app/core/weather/weather_service.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

class _Runtime implements NanoUiRuntime {
  bool closed = false;
  int steps = 0;
  final buttons = <(int, int)>[];
  @override
  int get generation => 7;
  @override
  Uint8List copyFrame() => Uint8List(1024)..[0] = 0x80;
  @override
  void button(int key, int pressed, int milliseconds) =>
      buttons.add((key, pressed));
  @override
  void step(int milliseconds) => steps++;
  @override
  void dispose() => closed = true;
}

class _LiveRuntime extends _Runtime implements LiveNanoUiRuntime {
  int clocks = 0;
  final weather = <WeatherDay?>[];
  @override
  bool setLocalClock(DateTime localTime) {
    clocks++;
    return true;
  }

  @override
  bool setWeather(WeatherDay? day, DateTime? fetchedAt) {
    weather.add(day);
    return true;
  }
}

class _WeatherSource implements PreviewWeatherSource {
  _WeatherSource(this.value);
  final PreviewWeather value;
  int loads = 0;
  int closes = 0;
  @override
  String get label => '测试天气源';
  @override
  PreviewWeather? cached(DateTime now) => value;
  @override
  Future<PreviewWeather?> load(DateTime now) async {
    loads++;
    return value;
  }

  @override
  void close() => closes++;
}

class _Assets extends CachingAssetBundle {
  @override
  Future<ByteData> load(String key) async =>
      ByteData.sublistView(Uint8List.fromList('asset:$key'.codeUnits));
}

void main() {
  test('model-only session never creates a native runtime or synthetic frame',
      () async {
    final session = await WatchPreviewSession.open(
        simulate: false,
        assets: _Assets(),
        createRuntime: () => throw StateError('must not load native DLL'));
    final client = HttpClient();
    addTearDown(() async {
      client.close(force: true);
      await session.close();
    });
    final mode =
        await (await client.getUrl(session.uri.resolve('mode'))).close();
    expect(jsonDecode(await utf8.decodeStream(mode)), {'interactive': false});
    final frame =
        await (await client.getUrl(session.uri.resolve('frame'))).close();
    expect(frame.statusCode, 204);
    await frame.drain<void>();
    session.button(2, 1);
    session.pause();
    session.resume();
  });

  test('paused session stops ticks and resumes the same runtime', () async {
    final runtime = _Runtime();
    final session = await WatchPreviewSession.open(
        createRuntime: () => runtime, assets: _Assets());
    addTearDown(session.close);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(runtime.steps, greaterThan(0));
    session.pause();
    final stoppedAt = runtime.steps;
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(runtime.steps, stoppedAt);
    session.resume();
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(runtime.steps, greaterThan(stoppedAt));
    expect(runtime.closed, isFalse);
  });

  test('live clock pauses/resumes and weather source is session scoped',
      () async {
    final runtime = _LiveRuntime();
    final source = _WeatherSource(PreviewWeather(
      WeatherDay(
        date: DateTime(2026, 10, 25),
        code: 63,
        lowC: 19,
        highC: 22.9,
        rainProbability: 50,
      ),
      DateTime.utc(2026, 10, 25),
    ));
    final session = await WatchPreviewSession.open(
      createRuntime: () => runtime,
      weatherSource: source,
      assets: _Assets(),
    );
    await Future<void>.delayed(const Duration(milliseconds: 40));
    expect(runtime.clocks, 1);
    expect(source.loads, 1);
    expect(runtime.weather, hasLength(1));
    session.pause();
    await Future<void>.delayed(const Duration(milliseconds: 1050));
    expect(runtime.clocks, 1);
    expect(source.loads, 1);
    session.resume();
    expect(runtime.clocks, 2);
    await session.close();
    expect(source.closes, 1);
  });
  test('serves only session assets and exact packed frames', () async {
    final runtime = _Runtime();
    final session = await WatchPreviewSession.open(
        createRuntime: () => runtime, assets: _Assets());
    final client = HttpClient();
    addTearDown(() async {
      client.close(force: true);
      await session.close();
    });
    Future<HttpClientResponse> get(Uri uri) async =>
        (await client.getUrl(uri)).close();

    final index = await get(session.uri);
    expect(index.statusCode, 200);
    expect(index.headers.contentType?.mimeType, 'text/html');
    await index.drain<void>();
    final poseModule = await get(session.uri.resolve('pose_transform.js'));
    expect(poseModule.statusCode, 200);
    expect(poseModule.headers.contentType?.mimeType, 'text/javascript');
    expect(await utf8.decodeStream(poseModule),
        'asset:assets/watch_sim/pose_transform.js');
    final frame = await get(session.uri.resolve('frame'));
    expect(frame.headers.value('X-Frame-Sequence'), '7');
    final bytes = await frame.fold<List<int>>([], (a, b) => a..addAll(b));
    expect(bytes.length, 1024);
    expect(bytes.first, 128);
    final unchanged = await get(session.uri.resolve('frame?after=7'));
    expect(unchanged.statusCode, 204);
    await unchanged.drain<void>();
    for (final uri in [
      session.uri.replace(path: '/frame'),
      session.uri.resolve('simulator.cpp'),
      session.uri.resolve('../pubspec.yaml')
    ]) {
      final response = await get(uri);
      expect(response.statusCode, 404);
      await response.drain<void>();
    }
    expect(session.permits(session.uri), isTrue);
    expect(session.permits(Uri.parse('https://example.com')), isFalse);
    expect(session.permits(session.uri.resolve('frame')), isFalse);
  });

  test(
      'pause cancels held keys, filters duplicate edges and close is idempotent',
      () async {
    final runtime = _Runtime();
    final session = await WatchPreviewSession.open(
        createRuntime: () => runtime, assets: _Assets());
    session.button(2, 1);
    session.button(2, 1);
    session.button(3, 1);
    session.button(4, 1);
    session.button(9, 1);
    session.pause();
    session.button(2, 0);
    expect(runtime.buttons, [(2, 1), (2, -1)]);
    session.resume();
    session.button(0, 1);
    session.button(0, 0);
    await session.close();
    await session.close();
    session.button(2, 1);
    expect(runtime.closed, isTrue);
    expect(runtime.buttons, [(2, 1), (2, -1), (0, 1), (0, 0)]);
  });
}
