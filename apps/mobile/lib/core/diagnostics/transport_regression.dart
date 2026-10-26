import 'dart:async';
import 'dart:convert';

abstract interface class RegressionTransport {
  bool get isReady;
  Future<void> verifyBleEcho();
  Future<RegressionReply> request(String topic, String value);
  Future<void> reconnect();
}

class RegressionReply {
  const RegressionReply({
    required this.topic,
    required this.value,
    required this.requestId,
  });

  final String topic;
  final String value;
  final String requestId;
}

enum RegressionProfile { quick, reconnect, boundaries, soak }

enum RegressionStatus { pass, fail, cancelled }

enum RegressionRunKind { hardware, hostMock }

class RegressionStepResult {
  const RegressionStepResult({
    required this.index,
    required this.name,
    required this.status,
    required this.elapsedMs,
    this.requestId,
    this.payloadKind,
    this.payloadBytes,
    this.errorCode,
  });

  final int index;
  final String name;
  final RegressionStatus status;
  final int elapsedMs;
  final String? requestId;
  final String? payloadKind;
  final int? payloadBytes;
  final String? errorCode;

  Map<String, Object?> toJson() => {
        'index': index,
        'name': name,
        'status': status.name,
        'elapsedMs': elapsedMs,
        if (requestId != null) 'requestId': requestId,
        if (payloadKind != null) 'payloadKind': payloadKind,
        if (payloadBytes != null) 'payloadBytes': payloadBytes,
        if (errorCode != null) 'errorCode': errorCode,
      };
}

class RegressionSummary {
  const RegressionSummary({
    required this.profile,
    required this.runKind,
    required this.status,
    required this.completed,
    required this.elapsedMs,
    required this.passed,
    required this.failed,
    required this.cancelled,
    required this.steps,
  });

  final RegressionProfile profile;
  final RegressionRunKind runKind;
  final RegressionStatus status;
  final bool completed;
  final int elapsedMs;
  final int passed;
  final int failed;
  final int cancelled;
  final List<RegressionStepResult> steps;

  Map<String, Object?> toJson() => {
        'profile': profile.name,
        'runKind': runKind.name,
        'status': status.name,
        'completed': completed,
        'elapsedMs': elapsedMs,
        'counts': {'passed': passed, 'failed': failed, 'cancelled': cancelled},
        'steps': steps.map((step) => step.toJson()).toList(growable: false),
      };

  String encodeJson() => jsonEncode(toJson());
}

typedef RegressionProgress = void Function(RegressionStepResult result);
typedef RegressionErrorClassifier = String Function(Object error);

class TransportRegressionRunner {
  TransportRegressionRunner({
    required this.transport,
    required this.runKind,
    this.onResult,
    this.classifyError,
  });

  final RegressionTransport transport;
  final RegressionRunKind runKind;
  final RegressionProgress? onResult;
  final RegressionErrorClassifier? classifyError;
  bool _running = false;
  bool _cancelRequested = false;

  bool get running => _running;

  void cancel() {
    if (_running) _cancelRequested = true;
  }

  Future<RegressionSummary> run(RegressionProfile profile) async {
    if (_running) throw StateError('regression_already_running');
    _running = true;
    _cancelRequested = false;
    final watch = Stopwatch()..start();
    final results = <RegressionStepResult>[];
    final requestIds = <String>{};
    var terminal = RegressionStatus.pass;
    try {
      if (!transport.isReady) {
        _record(
          results,
          'ready',
          RegressionStatus.fail,
          watch,
          errorCode: 'not_ready',
        );
        terminal = RegressionStatus.fail;
      } else {
        final operations = _operations(profile);
        for (final operation in operations) {
          if (_cancelRequested) {
            _record(results, operation.name, RegressionStatus.cancelled, watch,
                payloadKind: operation.payloadKind,
                payloadBytes: operation.payloadBytes);
            terminal = RegressionStatus.cancelled;
            break;
          }
          final stepWatch = Stopwatch()..start();
          String? requestId;
          try {
            if (operation.reconnect) {
              await transport.reconnect();
              if (_cancelRequested) {
                _record(results, operation.name, RegressionStatus.cancelled,
                    stepWatch,
                    payloadKind: operation.payloadKind,
                    payloadBytes: operation.payloadBytes);
                terminal = RegressionStatus.cancelled;
                break;
              }
              if (!transport.isReady) throw StateError('not_ready');
              _record(
                  results, operation.name, RegressionStatus.pass, stepWatch);
              continue;
            }
            if (operation.bleEcho) {
              await transport.verifyBleEcho();
            } else {
              final reply =
                  await transport.request(operation.topic!, operation.value!);
              requestId = reply.requestId;
              if (reply.topic != operation.topic || reply.requestId.isEmpty) {
                throw _RegressionMismatch();
              }
              if (!requestIds.add(reply.requestId)) {
                throw _DuplicateRequestId();
              }
              if (operation.topic == 'diag/echo' &&
                  reply.value != operation.value) {
                throw _RegressionMismatch();
              }
              if (operation.topic != 'diag/echo' && reply.value.isEmpty) {
                throw _EmptyDiagnosticReply();
              }
            }
            if (_cancelRequested) {
              _record(results, operation.name, RegressionStatus.cancelled,
                  stepWatch,
                  requestId: _safeRequestId(requestId),
                  payloadKind: operation.payloadKind,
                  payloadBytes: operation.payloadBytes);
              terminal = RegressionStatus.cancelled;
              break;
            }
            _record(results, operation.name, RegressionStatus.pass, stepWatch,
                requestId: _safeRequestId(requestId),
                payloadKind: operation.payloadKind,
                payloadBytes: operation.payloadBytes);
          } catch (error) {
            _record(
              results,
              operation.name,
              _cancelRequested
                  ? RegressionStatus.cancelled
                  : RegressionStatus.fail,
              stepWatch,
              requestId: _safeRequestId(requestId),
              payloadKind: operation.payloadKind,
              payloadBytes: operation.payloadBytes,
              errorCode: _cancelRequested ? null : _errorCode(error),
            );
            terminal = _cancelRequested
                ? RegressionStatus.cancelled
                : RegressionStatus.fail;
            break;
          }
        }
      }
    } finally {
      watch.stop();
      _running = false;
    }
    final frozen = List<RegressionStepResult>.unmodifiable(results);
    return RegressionSummary(
      profile: profile,
      runKind: runKind,
      status: terminal,
      completed: terminal == RegressionStatus.pass,
      elapsedMs: watch.elapsedMilliseconds,
      passed: frozen.where((r) => r.status == RegressionStatus.pass).length,
      failed: frozen.where((r) => r.status == RegressionStatus.fail).length,
      cancelled:
          frozen.where((r) => r.status == RegressionStatus.cancelled).length,
      steps: frozen,
    );
  }

