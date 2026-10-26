import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:crabit_watch_app/core/managers/storage_manager.dart';
import 'package:crabit_watch_app/core/transport/ble_auth.dart';
import 'package:crabit_watch_app/core/transport/ble_transport.dart';
import '../device/remembered_connection.dart';
import '../device/audit_inbox.dart';

const String crabitWatchName = 'Crabit-Watch';
const int bleWriteChunkMaxBytes = 20;
const bool bleTimingEnabled = bool.fromEnvironment('CRABIT_BLE_TIMING');

bool isCrabitWatchName(String name) {
  return name.toUpperCase().contains(crabitWatchName.toUpperCase());
}

// ================= C2 数据模型 =================
class ProbeEvent {
  final String mac;
  final String ssid;
  final DateTime timestamp;
  ProbeEvent.fromJson(Map<String, dynamic> json)
      : mac = json['m']?.toString() ?? "UNKNOWN",
        ssid = json['s']?.toString() ?? "<BROADCAST>",
        timestamp = DateTime.now();
}

class EapolEvent {
  final String apMac;
  final String clientMac;
  final String hexPayload;
  final String ssid;
  final DateTime timestamp;
  EapolEvent.fromJson(Map<String, dynamic> json)
      : apMac = json['s']?.toString() ?? "UNKNOWN",
        clientMac = json['d']?.toString() ?? "UNKNOWN",
        hexPayload = json['p']?.toString() ?? "",
        ssid = json['n']?.toString() ?? "UNKNOWN", // 从上行数据中取网络名称
        timestamp = DateTime.now();
}

class DiagnosticEvent {
  final String topic;
  final String value;
  final String? requestId;
  final DateTime timestamp;

  DiagnosticEvent({required this.topic, required this.value, this.requestId})
      : timestamp = DateTime.now();
}

enum PairTransferStage {
  idle,
  scanning,
  connecting,
  waitingForWatch,
  diagnosticReady,
  authenticating,
  authenticated,
  verifying,
  verified,
  failed,
}

class PairTransferStatus {
  const PairTransferStatus({
    required this.stage,
    this.requestId,
    this.latency,
    this.detail,
  });

  final PairTransferStage stage;
  final String? requestId;
  final Duration? latency;
  final String? detail;

  bool get canRetry =>
      stage == PairTransferStage.authenticated ||
      stage == PairTransferStage.failed ||
      stage == PairTransferStage.verified;
}

bool isMatchingPairTransferEcho(String? expectedProbe, String value) {
  return expectedProbe != null && value == expectedProbe;
}

String pairTransferProbeForRequestId(String requestId) {
  return 'TEST:P:$requestId';
}

/// 专门负责与手表通信的单例模块
class BleManager {
  // 单例模式
  static final BleManager _instance = BleManager._internal();
  factory BleManager() => _instance;
  BleManager._internal() {
    _transport.onUnusable = () {
      rememberedConnection.fault();
      unawaited(_disconnectAfterFault());
    };
  }

  // 对 UI 层暴露的响应式状态
  final ValueNotifier<bool> isConnected = ValueNotifier(false);
  final ValueNotifier<bool> isAuthenticated = ValueNotifier(false);
  final ValueNotifier<bool> isScanning = ValueNotifier(false);
  final ValueNotifier<PairTransferStatus> pairTransferStatus = ValueNotifier(
    const PairTransferStatus(stage: PairTransferStage.idle),
  );

  // 向 UI 层广播 C2 WiFi 探针侦听数据的流
  final StreamController<ProbeEvent> _probeStreamController =
      StreamController<ProbeEvent>.broadcast();
  Stream<ProbeEvent> get probeStream => _probeStreamController.stream;

  final StreamController<EapolEvent> _eapolStreamController =
      StreamController<EapolEvent>.broadcast();
  Stream<EapolEvent> get eapolStream => _eapolStreamController.stream;

  final StreamController<DiagnosticEvent> _diagnosticStreamController =
      StreamController<DiagnosticEvent>.broadcast();
  Stream<DiagnosticEvent> get diagnosticStream =>
      _diagnosticStreamController.stream;

  BluetoothDevice? _watchDevice;
  BluetoothCharacteristic? _rxCharacteristic;
  BluetoothCharacteristic? _txCharacteristic;

