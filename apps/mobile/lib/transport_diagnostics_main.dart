import 'package:flutter/material.dart';
import 'core/diagnostics/transport_automation.dart';
import 'core/managers/storage_manager.dart';
import 'main.dart' show MyApp;
import 'presentation/pages/misc/transport_diagnostics_page.dart';

/// Opt-in diagnostic entry. Merely opening it never scans or connects a watch.
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await StorageManager().init();
  TransportAutomation.enable();
  runApp(const MyApp(home: TransportDiagnosticsPage()));
}
