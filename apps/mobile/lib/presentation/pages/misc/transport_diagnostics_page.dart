import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:crabit_watch_app/core/diagnostics/ble_regression_adapter.dart';
import 'package:crabit_watch_app/core/diagnostics/transport_regression.dart';
import 'package:crabit_watch_app/core/diagnostics/transport_automation.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';

class TransportDiagnosticsPage extends StatefulWidget {
  const TransportDiagnosticsPage({super.key});
  @override
  State<TransportDiagnosticsPage> createState() =>
      _TransportDiagnosticsPageState();
}

class _TransportDiagnosticsPageState extends State<TransportDiagnosticsPage> {
  final _ble = BleManager();
  final List<String> _logs = [];
  late final TransportRegressionRunner _runner;
  RegressionSummary? _summary;
  bool _busy = false;
  bool _baselinePassed = false;
  String _progress = '尚未运行实机回归';
  DateTime? _startedAt;

  @override
  void initState() {
    super.initState();
    _runner = TransportRegressionRunner(
      transport: BleRegressionAdapter(_ble),
      runKind: RegressionRunKind.hardware,
      classifyError: (error) =>
          error is TransportException ? error.code.name : 'transport_error',
      onResult: (result) {
        if (!mounted) return;
        setState(() {
          _progress =
              '${result.index} · ${result.name} · ${result.status.name}';
          _append('${result.name} ${result.status.name} ${result.elapsedMs}ms'
              ' id=${result.requestId ?? "-"} ${result.errorCode ?? ""}');
        });
      },
    );
    TransportAutomation.attach(this,
        snapshot: _automationSnapshot, action: _automationAction);
  }

  @override
  void dispose() {
    _runner.cancel();
    TransportAutomation.detach(this);
    super.dispose();
  }

  Map<String, Object?> _automationSnapshot() => {
        'available': true,
        'busy': _busy,
        'ready': BleRegressionAdapter(_ble).isReady,
        'pairStage': _ble.pairTransferStatus.value.stage.name,
        'baselinePassed': _baselinePassed,
        'progress': _progress,
        'clientSource': const String.fromEnvironment('CRABIT_SOURCE_SHA',
            defaultValue: 'unrecorded'),
        'firmwareIdentity': 'user-confirmation-required',
        if (_summary != null) 'summary': _summary!.toJson(),
      };

  Map<String, Object?> _automationAction(String command) {
    if (command == 'cancel') {
      _runner.cancel();
      return {'accepted': _runner.running};
    }
    if (_busy) return {'accepted': false, 'reason': 'busy'};
    if (command == 'connect') {
      unawaited(_connect());
      return {'accepted': true};
    }
    final profile =
        RegressionProfile.values.where((p) => p.name == command).firstOrNull;
    if (profile == null) {
      return {'accepted': false, 'reason': 'unknown_command'};
    }
    if (!BleRegressionAdapter(_ble).isReady) {
      return {'accepted': false, 'reason': 'not_ready'};
    }
    if (profile != RegressionProfile.quick && !_baselinePassed) {
      return {'accepted': false, 'reason': 'baseline_required'};
    }
    unawaited(_run(profile));
    return {'accepted': true};
  }

  void _append(String line) {
    _logs.insert(0, line);
    if (_logs.length > 300) _logs.removeLast();
  }