  final String targetDeviceName = crabitWatchName;
  final String serviceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  final String rxUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
  final String txUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;
  StreamSubscription<List<int>>? _notifySubscription;

  final BleTransport _transport = BleTransport();
  final Random _rnd = Random();
  int _connectionEpoch = 0;
  bool _connecting = false;
  bool _disconnecting = false;
  final NativeTeardownFence _teardownFence = NativeTeardownFence();
  Future<void>? _connectionOperation;
  Completer<BluetoothDevice>? _scanFound;
  bool _diagnosticMode = false;
  Future<void>? _pairVerification;
  int _pairTransferEpoch = 0;
  bool _productSession = false;
  bool _syncTimeAfterPairing = true;

  late final RememberedConnection rememberedConnection = RememberedConnection(
    readSetting: (key) => StorageManager().getSetting(key),
    putSetting: (key, value) =>
        StorageManager().getBox(StorageManager.boxSettings).put(key, value),
    connectRemembered: _reconnectRemembered,
    cancelAutomatic: _cancelAutomaticConnection,
    isReady: () => isReady,
    isOccupied: () =>
        isConnected.value ||
        isScanning.value ||
        _connecting ||
        _disconnecting ||
        _teardownFence.busy ||
        _connectionOperation != null,
  );

  bool get isReady =>
      isConnected.value &&
      _transport.connected &&
      isAuthenticated.value &&
      (diagnosticMode
          ? pairTransferStatus.value.stage ==
                  PairTransferStage.diagnosticReady ||
              pairTransferStatus.value.stage == PairTransferStage.verified
          : pairTransferStatus.value.stage == PairTransferStage.verified);
  bool get isProductReady => isReady && !diagnosticMode;
  String? get connectedDeviceId => _watchDevice?.remoteId.str;
  bool get diagnosticMode => _diagnosticMode && !_productSession;
  bool get canReadBackground => isReady && !_transport.hasPendingWork;

  String? get bleAccessCode {
    final value = StorageManager().getSetting('ble_access_code');
    return value is String && isValidBleAccessCode(value) ? value : null;
  }

  Future<void> saveBleAccessCode(String code) async {
    if (!isValidBleAccessCode(code)) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    await StorageManager()
        .getBox(StorageManager.boxSettings)
        .put('ble_access_code', code);
  }

  Future<void> _cancelAutomaticConnection() async {
    try {
      await _teardownFence.run(_disconnectNative);
    } on TransportException {
      rememberedConnection.fault();
    }
  }

  Future<void> _disconnectAfterFault() async {
    await _cancelAutomaticConnection();
    _setPairTransfer(PairTransferStage.failed, detail: '通信失败；请手动检查');
  }

  /// Product connection skips RTC writes, but remains a product session rather
  /// than being misreported as a diagnostic session.
  Future<void> connectProduct() {
    if (bleAccessCode == null) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    if (_connectionOperation != null ||
        _disconnecting ||
        _connecting ||
        isScanning.value ||
        isConnected.value ||
        _teardownFence.busy ||
        rememberedConnection.automaticRunning) {
      throw const TransportException(TransportErrorCode.busy);
    }
    rememberedConnection.explicitConnect(diagnostic: false);
    _productSession = true;
    return _startConnection(diagnosticMode: false, syncTime: false);
  }

  Future<void> _reconnectRemembered(String remoteId) async {
    _productSession = true;
    await _startConnection(
        diagnosticMode: false, remoteId: remoteId, syncTime: false);
    if (isConnected.value && !isReady) {
      try {
        await waitForReady();
      } on TransportException {
        // Do not repeatedly request pairing/verification after a rejection or
        // an unresponsive watch. Revoked attempts are cancellation, not faults.
        if (rememberedConnection.automaticAttemptCurrent) {
          rememberedConnection.fault();
        }
        rethrow;
      }
    }
  }

  void _log(String tag, String msg) {
    debugPrint("[BLE_CORE $tag] $msg");
  }

  void _setPairTransfer(
    PairTransferStage stage, {
    String? requestId,
    Duration? latency,
    String? detail,
  }) {
    if (bleTimingEnabled) {
      _log('STAGE',
          'epoch=$_connectionEpoch stage=${stage.name} connected=${isConnected.value} authenticated=${isAuthenticated.value}');
    }
    pairTransferStatus.value = PairTransferStatus(
      stage: stage,
      requestId: requestId,
      latency: latency,
      detail: detail,
    );
  }

