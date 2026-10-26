import 'package:hive_flutter/hive_flutter.dart';
import 'dart:convert';
import 'package:crypto/crypto.dart';

class StorageManager {
  static final StorageManager _instance = StorageManager._internal();
  factory StorageManager() => _instance;
  StorageManager._internal();

  // Box names
  static const String boxSettings = 'settings';
  static const String boxProbeLogs = 'probe_logs';
  static const String boxEapolLogs = 'eapol_logs';
  static const String boxBatteryHistory = 'battery_history';

  Future<void> init() async {
    await Hive.initFlutter();

    // Open boxes for permanent storage
    await Hive.openBox(boxSettings);
    await Hive.openBox(boxProbeLogs);
    await Hive.openBox(boxEapolLogs);
    await Hive.openBox(boxBatteryHistory);
  }

  // Generic helpers
  Box getBox(String name) => Hive.box(name);

  // Example: Settings management
  void saveSetting(String key, dynamic value) {
    getBox(boxSettings).put(key, value);
  }

  dynamic getSetting(String key, {dynamic defaultValue}) {
    return getBox(boxSettings).get(key, defaultValue: defaultValue);
  }

  // Log management
  Future<void> saveProbe(Map<String, dynamic> probe) async {
    await getBox(boxProbeLogs).add(probe);
  }

  Future<void> saveEapol(Map<String, dynamic> eapol) async {
    await getBox(boxEapolLogs).add(eapol);
  }

  // Stable content key makes retry after a lost device ACK idempotent, including
  // app restarts. Identical records from the same watch are intentionally folded.
  Future<bool> saveAuditRecord(
      String device, Map<String, dynamic> record) async {
    final ordered = Map.fromEntries(
        record.entries.toList()..sort((a, b) => a.key.compareTo(b.key)));
    final digest =
        sha256.convert(utf8.encode('$device:${jsonEncode(ordered)}'));
    final box = getBox(record['t'] == 'wf/prb' ? boxProbeLogs : boxEapolLogs);
    final key = 'audit:$digest';
    if (box.containsKey(key)) return false;
    await box.put(key, record);
    return true;
  }

  List<Map<String, dynamic>> getAllProbes() {
    return getBox(boxProbeLogs)
        .values
        .cast<Map<dynamic, dynamic>>()
        .map((e) => Map<String, dynamic>.from(e))
        .toList();
  }

  List<Map<String, dynamic>> getAllEapols() {
    return getBox(boxEapolLogs)
        .values
        .cast<Map<dynamic, dynamic>>()
        .map((e) => Map<String, dynamic>.from(e))
        .toList();
  }

  Future<void> clearAllLogs() async {
    await getBox(boxProbeLogs).clear();
    await getBox(boxEapolLogs).clear();
  }
}
