import 'package:flutter/material.dart';

import '../../../core/app_state.dart';
import '../../../core/battery/battery_history_controller.dart';
import '../../../core/battery/battery_history_store.dart';
import '../../../core/battery/ble_battery_gateway.dart';
import '../../../core/managers/ble_manager.dart';
import '../../../core/managers/storage_manager.dart';
import '../../widgets/app_shell.dart';
import '../ble/sniffer_page.dart';
import '../misc/debug_page.dart';
import '../settings/settings_page.dart';
import '../weather/weather_page.dart';
import '../tools/badusb_page.dart';
import 'dashboard_page.dart';

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WidgetsBindingObserver {
  late final List<Widget> _pages;
  late final BleBatteryGateway _batteryGateway;
  late final BatteryHistoryController _battery;
  int _selected = 0;
  void _select(int index) => setState(() => _selected = index);

  @override
  void initState() {
    super.initState();
    final ble = BleManager();
    _batteryGateway = BleBatteryGateway(ble);
    _batteryGateway.setForeground(WidgetsBinding.instance.lifecycleState ==
            null ||
        WidgetsBinding.instance.lifecycleState == AppLifecycleState.resumed);
    _battery = BatteryHistoryController(
      gateway: _batteryGateway,
      store: HiveBatteryHistoryStore(
          StorageManager().getBox(StorageManager.boxBatteryHistory)),
      canSample: () => ble.canReadBackground,
    );
    _pages = [
      DashboardPage(onNavigate: _select, batteryController: _battery),
      const BadUsbPage(),
      const SnifferPage(),
      SettingsPage(onOpenTimeWeather: () => _select(5)),
      const DebugPage(),
      const WeatherPage()
    ];
    AppState();
    WidgetsBinding.instance.addObserver(this);
    BleManager().rememberedConnection.setForeground(
        WidgetsBinding.instance.lifecycleState == null ||
            WidgetsBinding.instance.lifecycleState ==
                AppLifecycleState.resumed);
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // Native WebView focus may emit inactive on Windows; it is not background.
    if (state == AppLifecycleState.inactive) return;
    _batteryGateway.setForeground(state == AppLifecycleState.resumed);
    BleManager()
        .rememberedConnection
        .setForeground(state == AppLifecycleState.resumed);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    BleManager().rememberedConnection.setForeground(false);
    _battery.dispose();
    _batteryGateway.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AppShell(
      pages: _pages, selectedIndex: _selected, onDestinationSelected: _select);
}
