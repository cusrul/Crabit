import 'dart:async';
import 'dart:math';
import 'package:flutter/foundation.dart';
import '../managers/storage_manager.dart';
import 'weather_service.dart';

abstract interface class WeatherStore {
  WeatherLocation? get location;
  WeatherLocation? get cachedLocation;
  WeatherForecast? get forecast;
  DateTime? get fetchedAt;
  Future<void> saveLocation(WeatherLocation location);
  Future<void> saveForecast(
      WeatherForecast forecast, WeatherLocation location, DateTime fetchedAt);
}

class HiveWeatherStore implements WeatherStore {
  static const _locationKey = 'weather_location_v1',
      _cacheKey = 'weather_cache_v1';
  final StorageManager _storage;
  HiveWeatherStore(this._storage);
  @override
  WeatherLocation? get location {
    final value = _storage.getSetting(_locationKey);
    try {
      return value is Map ? WeatherLocation.fromJson(value) : null;
    } catch (_) {
      return null;
    }
  }

  @override
  WeatherForecast? get forecast {
    final value = _storage.getSetting(_cacheKey);
    try {
      return value is Map && value['forecast'] is Map
          ? WeatherForecast.fromJson(value['forecast'])
          : null;
    } catch (_) {
      return null;
    }
  }

  @override
  DateTime? get fetchedAt {
    final value = _storage.getSetting(_cacheKey);
    final millis = value is Map ? value['fetchedAt'] : null;
    return millis is int && millis >= 0 && millis <= 4102444799000
        ? DateTime.fromMillisecondsSinceEpoch(millis, isUtc: true)
        : null;
  }

  @override
  Future<void> saveLocation(WeatherLocation value) async => _storage
      .getBox(StorageManager.boxSettings)
      .put(_locationKey, value.toJson());
  @override
  WeatherLocation? get cachedLocation {
    final value = _storage.getSetting(_cacheKey);
    try {
      final raw = value is Map ? value['location'] : null;
      return raw is Map ? WeatherLocation.fromJson(raw) : null;
    } catch (_) {
      return null;
    }
  }

  @override
  Future<void> saveForecast(
      WeatherForecast value, WeatherLocation location, DateTime time) async {
    await _storage.getBox(StorageManager.boxSettings).put(_cacheKey, {
      'forecast': value.toJson(),
      'location': location.toJson(),
      'fetchedAt': time.toUtc().millisecondsSinceEpoch,
    });
  }
}

abstract interface class WeatherWatchGateway implements Listenable {
  bool get ready;
  Future<String> request(String topic, String value);
}

class WeatherUploadException implements Exception {
  const WeatherUploadException(this.message);
  final String message;
  @override
  String toString() => message;
}

class WeatherController extends ChangeNotifier {
  WeatherController({
    required this.service,
    required this.store,
    required this.watch,
    DateTime Function()? now,
    int Function()? localOffsetMinutes,
    int Function()? nextToken,
    bool autoSyncEnabled = true,
  })  : _now = now ?? DateTime.now,
        _localOffsetMinutes = localOffsetMinutes ??
            (() => DateTime.now().timeZoneOffset.inMinutes),
        _autoSyncEnabled = autoSyncEnabled,
        _nextToken = nextToken ?? _randomToken {
    location = store.location;
    forecast = store.forecast;
    cachedLocation = store.cachedLocation;
    fetchedAt = store.fetchedAt;
    _ready = watch.ready;
    watch.addListener(_linkChanged);
    if (_ready) _onReady();
  }
  final WeatherService service;
  final WeatherStore store;
  final WeatherWatchGateway watch;
  final DateTime Function() _now;
  final int Function() _localOffsetMinutes;
  final int Function() _nextToken;
  final bool _autoSyncEnabled;
  bool _ready = false,
      _disposed = false,
      _busy = false,
      _timeSyncing = false,
      _searching = false,
      _autoAttempted = false,
      _operationActive = false,
      _savingLocation = false;
  int _epoch = 0;
  final Set<int> _usedTokens = {};
  int _searchGeneration = 0, _selectionGeneration = 0;
  Future<void> _selectionTail = Future<void>.value();
  bool get ready => _ready;
  bool get busy => _busy || _savingLocation || (_ready && _operationActive);
  bool get timeSyncing => _timeSyncing;
  bool get searching => _searching;
  WeatherLocation? location;
  WeatherForecast? forecast;
  WeatherLocation? cachedLocation;
  DateTime? fetchedAt;
  String message = '选择城市以查看天气预报。';
  String timeMessage = '尚未同步手表时间。';
  DateTime? lastTimeSync;
  List<WeatherLocation> results = const [];
  bool get cacheFresh =>
      fetchedAt != null &&
      _now().toUtc().difference(fetchedAt!) < const Duration(minutes: 30) &&
      !_now().isBefore(fetchedAt!);
  bool get hasCurrentForecast =>
      location != null &&
      forecast != null &&
      cachedLocation?.latitude == location!.latitude &&
      cachedLocation?.longitude == location!.longitude;

