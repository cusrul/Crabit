import 'package:flutter/material.dart';

import '../../../core/managers/ble_manager.dart';
import '../../widgets/app_page.dart';
import 'transport_diagnostics_page.dart';

class DebugPage extends StatefulWidget {
  const DebugPage({super.key});

  @override
  State<DebugPage> createState() => _DebugPageState();
}

class _DebugPageState extends State<DebugPage> {
  final TextEditingController _cmdController = TextEditingController();

  @override
  void dispose() {
    _cmdController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AppPage(
        title: '调试',
        child: ListView(
            padding: const EdgeInsets.only(top: 20, bottom: 32),
            children: [
              Text('诊断与开发', style: Theme.of(context).textTheme.headlineLarge),
              const SizedBox(height: 8),
              Text('日常使用无需打开这些工具。',
                  style: TextStyle(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                      fontSize: 16)),
              const SizedBox(height: 28),
              Card(
                  child: ListTile(
                contentPadding: const EdgeInsets.all(20),
                leading: Icon(Icons.monitor_heart_outlined,
                    color: Theme.of(context).colorScheme.primary),
                title: const Text('链路诊断'),
                subtitle: const Text('BLE、UART、STM32 和 Wi-Fi 的手动回归检查'),
                trailing: const Icon(Icons.chevron_right),
                onTap: () => Navigator.of(context).push(MaterialPageRoute(
                    builder: (_) => const TransportDiagnosticsPage())),
              )),
              const SizedBox(height: 32),
              Text('原始报文', style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              const Text('仅发送你确认过的协议内容。发送不代表手表已执行成功。'),
              const SizedBox(height: 16),
              TextField(
                controller: _cmdController,
                minLines: 6,
                maxLines: 12,
                style: const TextStyle(
                    fontFamily: 'monospace', fontSize: 14, height: 1.6),
                decoration: const InputDecoration(
                    hintText: '输入原始报文…',
                    alignLabelWithHint: true,
                    labelText: '报文内容',
                    hintStyle: TextStyle(fontFamily: 'Roboto')),
              ),
              const SizedBox(height: 16),
              Align(
                  alignment: Alignment.centerLeft,
                  child: FilledButton.icon(
                    onPressed: () {
                      if (_cmdController.text.isNotEmpty) {
                        BleManager().sendRawData(_cmdController.text);
                      }
                    },
                    icon: const Icon(Icons.send_outlined, size: 18),
                    label: const Text('发送报文'),
                  )),
            ]),
      );
}