  void _invalidatePairTransfer() {
    _pairTransferEpoch++;
    _pairVerification = null;
  }

  // ================= 核心控制接口 =================

  Future<void> startScanAndConnect({bool diagnosticMode = false}) async {
    if (_connectionOperation != null) return;
    rememberedConnection.explicitConnect(diagnostic: diagnosticMode);
    _productSession = !diagnosticMode;
    await _startConnection(diagnosticMode: diagnosticMode);
  }

  Future<void> _startConnection({
    required bool diagnosticMode,
    String? remoteId,
    bool? syncTime,
  }) async {
    if (_connectionOperation != null) return;
    final operation = _scanAndConnect(
      diagnosticMode: diagnosticMode,
      remoteId: remoteId,
      syncTime: syncTime ?? !diagnosticMode,
    );
    _connectionOperation = operation;
    try {
      await operation;
    } finally {
      if (identical(_connectionOperation, operation)) {
        _connectionOperation = null;
      }
    }
  }

  Future<void> _scanAndConnect({
    required bool diagnosticMode,
    String? remoteId,
    required bool syncTime,
  }) async {
    if (!isValidBleAccessCode(bleAccessCode ?? '')) {
      _setPairTransfer(PairTransferStage.failed, detail: '请先设置 6 位设备验证码');
      return;
    }
    if (isScanning.value ||
        _connecting ||
        _disconnecting ||
        isConnected.value) {
      return;
    }
    final epoch = ++_connectionEpoch;
    _diagnosticMode = diagnosticMode;
    _syncTimeAfterPairing = syncTime;
    _invalidatePairTransfer();
    isAuthenticated.value = false;
    _setPairTransfer(PairTransferStage.scanning);
    isScanning.value = true;
    _log("⚙️ SYS", "开始雷达扫描: $targetDeviceName...");

    final found = Completer<BluetoothDevice>();
    _scanFound = found;
    // Install error/timeout handling before any platform await or cancellation.
    final foundDevice = found.future.timeout(const Duration(seconds: 11));
    // Future.wait below attaches both error handlers immediately.
    StreamSubscription<List<ScanResult>>? subscription;
    try {
      final scan = Future<void>.sync(() async {
        subscription = FlutterBluePlus.onScanResults.listen(
          (results) {
            if (found.isCompleted || epoch != _connectionEpoch) return;
            for (ScanResult r in results) {
              // Automatic discovery must never select a different nearby watch.
              if (remoteId != null
                  ? r.device.remoteId.str == remoteId
                  : isCrabitWatchName(r.device.platformName) ||
                      isCrabitWatchName(r.advertisementData.advName)) {
                _log("⚙️ SYS", "🎉 锁定目标！信号强度: ${r.rssi}dBm");
                found.complete(r.device);
                return;
              }
            }
          },
          onError: (Object _) {
            if (epoch == _connectionEpoch && !found.isCompleted) {
              found.completeError(
                const TransportException(TransportErrorCode.notConnected),
              );
            }
          },
        );
        _scanSubscription = subscription;
        await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
      });
      final results = await Future.wait<Object?>([
        scan,
        foundDevice,
      ], eagerError: true);
      final device = results[1] as BluetoothDevice;
      if (epoch != _connectionEpoch) return;
      await FlutterBluePlus.stopScan();
      if (epoch != _connectionEpoch) return;
      isScanning.value = false;
      await _connectToDevice(device, epoch);
    } on TimeoutException {
      if (epoch == _connectionEpoch) {
        _setPairTransfer(PairTransferStage.failed, detail: "未找到 Crabit-Watch");
      }
    } catch (_) {
      if (epoch == _connectionEpoch) {
        _setPairTransfer(PairTransferStage.failed, detail: "扫描或连接失败");
      }
    } finally {
      await subscription?.cancel();
      if (identical(_scanSubscription, subscription)) {
        _scanSubscription = null;
        isScanning.value = false;
      }
      if (identical(_scanFound, found)) _scanFound = null;
      if (epoch == _connectionEpoch) {
        try {
          await FlutterBluePlus.stopScan();
        } catch (_) {
          /* No device mutation. */
        }
      }
    }
  }

