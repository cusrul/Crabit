import 'dart:convert';
import 'dart:io';
import 'package:crabit_watch_app/core/device/audit_inbox.dart';
import 'package:crabit_watch_app/core/managers/storage_manager.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:hive_flutter/hive_flutter.dart';

void main() {
  final record = <String, dynamic>{
    't': 'wf/prb',
    'm': 'AA:BB:CC',
    's': '测试网络' * 10
  };
  final payload = utf8.encode(jsonEncode(record));
  String chunk(int offset) => payload
      .skip(offset)
      .take(64)
      .map((byte) => byte.toRadixString(16).padLeft(2, '0'))
      .join();

  test('persist before deletion and UTF8 reassembly across chunks', () async {
    expect(payload.length, greaterThan(128));
    var deleted = false;
    var persisted = false;
    final result = await AuditInbox.sync(
      request: (topic, value) async {
        if (topic == 'audit/ack') {
          expect(persisted, isTrue);
          deleted = true;
          return 'ok';
        }
        if (value == 'head') {
          return deleted
              ? 'id=0;size=0;pending=0;dropped=3'
              : 'id=1;size=${payload.length};pending=1;dropped=3';
        }
        return chunk(int.parse(value.split(':')[1]));
      },
      save: (data) async {
        expect(data, record);
        persisted = true;
        return true;
      },
      isCurrent: () => true,
    );
    expect(result.saved, 1);
    expect(result.dropped, 3);
    expect(deleted, isTrue);
  });

  test('storage failure never acknowledges', () async {
    final calls = <String>[];
    await expectLater(
        AuditInbox.sync(
          request: (topic, value) async {
            calls.add(topic);
            return value == 'head'
                ? 'id=1;size=${payload.length};pending=1;dropped=0'
                : chunk(int.parse(value.split(':')[1]));
          },
          save: (_) async => throw StateError('disk full'),
          isCurrent: () => true,
        ),
        throwsStateError);
    expect(calls, isNot(contains('audit/ack')));
  });

  test('disconnect during save never acknowledges a later session', () async {
    var current = true;
    final calls = <String>[];
    await expectLater(
        AuditInbox.sync(
          request: (topic, value) async {
            calls.add(topic);
            return value == 'head'
                ? 'id=1;size=${payload.length};pending=1;dropped=0'
                : chunk(int.parse(value.split(':')[1]));
          },
          save: (_) async {
            current = false;
            return true;
          },
          isCurrent: () => current,
        ),
        throwsStateError);
    expect(calls, isNot(contains('audit/ack')));
  });

  test('bad chunks and oversized records are not saved or acknowledged',
      () async {
    for (final size in [payload.length, 769]) {
      final calls = <String>[];
      await expectLater(
          AuditInbox.sync(
            request: (topic, value) async {
              calls.add(topic);
              return value == 'head'
                  ? 'id=1;size=$size;pending=1;dropped=0'
                  : 'GG';
            },
            save: (_) async => fail('must not persist invalid record'),
            isCurrent: () => true,
          ),
          throwsFormatException);
      expect(calls, isNot(contains('audit/ack')));
    }
  });

  test('duplicate save is acknowledged without a duplicate UI event', () async {
    var deleted = false;
    var events = 0;
    final result = await AuditInbox.sync(
      request: (topic, value) async {
        if (topic == 'audit/ack') {
          deleted = true;
          return 'ok';
        }
        if (value == 'head') {
          return deleted
              ? 'id=0;size=0;pending=0;dropped=0'
              : 'id=1;size=${payload.length};pending=1;dropped=0';
        }
        return chunk(int.parse(value.split(':')[1]));
      },
      save: (_) async => false,
      isCurrent: () => true,
      onSaved: (_) {
        events++;
      },
    );
    expect(result.saved, 0);
    expect(events, 0);
    expect(deleted, isTrue);
  });

  test('both bounded slots including a full captured EAPOL record are drained',
      () async {
    final eapol = <String, dynamic>{
      't': 'wf/eapol',
      's': '00:11:22:33:44:55',
      'd': 'AA:BB:CC:DD:EE:FF',
      'p': 'AB' * 300,
      'n': 'LAB',
    };
    final records = [record, eapol];
    var head = 0;
    var saved = 0;
    final result = await AuditInbox.sync(
      request: (topic, value) async {
        if (topic == 'audit/ack') {
          head++;
          return 'ok';
        }
        final data = utf8.encode(jsonEncode(records[head]));
        if (value == 'head') {
          return 'id=${head + 1};size=${data.length};pending=${2 - head};dropped=0';
        }
        return data
            .skip(int.parse(value.split(':')[1]))
            .take(64)
            .map((byte) => byte.toRadixString(16).padLeft(2, '0'))
            .join();
      },
      save: (data) async {
        expect(data, records[saved]);
        saved++;
        return true;
      },
      isCurrent: () => true,
    );
    expect(result.saved, 2);
    expect(head, 2);
  });

  test(
      'Hive content keys deduplicate retries but keep different device records',
      () async {
    final directory =
        await Directory.systemTemp.createTemp('crabit-audit-hive-');
    try {
      Hive.init(directory.path);
      await Hive.openBox(StorageManager.boxProbeLogs);
      final storage = StorageManager();
      expect(await storage.saveAuditRecord('watch-A', record), isTrue);
      expect(
          await storage.saveAuditRecord(
              'watch-A', Map.fromEntries(record.entries.toList().reversed)),
          isFalse);
      expect(await storage.saveAuditRecord('watch-B', record), isTrue);
      expect(storage.getAllProbes(), hasLength(2));
    } finally {
      await Hive.close();
      await directory.delete(recursive: true);
    }
  });
}