  Future<void> _connect() async {
    setState(() {
      _busy = true;
      _baselinePassed = false;
    });
    try {
      await _ble.prepareDiagnosticConnection();
      if (mounted) setState(() => _append('诊断连接就绪；尚未启动测试'));
    } on TransportException catch (error) {
      if (mounted) setState(() => _append('连接：${error.code.name}'));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _run(RegressionProfile profile) async {
    if (_busy) return;
    setState(() {
      _busy = true;
      _summary = null;
      _startedAt = DateTime.now().toUtc();
    });
    try {
      final summary = await _runner.run(profile);
      if (!mounted) return;
      setState(() {
        _summary = summary;
        if (!summary.completed) _baselinePassed = false;
        if (profile == RegressionProfile.quick) {
          _baselinePassed = summary.completed;
        }
        _progress = '${profile.name} · ${summary.status.name} · '
            '${summary.passed} 通过 / ${summary.failed} 失败 / ${summary.cancelled} 取消';
      });
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _single(String topic, String value) async {
    if (_busy) return;
    setState(() => _busy = true);
    final timer = Stopwatch()..start();
    try {
      final event = await _ble.requestDiagnosticChecked(topic, value);
      if (mounted) {
        setState(() => _append(
            '$topic ${timer.elapsedMilliseconds}ms id=${event.requestId} ${event.value}'));
      }
    } on TransportException catch (error) {
      if (mounted) setState(() => _append('$topic ${error.code.name}'));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _copyReport() async {
    final summary = _summary;
    if (summary == null) return;
    await Clipboard.setData(ClipboardData(
        text: jsonEncode({
      'schema': 1,
      'clientSource': const String.fromEnvironment('CRABIT_SOURCE_SHA',
          defaultValue: 'unrecorded'),
      'platform': Platform.operatingSystem,
      'startedAtUtc': _startedAt?.toIso8601String(),
      'firmwareIdentity': 'user-confirmation-required',
      'scope':
          'BLE and read-only diagnostic transport; no Wi-Fi connection acceptance',
      'result': summary.toJson(),
    })));
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text('已复制脱敏测试报告')));
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('链路诊断')),
        body: ValueListenableBuilder<PairTransferStatus>(
          valueListenable: _ble.pairTransferStatus,
          builder: (context, pair, _) {
            final ready = BleRegressionAdapter(_ble).isReady;
            final enabled = !_busy && ready;
            return ListView(padding: const EdgeInsets.all(16), children: [
              Text(ready ? '诊断链路就绪' : '链路状态：${pair.stage.name}',
                  style: Theme.of(context).textTheme.titleMedium),
              if (pair.detail != null) Text(pair.detail!),
              const SizedBox(height: 8),
              const Text(
                  '首次使用先在主页保存设备验证码。再在 STM32 的 BLE LINK 中 START，确认 UART PASS / RAM。'
                  '连接时会自动发送本机保存的 6 位设备验证码；本诊断页只提供 echo、快照、IMU 和 Wi-Fi 状态读取。'
                  '应用会阻止诊断会话触发设置写入、键鼠控制和原始命令；诊断连接不自动校时。'),
              const SizedBox(height: 12),
              Wrap(spacing: 8, runSpacing: 8, children: [
                FilledButton(
                    onPressed: _busy ? null : _connect,
                    child: const Text('手表已准备，建立诊断连接')),
                FilledButton.tonal(
                    onPressed:
                        enabled ? () => _run(RegressionProfile.quick) : null,
                    child: const Text('运行基础回归 · 42 项')),
                OutlinedButton(
                    onPressed: _runner.running
                        ? () {
                            _runner.cancel();
                            setState(() => _progress = '正在停止；等待当前请求结束，不发送下一项');
                          }
                        : null,
                    child: const Text('停止')),
              ]),
              const SizedBox(height: 12),
              Text(_progress),
              if (_busy)
                const Padding(
                    padding: EdgeInsets.symmetric(vertical: 8),
                    child: LinearProgressIndicator()),
              const Divider(height: 28),
              const Text('基础回归通过后，分组推进；任一失败立即停止，不自动重试或复位。'),
              Wrap(spacing: 8, runSpacing: 8, children: [
                OutlinedButton(
                    onPressed: enabled && _baselinePassed
                        ? () => _run(RegressionProfile.reconnect)
                        : null,
                    child: const Text('重连 · 5 次')),
                OutlinedButton(
                    onPressed: enabled && _baselinePassed
                        ? () => _run(RegressionProfile.boundaries)
                        : null,
                    child: const Text('数据边界')),
                OutlinedButton(
                    onPressed: enabled && _baselinePassed
                        ? () => _run(RegressionProfile.soak)
                        : null,
                    child: const Text('稳定性 · 100 轮')),
                TextButton(
                    onPressed: !_busy && _summary != null ? _copyReport : null,
                    child: const Text('复制报告')),
              ]),
              const Divider(height: 28),
              Wrap(spacing: 8, children: [
                TextButton(
                    onPressed: enabled
                        ? () => _single('diag/echo', 'flutter-diag')
                        : null,
                    child: const Text('单次回显')),
                TextButton(
                    onPressed:
                        enabled ? () => _single('diag/snapshot', 'read') : null,
                    child: const Text('STM 传感器')),
                TextButton(
                    onPressed:
                        enabled ? () => _single('diag/wifi', 'status') : null,
                    child: const Text('Wi-Fi 状态')),
              ]),
              const Text('Wi-Fi 状态返回不代表已完成联网测试。屏幕显示和退出仍需最终目视验收。'),
              const SizedBox(height: 16),
              SelectableText(_logs.isEmpty ? '尚无测试记录' : _logs.join('\n'),
                  style:
                      const TextStyle(fontFamily: 'monospace', fontSize: 12)),
            ]);
          },
        ),
      );
}