  Future<void> disconnect({bool checked = false}) async {
    // Synchronous revocation precedes ALL native awaits and cancels backoff too.
    rememberedConnection.stopByUser();
    try {
      await _teardownFence.run(_disconnectNative);
    } on TransportException catch (error) {
      _setPairTransfer(
        PairTransferStage.failed,
        detail: '断开未完成：${error.code.name}',
      );
      if (checked) rethrow;
    }
  }

  Future<void> _disconnectNative() async {
    _disconnecting = true;
    ++_connectionEpoch;
    _connecting = false;
    _transport.close();
    _invalidatePairTransfer();
    isAuthenticated.value = false;
    isConnected.value = false;
    _rxCharacteristic = null;
    _txCharacteristic = null;
    final device = _watchDevice;
    _watchDevice = null;
    final scanning = isScanning.value;
    isScanning.value = false;
    final found = _scanFound;
    _scanFound = null;
    if (found != null && !found.isCompleted) {
      found.completeError(
        const TransportException(TransportErrorCode.disconnected),
      );
    }
    try {
      await _scanSubscription?.cancel();
      _scanSubscription = null;
      await _notifySubscription?.cancel();
      _notifySubscription = null;
      await _connectionSubscription?.cancel();
      _connectionSubscription = null;
      _setPairTransfer(PairTransferStage.idle, detail: '已断开');
      if (scanning) await FlutterBluePlus.stopScan();
      await device?.disconnect();
      await _transport.waitForIdle();
    } on TransportException {
      _setPairTransfer(PairTransferStage.failed, detail: '旧通信操作尚未释放');
      rethrow;
    } catch (_) {
      _setPairTransfer(PairTransferStage.failed, detail: '断开连接失败');
      throw const TransportException(TransportErrorCode.disconnected);
    } finally {
      _disconnecting = false;
    }
  }

  // ================= 内部连接与解析逻辑 =================

  Future<void> _connectToDevice(BluetoothDevice device, int epoch) async {
    _connecting = true;
    _watchDevice = device;
    _setPairTransfer(PairTransferStage.connecting);
    try {
      await device.connect(
        autoConnect: false,
        timeout: const Duration(seconds: 12),
        // 核心修改：使用官方实际存在的免费授权枚举
        license: License.free,
      );
      if (epoch != _connectionEpoch) {
        await device.disconnect();
        return;
      }
      isConnected.value = true;

      // 向平台请求更大的 BLE MTU，减少 20 字节写入分片
      if (Platform.isAndroid) {
        try {
          await device.requestMtu(512); // 请求 MTU 为 512，实际结果由平台与对端协商。
          _log("⚙️ SYS", "MTU 提速至 512 字节！");
        } catch (e) {
          _log("⚠️ SYS", "MTU 提速失败，使用默认速率");
        }
      }
      if (epoch != _connectionEpoch) return;

      await _connectionSubscription?.cancel();
      if (epoch != _connectionEpoch) return;
      _connectionSubscription = device.connectionState.listen((state) {
        if (epoch != _connectionEpoch) return;
        if (state == BluetoothConnectionState.disconnected) {
          final wasVerified = isReady && _productSession;
          ++_connectionEpoch;
          _connecting = false;
          isConnected.value = false;
          isAuthenticated.value = false;
          _rxCharacteristic = null;
          _txCharacteristic = null;
          _transport.close();
          _invalidatePairTransfer();
          _setPairTransfer(
            PairTransferStage.failed,
            detail: '设备已断开；检查验证码和手表 BLE LINK 后手动重试',
          );
          _log("❌ SYS", "物理链路断开！");
          rememberedConnection.connectionLost(wasVerified: wasVerified);
        }
      });

      if (Platform.isAndroid) {
        try {
          await device.clearGattCache();
          await Future.delayed(const Duration(milliseconds: 1500));
        } catch (_) {}
      }
      if (epoch != _connectionEpoch) return;

      List<BluetoothService> services = await device.discoverServices();
      if (epoch != _connectionEpoch) return;
      for (BluetoothService service in services) {
        if (service.uuid.toString() == serviceUuid) {
          for (BluetoothCharacteristic c in service.characteristics) {
            if (c.uuid.toString() == rxUuid) _rxCharacteristic = c;
            if (c.uuid.toString() == txUuid) {
              _txCharacteristic = c;
              await _txCharacteristic!.setNotifyValue(true);
              if (epoch != _connectionEpoch) return;
            }
          }
        }
      }
      if (_rxCharacteristic != null && _txCharacteristic != null) {
        final rx = _rxCharacteristic!;
        if (bleTimingEnabled) {
          debugPrint(
              '[BLE_TIMING] connected mtu=${device.mtuNow} uploadChunk=20');
        }
        _transport.open(
          (chunk) async {
            final watch = bleTimingEnabled ? (Stopwatch()..start()) : null;
            try {
              await rx.write(chunk, withoutResponse: false, timeout: 4);
            } finally {
              if (watch != null) {
                debugPrint(
                    '[BLE_TIMING] write bytes=${chunk.length} us=${watch.elapsedMicroseconds} mtu=${device.mtuNow}');
              }
            }
          },
        );
        _setupNotifyListener(epoch);
        _log("✅ SYS", "全部通道就绪！");
        final code = bleAccessCode;
        if (code == null) {
          throw const TransportException(TransportErrorCode.invalidPayload);
        }
        _setPairTransfer(PairTransferStage.authenticating, detail: '正在验证设备码');
        if (bleTimingEnabled) _log('AUTH', 'write_started');
        await _transport.sendRaw(
          bleAuthenticationCommand(code),
          requireAuth: false,
          timeout: const Duration(seconds: 2),
        );
        if (bleTimingEnabled) _log('AUTH', 'write_completed; awaiting_result');
      } else {
        rememberedConnection.fault();
        await _cancelAutomaticConnection();
        if (epoch + 1 != _connectionEpoch) return;
        _setPairTransfer(
          PairTransferStage.failed,
          detail: "未找到 Crabit GATT 通道",
        );
      }
    } catch (error) {
      if (bleTimingEnabled) {
        _log('CONNECT', 'failed type=${error.runtimeType}');
      }
      if (epoch == _connectionEpoch) {
        try {
          await _cancelAutomaticConnection();
        } catch (_) {
          /* State is already invalidated. */
        }
        if (epoch + 1 != _connectionEpoch) return;
        _setPairTransfer(PairTransferStage.failed, detail: "连接或 GATT 初始化失败");
      }
    } finally {
      if (epoch == _connectionEpoch) _connecting = false;
    }
  }

