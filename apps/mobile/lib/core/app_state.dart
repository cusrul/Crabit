import 'package:flutter/material.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/managers/storage_manager.dart';

// We will use ValueNotifiers for simple, robust state management that works everywhere.
class AppState {
  static final AppState _instance = AppState._internal();
  factory AppState() => _instance;
  AppState._internal() {
    _init();
  }

  final ValueNotifier<List<ProbeEvent>> probeList = ValueNotifier([]);
  final ValueNotifier<List<EapolEvent>> eapolList = ValueNotifier([]);
  final ValueNotifier<bool> isConnected = ValueNotifier(false);

  void _init() {
    final storage = StorageManager();
    final ble = BleManager();

    // Load History
    probeList.value = storage
        .getAllProbes()
        .map((e) => ProbeEvent.fromJson(e))
        .toList()
        .reversed
        .toList();
    eapolList.value =
        storage.getAllEapols().map((e) => EapolEvent.fromJson(e)).toList();

    // Sync Connection
    isConnected.value = ble.isConnected.value;
    ble.isConnected.addListener(() {
      isConnected.value = ble.isConnected.value;
    });

    // Sync Data
    ble.probeStream.listen((event) {
      probeList.value = [event, ...probeList.value];
    });
    ble.eapolStream.listen((event) {
      eapolList.value = [...eapolList.value, event];
    });
  }

  void clearProbes() {
    StorageManager().getBox(StorageManager.boxProbeLogs).clear();
    probeList.value = [];
  }

  void clearEapols() {
    StorageManager().getBox(StorageManager.boxEapolLogs).clear();
    eapolList.value = [];
  }
}
