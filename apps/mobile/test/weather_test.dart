import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/core/weather/weather_controller.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';
import 'package:crabit_watch_app/core/weather/weather_service.dart';
import 'package:crabit_watch_app/presentation/pages/weather/weather_page.dart';

Map<String, dynamic> forecastJson({int count = 3}) => {
      'timezone': 'Asia/Shanghai',
      'utc_offset_seconds': 28800,
      'daily': {
        'time': List.generate(count, (i) => '2026-10-${20 + i}'),
        'weather_code': List<int>.filled(count, 2),
        'temperature_2m_min': List<double>.filled(count, 8.1),
        'temperature_2m_max': List<double>.filled(count, 17.4),
        'precipitation_probability_max': List<int>.filled(count, 20),
      },
    };

class _Service implements WeatherService {
  Object? failure;
  int fetches = 0;
  @override
  void close() {}
  @override
  Future<List<WeatherLocation>> search(String query) async => [
        const WeatherLocation(name: 'Test City', latitude: 31, longitude: 121),
      ];
  @override
  Future<WeatherForecast> forecast(
    WeatherLocation location, {
    int days = 3,
  }) async {
    fetches++;
    if (failure case final error?) throw error;
    return WeatherForecast.parse(
      forecastJson(count: days),
      requestedDays: days,
    );
  }
}

class _Store implements WeatherStore {
  @override
  WeatherLocation? location = const WeatherLocation(
    name: 'Test City',
    latitude: 31,
    longitude: 121,
  );
  @override
  WeatherLocation? cachedLocation;
  @override
  WeatherForecast? forecast;
  @override
  DateTime? fetchedAt;
  int writes = 0;
  Completer<void>? pendingSave;
  bool failSave = false;
  @override
  Future<void> saveLocation(WeatherLocation value) async {
    if (pendingSave case final pending?) await pending.future;
    if (failSave) throw StateError('disk full');
    location = value;
  }

  @override
  Future<void> saveForecast(
      WeatherForecast value, WeatherLocation place, DateTime time) async {
    writes++;
    forecast = value;
    cachedLocation = place;
    fetchedAt = time;
  }
}

class _Watch extends ChangeNotifier implements WeatherWatchGateway {
  @override
  bool ready = false;
  final calls = <String>[];
  String? failReply;
  String? Function(String value)? reply;
  Completer<String>? pending;
  @override
  Future<String> request(String topic, String value) {
    calls.add('$topic|$value');
    if (reply case final respond?) return Future.value(respond(value));
    if (pending case final completer?) return completer.future;
    if (failReply != null && value.startsWith('day=')) {
      return Future.value(failReply!);
    }
    return Future.value(value.startsWith('commit=') ? 'DONE' : 'OK');
  }

  void setReady(bool value) {
    ready = value;
    notifyListeners();
  }
}

