const int usbScriptMaxBytes = 512;
const int usbScriptMaxLineBytes = 128;
const int usbScriptMaxActions = 128;
const int usbScriptMaxDelayMs = 30000;
const int usbScriptChunkBytes = 64;
const int usbScriptKeyRuntimeMs = 40;

class UsbScriptValidationException implements Exception {
  const UsbScriptValidationException(this.message);

  final String message;

  @override
  String toString() => message;
}

class ValidatedUsbScript {
  const ValidatedUsbScript({
    required this.bytes,
    required this.actions,
    required this.totalDurationMs,
  });

  final List<int> bytes;
  final int actions;
  final int totalDurationMs;
}

ValidatedUsbScript validateUsbScript(String source) {
  for (final codeUnit in source.codeUnits) {
    if (codeUnit > 0x7f ||
        (codeUnit < 0x20 && codeUnit != 0x0a && codeUnit != 0x0d)) {
      throw const UsbScriptValidationException('脚本只能包含 ASCII 文本和换行。');
    }
  }

  final normalized = source.replaceAll('\r\n', '\n').replaceAll('\r', '\n');
  final bytes = normalized.codeUnits;
  if (bytes.isEmpty) {
    throw const UsbScriptValidationException('脚本不能为空。');
  }
  if (bytes.length > usbScriptMaxBytes) {
    throw const UsbScriptValidationException('脚本超过 512 字节限制。');
  }

  const simpleKeys = {
    'ENTER',
    'TAB',
    'ESC',
    'BACKSPACE',
    'SPACE',
    'UP',
    'DOWN',
    'LEFT',
    'RIGHT',
    'DELETE',
    'HOME',
    'END',
  };
  const modifiers = {'CTRL', 'ALT', 'SHIFT', 'GUI'};
  final lines = normalized.split('\n');
  var actions = 0;
  var totalDurationMs = 0;

  for (var index = 0; index < lines.length; index++) {
    final line = lines[index];
    final lineNumber = index + 1;
    if (line.codeUnits.length > usbScriptMaxLineBytes) {
      throw UsbScriptValidationException('第 $lineNumber 行超过 128 字节限制。');
    }
    if (line.isEmpty) continue;
    var lineActions = 1;
    var lineRuntimeMs = usbScriptKeyRuntimeMs;

    if (line.startsWith('STRING ')) {
      final payload = line.substring('STRING '.length);
      if (payload.isEmpty ||
          payload.codeUnits.any((unit) => unit < 0x20 || unit > 0x7e)) {
        throw UsbScriptValidationException(
            '第 $lineNumber 行 STRING 需要可打印 ASCII 文本。');
      }
      lineActions = payload.length;
      lineRuntimeMs = lineActions * usbScriptKeyRuntimeMs;
    } else if (line.startsWith('DELAY ')) {
      final value = line.substring('DELAY '.length);
      if (!RegExp(r'^\d{1,5}$').hasMatch(value)) {
        throw UsbScriptValidationException('第 $lineNumber 行 DELAY 格式无效。');
      }
      final delay = int.parse(value);
      if (delay > usbScriptMaxDelayMs) {
        throw UsbScriptValidationException('第 $lineNumber 行 DELAY 超过 30000。');
      }
      lineRuntimeMs = delay;
    } else if (simpleKeys.contains(line)) {
      // A single key action.
    } else {
      final combination =
          RegExp(r'^(CTRL|ALT|SHIFT|GUI) (\S+)$').firstMatch(line);
      if (combination == null || !modifiers.contains(combination.group(1))) {
        throw UsbScriptValidationException('第 $lineNumber 行命令不受支持。');
      }
      final key = combination.group(2)!;
      final onePrintableKey = key.length == 1 &&
          key.codeUnitAt(0) >= 0x21 &&
          key.codeUnitAt(0) <= 0x7e;
      if (!onePrintableKey && !simpleKeys.contains(key)) {
        throw UsbScriptValidationException('第 $lineNumber 行组合键必须只包含一个受支持的按键。');
      }
    }

    actions += lineActions;
    if (actions > usbScriptMaxActions) {
      throw const UsbScriptValidationException('脚本超过 128 个动作限制。');
    }
    totalDurationMs += lineRuntimeMs;
    if (totalDurationMs > usbScriptMaxDelayMs) {
      throw const UsbScriptValidationException('脚本预计总时长超过 30 秒。');
    }
  }

  if (actions == 0) {
    throw const UsbScriptValidationException('脚本至少需要一个动作。');
  }
  return ValidatedUsbScript(
    bytes: List.unmodifiable(bytes),
    actions: actions,
    totalDurationMs: totalDurationMs,
  );
}
