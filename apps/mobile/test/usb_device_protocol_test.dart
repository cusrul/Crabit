import 'dart:async';

import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/usb/usb_device_client.dart';
import 'package:crabit_watch_app/core/usb/usb_device_gateway.dart';
import 'package:crabit_watch_app/core/usb/usb_script_protocol.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crabit_watch_app/presentation/pages/tools/badusb_page.dart';
import 'package:crabit_watch_app/core/scripts/usb_script.dart';
import 'package:crabit_watch_app/core/scripts/usb_script_repository.dart';

void main() {
  group('validateUsbScript', () {
    test('accepts supported ASCII actions and counts limits', () {
      final script = validateUsbScript(
        'STRING Hello!\r\nDELAY 100\nCTRL c\nGUI ENTER\nUP',
      );

      expect(script.bytes,
          'STRING Hello!\nDELAY 100\nCTRL c\nGUI ENTER\nUP'.codeUnits);
      expect(script.actions, 10);
      expect(script.totalDurationMs, 460);
    });

    test('rejects non-ASCII, unsupported commands and invalid combinations',
        () {
      for (final source in [
        'STRING 你好',
        'DELAY 30001',
        'DELAY 20000\nDELAY 10001',
        'MOUSE MOVE',
        'CTRL ALT x',
        'GUI NOT_A_KEY',
        'STRING',
      ]) {
        expect(() => validateUsbScript(source),
            throwsA(isA<UsbScriptValidationException>()),
            reason: source);
      }
    });

    test('enforces 512 bytes and 128 actions', () {
      expect(
        () => validateUsbScript('STRING ${'a' * 506}'),
        throwsA(isA<UsbScriptValidationException>()),
      );
      expect(
        () => validateUsbScript(List.filled(129, 'UP').join('\n')),
        throwsA(isA<UsbScriptValidationException>()),
      );
      expect(validateUsbScript(List.filled(128, 'UP').join('\n')).actions, 128);
    });

    test('counts STRING characters as actions and enforces 128-byte lines', () {
      final maxLine = validateUsbScript('STRING ${'a' * 121}');
      expect(maxLine.bytes.length, 128);
      expect(maxLine.actions, 121);
      expect(maxLine.totalDurationMs, 121 * usbScriptKeyRuntimeMs);
      expect(
        () => validateUsbScript('STRING ${'a' * 122}'),
        throwsA(isA<UsbScriptValidationException>()),
      );

      final maxActions =
          validateUsbScript('STRING ${'a' * 121}\nSTRING ${'b' * 7}');
      expect(maxActions.actions, 128);
      expect(
        () => validateUsbScript('STRING ${'a' * 121}\nSTRING ${'b' * 8}'),
        throwsA(isA<UsbScriptValidationException>()),
      );
    });

    test('includes per-key time when enforcing the 30-second runtime budget',
        () {
      final atLimit = validateUsbScript('DELAY 29960\nCTRL a');
      expect(atLimit.totalDurationMs, 30000);
      expect(
        () => validateUsbScript('DELAY 29961\nCTRL a'),
        throwsA(isA<UsbScriptValidationException>()),
      );
    });

    test('accepts canonical uppercase and lowercase modifier chords', () {
      final script = validateUsbScript('CTRL A\nGUI r');

      expect(script.bytes, 'CTRL A\nGUI r'.codeUnits);
      expect(script.actions, 2);
      expect(script.totalDurationMs, 2 * usbScriptKeyRuntimeMs);
    });
  });

  group('UsbDeviceReply', () {
    test('parses strict business fields and preserves request_id', () {
      final reply = UsbDeviceReply.fromEvent(_event(
        'result=snapshot;state=ready;id=1a2b3c4d;received=12;length=12;error=none;usb=1',
      ));

      expect(reply.requestId, '42');
      expect(reply.result, UsbDeviceResult.snapshot);
      expect(reply.state, UsbRamState.ready);
      expect(reply.id, '1a2b3c4d');
      expect(reply.usbConnected, isTrue);
    });

    test('rejects uncorrelated, wrong-topic and malformed replies', () {
      expect(
        () => UsbDeviceReply.fromEvent(_event(
          'result=snapshot;state=empty;id=00000000;received=0;length=0;error=none;usb=0',
          requestId: null,
        )),
        throwsFormatException,
      );
      expect(
        () => UsbDeviceReply.fromEvent(_event('result=snapshot', topic: 'x')),
        throwsFormatException,
      );
      for (final value in [
        'result=ack;state=ready;id=1a2b3c4d;received=1;length=1;error=none;usb=0',
        'result=snapshot;state=ready;id=ABCDEF12;received=1;length=1;error=none;usb=0',
        'result=snapshot;state=ready;id=1a2b3c4d;received=2;length=1;error=none;usb=0',
        'result=snapshot;state=ready;id=1a2b3c4d;received=1;length=1;error=none;usb=0;extra=x',
        'result=snapshot;state=ready;id=1a2b3c4d;received=1;length=1;error=none;usb=0;usb=1',
      ]) {
        expect(() => UsbDeviceReply.fromEvent(_event(value)),
            throwsFormatException,
            reason: value);
      }

      for (final requestId in ['', '0', '65536', '-1', '1.0', 'abc']) {
        expect(
          () => UsbDeviceReply.fromEvent(_event(
            'result=snapshot;state=empty;id=00000000;received=0;length=0;error=none;usb=0',
            requestId: requestId,
          )),
          throwsFormatException,
          reason: requestId,
        );
      }
      expect(
        UsbDeviceReply.fromEvent(_event(
          'result=snapshot;state=empty;id=00000000;received=0;length=0;error=none;usb=0',
          requestId: '65535',
        )).requestId,
        '65535',
      );
      expect(
        UsbDeviceReply.fromEvent(_event(
          'result=snapshot;state=empty;id=00000000;received=0;length=0;error=none;usb=0',
          requestId: '1',
        )).requestId,
        '1',
      );
    });
  });

  group('UsbDeviceClient', () {
    test('serializes begin, 64-byte chunks, and commit without running',
        () async {
      final gateway = _FakeGateway();
      final client = UsbDeviceClient(
        gateway: gateway,
        idGenerator: () => '1a2b3c4d',
      );
      final source = List.filled(60, 'UP').join('\n');
      final reply = await client.upload(source);

      expect(reply.state, UsbRamState.ready);
      expect(reply.requestId, '5');
      expect(gateway.commands.first, 'begin:1a2b3c4d:${source.length}');
      expect(gateway.commands[1], startsWith('chunk:1a2b3c4d:0:'));
      expect(gateway.commands[2], startsWith('chunk:1a2b3c4d:64:'));
      expect(gateway.commands[3], startsWith('chunk:1a2b3c4d:128:'));
      expect(gateway.commands.last, 'commit:1a2b3c4d');
      expect(gateway.commands.where((command) => command.startsWith('run:')),
          isEmpty);
      expect(client.canRun(reply.id), isTrue);
    });

    test('run is single-use per uploaded id and terminal states need new id',
        () async {
      var nextId = 0;
      final gateway = _FakeGateway();
      final client = UsbDeviceClient(
        gateway: gateway,
        idGenerator: () => (++nextId).toRadixString(16).padLeft(8, '0'),
      );
      final first = await client.upload('ENTER');
      expect(client.canRun(first.id), isTrue);

      gateway.usbConnected = false;
      await client.status();
      expect(client.canRun(first.id), isFalse);
      await expectLater(client.run(first.id), throwsStateError);
      expect(gateway.commands.where((command) => command.startsWith('run:')),
          isEmpty,
          reason: 'offline USB must not consume the one-shot attempt');
      gateway.usbConnected = true;
      await client.status();

      final accepted = await client.run(first.id);
      expect(accepted.result, UsbDeviceResult.queued);
      expect(accepted.state, UsbRamState.running);
      expect(client.canRun(first.id), isFalse);
      await expectLater(client.run(first.id), throwsStateError);
      expect(gateway.commands.where((command) => command == 'run:${first.id}'),
          hasLength(1));

      final stopped = await client.stop(first.id);
      expect(stopped.state, UsbRamState.stopped);
      expect(client.canRun(first.id), isFalse);

      final second = await client.upload('TAB');
      expect(second.id, isNot(first.id));
      expect(client.canRun(second.id), isTrue);
    });

    test('does not retry or commit after a chunk failure', () async {
      final gateway = _FakeGateway(failOnCommand: 'chunk:');
      final client = UsbDeviceClient(
        gateway: gateway,
        idGenerator: () => 'a1b2c3d4',
      );

      await expectLater(client.upload('STRING ${'b' * 80}'), throwsStateError);
      expect(gateway.commands, hasLength(2));
      expect(gateway.commands.last, startsWith('chunk:'));
      expect(gateway.commands.where((command) => command.startsWith('commit:')),
          isEmpty);
    });

    test('requires queued/running/error-none reply and stays one-shot on error',
        () async {
      final cases = [
        (_FakeGateway()..runResult = UsbDeviceResult.accepted),
        (_FakeGateway()..runState = UsbRamState.ready),
        (_FakeGateway()..runError = 'unavailable'),
        (_FakeGateway()..runIdOverride = 'deadbeef'),
      ];
      for (final gateway in cases) {
        final client = UsbDeviceClient(
          gateway: gateway,
          idGenerator: () => 'a1b2c3d4',
        );
        final ready = await client.upload('ENTER');
        await expectLater(client.run(ready.id), throwsFormatException);
        await expectLater(client.run(ready.id), throwsStateError);
        expect(
            gateway.commands.where((command) => command == 'run:${ready.id}'),
            hasLength(1));
      }
    });

    test('accepts only standardized stop result/state pairs without errors',
        () async {
      final validCases = [
        (
          _FakeGateway()
            ..stopResult = UsbDeviceResult.accepted
            ..stopState = UsbRamState.stopping,
          UsbDeviceResult.accepted,
          UsbRamState.stopping,
        ),
        (
          _FakeGateway()
            ..stopResult = UsbDeviceResult.stopped
            ..stopState = UsbRamState.stopped,
          UsbDeviceResult.stopped,
          UsbRamState.stopped,
        ),
      ];
      for (final (gateway, result, state) in validCases) {
        final client = UsbDeviceClient(
          gateway: gateway,
          idGenerator: () => 'a1b2c3d4',
        );
        final ready = await client.upload('ENTER');
        await client.run(ready.id);

        final stopped = await client.stop(ready.id);
        expect(stopped.result, result);
        expect(stopped.state, state);
      }

      final invalidCases = [
        _FakeGateway()
          ..stopResult = UsbDeviceResult.accepted
          ..stopState = UsbRamState.stopped,
        _FakeGateway()
          ..stopResult = UsbDeviceResult.stopped
          ..stopState = UsbRamState.stopping,
        _FakeGateway()..stopError = 'unavailable',
        _FakeGateway()..stopIdOverride = 'deadbeef',
      ];
      for (final gateway in invalidCases) {
        final client = UsbDeviceClient(
          gateway: gateway,
          idGenerator: () => 'a1b2c3d4',
        );
        final ready = await client.upload('ENTER');
        await client.run(ready.id);

        await expectLater(client.stop(ready.id), throwsFormatException);
      }
    });

    test('rejects overlapping operations instead of queueing them', () async {
      final gate = Completer<void>();
      final gateway = _FakeGateway(beforeReply: (_) => gate.future);
      final client = UsbDeviceClient(gateway: gateway);
      final first = client.status();
      await Future<void>.delayed(Duration.zero);
      await expectLater(client.status(), throwsStateError);
      gate.complete();
      await first;
    });
  });

  for (final failure in <String?>[null, 'chunk:', 'run:']) {
    testWidgets('local script one-click workflow failure=$failure',
        (tester) async {
      final gateway = _FakeGateway(failOnCommand: failure);
      await tester.pumpWidget(MaterialApp(
          home: BadUsbPage(
              repository: _MemoryUsbRepository(), gateway: gateway)));
      await tester.pumpAndSettle();
      await tester.tap(find.text('本地脚本'));
      await tester.pumpAndSettle();
      await tester
          .ensureVisible(find.byKey(const ValueKey('script-example-text')));
      await tester.pumpAndSettle();
      await tester
          .ensureVisible(find.byKey(const ValueKey('run-local-example-text')));
      await tester.tap(find.byKey(const ValueKey('run-local-example-text')));
      await tester.pumpAndSettle();
      expect(gateway.commands, isEmpty);
      await tester.tap(find.widgetWithText(FilledButton, '运行一次'));
      await tester.pumpAndSettle();
      expect(gateway.commands.first, 'status');
      final runs = gateway.commands.where((c) => c.startsWith('run:'));
      expect(runs.length, failure == 'chunk:' ? 0 : 1);
      if (failure == 'run:') {
        expect(find.textContaining('结果未知'), findsOneWidget);
        await tester.tap(find.widgetWithText(OutlinedButton, '停止').first);
        await tester.pumpAndSettle();
        expect(gateway.commands.any((c) => c.startsWith('stop:')), isTrue);
      }
      await tester.pumpWidget(const SizedBox());
    });
  }

  testWidgets('one-click aborts before upload when USB is disconnected',
      (tester) async {
    final gateway = _FakeGateway()..usbConnected = false;
    await tester.pumpWidget(MaterialApp(
        home:
            BadUsbPage(repository: _MemoryUsbRepository(), gateway: gateway)));
    await tester.pumpAndSettle();
    await tester.enterText(
        find.byKey(const ValueKey('usb-script-editor')), 'ENTER');
    await tester.tap(find.widgetWithText(FilledButton, '运行到 Crabit').first);
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(FilledButton, '运行一次'));
    await tester.pumpAndSettle();
    expect(gateway.commands, ['status']);
    expect(find.textContaining('USB 尚未连接'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('one-click cancellation sends nothing', (tester) async {
    final gateway = _FakeGateway();
    await tester.pumpWidget(MaterialApp(
        home:
            BadUsbPage(repository: _MemoryUsbRepository(), gateway: gateway)));
    await tester.pumpAndSettle();
    await tester.enterText(
        find.byKey(const ValueKey('usb-script-editor')), 'ENTER');
    await tester.tap(find.widgetWithText(FilledButton, '运行到 Crabit').first);
    await tester.pumpAndSettle();
    await tester.tap(find.text('取消'));
    await tester.pumpAndSettle();
    expect(gateway.commands, isEmpty);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('device tab requires ready state and confirms the host target',
      (tester) async {
    final gateway = _FakeGateway(ready: false);
    final repository = _MemoryUsbRepository();
    await tester.pumpWidget(MaterialApp(
      home: BadUsbPage(repository: repository, gateway: gateway),
    ));
    await tester.pumpAndSettle();
    await tester.tap(find.text('设备 RAM'));
    await tester.pumpAndSettle();

    expect(find.textContaining('512 字节'), findsOneWidget);
    final runButton = tester
        .widget<FilledButton>(find.byKey(const ValueKey('usb-run-button')));
    expect(runButton.onPressed, isNull);

    await tester.tap(find.byKey(const ValueKey('usb-upload-button')));
    await tester.pumpAndSettle();
    expect(gateway.commands, isEmpty,
        reason: 'unverified product connection must not send commands');
  });

  testWidgets('hidden shell page stops USB status polling', (tester) async {
    final gateway = _FakeGateway()
      .._id = '000000ab'
      .._state = UsbRamState.running;
    final visible = ValueNotifier(true);
    await tester.pumpWidget(MaterialApp(
      home: ValueListenableBuilder<bool>(
        valueListenable: visible,
        builder: (context, enabled, child) =>
            TickerMode(enabled: enabled, child: child!),
        child: BadUsbPage(repository: _MemoryUsbRepository(), gateway: gateway),
      ),
    ));
    await tester.pumpAndSettle();
    await tester.tap(find.text('设备 RAM'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('usb-status-button')));
    await tester.pumpAndSettle();
    await tester.pump(const Duration(seconds: 2));
    await tester.pump();
    expect(gateway.commands.length, greaterThan(1));
    visible.value = false;
    await tester.pump();
    final count = gateway.commands.length;
    await tester.pump(const Duration(seconds: 5));
    expect(gateway.commands.length, count);
    await tester.pumpWidget(const SizedBox());
    visible.dispose();
  });

  testWidgets('requires explicit confirmation and runs an uploaded id once',
      (tester) async {
    final gateway = _FakeGateway();
    await tester.pumpWidget(MaterialApp(
      home: BadUsbPage(
        repository: _MemoryUsbRepository(),
        gateway: gateway,
      ),
    ));
    await tester.pumpAndSettle();
    await tester.enterText(
        find.byKey(const ValueKey('usb-script-editor')), 'ENTER');
    expect(
      tester
          .widget<TextField>(find.byKey(const ValueKey('usb-script-editor')))
          .controller!
          .text,
      'ENTER',
    );
    await tester.tap(find.text('设备 RAM'));
    await tester.pumpAndSettle();
    expect(
      tester
          .widget<FilledButton>(find.byKey(const ValueKey('usb-upload-button')))
          .onPressed,
      isNotNull,
    );
    await tester.tap(find.byKey(const ValueKey('usb-upload-button')));
    await tester.pumpAndSettle();

    expect(gateway.commands, isNotEmpty);
    expect(find.text('设备状态：ready'), findsOneWidget);
    expect(find.textContaining('操作未完成'), findsNothing);
    expect(
      tester
          .widget<FilledButton>(find.byKey(const ValueKey('usb-run-button')))
          .onPressed,
      isNotNull,
    );
    await tester.tap(find.byKey(const ValueKey('usb-run-button')));
    await tester.pumpAndSettle();
    expect(find.textContaining('输入焦点放到目标窗口'), findsOneWidget);
    expect(gateway.commands.where((command) => command.startsWith('run:')),
        isEmpty);

    await tester.tap(find.widgetWithText(FilledButton, '运行一次').last);
    await tester.pumpAndSettle();
    expect(gateway.commands.where((command) => command.startsWith('run:')),
        hasLength(1));
    expect(
      tester
          .widget<FilledButton>(find.byKey(const ValueKey('usb-run-button')))
          .onPressed,
      isNull,
    );
  });
}

DiagnosticEvent _event(String value,
        {String topic = usbDeviceTopic, String? requestId = '42'}) =>
    DiagnosticEvent(topic: topic, value: value, requestId: requestId);

class _FakeGateway implements UsbDeviceGateway {
  _FakeGateway({
    bool ready = true,
    this.failOnCommand,
    this.beforeReply,
  }) : _ready = ready;

  final String? failOnCommand;
  final Future<void> Function(String command)? beforeReply;
  final List<String> commands = [];
  final bool _ready;
  bool usbConnected = true;
  UsbDeviceResult runResult = UsbDeviceResult.queued;
  UsbRamState runState = UsbRamState.running;
  String runError = 'none';
  String? runIdOverride;
  UsbDeviceResult stopResult = UsbDeviceResult.stopped;
  UsbRamState stopState = UsbRamState.stopped;
  String stopError = 'none';
  String? stopIdOverride;
  String _id = '00000000';
  int _length = 0;
  int _received = 0;
  int _requestId = 0;
  UsbRamState _state = UsbRamState.empty;

  @override
  bool get isProductReady => _ready;

  @override
  Future<UsbDeviceReply> request(String command) async {
    if (!_ready) throw StateError('not ready');
    commands.add(command);
    await beforeReply?.call(command);
    if (failOnCommand != null && command.startsWith(failOnCommand!)) {
      throw StateError('injected failure');
    }

    var result = UsbDeviceResult.snapshot;
    var error = 'none';
    var replyScriptId = _id;
    if (command.startsWith('begin:')) {
      final fields = command.split(':');
      _id = fields[1];
      replyScriptId = _id;
      _length = int.parse(fields[2]);
      _received = 0;
      _state = UsbRamState.uploading;
      result = UsbDeviceResult.accepted;
    } else if (command.startsWith('chunk:')) {
      final fields = command.split(':');
      _received = int.parse(fields[2]) + fields[3].length ~/ 2;
      result = UsbDeviceResult.accepted;
    } else if (command.startsWith('commit:')) {
      _state = UsbRamState.ready;
      result = UsbDeviceResult.ready;
    } else if (command.startsWith('run:')) {
      _state = runState;
      result = runResult;
      error = runError;
      replyScriptId = runIdOverride ?? _id;
    } else if (command.startsWith('stop:')) {
      _state = stopState;
      result = stopResult;
      error = stopError;
      replyScriptId = stopIdOverride ?? _id;
    } else if (command.startsWith('clear:')) {
      _id = '00000000';
      replyScriptId = _id;
      _length = 0;
      _received = 0;
      _state = UsbRamState.empty;
      result = UsbDeviceResult.cleared;
    }

    return UsbDeviceReply.fromEvent(_event(
      'result=${result.name};state=${_state.name};id=$replyScriptId;received=$_received;length=$_length;error=$error;usb=${usbConnected ? 1 : 0}',
      requestId: (++_requestId).toString(),
    ));
  }
}

class _MemoryUsbRepository implements UsbScriptRepository {
  @override
  Future<void> delete(String id) async {}

  @override
  Future<List<UsbScript>> load() async => [];

  @override
  Future<void> save(UsbScript script) async {}
}