  void _linkChanged() {
    if (_disposed || _ready == watch.ready) return;
    _ready = watch.ready;
    if (!_ready) _autoAttempted = false;
    ++_epoch;
    _busy = false;
    message = _ready ? '设备已就绪。' : '设备离线；天气可查看缓存，手表同步不可用。';
    if (!_ready && _timeSyncing) {
      _timeSyncing = false;
      timeMessage = '连接已断开；本次校时未获确认。';
    }
    notifyListeners();
    if (_ready) _onReady();
  }

  void _onReady() {
    // Let an old connection's pending future settle before using the new one.
    if (_autoAttempted || _operationActive) return;
    _autoAttempted = true;
    if (_autoSyncEnabled) {
      if (location != null) {
        unawaited(_refresh(sync: true, automatic: true));
      } else {
        unawaited(_syncTime(automatic: true));
      }
    }
  }

  Future<void> search(String query) async {
    if (_disposed) return;
    final generation = ++_searchGeneration;
    _searching = true;
    message = '正在搜索城市…';
    notifyListeners();
    try {
      final found = await service.search(query);
      if (_disposed || generation != _searchGeneration) return;
      results = found;
      message = found.isEmpty ? '没有找到城市，请换个关键词。' : '选择一个城市保存位置。';
    } catch (_) {
      if (_disposed || generation != _searchGeneration) return;
      message = '城市搜索失败，请检查网络后重试。';
    } finally {
      if (!_disposed && generation == _searchGeneration) {
        _searching = false;
        notifyListeners();
      }
    }
  }

  Future<void> select(WeatherLocation value) async {
    if (_busy || _disposed) return;
    final generation = ++_selectionGeneration;
    _savingLocation = true;
    message = '正在保存位置…';
    notifyListeners();
    try {
      final write = _selectionTail
          .catchError((_) {})
          .then((_) => store.saveLocation(value));
      _selectionTail = write;
      await write;
      if (_disposed || generation != _selectionGeneration) return;
      location = value;
      results = const [];
      message = '已保存 ${value.name}。';
    } catch (_) {
      if (_disposed || generation != _selectionGeneration) return;
      message = '位置保存失败；已保留之前的位置，请重试。';
    } finally {
      if (!_disposed && generation == _selectionGeneration) {
        _savingLocation = false;
        notifyListeners();
      }
    }
  }

  Future<void> refresh() => _refresh(sync: false);
  Future<void> sync() => _refresh(sync: true);
  Future<void> syncTime() => _syncTime();

  Future<void> _syncTime({bool automatic = false}) async {
    if (_disposed || !_ready || _operationActive || _savingLocation) return;
    final epoch = _epoch;
    final sentAt = _now();
    bool current() => !_disposed && _ready && epoch == _epoch;
    _busy = true;
    _timeSyncing = true;
    _operationActive = true;
    timeMessage = automatic ? '设备已就绪，正在同步手表时间…' : '正在同步手表时间…';
    notifyListeners();
    try {
      if (!current()) return;
      final value = 'clock=${sentAt.toUtc().millisecondsSinceEpoch ~/ 1000},'
          '${_localOffsetMinutes()}';
      final reply = await watch.request('app/weather', value);
      if (!current()) return;
      if (reply == 'DONE') {
        lastTimeSync = _now().toLocal();
        timeMessage = '手表已确认校时完成。';
      } else if (reply == 'RTC_FAILED') {
        timeMessage = '手表 RTC 写入失败。';
      } else {
        timeMessage = '手表未确认校时完成，请检查连接后重试。';
      }
    } catch (_) {
      if (current()) {
        timeMessage = '未取得手表校时完成确认；请检查连接后刷新状态。';
      }
    } finally {
      _operationActive = false;
      if (!_disposed) {
        _busy = false;
        _timeSyncing = false;
        notifyListeners();
        if (_ready && !_autoAttempted) _onReady();
      }
    }
  }

