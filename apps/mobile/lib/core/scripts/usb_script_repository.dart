import 'package:hive/hive.dart';

import 'usb_script.dart';

abstract interface class UsbScriptRepository {
  Future<List<UsbScript>> load();
  Future<void> save(UsbScript script);
  Future<void> delete(String id);
}

class HiveUsbScriptRepository implements UsbScriptRepository {
  HiveUsbScriptRepository(this.settingsBox);

  static const storageKey = 'usb_scripts_v1';
  static final _mutationQueues = Expando<Future<void>>('usb-script-mutations');
  final Box settingsBox;

  @override
  Future<List<UsbScript>> load() async {
    final raw = settingsBox.get(storageKey);
    if (raw is! List) return [];
    return raw
        .whereType<Map>()
        .map(UsbScript.fromMap)
        .whereType<UsbScript>()
        .toList();
  }

  @override
  Future<void> save(UsbScript script) => _serialize(() async {
        final scripts = await load();
        final index = scripts.indexWhere((item) => item.id == script.id);
        if (index < 0) {
          scripts.add(script);
        } else {
          scripts[index] = script;
        }
        await settingsBox.put(
            storageKey, scripts.map((item) => item.toMap()).toList());
      });

  @override
  Future<void> delete(String id) => _serialize(() async {
        final scripts = await load()
          ..removeWhere((item) => item.id == id);
        await settingsBox.put(
            storageKey, scripts.map((item) => item.toMap()).toList());
      });

  Future<void> _serialize(Future<void> Function() operation) {
    final previous = _mutationQueues[settingsBox] ?? Future<void>.value();
    final current = previous.then(
      (_) => operation(),
      onError: (Object _, StackTrace __) => operation(),
    );
    _mutationQueues[settingsBox] = current.catchError((Object _) {});
    return current;
  }
}
