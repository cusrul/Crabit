import 'package:flutter/foundation.dart';

import '../managers/ble_manager.dart';
import '../transport/ble_transport.dart';
import '../transport/ble_auth.dart';
import 'device_link.dart';

enum DevicePhase {
  disconnected,
  scanning,
  connecting,
  authenticating,
  waitingForWatch,
  verifying,
  ready,
  failed,
}

class DeviceReading {
  const DeviceReading({required this.event, required this.elapsed});
  final DiagnosticEvent event;
  final Duration elapsed;
}

/// Presentation state only; the proven transport retains request/queue ownership.
class DeviceCenterController extends ChangeNotifier {
  DeviceCenterController({required this.link}) {
    _wasConnected = link.connected;
    _wasAuthenticated = link.authenticated;
    _wasReady = isReady;
    link.addListener(_linkChanged);
  }

  final DeviceLink link;
  bool _busy = false;
  bool _disconnecting = false;
  bool _disposed = false;
  int _session = 0;
  int _operation = 0;
  bool _wasConnected = false, _wasAuthenticated = false, _wasReady = false;
  String? _error;
  final Map<String, DeviceReading> _results = {};

  bool get busy => _busy;
  bool get disconnecting => _disconnecting;
  String? get error => _error;
  bool get isReady =>
      !_disconnecting &&
      link.connected &&
      link.authenticated &&
      link.ready &&
      link.pairing.stage == PairTransferStage.verified &&
      !link.scanning;
  bool get canRead => !_busy && isReady;
  bool get canVerify =>
      !_busy &&
      link.connected &&
      link.authenticated &&
      !link.scanning &&
      link.pairing.stage != PairTransferStage.verifying;
  String? get bleAccessCode => link.bleAccessCode;

  DevicePhase get phase {
    if (link.scanning) return DevicePhase.scanning;
    if (isReady) return DevicePhase.ready;
    if (link.pairing.stage == PairTransferStage.failed) {
      return DevicePhase.failed;
    }
    if (!link.connected) {
      return link.pairing.stage == PairTransferStage.connecting
          ? DevicePhase.connecting
          : DevicePhase.disconnected;
    }
    if (link.authenticated) return DevicePhase.verifying;
    if (link.pairing.stage == PairTransferStage.authenticating) {
      return DevicePhase.authenticating;
    }
    if (link.pairing.stage == PairTransferStage.waitingForWatch) {
      return DevicePhase.waitingForWatch;
    }
    return DevicePhase.connecting;
  }

  DeviceReading? resultFor(String topic) => _results[topic];

  void _linkChanged() {
    if (_disposed) return;
    if ((_wasConnected && !link.connected) ||
        (_wasAuthenticated && !link.authenticated) ||
        (_wasReady && !isReady)) {
      ++_session;
      _results.clear();
      _error = null;
    }
    _wasConnected = link.connected;
    _wasAuthenticated = link.authenticated;
    _wasReady = isReady;
    notifyListeners();
  }

  Future<void> connect() async {
    if (_disposed ||
        _busy ||
        link.connected ||
        link.scanning ||
        phase == DevicePhase.connecting) {
      return;
    }
    ++_session;
    _results.clear();
    await _perform(link.connect);
  }

  Future<void> saveBleAccessCode(String code) async {
    if (!isValidBleAccessCode(code)) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    await link.saveBleAccessCode(code);
    if (!_disposed) notifyListeners();
  }

  // Cancellation is not queued behind the work it is meant to cancel.
  Future<void> disconnect() async {
    if (_disposed || _disconnecting) return;
    ++_session;
    final operation = ++_operation;
    _results.clear();
    _error = null;
    _busy = true;
    _disconnecting = true;
    notifyListeners();
    try {
      await link.disconnect();
    } catch (error) {
      if (!_disposed && operation == _operation) _error = _actionError(error);
    } finally {
      if (!_disposed && operation == _operation) {
        _disconnecting = false;
        _busy = false;
        notifyListeners();
      }
    }
  }

  Future<void> verify() async {
    if (_disposed || !canVerify) return;
    await _perform(link.verify);
  }

  Future<void> readDiagnostic(String topic) async {
    if (_disposed || !canRead) return;
    if (!const {'diag/echo', 'diag/snapshot', 'diag/wifi'}.contains(topic)) {
      _error = '此页面不支持该操作';
      notifyListeners();
      return;
    }
    await _perform(() async {
      final session = _session;
      final stopwatch = Stopwatch()..start();
      final value = topic == 'diag/echo' ? 'crabit-check' : '';
      // Clear the previous reading before retry: an old success is not a new one.
      _results.remove(topic);
      final reply = await link.read(topic, value);
      if (_disposed || session != _session || !isReady) return;
      if (reply.topic != topic ||
          (topic == 'diag/echo' && reply.value != value)) {
        throw const TransportException(TransportErrorCode.protocolError);
      }
      _results[topic] = DeviceReading(event: reply, elapsed: stopwatch.elapsed);
    });
  }

  Future<void> _perform(Future<void> Function() action) async {
    if (_disposed || _busy) return;
    _busy = true;
    _error = null;
    final session = _session;
    final operation = ++_operation;
    notifyListeners();
    try {
      if (_disposed || session != _session || operation != _operation) return;
      await action();
    } catch (error) {
      if (!_disposed && session == _session && operation == _operation) {
        _error = _actionError(error);
      }
    } finally {
      if (!_disposed && operation == _operation) {
        _busy = false;
        notifyListeners();
      }
    }
  }

  static String _actionError(Object error) => error is TransportException
      ? _transportError(error.code)
      : '操作未完成，请检查连接后手动重试';

  static String _transportError(TransportErrorCode code) => switch (code) {
        TransportErrorCode.invalidPayload => '请先保存固件对应的 6 位设备验证码',
        TransportErrorCode.timeout => '请求超时，未收到结果；不会自动重发',
        TransportErrorCode.notConnected ||
        TransportErrorCode.disconnected =>
          '连接已断开，请重新连接手表',
        TransportErrorCode.notAuthenticated => '连接或授权尚未就绪，请查看上方状态',
        TransportErrorCode.busy => '通信正忙，请稍后手动重试',
        _ => '通信未完成（${code.name}），请手动重试',
      };

  @override
  void dispose() {
    _disposed = true;
    ++_session;
    ++_operation;
    link.removeListener(_linkChanged);
    super.dispose();
  }
}
