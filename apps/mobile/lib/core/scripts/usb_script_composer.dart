/// Insert a fragment as complete lines without replacing the surrounding draft.
/// An invalid selection (e.g. an editor not yet focused) means append.
({String text, int cursor}) insertUsbScriptFragment(
  String draft,
  String fragment, {
  int start = -1,
  int end = -1,
}) {
  final body = fragment
      .replaceAll('\r\n', '\n')
      .replaceAll('\r', '\n')
      .replaceAll(RegExp(r'^\n+|\n+$'), '');
  if (body.isEmpty) return (text: draft, cursor: draft.length);
  if (start < 0 || end < start || end > draft.length) {
    start = end = draft.length;
  }
  final before = draft.substring(0, start);
  final after = draft.substring(end);
  final leading = before.isNotEmpty && !before.endsWith('\n') ? '\n' : '';
  final trailing = after.startsWith('\n') ? '' : '\n';
  final inserted = '$leading$body$trailing';
  return (
    text: '$before$inserted$after',
    cursor: before.length + inserted.length
  );
}
