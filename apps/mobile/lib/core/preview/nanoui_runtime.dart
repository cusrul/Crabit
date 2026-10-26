import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import '../weather/weather_service.dart';

/// Host port boundary. Every method is called on the owning Dart isolate.
abstract interface class NanoUiRuntime {
  void step(int milliseconds);
  void button(int key, int pressed, int milliseconds);
  int get generation;
  Uint8List copyFrame();
  void dispose();
}

/// Optional simulator data ABI; kept separate so existing runtime fakes and
/// alternate ports do not need to implement host-only preview inputs.
abstract interface class LiveNanoUiRuntime {
  bool setLocalClock(DateTime localTime);
  bool setWeather(WeatherDay? day, DateTime? fetchedAt);
}

class NativeNanoUiRuntime implements NanoUiRuntime, LiveNanoUiRuntime {
  NativeNanoUiRuntime._(DynamicLibrary library) {
    _start = library
        .lookupFunction<Int32 Function(), int Function()>('crabit_sim_start');
    _stop = library
        .lookupFunction<Void Function(), void Function()>('crabit_sim_stop');
    _step = library.lookupFunction<Void Function(Uint32), void Function(int)>(
        'crabit_sim_step');
    _button = library.lookupFunction<Void Function(Int32, Int32, Uint32),
        void Function(int, int, int)>('crabit_sim_button');
    _frame = library.lookupFunction<Pointer<Uint8> Function(),
        Pointer<Uint8> Function()>('crabit_sim_frame');
    _generation = library.lookupFunction<Uint32 Function(), int Function()>(
        'crabit_sim_generation');
    _setClock =
        library.lookupFunction<Int32 Function(Uint32), int Function(int)>(
            'crabit_sim_set_clock');
    _setWeather = library.lookupFunction<
        Int32 Function(Uint32, Int32, Int32, Int32, Int32, Uint32),
        int Function(int, int, int, int, int, int)>('crabit_sim_set_weather');
    _showClock = library.lookupFunction<Int32 Function(), int Function()>(
        'crabit_sim_show_clock');
    if (_start() != 0) throw StateError('NanoUI 初始化失败');
    _active = true;
    if (_showClock() != 0) {
      _stop();
      _active = false;
      throw StateError('NanoUI 时钟预览启动失败');
    }
  }

  factory NativeNanoUiRuntime.open() {
    if (!Platform.isWindows) {
      throw UnsupportedError('这版 NanoUI 试用先支持 Windows');
    }
    if (_active) throw StateError('NanoUI 预览已经打开');
    final directory = File(Platform.resolvedExecutable).parent.path;
    return NativeNanoUiRuntime._(
        DynamicLibrary.open('$directory/crabit_nanoui_sim.dll'));
  }

  static bool _active = false;
  bool _disposed = false;
  late final int Function() _start;
  late final void Function() _stop;
  late final void Function(int) _step;
  late final void Function(int, int, int) _button;
  late final Pointer<Uint8> Function() _frame;
  late final int Function() _generation;
  late final int Function(int) _setClock;
  late final int Function(int, int, int, int, int, int) _setWeather;
  late final int Function() _showClock;

  @override
  bool setLocalClock(DateTime localTime) {
    if (_disposed) return false;
    final localEpoch = localTime.toUtc().millisecondsSinceEpoch ~/ 1000 +
        localTime.timeZoneOffset.inSeconds;
    return _setClock(localEpoch & 0xffffffff) == 0;
  }

  @override
  bool setWeather(WeatherDay? day, DateTime? fetchedAt) {
    if (_disposed) return false;
    if (day == null || fetchedAt == null) {
      return _setWeather(0, 0, 0, 0, 0, 0) == 0;
    }
    final date = day.date;
    final ymd = date.year * 10000 + date.month * 100 + date.day;
    return _setWeather(
            ymd,
            day.code,
            day.lowTenths,
            day.highTenths,
            day.rainProbability,
            fetchedAt.toUtc().millisecondsSinceEpoch ~/ 1000) ==
        0;
  }

  @override
  void step(int milliseconds) {
    if (!_disposed) _step(milliseconds);
  }

  @override
  void button(int key, int pressed, int milliseconds) {
    if (!_disposed) _button(key, pressed, milliseconds);
  }

  @override
  int get generation => _disposed ? 0 : _generation();

  @override
  Uint8List copyFrame() {
    if (_disposed) throw StateError('NanoUI 预览已关闭');
    final pointer = _frame();
    if (pointer == nullptr) throw StateError('NanoUI 没有可用画面');
    // HTTP must never retain a view onto native memory after the next tick.
    return Uint8List.fromList(pointer.asTypedList(1024));
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _stop();
    _active = false;
  }
}