  void _setupNotifyListener(int epoch) {
    _notifySubscription?.cancel();
    _notifySubscription = _txCharacteristic!.onValueReceived.listen((value) {
      if (epoch != _connectionEpoch || !_transport.connected) return;
      if (bleTimingEnabled) {
        debugPrint(
            '[BLE_TIMING] notify bytes=${value.length} atUs=${DateTime.now().microsecondsSinceEpoch}');
      }
      try {
        for (final json in _transport.decoder.add(value)) {
          _transport.accept(json);
          _handleIncomingJson(json);
        }
      } on TransportException {
        rememberedConnection.fault();
        _transport.close(TransportErrorCode.protocolError);
        _invalidatePairTransfer();
        isAuthenticated.value = false;
        _setPairTransfer(PairTransferStage.failed, detail: '通知数据格式错误');
        // Do not hide the failing device state by reconnecting/resetting it.
      }
    });
  }

  void _handleIncomingJson(Map<String, dynamic> json) {
    final String? topic = json['t']?.toString();
    if (topic == null) return;

    if (topic == 'auth') {
      if (bleTimingEnabled) {
        _log('AUTH',
            'result_accepted=${json['v']?.toString() == '1'} connected=${isConnected.value} transport=${_transport.connected}');
      }
      if (json['v']?.toString() != '1' ||
          !isConnected.value ||
          !_transport.connected) {
        return;
      }
      isAuthenticated.value = true;
      _transport.authenticated = true;
      if (diagnosticMode) {
        _setPairTransfer(PairTransferStage.diagnosticReady,
            detail: '验证码通过；诊断请求已开放');
      } else {
        _setPairTransfer(PairTransferStage.authenticated);
        unawaited(_completeProductConnection());
      }
      return;
    }

    if (topic == "ack") {
      return;
    }

    if (topic.startsWith("diag/")) {
      final event = DiagnosticEvent(
        topic: topic,
        value: json['v']?.toString() ?? "",
        requestId: json['i']?.toString(),
      );
      _diagnosticStreamController.add(event);
      return;
    }

    if (topic == "sys/notify") {
      return;
    }

    // 处理来自手表 C2_UPLINK 的 WiFi 探针侦听数据
    if (topic == "wf/prb") {
      StorageManager().saveProbe(json); // 持久化到 Hive
      _probeStreamController.add(ProbeEvent.fromJson(json));
      return;
    }

    // 处理 WPA2 握手包截获数据
    if (topic == "wf/eapol") {
      StorageManager().saveEapol(json); // 持久化到 Hive
      _eapolStreamController.add(EapolEvent.fromJson(json));
      return;
    }

    if (topic == "sys/ble") {
      String val = json['v']?.toString() ?? "";
      if (val == 'STOPPED') {
        // The watch explicitly closed communication. Do not treat its following
        // physical power-off as an unexpected loss and immediately reconnect.
        rememberedConnection.stopByUser();
        isAuthenticated.value = false;
        _transport.close(TransportErrorCode.disconnected);
        _invalidatePairTransfer();
        _setPairTransfer(PairTransferStage.idle, detail: '手表已关闭通信');
        return;
      }
      if (val == "PAIR_CONFIRM_ON_WATCH") {
        _setPairTransfer(PairTransferStage.waitingForWatch);
      } else if (val == "AUTH_FAIL" ||
          val == "CONN_LOCK" ||
          val == "PAIR_TIMEOUT" ||
          val == "PAIR_CANCELLED") {
        if (bleTimingEnabled) _log('AUTH', 'device_rejected=$val');
        rememberedConnection.fault();
        isAuthenticated.value = false;
        _transport.close(TransportErrorCode.notAuthenticated);
        _invalidatePairTransfer();
        _setPairTransfer(PairTransferStage.failed, detail: val);
      }
    }
  }