  String _errorCode(Object error) {
    if (error is _RegressionMismatch) return 'reply_mismatch';
    if (error is _DuplicateRequestId) return 'duplicate_request_id';
    if (error is _EmptyDiagnosticReply) return 'empty_diagnostic_reply';
    final code = classifyError?.call(error);
    return code == null || !RegExp(r'^[a-zA-Z0-9_.-]{1,64}$').hasMatch(code)
        ? 'transport_error'
        : code;
  }

  String? _safeRequestId(String? id) =>
      id != null && RegExp(r'^[a-zA-Z0-9_.:-]{1,64}$').hasMatch(id) ? id : null;

  void _record(
    List<RegressionStepResult> results,
    String name,
    RegressionStatus status,
    Stopwatch timer, {
    String? requestId,
    String? payloadKind,
    int? payloadBytes,
    String? errorCode,
  }) {
    final result = RegressionStepResult(
      index: results.length + 1,
      name: name,
      status: status,
      elapsedMs: timer.elapsedMilliseconds,
      requestId: requestId != null && requestId.isNotEmpty ? requestId : null,
      payloadKind: payloadKind,
      payloadBytes: payloadBytes,
      errorCode: errorCode,
    );
    results.add(result);
    try {
      onResult?.call(result);
    } catch (_) {
      // UI progress listeners must not change transport outcomes.
    }
  }

  List<_Operation> _operations(RegressionProfile profile) {
    final ops = <_Operation>[];
    void ble(String name) => ops.add(_Operation(name: name, bleEcho: true));
    void req(String topic, String value, String name,
            {String? kind, int? bytes}) =>
        ops.add(_Operation(
          name: name,
          topic: topic,
          value: value,
          payloadKind: kind,
          payloadBytes: bytes,
        ));
    switch (profile) {
      case RegressionProfile.quick:
        for (var i = 0; i < 20; i++) {
          ble('ble_echo_${i + 1}');
          req('diag/echo', 'reg-${i + 1}', 'echo_${i + 1}',
              kind: 'ascii', bytes: utf8.encode('reg-${i + 1}').length);
        }
        req('diag/snapshot', 'read', 'snapshot', kind: 'ascii', bytes: 4);
        req('diag/wifi', 'status', 'wifi_status', kind: 'ascii', bytes: 6);
        break;
      case RegressionProfile.reconnect:
        for (var i = 0; i < 5; i++) {
          ops.add(_Operation(name: 'reconnect_${i + 1}', reconnect: true));
          ble('reconnect_ble_echo_${i + 1}');
          req('diag/echo', 'reconnect-${i + 1}', 'reconnect_echo_${i + 1}',
              kind: 'ascii', bytes: utf8.encode('reconnect-${i + 1}').length);
        }
        break;
      case RegressionProfile.boundaries:
        for (final size in [19, 20, 21, 39, 40, 41, 128, 191]) {
          final value = List.filled(size, 'A').join();
          req('diag/echo', value, 'ascii_${size}b', kind: 'ascii', bytes: size);
        }
        for (final sample in [
          '中文边界',
          '{"brace":"{}"}',
          r'{"q":"a\b","braces":"{}"}'
        ]) {
          req('diag/echo', sample, 'sample_${ops.length + 1}',
              kind: sample.contains('中文') ? 'unicode' : 'json_special',
              bytes: utf8.encode(sample).length);
        }
        break;
      case RegressionProfile.soak:
        for (var i = 0; i < 100; i++) {
          req('diag/echo', 'soak-${i + 1}', 'soak_echo_${i + 1}',
              kind: 'ascii', bytes: utf8.encode('soak-${i + 1}').length);
          req('diag/snapshot', 'read', 'soak_snapshot_${i + 1}',
              kind: 'ascii', bytes: 4);
        }
        break;
    }
    return ops;
  }
}

class _Operation {
  const _Operation({
    required this.name,
    this.bleEcho = false,
    this.reconnect = false,
    this.topic,
    this.value,
    this.payloadKind,
    this.payloadBytes,
  });
  final String name;
  final bool bleEcho;
  final bool reconnect;
  final String? topic;
  final String? value;
  final String? payloadKind;
  final int? payloadBytes;
}

class _RegressionMismatch implements Exception {}

class _DuplicateRequestId implements Exception {}

class _EmptyDiagnosticReply implements Exception {}
