import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:math';

enum TransportErrorCode {
  notConnected,
  notAuthenticated,
  invalidPayload,
  writeFailed,
  timeout,
  disconnected,
  protocolError,
  busy,
}

/// Deliberately contains no payload, token or native exception text.
class TransportException implements Exception {
  const TransportException(this.code);
  final TransportErrorCode code;
  @override
  String toString() => 'BLE transport: ${code.name}';
}

class TransportReply {
  const TransportReply(this.topic, this.value, this.requestId);
  final String topic;
  final String value;
  final String requestId;
}

/// Buffer bytes, not decoded fragments: a notification can split a UTF-8 codepoint.
/// Only complete JSON objects are decoded; braces inside strings are ordinary data.
class BleJsonDecoder {
  static const maxFrameBytes = 4096;
  final List<int> _frame = [];
  int _depth = 0;
  bool _quoted = false;
  bool _escaped = false;

  void reset() {
    _frame.clear();
    _depth = 0;
    _quoted = false;
    _escaped = false;
  }

  List<Map<String, dynamic>> add(List<int> bytes) {
    final messages = <Map<String, dynamic>>[];
    for (final byte in bytes) {
      if (_frame.isEmpty && byte != 123) continue;
      _frame.add(byte);
      if (_frame.length > maxFrameBytes) {
        reset();
        throw const TransportException(TransportErrorCode.protocolError);
      }
      if (_quoted) {
        if (_escaped) {
          _escaped = false;
        } else if (byte == 92) {
          _escaped = true;
        } else if (byte == 34) {
          _quoted = false;
        }
      } else if (byte == 34) {
        _quoted = true;
      } else if (byte == 123) {
        _depth++;
      } else if (byte == 125) {
        _depth--;
        if (_depth == 0) {
          try {
            messages
                .add(jsonDecode(utf8.decode(_frame)) as Map<String, dynamic>);
          } on FormatException {
            throw const TransportException(TransportErrorCode.protocolError);
          } finally {
            reset();
          }
        }
      }
    }
    return messages;
  }
}

typedef BleChunkWriter = Future<void> Function(List<int> chunk);

/// Bound the caller's teardown wait without pretending a timed-out native
/// operation was cancelled. A second caller joins it, never starts another one.
class NativeTeardownFence {
  Future<void>? _operation;
  bool get busy => _operation != null;

  Future<void> run(
    Future<void> Function() teardown, {
    Duration timeout = const Duration(seconds: 6),
  }) async {
    var operation = _operation;
    if (operation == null) {
      operation = Future<void>.sync(teardown);
      _operation = operation;
      final current = operation;
      void release() {
        if (identical(_operation, current)) _operation = null;
      }

      // Handle eventual errors even after every caller has already timed out.
      unawaited(operation.then<void>((_) => release(),
          onError: (Object _, StackTrace __) => release()));
    }
    try {
      await operation.timeout(timeout);
    } on TimeoutException {
      throw const TransportException(TransportErrorCode.busy);
    }
  }
}

class _Write {
  _Write(this.bytes, this.generation);
  final List<int> bytes;
  final int generation;
  final done = Completer<void>();
  Timer? timer;
  bool started = false;
}

class _Request {
  _Request(this.topic, this.ackOnly);
  final String topic;
  final bool ackOnly;
  final done = Completer<TransportReply>();
}

/// The same bounded message writer/correlator is used by product and regression.
/// It owns no SDK/device/pairing policy. All SDK writes stay in BleManager.
class BleTransport {
  static const chunkBytes = 20;
  static const maxTopicBytes = 47;
  static const maxValueBytes = 191;
  static const maxCommandBytes = 512;
  static const maxPending = 16;

  final BleJsonDecoder decoder = BleJsonDecoder();
  final Queue<_Write> _writes = Queue<_Write>();
  final Map<String, _Request> _requests = {};
  BleChunkWriter? _writer;
  bool authenticated = false;
  bool _pumping = false;
  Completer<void>? _idle;
  int _generation = 0;
  int _nextId = 1;
  Completer<void>? _probe;
  String? _expectedProbe;
  void Function()? onUnusable;

  bool get connected => _writer != null;
  int get generation => _generation;
  bool get hasPendingWork =>
      _pumping || _writes.isNotEmpty || _requests.isNotEmpty || _probe != null;

  void open(BleChunkWriter writer) {
    // Never overlap a replacement native writer with a stalled previous one.
    // Teardown must establish this fence; rejection is observable, not a queue
    // which silently waits behind the previous device forever.
    if (_pumping) throw const TransportException(TransportErrorCode.busy);
    close();
    _writer = writer;
  }

  Future<void> waitForIdle(
      {Duration timeout = const Duration(seconds: 5)}) async {
    final idle = _idle;
    if (idle == null) return;
    try {
      await idle.future.timeout(timeout);
    } on TimeoutException {
      throw const TransportException(TransportErrorCode.busy);
    }
  }

  void close([TransportErrorCode reason = TransportErrorCode.disconnected]) {
    _generation++;
    _writer = null;
    authenticated = false;
    decoder.reset();
    final error = TransportException(reason);
    for (final write in _writes) {
      write.timer?.cancel();
      if (!write.done.isCompleted) write.done.completeError(error);
    }
    _writes.clear();
    for (final request in _requests.values) {
      if (!request.done.isCompleted) request.done.completeError(error);
    }
    _requests.clear();
    final probe = _probe;
    _probe = null;
    _expectedProbe = null;
    if (probe != null && !probe.isCompleted) probe.completeError(error);
  }