  Future<void> _completeProductConnection() async {
    final epoch = _connectionEpoch;
    await _verifyProductPath();
    if (epoch == _connectionEpoch && isReady && _productSession) {
      final id = _watchDevice?.remoteId.str;
      if (id != null) {
        await rememberedConnection.rememberVerified(
          id,
          () => epoch == _connectionEpoch && isReady && _productSession,
        );
      }
    }
    if (epoch == _connectionEpoch && isReady && _syncTimeAfterPairing) {
      await sendReliableJson(
        "sys/time",
        DateTime.now().toString().substring(0, 19),
      );
    }
    if (epoch == _connectionEpoch && isReady && _productSession) {
      unawaited(syncAuditRecords());
    }
  }

  final auditSyncStatus = ValueNotifier<String>('连接后自动同步审计记录');
  int? _auditSyncEpoch;

  Future<void> syncAuditRecords() async {
    final epoch = _connectionEpoch;
    if (!isReady ||
        diagnosticMode ||
        !_productSession ||
        _auditSyncEpoch == epoch) {
      return;
    }
    final device = _watchDevice?.remoteId.str;
    if (device == null) return;
    _auditSyncEpoch = epoch;
    bool current() => epoch == _connectionEpoch && isReady && !diagnosticMode;
    auditSyncStatus.value = '正在同步主控暂存记录';
    try {
      final result = await AuditInbox.sync(
        request: (topic, value) async =>
            (await requestDeviceChecked(topic, value)).value,
        save: (record) => StorageManager().saveAuditRecord(device, record),
        isCurrent: current,
        onSaved: (record) {
          if (record['t'] == 'wf/prb') {
            _probeStreamController.add(ProbeEvent.fromJson(record));
          } else {
            _eapolStreamController.add(EapolEvent.fromJson(record));
          }
        },
      );
      if (current()) {
        auditSyncStatus.value =
            '同步完成：新增 ${result.saved} 条，主控拒收 ${result.dropped} 次';
      }
    } catch (_) {
      if (current()) auditSyncStatus.value = '同步未完成，可重试；主控未确认记录仍保留';
    } finally {
      if (_auditSyncEpoch == epoch) _auditSyncEpoch = null;
    }
  }

  Future<void> verifyPairTransfer() async {
    if (!isAuthenticated.value || _rxCharacteristic == null) {
      _setPairTransfer(PairTransferStage.failed, detail: '设备验证码尚未通过');
      return;
    }
    if (_pairVerification != null) return _pairVerification;
    final operation =
        _productSession ? _verifyProductPath() : _verifyPairTransfer();
    _pairVerification = operation;
    try {
      await operation;
    } finally {
      if (identical(_pairVerification, operation)) _pairVerification = null;
    }
  }

