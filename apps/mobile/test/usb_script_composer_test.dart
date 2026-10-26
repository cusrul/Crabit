import 'package:crabit_watch_app/core/scripts/usb_script_composer.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('compose atomic actions with automatic line boundaries', () {
    var value = insertUsbScriptFragment('', 'STRING hello');
    value = insertUsbScriptFragment(value.text, 'DELAY 100',
        start: value.cursor, end: value.cursor);
    value = insertUsbScriptFragment(value.text, 'ENTER',
        start: value.cursor, end: value.cursor);
    expect(value.text, 'STRING hello\nDELAY 100\nENTER\n');
    expect(value.cursor, value.text.length);
  });
  test('append fallback and multiline fragments do not join commands', () {
    expect(insertUsbScriptFragment('ENTER', '\r\nDELAY 100\r\nENTER\r\n').text,
        'ENTER\nDELAY 100\nENTER\n');
    expect(insertUsbScriptFragment('ENTER\n', 'TAB\n').text, 'ENTER\nTAB\n');
  });
  test('insert and replace selected text without deleting surrounding draft',
      () {
    final inserted =
        insertUsbScriptFragment('ENTER\nTAB', 'DELAY 100', start: 6, end: 6);
    expect(inserted.text, 'ENTER\nDELAY 100\nTAB');
    final replaced =
        insertUsbScriptFragment('ENTER\nTAB', 'SPACE', start: 6, end: 9);
    expect(replaced.text, 'ENTER\nSPACE\n');
  });
}