  void _requireReady({bool requireAuth = true}) {
    if (!connected) {
      throw const TransportException(TransportErrorCode.notConnected);
    }
    if (requireAuth && !authenticated) {
      throw const TransportException(TransportErrorCode.notAuthenticated);
    }
  }

  /// One queue slot is one entire message, never one chunk. Concurrent callers
  /// cannot interleave 20-byte fragments. Queue time is part of the deadline.
  Future<void> sendRaw(
    String data, {
    bool requireAuth = true,
    Duration timeout = const Duration(seconds: 4),
  }) async {
    _requireReady(requireAuth: requireAuth);
    final bytes = utf8.encode(data);
    if (bytes.isEmpty ||
        bytes.length > maxCommandBytes ||
        timeout <= Duration.zero) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    if (_writes.length >= maxPending) {
      throw const TransportException(TransportErrorCode.busy);
    }
    final write = _Write(bytes, _generation);
    _writes.add(write);
    write.timer = Timer(timeout, () {
      if (write.done.isCompleted) return;
      write.done
          .completeError(const TransportException(TransportErrorCode.timeout));
      if (write.started) {
        // A partially written command must never be followed by another one.
        close(TransportErrorCode.timeout);
        onUnusable?.call();
      } else {
        _writes.remove(write);
      }
    });
    unawaited(_pump());
    return write.done.future;
  }

  Future<void> _pump() async {
    if (_pumping) return;
    _pumping = true;
    final idle = Completer<void>();
    _idle = idle;
    try {
      while (_writes.isNotEmpty) {
        final write = _writes.first;
        final writer = _writer;
        if (write.done.isCompleted ||
            writer == null ||
            write.generation != _generation) {
          _writes.removeFirst();
          continue;
        }
        write.started = true;
        try {
          for (int offset = 0;
              offset < write.bytes.length;
              offset += chunkBytes) {
            if (write.generation != _generation || write.done.isCompleted) {
              break;
            }
            await writer(write.bytes
                .sublist(offset, min(offset + chunkBytes, write.bytes.length)));
          }
          if (!write.done.isCompleted) write.done.complete();
        } catch (_) {
          if (write.generation == _generation) {
            close(TransportErrorCode.writeFailed);
            onUnusable?.call();
          }
        } finally {
          write.timer?.cancel();
          _writes.remove(write);
        }
      }
    } finally {
      _pumping = false;
      _idle = null;
      idle.complete();
    }
  }

  void accept(Map<String, dynamic> json) {
    final topic = json['t']?.toString();
    final value = json['v']?.toString() ?? '';
    if (topic == 'sys/notify' && value == _expectedProbe) {
      final probe = _probe;
      if (probe != null && !probe.isCompleted) probe.complete();
    }
    final id = (topic == 'ack' ? json['v'] : json['i'])?.toString();
    final request = _requests[id];
    if (request == null || request.done.isCompleted) return;
    if ((request.ackOnly && topic == 'ack') ||
        (!request.ackOnly && topic == request.topic)) {
      request.done.complete(TransportReply(topic!, value, id!));
    }
  }

  Future<void> verifyEcho(
    String probe, {
    Duration timeout = const Duration(seconds: 4),
    bool requireAuth = true,
  }) async {
    _requireReady(requireAuth: requireAuth);
    if (!probe.startsWith('TEST:') || utf8.encode(probe).length > chunkBytes) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    if (_probe != null) throw const TransportException(TransportErrorCode.busy);
    final pending = Completer<void>();
    _probe = pending;
    _expectedProbe = probe;
    // Attach the response listener before the first write (immediate replies).
    final response = pending.future.timeout(timeout, onTimeout: () {
      throw const TransportException(TransportErrorCode.timeout);
    });
    final operation = Future.wait<void>(
        [sendRaw(probe, timeout: timeout, requireAuth: requireAuth), response],
        eagerError: true);
    try {
      await operation;
    } finally {
      if (identical(_probe, pending)) {
        _probe = null;
        _expectedProbe = null;
      }
    }
  }

  /// IDs never wrap/reuse within a process, including after reconnect. Exhaustion
  /// is explicit rather than allowing a late old response to match a new request.
  Future<TransportReply> request(
    String topic,
    String value, {
    bool ackOnly = false,
    bool requireAuth = true,
    Duration timeout = const Duration(seconds: 4),
  }) async {
    _requireReady(requireAuth: requireAuth);
    if (topic.isEmpty ||
        utf8.encode(topic).length > maxTopicBytes ||
        utf8.encode(value).length > maxValueBytes ||
        topic.contains('\u0000') ||
        value.contains('\u0000') ||
        timeout <= Duration.zero) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    if (_requests.length >= maxPending || _nextId > 65535) {
      throw const TransportException(TransportErrorCode.busy);
    }
    final id = (_nextId++).toString();
    final payload = jsonEncode({'i': int.parse(id), 't': topic, 'v': value});
    if (utf8.encode(payload).length > maxCommandBytes) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    final pending = _Request(topic, ackOnly);
    _requests[id] = pending;
    final response = pending.done.future.timeout(timeout, onTimeout: () {
      throw const TransportException(TransportErrorCode.timeout);
    });
    // Both futures get an error handler immediately, even if a write fails first.
    final operation = Future.wait<Object?>([
      sendRaw(payload, requireAuth: requireAuth, timeout: timeout),
      response,
    ], eagerError: true);
    try {
      final results = await operation;
      return results[1] as TransportReply;
    } finally {
      if (identical(_requests[id], pending)) _requests.remove(id);
    }
  }
}