  Future<void> _verifyProductPath() async {
    if (!isAuthenticated.value || !_transport.connected) {
      _setPairTransfer(PairTransferStage.failed, detail: '设备验证码尚未通过');
      return;
    }
    final int connectionEpoch = _connectionEpoch;
    final int operationEpoch = ++_pairTransferEpoch;
    const value = 'crabit-ready';
    final Stopwatch stopwatch = Stopwatch()..start();
    _setPairTransfer(PairTransferStage.verifying);
    if (bleTimingEnabled) _log('STM32_ECHO', 'request_started budget_ms=4000');
    try {
      final reply = await _transport.request(
        'diag/echo',
        value,
        timeout: const Duration(seconds: 4),
      );
      if (operationEpoch != _pairTransferEpoch ||
          connectionEpoch != _connectionEpoch) {
        return;
      }
      if (reply.topic != 'diag/echo' || reply.value != value) {
        throw const TransportException(TransportErrorCode.protocolError);
      }
      stopwatch.stop();
      if (bleTimingEnabled) {
        _log('STM32_ECHO',
            'verified elapsed_ms=${stopwatch.elapsedMilliseconds}');
      }
      _setPairTransfer(
        PairTransferStage.verified,
        requestId: reply.requestId,
        latency: stopwatch.elapsed,
      );
    } on TransportException catch (error) {
      if (bleTimingEnabled) {
        _log('STM32_ECHO', 'failed code=${error.code.name}');
      }
      if (operationEpoch == _pairTransferEpoch &&
          connectionEpoch == _connectionEpoch) {
        rememberedConnection.fault();
        _setPairTransfer(
          PairTransferStage.failed,
          detail: 'STM32 回显验证失败：${error.code.name}',
        );
      }
    }
  }

  Future<void> _verifyPairTransfer() async {
    final int epoch = ++_pairTransferEpoch;
    final String requestId = (_rnd.nextInt(900000) + 100000).toString();
    final String probe = pairTransferProbeForRequestId(requestId);
    final Stopwatch stopwatch = Stopwatch()..start();
    _setPairTransfer(PairTransferStage.verifying, requestId: requestId);

    try {
      await _transport.verifyEcho(probe);
      if (epoch != _pairTransferEpoch) return;
      stopwatch.stop();
      _setPairTransfer(
        PairTransferStage.verified,
        requestId: requestId,
        latency: stopwatch.elapsed,
      );
    } on TransportException catch (error) {
      if (epoch == _pairTransferEpoch) {
        rememberedConnection.fault();
        _setPairTransfer(
          PairTransferStage.failed,
          detail: '传输验证失败：${error.code.name}',
        );
      }
    }
  }

  // ================= 发送层 =================

  Future<void> sendRawData(String data) async {
    if (!isProductReady) {
      _log('BLOCKED', 'raw command before product session is verified');
      return;
    }
    try {
      await _transport.sendRaw(data);
    } on TransportException catch (error) {
      // Compatibility API: legacy fire-and-forget callers do not receive an
      // unhandled error. New request APIs below retain the precise failure.
      _log('TX', error.code.name);
    }
  }

  Future<bool> sendReliableJson(
    String topic,
    String value, {
    int maxRetries = 0,
  }) async {
    if (!isProductReady) return false;

    // ACK means transport receipt, not successful business execution. Do not
    // replay side-effecting commands on lost ACK: firmware has no deduplication.
    // Preserve the named argument for source compatibility, but explicitly
    // reject replay requests rather than silently ignoring a requested policy.
    if (maxRetries != 0) return false;
    try {
      await _transport.request(
        topic,
        value,
        ackOnly: true,
        requireAuth: true,
      );
      return true;
    } on TransportException catch (error) {
      _log('ACK', error.code.name);
      return false;
    }
  }

