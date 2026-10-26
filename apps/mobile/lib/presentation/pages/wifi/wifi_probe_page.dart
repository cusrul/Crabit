import 'package:flutter/material.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/app_state.dart';
import 'audit_sync_action.dart';

class WifiProbePage extends StatefulWidget {
  const WifiProbePage({super.key});

  @override
  State<WifiProbePage> createState() => _WifiProbePageState();
}

class _WifiProbePageState extends State<WifiProbePage> {
  final Map<String, Set<String>> _macProfiles = {};
  final ScrollController _scrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    // Build initial profiles
    final history = AppState().probeList.value;
    for (var probe in history) {
      if (probe.ssid != "<BROADCAST>") {
        _macProfiles.putIfAbsent(probe.mac, () => {}).add(probe.ssid);
      }
    }
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0D0D0D),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text(
          'C2 UPLINK // 探针雷达',
          style: TextStyle(
              color: Colors.orangeAccent,
              fontFamily: 'monospace',
              fontWeight: FontWeight.bold,
              letterSpacing: 1.5),
        ),
        iconTheme: const IconThemeData(color: Colors.orangeAccent),
        actions: [
          const AuditSyncAction(),
          IconButton(
            icon: const Icon(Icons.delete_sweep),
            onPressed: () => AppState().clearProbes(),
          ),
        ],
      ),
      body: ValueListenableBuilder<List<ProbeEvent>>(
        valueListenable: AppState().probeList,
        builder: (context, probes, child) {
          return Column(
            children: [
              _buildStatusHeader(probes.length),
              const Divider(
                  color: Colors.orangeAccent, height: 1, thickness: 0.5),
              Expanded(
                child: probes.isEmpty
                    ? _buildEmptyState()
                    : ListView.builder(
                        controller: _scrollController,
                        padding: const EdgeInsets.symmetric(
                            horizontal: 12, vertical: 8),
                        physics: const BouncingScrollPhysics(),
                        itemCount: probes.length,
                        itemBuilder: (context, index) {
                          final item = probes[index];
                          if (item.ssid != "<BROADCAST>") {
                            _macProfiles
                                .putIfAbsent(item.mac, () => {})
                                .add(item.ssid);
                          }
                          return _buildProbeItem(item);
                        },
                      ),
              ),
            ],
          );
        },
      ),
    );
  }

  Widget _buildStatusHeader(int count) {
    return Container(
      padding: const EdgeInsets.all(16),
      color: Colors.orangeAccent.withValues(alpha: 0.05),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Row(
            children: [
              const Icon(Icons.radar, color: Colors.orangeAccent, size: 20),
              const SizedBox(width: 8),
              const Text("LIVE & PERSISTENT",
                  style: TextStyle(
                      color: Colors.orangeAccent,
                      fontFamily: 'monospace',
                      fontWeight: FontWeight.bold)),
            ],
          ),
          Text("TOTAL: $count",
              style: const TextStyle(
                  color: Colors.cyanAccent,
                  fontFamily: 'monospace',
                  fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }

  Widget _buildProbeItem(ProbeEvent item) {
    final String timeStr = item.timestamp.toIso8601String().substring(11, 19);
    final bool isBroadcast = item.ssid == "<BROADCAST>";
    final Set<String> history = _macProfiles[item.mac] ?? {};

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Colors.black45,
        border: Border(
            left: BorderSide(
                color: isBroadcast ? Colors.grey : Colors.orangeAccent,
                width: 3)),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text("[$timeStr]",
              style: const TextStyle(
                  color: Colors.white38,
                  fontFamily: 'monospace',
                  fontSize: 12)),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text("SRC MAC: ${item.mac}",
                    style: const TextStyle(
                        color: Colors.cyanAccent,
                        fontFamily: 'monospace',
                        fontSize: 13,
                        fontWeight: FontWeight.bold)),
                const SizedBox(height: 4),
                Text("PROBE REQ: ${item.ssid}",
                    style: TextStyle(
                        color:
                            isBroadcast ? Colors.white54 : Colors.orangeAccent,
                        fontFamily: 'monospace',
                        fontSize: 13)),
                if (history.length > 1)
                  Padding(
                    padding: const EdgeInsets.only(top: 6),
                    child: Text("🔍 轨迹画像: ${history.join(" | ")}",
                        style: TextStyle(
                            color: Colors.cyanAccent.withValues(alpha: 0.6),
                            fontFamily: 'monospace',
                            fontSize: 11)),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildEmptyState() {
    return const Center(
      child: Text(">_ NO LOGS FOUND...\n等待数据或检查持久化存储",
          textAlign: TextAlign.center,
          style: TextStyle(
              color: Colors.white24, fontFamily: 'monospace', height: 1.8)),
    );
  }
}
