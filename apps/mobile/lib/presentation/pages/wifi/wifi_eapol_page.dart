import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:crabit_watch_app/core/app_state.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'audit_sync_action.dart';

class WifiEapolPage extends StatefulWidget {
  const WifiEapolPage({super.key});

  @override
  State<WifiEapolPage> createState() => _WifiEapolPageState();
}

class _WifiEapolPageState extends State<WifiEapolPage> {
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF110000),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text('WPA2 // HANDSHAKE VAULT',
            style: TextStyle(
                color: Colors.redAccent,
                fontFamily: 'monospace',
                fontWeight: FontWeight.bold,
                letterSpacing: 1.2)),
        iconTheme: const IconThemeData(color: Colors.redAccent),
        actions: [
          const AuditSyncAction(),
          IconButton(
            icon: const Icon(Icons.delete_sweep),
            onPressed: () => AppState().clearEapols(),
          ),
        ],
      ),
      body: ValueListenableBuilder<List<EapolEvent>>(
        valueListenable: AppState().eapolList,
        builder: (context, handshakes, child) {
          return Column(
            children: [
              _buildWarningHeader(handshakes.length),
              Expanded(
                child: handshakes.isEmpty
                    ? const Center(
                        child: Text(
                            "NO STORED HANDSHAKES\n等待捕捉 4-WAY HANDSHAKE",
                            textAlign: TextAlign.center,
                            style: TextStyle(
                                color: Colors.redAccent,
                                fontFamily: 'monospace',
                                height: 1.8,
                                fontSize: 13)))
                    : ListView.builder(
                        padding: const EdgeInsets.all(12),
                        itemCount: handshakes.length,
                        itemBuilder: (context, index) {
                          final event =
                              handshakes[handshakes.length - 1 - index];
                          return _buildEapolCard(event);
                        },
                      ),
              ),
            ],
          );
        },
      ),
    );
  }

  Widget _buildWarningHeader(int count) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      color: Colors.redAccent.withValues(alpha: 0.15),
      child: Column(
        children: [
          const Icon(Icons.warning_amber_rounded,
              color: Colors.redAccent, size: 36),
          const SizedBox(height: 8),
          const Text("RESTRICTED AREA / 高危数据持久化区",
              style: TextStyle(
                  color: Colors.redAccent,
                  fontWeight: FontWeight.bold,
                  fontFamily: 'monospace')),
          const SizedBox(height: 4),
          Text("TOTAL VAULT ITEMS: $count",
              style: const TextStyle(
                  color: Colors.white, fontFamily: 'monospace')),
        ],
      ),
    );
  }

  Widget _buildEapolCard(EapolEvent event) {
    final String timeStr = event.timestamp.toIso8601String().substring(11, 19);
    return Card(
      color: Colors.black54,
      margin: const EdgeInsets.only(bottom: 12),
      shape: RoundedRectangleBorder(
          side: BorderSide(
              color: Colors.redAccent.withValues(alpha: 0.5), width: 1.5),
          borderRadius: BorderRadius.circular(12)),
      child: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: [
              Text("[$timeStr] EAPOL RECORD",
                  style: const TextStyle(
                      color: Colors.white70,
                      fontFamily: 'monospace',
                      fontSize: 12)),
              const Icon(Icons.lock_open, color: Colors.redAccent, size: 16)
            ]),
            const Divider(color: Colors.white24, height: 20),
            Text("SSID:    ${event.ssid}",
                style: const TextStyle(
                    color: Colors.redAccent,
                    fontFamily: 'monospace',
                    fontSize: 13,
                    fontWeight: FontWeight.bold)),
            const SizedBox(height: 4),
            Text("AP MAC:  ${event.apMac}",
                style: const TextStyle(
                    color: Colors.cyanAccent,
                    fontFamily: 'monospace',
                    fontSize: 13)),
            const SizedBox(height: 4),
            Text("CLI MAC: ${event.clientMac}",
                style: const TextStyle(
                    color: Colors.orangeAccent,
                    fontFamily: 'monospace',
                    fontSize: 13)),
            const SizedBox(height: 12),
            Container(
                padding: const EdgeInsets.all(8),
                width: double.infinity,
                color: Colors.red.withValues(alpha: 0.1),
                child: SelectableText(event.hexPayload,
                    style: TextStyle(
                        color: Colors.redAccent.shade100,
                        fontFamily: 'monospace',
                        fontSize: 11))),
            const SizedBox(height: 12),
            OutlinedButton.icon(
              onPressed: () {
                String cleanHex = event.hexPayload
                    .toUpperCase()
                    .replaceAll(RegExp(r'[^0-9A-F]'), '');
                Clipboard.setData(
                    ClipboardData(text: "RAW_EAPOL_DATA:$cleanHex"));
                ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text("COPIED TO CLIPBOARD")));
              },
              icon: const Icon(Icons.copy, size: 16, color: Colors.redAccent),
              label: const Text("Copy RAW Data",
                  style: TextStyle(
                      color: Colors.redAccent, fontFamily: 'monospace')),
              style: OutlinedButton.styleFrom(
                  side: BorderSide(
                      color: Colors.redAccent.withValues(alpha: 0.5))),
            ),
          ],
        ),
      ),
    );
  }
}