  Future<DiagnosticEvent> requestDiagnosticChecked(
    String topic,
    String value, {
    Duration timeout = const Duration(seconds: 4),
  }) async {
    if (!const {
      'diag/echo',
      'diag/snapshot',
      'diag/wifi',
      'diag/imu',
    }.contains(topic)) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    if (!_transport.connected) {
      throw const TransportException(TransportErrorCode.notConnected);
    }
    if (!isReady) {
      throw const TransportException(TransportErrorCode.notAuthenticated);
    }
    final epoch = _connectionEpoch;
    try {
      final reply = await _transport.request(topic, value,
          timeout: timeout, requireAuth: true);
      return DiagnosticEvent(
        topic: reply.topic,
        value: reply.value,
        requestId: reply.requestId,
      );
    } on TransportException catch (error) {
      // A stale/cancelled read must not fault a new or reconnecting session.
      if (epoch == _connectionEpoch &&
          !const {
            TransportErrorCode.disconnected,
            TransportErrorCode.notConnected,
            TransportErrorCode.notAuthenticated,
          }.contains(error.code)) {
        rememberedConnection.fault();
      }
      rethrow;
    }
  }

  /// Correlated product settings and RAM USB script service requests.
  /// This deliberately does not accept ACK-only commands or arbitrary topics.
  Future<DiagnosticEvent> requestDeviceChecked(
    String topic,
    String value, {
    Duration timeout = const Duration(seconds: 4),
  }) async {
    if (!const {
      'sys/settings',
      'sys/settings/set',
      'sys/prefs',
      'sys/prefs/set',
      'diag/battery',
      'app/usb',
      'app/weather',
      'audit/read',
      'audit/ack',
    }.contains(topic)) {
      throw const TransportException(TransportErrorCode.invalidPayload);
    }
    if (diagnosticMode) {
      throw const TransportException(TransportErrorCode.notAuthenticated);
    }
    if (!_transport.connected) {
      throw const TransportException(TransportErrorCode.notConnected);
    }
    if (!isReady) {
      throw const TransportException(TransportErrorCode.notAuthenticated);
    }
    final reply = await _transport.request(topic, value, timeout: timeout);
    return DiagnosticEvent(
        topic: reply.topic, value: reply.value, requestId: reply.requestId);
  }

  Future<void> verifyBleEchoChecked() {
    _productSession = false;
    rememberedConnection.enterDiagnostics();
    return _transport.verifyEcho(
      pairTransferProbeForRequestId((_rnd.nextInt(900000) + 100000).toString()),
      requireAuth: true,
    );
  }

  Future<void> reconnectForDiagnostics() async {
    if (!_diagnosticMode) {
      throw const TransportException(TransportErrorCode.busy);
    }
    await prepareDiagnosticConnection();
  }

  Future<void> prepareDiagnosticConnection() async {
    _productSession = false;
    rememberedConnection.enterDiagnostics();
    await disconnect(checked: true);
    final epoch = _connectionEpoch;
    // A stale native connect must finish/disconnect before another one starts.
    final previous = _connectionOperation;
    if (previous != null) {
      try {
        await previous.timeout(const Duration(seconds: 15));
      } on TimeoutException {
        throw const TransportException(TransportErrorCode.busy);
      }
    }
    if (epoch != _connectionEpoch) {
      throw const TransportException(TransportErrorCode.disconnected);
    }
    await startScanAndConnect(diagnosticMode: true);
    await waitForReady(
        timeout: bleAuthenticationTimeout + const Duration(seconds: 5));
  }

  Future<void> waitForReady({
    Duration timeout = const Duration(seconds: 35),
  }) async {
    final stopwatch = Stopwatch()..start();
    while (!isReady) {
      if (pairTransferStatus.value.stage == PairTransferStage.failed ||
          pairTransferStatus.value.stage == PairTransferStage.idle) {
        throw const TransportException(TransportErrorCode.notAuthenticated);
      }
      if (stopwatch.elapsed >= timeout) {
        if (bleTimingEnabled) {
          _log('READY_TIMEOUT',
              'stage=${pairTransferStatus.value.stage.name} connected=${isConnected.value} authenticated=${isAuthenticated.value} transport=${_transport.connected} elapsed_ms=${stopwatch.elapsedMilliseconds}');
        }
        throw const TransportException(TransportErrorCode.timeout);
      }
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  }

  Future<DiagnosticEvent?> requestDiagnostic(
    String topic,
    String value, {
    Duration timeout = const Duration(seconds: 4),
  }) async {
    try {
      return await requestDiagnosticChecked(topic, value, timeout: timeout);
    } on TransportException {
      return null;
    }
  }
}
