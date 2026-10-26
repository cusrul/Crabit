import 'dart:convert';

typedef AuditRequest = Future<String> Function(String topic, String value);
typedef AuditSave = Future<bool> Function(Map<String, dynamic> record);

class AuditSyncResult {
  const AuditSyncResult(this.saved, this.dropped);
  final int saved;
  final int dropped;
}

/// Pull the bounded STM32 RAM inbox. A transport ACK is not durable delivery:
/// delete a record only after local persistence has completed successfully.
class AuditInbox {
  static Future<AuditSyncResult> sync({
    required AuditRequest request,
    required AuditSave save,
    required bool Function() isCurrent,
    void Function(Map<String, dynamic>)? onSaved,
  }) async {
    var saved = 0;
    var dropped = 0;
    void checkSession() {
      if (!isCurrent()) throw StateError('audit session changed');
    }

    for (var recordIndex = 0; recordIndex < 2; recordIndex++) {
      checkSession();
      final header = await request('audit/read', 'head');
      checkSession();
      final match = RegExp(r'^id=(\d+);size=(\d+);pending=(\d+);dropped=(\d+)$')
          .firstMatch(header);
      if (match == null) throw const FormatException('audit header');
      final id = int.parse(match[1]!);
      final size = int.parse(match[2]!);
      final pending = int.parse(match[3]!);
      dropped = int.parse(match[4]!);
      if (id == 0 && size == 0 && pending == 0) break;
      if (id <= 0 ||
          id > 0xffffffff ||
          size < 1 ||
          size > 768 ||
          pending < 1 ||
          pending > 2) {
        throw const FormatException('audit bounds');
      }
      final bytes = <int>[];
      while (bytes.length < size) {
        final chunk = await request('audit/read', '$id:${bytes.length}');
        checkSession();
        final expected = (size - bytes.length).clamp(1, 64);
        if (chunk.length != expected * 2 ||
            !RegExp(r'^[0-9a-fA-F]+$').hasMatch(chunk)) {
          throw const FormatException('audit chunk');
        }
        for (var i = 0; i < chunk.length; i += 2) {
          bytes.add(int.parse(chunk.substring(i, i + 2), radix: 16));
        }
      }
      final decoded = jsonDecode(utf8.decode(bytes));
      if (decoded is! Map<String, dynamic> ||
          !const ['wf/prb', 'wf/eapol'].contains(decoded['t'])) {
        throw const FormatException('audit record');
      }
      final fields =
          decoded['t'] == 'wf/prb' ? ['m', 's'] : ['s', 'd', 'p', 'n'];
      if (fields.any((field) => decoded[field] is! String)) {
        throw const FormatException('audit fields');
      }
      checkSession();
      final inserted = await save(decoded);
      checkSession();
      if (inserted) {
        saved++;
        onSaved?.call(decoded);
      }
      final ack = await request('audit/ack', '$id');
      checkSession();
      if (ack != 'ok') throw const FormatException('audit acknowledgement');
    }
    return AuditSyncResult(saved, dropped);
  }
}
