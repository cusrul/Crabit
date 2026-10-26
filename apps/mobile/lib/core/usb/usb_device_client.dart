import 'dart:math';

import 'usb_device_gateway.dart';
import 'usb_script_protocol.dart';

typedef UsbScriptIdGenerator = String Function();

class UsbDeviceClient {
  UsbDeviceClient({
    required this.gateway,
    UsbScriptIdGenerator? idGenerator,
  }) : _idGenerator = idGenerator ?? _randomId;

  final UsbDeviceGateway gateway;
  final UsbScriptIdGenerator _idGenerator;
  final Set<String> _usedIds = {};
  bool _busy = false;
  String? _scriptId;
  UsbDeviceReply? _lastReply;
  String? _runAttemptedId;

  bool get busy => _busy;
  String? get scriptId => _scriptId;
  UsbDeviceReply? get lastReply => _lastReply;

  bool canRun(String id) => !_busy && _canRunWithoutBusy(id);

  bool _canRunWithoutBusy(String id) =>
      gateway.isProductReady &&
      id == _scriptId &&
      _lastReply?.id == id &&
      _lastReply?.state == UsbRamState.ready &&
      _lastReply?.usbConnected == true &&
      _runAttemptedId != id;

  Future<UsbDeviceReply> status() => _exclusive(() async {
        final reply = await _request('status');
        if (reply.result != UsbDeviceResult.snapshot) {
          throw const FormatException('设备没有返回状态快照。');
        }
        _scriptId = reply.hasScript ? reply.id : null;
        _lastReply = reply;
        return reply;
      });

  Future<UsbDeviceReply> upload(String source) => _exclusive(() async {
        final script = validateUsbScript(source);
        final id = _newId();
        _scriptId = id;
        _lastReply = null;
        _runAttemptedId = null;

        var reply = await _request('begin:$id:${script.bytes.length}');
        _expect(reply,
            result: UsbDeviceResult.accepted,
            state: UsbRamState.uploading,
            id: id,
            received: 0,
            length: script.bytes.length);
        _lastReply = reply;

        for (var offset = 0; offset < script.bytes.length;) {
          final end = min(offset + usbScriptChunkBytes, script.bytes.length);
          final chunk = script.bytes.sublist(offset, end);
          final hex = chunk
              .map((byte) => byte.toRadixString(16).padLeft(2, '0'))
              .join();
          reply = await _request('chunk:$id:$offset:$hex');
          _expect(reply,
              result: UsbDeviceResult.accepted,
              state: UsbRamState.uploading,
              id: id,
              received: end,
              length: script.bytes.length);
          _lastReply = reply;
          offset = end;
        }

        reply = await _request('commit:$id');
        _expect(reply,
            result: UsbDeviceResult.ready,
            state: UsbRamState.ready,
            id: id,
            received: script.bytes.length,
            length: script.bytes.length);
        _lastReply = reply;
        return reply;
      });

  Future<UsbDeviceReply> run(String id) => _exclusive(() async {
        if (!_canRunWithoutBusy(id)) {
          throw StateError('只能对当前 ready 脚本显式运行一次。');
        }
        // Latch before sending: if the reply is lost, a second request must not
        // accidentally execute the same script again.
        _runAttemptedId = id;
        final reply = await _request('run:$id');
        if (reply.id != id ||
            reply.result != UsbDeviceResult.queued ||
            reply.state != UsbRamState.running ||
            reply.error != 'none') {
          _lastReply = reply;
          throw const FormatException('设备未接受脚本运行请求。');
        }
        _lastReply = reply;
        return reply;
      });

  Future<UsbDeviceReply> stop(String id) => _exclusive(() async {
        if (id != _scriptId ||
            !gateway.isProductReady ||
            (!{UsbRamState.running, UsbRamState.stopping}
                    .contains(_lastReply?.state) &&
                _runAttemptedId != id)) {
          throw StateError('没有可停止的当前设备脚本。');
        }
        final reply = await _request('stop:$id');
        if (reply.id != id ||
            reply.error != 'none' ||
            !((reply.result == UsbDeviceResult.accepted &&
                    reply.state == UsbRamState.stopping) ||
                (reply.result == UsbDeviceResult.stopped &&
                    reply.state == UsbRamState.stopped))) {
          _lastReply = reply;
          throw const FormatException('设备没有确认停止请求。');
        }
        _lastReply = reply;
        return reply;
      });

  Future<UsbDeviceReply> clear(String id) => _exclusive(() async {
        if (id != _scriptId || !gateway.isProductReady) {
          throw StateError('没有可清除的当前设备脚本。');
        }
        final reply = await _request('clear:$id');
        if (reply.result != UsbDeviceResult.cleared ||
            reply.state != UsbRamState.empty ||
            reply.id != '00000000') {
          _lastReply = reply;
          throw const FormatException('设备没有确认清除 RAM 脚本。');
        }
        _scriptId = null;
        _runAttemptedId = null;
        _lastReply = reply;
        return reply;
      });

  Future<UsbDeviceReply> _request(String command) async {
    if (!gateway.isProductReady) {
      throw StateError('需要已验证的 Crabit 产品连接。');
    }
    final reply = await gateway.request(command);
    _lastReply = reply;
    return reply;
  }

  Future<T> _exclusive<T>(Future<T> Function() operation) async {
    if (_busy) throw StateError('USB 设备操作正在进行。');
    _busy = true;
    try {
      return await operation();
    } finally {
      _busy = false;
    }
  }

  String _newId() {
    for (var attempt = 0; attempt < 32; attempt++) {
      final candidate = _idGenerator();
      if (RegExp(r'^[0-9a-f]{8}$').hasMatch(candidate) &&
          candidate != '00000000' &&
          !_usedIds.contains(candidate)) {
        _usedIds.add(candidate);
        return candidate;
      }
    }
    throw StateError('无法生成新的 USB 脚本 id。');
  }

  static String _randomId() {
    final random = Random.secure();
    return List.generate(
      8,
      (_) => random.nextInt(16).toRadixString(16),
    ).join();
  }

  static void _expect(
    UsbDeviceReply reply, {
    required UsbDeviceResult result,
    required UsbRamState state,
    required String id,
    required int received,
    required int length,
  }) {
    if (reply.result != result ||
        reply.state != state ||
        reply.id != id ||
        reply.received != received ||
        reply.length != length ||
        reply.error != 'none') {
      throw const FormatException('设备业务回复与 USB 脚本操作不匹配。');
    }
  }
}