  Future<void> _refresh({required bool sync, bool automatic = false}) async {
    final selected = location;
    if (_disposed ||
        _operationActive ||
        selected == null ||
        (sync && !_ready)) {
      return;
    }
    final epoch = _epoch;
    bool current() => !_disposed && (!sync || (_ready && epoch == _epoch));
    _busy = true;
    _operationActive = true;
    message = automatic
        ? '设备刚就绪，正在更新天气…'
        : sync
            ? '正在获取并同步天气…'
            : '正在获取天气…';
    notifyListeners();
    var token = 0;
    var begun = false;
    try {
      final reuseCache = automatic && cacheFresh && hasCurrentForecast;
      final fresh = reuseCache ? forecast! : await service.forecast(selected);
      if (!current()) return;
      if (sync) {
        token = _uniqueToken();
        final begin =
            '$token,${(selected.latitude * 1000000).round()},${(selected.longitude * 1000000).round()},${fresh.offsetMinutes},${fresh.days.length}';
        _expect(
          await watch.request('app/weather', 'begin=$begin'),
          'OK',
          current,
        );
        begun = true;
        for (var i = 0; i < fresh.days.length; i++) {
          final day = fresh.days[i];
          final date =
              '${day.date.year.toString().padLeft(4, '0')}${day.date.month.toString().padLeft(2, '0')}${day.date.day.toString().padLeft(2, '0')}';
          _expect(
            await watch.request(
              'app/weather',
              'day=$token,$i,$date,${day.code},${day.lowTenths},${day.highTenths},${day.rainProbability}',
            ),
            'OK',
            current,
          );
        }
        final clockSentAt = _now();
        _timeSyncing = true;
        timeMessage = '正在随天气同步手表时间…';
        notifyListeners();
        if (!current()) return;
        String clockReply;
        try {
          final utc = clockSentAt.toUtc().millisecondsSinceEpoch ~/ 1000;
          clockReply = await watch.request('app/weather', 'time=$token,$utc');
        } catch (_) {
          if (current()) {
            _timeSyncing = false;
            timeMessage = '未取得手表校时完成确认；请检查连接后刷新状态。';
            notifyListeners();
          }
          rethrow;
        }
        if (!current()) return;
        _timeSyncing = false;
        if (clockReply == 'RTC_FAILED') {
          timeMessage = '手表 RTC 写入失败。';
          notifyListeners();
          throw const WeatherUploadException('天气未同步：手表 RTC 写入失败。');
        }
        if (clockReply != 'OK') {
          timeMessage = '手表未确认校时完成，请检查连接后重试。';
          notifyListeners();
        }
        _expect(clockReply, 'OK', current);
        lastTimeSync = _now().toLocal();
        timeMessage = '手表已确认校时完成。';
        notifyListeners();
        if (!current()) return;
        _expect(
          await watch.request('app/weather', 'commit=$token'),
          'DONE',
          current,
        );
        begun = false;
      }
      if (!current()) return;
      final time = reuseCache ? fetchedAt! : _now().toUtc();
      if (!reuseCache) await store.saveForecast(fresh, selected, time);
      if (!current()) return;
      forecast = fresh;
      cachedLocation = selected;
      fetchedAt = time;
      message = sync ? '天气已同步到手表。' : '天气已更新。';
    } catch (error) {
      if (!current()) return;
      message = error is WeatherUploadException
          ? error.message
          : sync
              ? '天气同步失败；保留上次完整缓存。'
              : '天气获取失败；保留上次完整缓存。';
      if (begun && token != 0 && _ready && epoch == _epoch) {
        try {
          await watch.request('app/weather', 'cancel=$token');
        } catch (_) {
          /* Preserve the original failure. */
        }
      }
    } finally {
      _operationActive = false;
      if (!_disposed) {
        _busy = false;
        _timeSyncing = false;
        notifyListeners();
        if (_ready && !_autoAttempted) _onReady();
      }
    }
  }

  int _uniqueToken() {
    var token = _nextToken();
    if (token < 1 || token > 2147483647) {
      throw const WeatherUploadException('无法生成有效同步编号。');
    }
    while (_usedTokens.contains(token)) {
      token = token == 2147483647 ? 1 : token + 1;
    }
    _usedTokens.add(token);
    return token;
  }

  void _expect(String reply, String expected, bool Function() current) {
    if (!current()) {
      throw const WeatherUploadException('设备连接已变化，已停止更新。');
    }
    if (reply != expected) {
      throw WeatherUploadException(
        reply == 'RTC_FAILED' ? '手表 RTC 写入失败。' : '设备返回非预期结果。',
      );
    }
  }

  @override
  void dispose() {
    _disposed = true;
    ++_epoch;
    _timeSyncing = false;
    ++_searchGeneration;
    ++_selectionGeneration;
    watch.removeListener(_linkChanged);
    super.dispose();
  }
}

int _randomToken() => Random.secure().nextInt(2147483647) + 1;