void main() {
  test('strictly parses firmware-compatible dates, ranges, and timezone', () {
    final parsed = WeatherForecast.parse(forecastJson(), requestedDays: 3);
    expect(parsed.offsetMinutes, 480);
    expect(parsed.days.first.lowTenths, 81);
    expect(parsed.days.first.date, DateTime.utc(2026, 10, 20));
    final bad = forecastJson()
      ..['daily']['time'] = ['2026-10-20', '2026-10-22', '2026-10-23'];
    expect(
      () => WeatherForecast.parse(bad, requestedDays: 3),
      throwsFormatException,
    );
    final invalid = forecastJson()
      ..['daily']['precipitation_probability_max'] = [20, 101, 30];
    expect(
      () => WeatherForecast.parse(invalid, requestedDays: 3),
      throwsFormatException,
    );
    final invalidDate = forecastJson()
      ..['daily']['time'] = ['2026-02-30', '2026-03-01', '2026-03-02'];
    expect(
      () => WeatherForecast.parse(invalidDate, requestedDays: 3),
      throwsFormatException,
    );
    final malformed = forecastJson()..['daily']['weather_code'] = [2, 'bad', 3];
    expect(() => WeatherForecast.parse(malformed, requestedDays: 3),
        throwsFormatException);
  });

  test(
    'weather upload is correlated and ordered; cache commits after DONE',
    () async {
      final service = _Service(), store = _Store(), watch = _Watch();
      store.fetchedAt = DateTime.utc(2026, 10, 20, 3);
      watch.ready = true;
      final controller = WeatherController(
        service: service,
        store: store,
        watch: watch,
        now: () => DateTime.utc(2026, 10, 20, 3),
        nextToken: () => 42,
        autoSyncEnabled: false,
      );
      await controller.sync();
      expect(watch.calls, [
        'app/weather|begin=42,31000000,121000000,480,3',
        'app/weather|day=42,0,20261020,2,81,174,20',
        'app/weather|day=42,1,20261021,2,81,174,20',
        'app/weather|day=42,2,20261022,2,81,174,20',
        'app/weather|time=42,1792465200',
        'app/weather|commit=42',
      ]);
      expect(store.writes, 1);
      expect(controller.forecast!.days.length, 3);
      controller.dispose();
    },
  );

  test('standalone clock sync needs no city or forecast and accepts only DONE',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    store.location = null;
    watch.ready = true;
    watch.reply = (_) => 'OK';
    final sentAt = DateTime.utc(2026, 10, 20, 3);
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
      now: () => sentAt,
      localOffsetMinutes: () => 480,
      autoSyncEnabled: false,
    );

    await controller.syncTime();

    expect(watch.calls, ['app/weather|clock=1792465200,480']);
    expect(service.fetches, 0);
    expect(store.writes, 0);
    expect(controller.lastTimeSync, isNull);
    expect(controller.timeMessage, contains('未确认'));

    watch.reply = (_) => 'RTC_FAILED';
    await controller.syncTime();
    expect(controller.timeMessage, contains('RTC'));
    expect(controller.lastTimeSync, isNull);

    watch.reply = (_) => 'DONE';
    await controller.syncTime();
    expect(controller.lastTimeSync, sentAt.toLocal());
    expect(controller.timeMessage, '手表已确认校时完成。');
    expect(service.fetches, 0);
    expect(store.writes, 0);
    controller.dispose();
  });

  test('standalone clock timeout preserves the last confirmed time', () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    watch.ready = true;
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
      now: () => DateTime.utc(2026, 10, 20, 3),
      localOffsetMinutes: () => -420,
      autoSyncEnabled: false,
    );
    final pending = Completer<String>();
    watch.pending = pending;

    final sync = controller.syncTime();
    await Future<void>.delayed(Duration.zero);
    expect(controller.timeSyncing, isTrue);
    pending.completeError(TimeoutException('request timeout'));
    await sync;

    expect(watch.calls, ['app/weather|clock=1792465200,-420']);
    expect(controller.lastTimeSync, isNull);
    expect(controller.timeMessage, contains('未取得'));
    expect(controller.busy, isFalse);
    controller.dispose();
  });

  test('late standalone clock reply after disconnect cannot publish success',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    watch.ready = true;
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
      autoSyncEnabled: false,
    );
    final pending = Completer<String>();
    watch.pending = pending;

    final sync = controller.syncTime();
    await Future<void>.delayed(Duration.zero);
    watch.setReady(false);
    pending.complete('DONE');
    await sync;

    expect(controller.lastTimeSync, isNull);
    expect(controller.timeMessage, contains('断开'));
    expect(controller.timeSyncing, isFalse);
    controller.dispose();
  });

  test('weather upload and standalone clock share one operation mutex',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    watch.ready = true;
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
      autoSyncEnabled: false,
      nextToken: () => 88,
    );

    final clockReply = Completer<String>();
    watch.pending = clockReply;
    final clock = controller.syncTime();
    await Future<void>.delayed(Duration.zero);
    await controller.sync();
    expect(watch.calls, hasLength(1));
    expect(watch.calls.single, startsWith('app/weather|clock='));
    expect(service.fetches, 0);
    watch.pending = null;
    clockReply.complete('DONE');
    await clock;

    final beginReply = Completer<String>();
    watch.pending = beginReply;
    final weather = controller.sync();
    await Future<void>.delayed(Duration.zero);
    expect(watch.calls.last, 'app/weather|begin=88,31000000,121000000,480,3');
    final callCount = watch.calls.length;
    await controller.syncTime();
    expect(watch.calls.length, callCount);
    watch.pending = null;
    beginReply.complete('OK');
    await weather;
    expect(watch.calls.last, 'app/weather|commit=88');
    expect(controller.lastTimeSync, isNotNull);
    controller.dispose();
  });

  test('time=OK remains confirmed when later weather commit fails', () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    watch.ready = true;
    watch.reply = (value) => value.startsWith('commit=') ? 'ERR:commit' : 'OK';
    final sentAt = DateTime.utc(2026, 10, 20, 3);
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
      now: () => sentAt,
      nextToken: () => 55,
      autoSyncEnabled: false,
    );

    await controller.sync();

    expect(controller.lastTimeSync, sentAt.toLocal());
    expect(controller.timeMessage, '手表已确认校时完成。');
    expect(controller.message, '设备返回非预期结果。');
    expect(watch.calls, contains('app/weather|commit=55'));
    expect(watch.calls.last, 'app/weather|cancel=55');
    expect(store.writes, 0);
    controller.dispose();
  });

  test('ready session without a city auto-syncs only when enabled', () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    store.location = null;
    watch.ready = true;
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
      autoSyncEnabled: false,
    );
    expect(watch.calls, isEmpty);
    controller.dispose();

    final autoWatch = _Watch()..ready = true;
    autoWatch.reply = (_) => 'DONE';
    final autoController = WeatherController(
      service: service,
      store: store,
      watch: autoWatch,
    );
    await Future<void>.delayed(Duration.zero);
    expect(autoWatch.calls, hasLength(1));
    expect(autoWatch.calls.single, startsWith('app/weather|clock='));
    expect(service.fetches, 0);
    autoController.dispose();
  });

  test('reconnected no-city auto clock waits for the old request to settle',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    store.location = null;
    watch.ready = true;
    final pending = Completer<String>();
    watch.pending = pending;
    final controller = WeatherController(
      service: service,
      store: store,
      watch: watch,
    );
    expect(watch.calls, hasLength(1));

    watch.setReady(false);
    watch.setReady(true);
    expect(watch.calls, hasLength(1));

    watch.pending = null;
    watch.reply = (_) => 'DONE';
    pending.complete('DONE');
    await Future<void>.delayed(Duration.zero);
    await Future<void>.delayed(Duration.zero);

    expect(watch.calls, hasLength(2));
    expect(watch.calls.last, startsWith('app/weather|clock='));
    expect(controller.lastTimeSync, isNotNull);
    expect(service.fetches, 0);
    controller.dispose();
  });

  test(
    'fetch/upload failures preserve last complete cache; RTC_FAILED is surfaced',
    () async {
      final service = _Service(), store = _Store(), watch = _Watch();
      final cached = WeatherForecast.parse(forecastJson(), requestedDays: 3);
      store.forecast = cached;
      store.cachedLocation = store.location;
      store.fetchedAt = DateTime.utc(2026, 10, 19);
      watch.ready = true;
      service.failure = StateError('offline');
      final controller = WeatherController(
        service: service,
        store: store,
        watch: watch,
        now: () => DateTime.utc(2026, 10, 20),
        nextToken: () => 7,
        autoSyncEnabled: false,
      );
      await controller.refresh();
      expect(store.writes, 0);
      expect(controller.forecast, same(cached));
      service.failure = null;
      watch.failReply = 'ERR:bad-day';
      await controller.sync();
      expect(store.writes, 0);
      expect(watch.calls.last, 'app/weather|cancel=7');
      watch.failReply = null;
      watch.reply = (value) => value.startsWith('time=') ? 'RTC_FAILED' : 'OK';
      await controller.sync();
      expect(controller.message, contains('RTC'));
      expect(store.writes, 0);
      controller.dispose();
    },
  );

  test(
    'disconnect during request prevents stale cache publish and auto-sync is once per ready session',
    () async {
      final service = _Service(), store = _Store(), watch = _Watch();
      final controller = WeatherController(
        service: service,
        store: store,
        watch: watch,
        now: () => DateTime.utc(2026, 10, 20),
        nextToken: () => 1,
      );
      final pending = Completer<String>();
      watch.pending = pending;
      watch.setReady(true);
      await Future<void>.delayed(Duration.zero);
      watch.setReady(false);
      pending.complete('OK');
      await Future<void>.delayed(const Duration(milliseconds: 10));
      expect(store.writes, 0);
      expect(controller.busy, false);
      controller.dispose();
    },
  );

  test(
      'new ready connection uploads fresh location-matched cache without refetching',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    final cached = WeatherForecast.parse(forecastJson(), requestedDays: 3);
    final city = store.location!;
    store.forecast = cached;
    store.cachedLocation = city;
    store.fetchedAt = DateTime.utc(2026, 10, 20, 2, 45);
    watch.ready = true;
    final controller = WeatherController(
        service: service,
        store: store,
        watch: watch,
        now: () => DateTime.utc(2026, 10, 20, 3),
        nextToken: () => 9);
    await Future<void>.delayed(const Duration(milliseconds: 10));
    expect(service.fetches, 0);
    expect(watch.calls.first, 'app/weather|begin=9,31000000,121000000,480,3');
    expect(watch.calls[4], startsWith('app/weather|time=9,'));
    expect(watch.calls.last, 'app/weather|commit=9');
    expect(store.writes, 0);
    expect(controller.fetchedAt, DateTime.utc(2026, 10, 20, 2, 45));
    controller.dispose();
  });

  test(
      'changing city never relabels the prior cached forecast; save failures are visible',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    store.forecast = WeatherForecast.parse(forecastJson(), requestedDays: 3);
    store.cachedLocation = store.location;
    final controller =
        WeatherController(service: service, store: store, watch: watch);
    final oldForecast = controller.forecast;
    const next =
        WeatherLocation(name: 'Other City', latitude: 29.5, longitude: 106.5);
    await controller.select(next);
    expect(controller.location, next);
    expect(controller.forecast, same(oldForecast));
    expect(controller.hasCurrentForecast, isFalse);
    store.failSave = true;
    await controller.select(
        const WeatherLocation(name: 'Fail City', latitude: 10, longitude: 20));
    expect(controller.location, next);
    expect(controller.message, contains('保存失败'));
    controller.dispose();
  });

  test(
      'late city search and location save cannot notify or mutate after dispose',
      () async {
    final service = _DelayedService(), store = _Store(), watch = _Watch();
    store.pendingSave = Completer<void>();
    final controller =
        WeatherController(service: service, store: store, watch: watch);
    var notifications = 0;
    controller.addListener(() => notifications++);
    final search = controller.search('Chongqing');
    final select = controller.select(
        const WeatherLocation(name: 'Later', latitude: 29.5, longitude: 106.5));
    final beforeDispose = notifications;
    controller.dispose();
    service.searchResult.complete(
        [const WeatherLocation(name: 'Stale', latitude: 1, longitude: 1)]);
    store.pendingSave!.complete();
    await Future.wait([search, select]);
    expect(notifications, beforeDispose);
    expect(controller.results, isEmpty);
  });

  test(
      'reconnection waits for the old upload before starting a new transaction',
      () async {
    final service = _Service(), store = _Store(), watch = _Watch();
    final oldReply = Completer<String>();
    watch.pending = oldReply;
    final controller = WeatherController(
        service: service, store: store, watch: watch, nextToken: () => 11);
    watch.setReady(true);
    await Future<void>.delayed(Duration.zero);
    expect(watch.calls.length, 1);
    watch.setReady(false);
    watch.setReady(true);
    expect(watch.calls.length, 1);
    watch.pending = null;
    oldReply.complete('OK');
    await Future<void>.delayed(const Duration(milliseconds: 10));
    expect(watch.calls[1], startsWith('app/weather|begin=12,'));
    expect(watch.calls.last, 'app/weather|commit=12');
    expect(store.writes, 1);
    expect(controller.busy, false);
    controller.dispose();
  });

  test('phone-only refresh still publishes when BLE readiness changes',
      () async {
    final service = _PendingForecastService(),
        store = _Store(),
        watch = _Watch();
    final controller = WeatherController(
        service: service, store: store, watch: watch, autoSyncEnabled: false);
    var notifications = 0;
    controller.addListener(() => notifications++);
    final pending = controller.refresh();
    watch.setReady(true);
    final afterReady = notifications;
    service.result
        .complete(WeatherForecast.parse(forecastJson(), requestedDays: 3));
    await pending;
    expect(notifications, greaterThan(afterReady));
    expect(controller.message, '天气已更新。');
    expect(controller.busy, false);
    expect(store.writes, 1);
    controller.dispose();
  });

  testWidgets(
      'weather controls reflect readiness and show Open-Meteo attribution',
      (tester) async {
    final output = Platform.environment['CRABIT_WEATHER_APP_SNAPSHOTS'];
    if (output != null) {
      final fontPath = Platform.environment['CRABIT_WEATHER_APP_FONT'];
      expect(fontPath, isNotNull);
      await tester.runAsync(() async {
        for (final family in ['WeatherRender', 'Roboto']) {
          await (FontLoader(family)
                ..addFont(
                    File(fontPath!).readAsBytes().then(ByteData.sublistView)))
              .load();
        }
        await (FontLoader('MaterialIcons')
              ..addFont(
                  File('build/unit_test_assets/fonts/MaterialIcons-Regular.otf')
                      .readAsBytes()
                      .then(ByteData.sublistView)))
            .load();
      });
    }
    final watch = _Watch();
    final controller = WeatherController(
        service: _Service(),
        store: _Store(),
        watch: watch,
        autoSyncEnabled: false);
    final boundary = GlobalKey();
    await tester.pumpWidget(MaterialApp(
        theme: output == null
            ? AppTheme.lightTheme
            : AppTheme.lightTheme.copyWith(
                textTheme: AppTheme.lightTheme.textTheme
                    .apply(fontFamily: 'WeatherRender')),
        home: RepaintBoundary(
            key: boundary, child: WeatherPage(controller: controller))));
    expect(find.text('时间与天气'), findsOneWidget);
    expect(find.byKey(const Key('weather-manual-location')), findsOneWidget);
    expect(
        tester
            .widget<FilledButton>(find.byKey(const Key('weather-sync-time')))
            .onPressed,
        isNull);

    final listView = find.byType(ListView).first;
    Future<void> revealWeatherControls() async {
      await tester.dragUntilVisible(
        find.byKey(const Key('weather-fetch')),
        listView,
        const Offset(0, -260),
      );
      await tester.pumpAndSettle();
    }

    await revealWeatherControls();
    expect(
        tester
            .widget<FilledButton>(find.byKey(const Key('weather-sync')))
            .onPressed,
        isNull);
    expect(
        tester
            .widget<OutlinedButton>(find.byKey(const Key('weather-fetch')))
            .onPressed,
        isNotNull);
    await tester.dragUntilVisible(
      find.textContaining('Open-Meteo'),
      listView,
      const Offset(0, -260),
    );
    await tester.pumpAndSettle();
    expect(find.textContaining('Open-Meteo'), findsOneWidget);
    if (output != null) {
      Future<void> capture(String name, Size size) async {
        tester.view.physicalSize = size;
        tester.view.devicePixelRatio = 1;
        await tester.pumpAndSettle();
        await tester.drag(listView, const Offset(0, 1200));
        await tester.pumpAndSettle();
        await revealWeatherControls();
        expect(tester.takeException(), isNull);
        final render = boundary.currentContext!.findRenderObject()!
            as RenderRepaintBoundary;
        await tester.runAsync(() async {
          final directory = Directory(output)..createSync(recursive: true);
          final frame = await render.toImage();
          final png = await frame.toByteData(format: ui.ImageByteFormat.png);
          await File('${directory.path}/$name.png')
              .writeAsBytes(png!.buffer.asUint8List());
          frame.dispose();
        });
      }

      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      await capture('simulated-offline-desktop', const Size(1100, 900));
      watch.setReady(true);
      await controller.sync();
      await capture('simulated-synced-desktop', const Size(1100, 900));
      await capture('simulated-synced-phone', const Size(360, 800));
    }
    await tester.pumpWidget(const SizedBox.shrink());
    controller.dispose();
  });
}

class _DelayedService extends _Service {
  final searchResult = Completer<List<WeatherLocation>>();
  @override
  Future<List<WeatherLocation>> search(String query) => searchResult.future;
}

class _PendingForecastService extends _Service {
  final result = Completer<WeatherForecast>();
  @override
  Future<WeatherForecast> forecast(WeatherLocation location, {int days = 3}) =>
      result.future;
}
