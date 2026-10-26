import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:flutter/services.dart';

import 'nanoui_runtime.dart';
import 'preview_live_data.dart';

/// Owns one headless NanoUI instance and its route-local, loopback asset server.
/// No firmware, BLE, filesystem serving or externally accessible socket.
class WatchPreviewSession {
  WatchPreviewSession._(this._runtime, this._server, this._assets, this._token,
      this._now, this._weatherSource) {
    if (_runtime is LiveNanoUiRuntime && _weatherSource != null) {
      _liveData = PreviewLiveData(
        source: _weatherSource,
        now: _now,
        apply: _applyWeather,
      );
    }
    _subscription = _server.listen(_serve);
    resume();
  }

  static Future<WatchPreviewSession> open({
    NanoUiRuntime Function()? createRuntime,
    AssetBundle? assets,
    bool simulate = true,
    PreviewWeatherSource? weatherSource,
    DateTime Function()? now,
  }) async {
    final runtime =
        simulate ? (createRuntime ?? NativeNanoUiRuntime.open)() : null;
    try {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final random = Random.secure();
      final token = List.generate(
              16, (_) => random.nextInt(256).toRadixString(16).padLeft(2, '0'))
          .join();
      return WatchPreviewSession._(
          runtime,
          server,
          assets ?? rootBundle,
          token,
          now ?? DateTime.now,
          weatherSource ??
              (runtime is LiveNanoUiRuntime
                  ? HostPreviewWeatherSource()
                  : null));
    } catch (_) {
      runtime?.dispose();
      rethrow;
    }
  }

  final NanoUiRuntime? _runtime;
  final HttpServer _server;
  final AssetBundle _assets;
  final String _token;
  final DateTime Function() _now;
  final PreviewWeatherSource? _weatherSource;
  PreviewLiveData? _liveData;
  (int, int, int, int, int, int)? _lastWeather;
  bool _weatherInitialized = false;
  final Stopwatch _clock = Stopwatch()..start();
  late final StreamSubscription<HttpRequest> _subscription;
  Timer? _timer;
  Timer? _clockTimer;
  bool _closed = false;
  bool _paused = true;
  final Set<int> _held = {};

  Uri get uri =>
      Uri.parse('http://127.0.0.1:${_server.port}/$_token/index.html');

  bool permits(Uri? target) =>
      target != null &&
      target.scheme == uri.scheme &&
      target.host == uri.host &&
      target.port == uri.port &&
      target.path == uri.path;

  String? get weatherSourceLabel => _weatherSource?.label;

  void button(int key, int pressed) {
    if (_closed ||
        _runtime == null ||
        _paused ||
        key < 0 ||
        key > 2 ||
        !const [-1, 0, 1].contains(pressed)) {
      return;
    }
    if (pressed == 1) {
      if (!_held.add(key)) return;
    } else if (!_held.remove(key)) {
      return;
    }
    _runtime.button(key, pressed, _clock.elapsedMilliseconds);
  }

  void pause() {
    if (_closed || _paused) return;
    for (final key in _held.toList()) {
      _runtime?.button(key, -1, _clock.elapsedMilliseconds);
    }
    _held.clear();
    _paused = true;
    _timer?.cancel();
    _timer = null;
    _clockTimer?.cancel();
    _clockTimer = null;
    _liveData?.setActive(false);
  }

  void resume() {
    if (_closed || !_paused) return;
    _paused = false;
    if (_runtime != null) {
      _updateClock();
      _clockTimer = Timer.periodic(const Duration(seconds: 1), (_) {
        _updateClock();
      });
      _liveData?.setActive(true);
      _timer = Timer.periodic(const Duration(milliseconds: 16), (_) {
        _liveData?.refreshIfDue();
        _runtime.step(_clock.elapsedMilliseconds);
      });
    }
  }

  void _updateClock() {
    final runtime = _runtime;
    if (runtime is NanoUiRuntime &&
        runtime is LiveNanoUiRuntime &&
        !_paused &&
        !_closed) {
      (runtime as LiveNanoUiRuntime).setLocalClock(_now());
    }
  }

  void _applyWeather(PreviewWeather? value) {
    final runtime = _runtime;
    if (runtime is! NanoUiRuntime || runtime is! LiveNanoUiRuntime) return;
    final day = value?.day;
    final fetchedAt = value?.fetchedAt;
    final key = day == null || fetchedAt == null
        ? null
        : (
            day.date.year * 10000 + day.date.month * 100 + day.date.day,
            day.code,
            day.lowTenths,
            day.highTenths,
            day.rainProbability,
            fetchedAt.toUtc().millisecondsSinceEpoch ~/ 1000,
          );
    if (_weatherInitialized && key == _lastWeather) return;
    if (!(runtime as LiveNanoUiRuntime).setWeather(day, fetchedAt)) return;
    _weatherInitialized = true;
    _lastWeather = key;
  }

  static const _routes = <String, String>{
    'index.html': 'assets/watch_sim/index.html',
    'viewer.js': 'assets/watch_sim/viewer.js',
    'pose_transform.js': 'assets/watch_sim/pose_transform.js',
    'model_bindings.js': 'assets/watch_sim/model_bindings.js',
    'picking.js': 'assets/watch_sim/picking.js',
    'model.glb': 'assets/crabit_collection.glb',
    'vendor/three.module.js': 'assets/watch_sim/vendor/three.module.js',
    'vendor/three.core.js': 'assets/watch_sim/vendor/three.core.js',
    'vendor/loaders/GLTFLoader.js':
        'assets/watch_sim/vendor/loaders/GLTFLoader.js',
    'vendor/controls/OrbitControls.js':
        'assets/watch_sim/vendor/controls/OrbitControls.js',
    'vendor/utils/BufferGeometryUtils.js':
        'assets/watch_sim/vendor/utils/BufferGeometryUtils.js',
  };

  Future<void> _serve(HttpRequest request) async {
    final response = request.response;
    try {
      response.headers.set(HttpHeaders.cacheControlHeader, 'no-store');
      response.headers.set('X-Content-Type-Options', 'nosniff');
      if (_closed ||
          request.method != 'GET' ||
          !request.uri.path.startsWith('/$_token/')) {
        response.statusCode = HttpStatus.notFound;
      } else {
        final path = request.uri.path.substring(_token.length + 2);
        if (path == 'mode') {
          response.headers.contentType = ContentType.json;
          response.write('{"interactive":${_runtime != null}}');
        } else if (path == 'frame' && _runtime == null) {
          response.statusCode = HttpStatus.noContent;
        } else if (path == 'frame') {
          final generation = _runtime!.generation;
          response.headers.set('X-Frame-Sequence', '$generation');
          if (request.uri.queryParameters['after'] == '$generation') {
            response.statusCode = HttpStatus.noContent;
          } else {
            final frame = _runtime.copyFrame();
            response.headers.contentType = ContentType.binary;
            response.contentLength = frame.length;
            response.add(frame);
          }
        } else if (_routes[path] case final String asset) {
          final data = await _assets.load(asset);
          if (_closed) return;
          final bytes = Uint8List.sublistView(data);
          response.headers.contentType = path.endsWith('.js')
              ? ContentType('text', 'javascript', charset: 'utf-8')
              : path.endsWith('.html')
                  ? ContentType.html
                  : ContentType.binary;
          response.contentLength = bytes.length;
          response.add(bytes);
        } else {
          response.statusCode = HttpStatus.notFound;
        }
      }
      await response.close();
    } catch (_) {
      if (!_closed) {
        try {
          response.statusCode = HttpStatus.internalServerError;
          await response.close();
        } catch (_) {
          // The WebView can disconnect while an asset is being delivered.
        }
      }
    }
  }

  Future<void> close() async {
    if (_closed) return;
    pause();
    _closed = true;
    _clock.stop();
    _runtime?.dispose();
    _liveData?.close();
    if (_liveData == null) _weatherSource?.close();
    await _server.close(force: true);
    await _subscription.cancel();
  }
}
