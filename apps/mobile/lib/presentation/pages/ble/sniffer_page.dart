import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:crypto/crypto.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart'; // 提供 compute
import 'package:path_provider/path_provider.dart';
import 'package:share_plus/share_plus.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';

class SnifferPage extends StatefulWidget {
  const SnifferPage({super.key});

  @override
  State<SnifferPage> createState() => _SnifferPageState();
}

// 将重度密码学运算放到后台 isolate，避免阻塞 UI 线程。
// 注意：Isolate 函数必须放在类外面的顶级作用域，或者标记为 static
Uint8List _isolateComputePmk(Map<String, String> args) {
  final password = args['password']!;
  final ssid = args['ssid']!;

  final passBytes = utf8.encode(password);
  final saltBytes = utf8.encode(ssid);
  final mac = Hmac(sha1, passBytes);

  int iterations = 4096;
  int keyLength = 32;
  final U = Uint8List(20);
  final T = Uint8List(20);
  final key = Uint8List(keyLength);
  int blockCount = (keyLength / 20).ceil();
  int keyOffset = 0;

  for (int i = 1; i <= blockCount; i++) {
    final saltAndCount = Uint8List(saltBytes.length + 4);
    saltAndCount.setAll(0, saltBytes);
    saltAndCount.buffer.asByteData().setUint32(saltBytes.length, i, Endian.big);

    var uBytes = mac.convert(saltAndCount).bytes;
    U.setAll(0, uBytes);
    T.setAll(0, uBytes);

    for (int iter = 1; iter < iterations; iter++) {
      uBytes = mac.convert(U).bytes;
      U.setAll(0, uBytes);
      for (int k = 0; k < 20; k++) {
        T[k] ^= U[k];
      }
    }
    int toCopy = (keyLength - keyOffset) < 20 ? (keyLength - keyOffset) : 20;
    key.setRange(keyOffset, keyOffset + toCopy, T);
    keyOffset += toCopy;
  }
  return key;
}

class _SnifferPageState extends State<SnifferPage> {
  final List<ProbeEvent> _probeLogs = [];
  final List<EapolEvent> _eapolLogs = [];

  StreamSubscription? _probeSub;
  StreamSubscription? _eapolSub;

  @override
  void initState() {
    super.initState();
    _probeSub = BleManager().probeStream.listen((event) {
      if (!mounted) return;
      setState(() {
        // 按收到顺序存储，保证时间轴正确
        _probeLogs.add(event);
        if (_probeLogs.length > 200) _probeLogs.removeAt(0); // 满了就删掉最老的
      });
    });

    _eapolSub = BleManager().eapolStream.listen((event) {
      if (!mounted) return;
      HapticFeedback.heavyImpact();
      setState(() {
        // 保证 M1 先于 M2 存储，维持握手顺序
        _eapolLogs.add(event);
      });
      _showPwnedAlert(event.apMac);
    });
  }

  @override
  void dispose() {
    _probeSub?.cancel();
    _eapolSub?.cancel();
    super.dispose();
  }

  void _showPwnedAlert(String bssid) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).hideCurrentSnackBar();
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Row(
          children: [
            const Icon(Icons.check_circle_outline, color: AppPalette.blue),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                "已记录认证数据\nBSSID: $bssid",
                style: const TextStyle(
                    fontWeight: FontWeight.w600, color: AppPalette.ink),
              ),
            ),
          ],
        ),
        backgroundColor: AppPalette.surface,
        behavior: SnackBarBehavior.floating,
        shape: RoundedRectangleBorder(
            side: const BorderSide(color: AppPalette.line),
            borderRadius: BorderRadius.circular(8)),
        duration: const Duration(seconds: 4),
      ),
    );
  }

  // 导出 RAW_EAPOL_DATA 格式文本，便于外部工具处理。
  Future<void> _exportRawLogFile([List<EapolEvent>? targetLogs]) async {
    final logsToExport = targetLogs ?? _eapolLogs;
    if (logsToExport.isEmpty) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text("暂无可导出的认证记录")));
      return;
    }

    try {
      StringBuffer sb = StringBuffer();

      // 因为我们改成了 .add()，这里遍历天然就是真实的时间顺序！
      for (var log in logsToExport) {
        String cleanHex =
            log.hexPayload.toUpperCase().replaceAll(RegExp(r'[^0-9A-F]'), '');

        if (cleanHex.length % 2 != 0) {
          cleanHex = cleanHex.substring(0, cleanHex.length - 1);
        }

        // 为伪造认证四步握手准备所需字段
        sb.writeln("TARGET_SSID:${log.ssid}");
        sb.writeln("TARGET_BSSID:${log.apMac}");
        sb.writeln("RAW_EAPOL_DATA:$cleanHex");
        sb.writeln("---"); // 分隔符
      }

      final directory = await getTemporaryDirectory();
      final File logFile = File(
          '${directory.path}/raw_loot_${DateTime.now().millisecondsSinceEpoch}.txt');
      await logFile.writeAsString(sb.toString());

      await SharePlus.instance.share(ShareParams(
        files: [XFile(logFile.path)],
        text: "Here is the RAW EAPOL log for your Python script.",
      ));
    } catch (e) {
      if (!mounted) return;
      debugPrint("导出失败: $e");
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text("Error: $e")));
    }
  }

  void _clearAllEapolLogs() {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: AppPalette.surface,
        shape: RoundedRectangleBorder(
            side: const BorderSide(color: AppPalette.line),
            borderRadius: BorderRadius.circular(12)),
        title: const Text("清除网络记录？",
            style:
                TextStyle(color: AppPalette.ink, fontWeight: FontWeight.w600)),
        content: const Text("确定清除所有已记录的认证数据吗？此操作无法撤销。",
            style: TextStyle(color: AppPalette.ink)),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx), child: const Text("取消")),
          TextButton(
            onPressed: () {
              setState(() => _eapolLogs.clear());
              Navigator.pop(ctx);
            },
            child: const Text("清除",
                style: TextStyle(
                    color: AppPalette.blue, fontWeight: FontWeight.w600)),
          ),
        ],
      ),
    );
  }

  void _copyGroupRawData(List<EapolEvent> group, String ssid, String bssid) {
    StringBuffer sb = StringBuffer();
    sb.writeln("TARGET_SSID:$ssid");
    sb.writeln("TARGET_BSSID:$bssid");
    for (var log in group) {
      String cleanHex =
          log.hexPayload.toUpperCase().replaceAll(RegExp(r'[^0-9A-F]'), '');
      if (cleanHex.length % 2 != 0) {
        cleanHex = cleanHex.substring(0, cleanHex.length - 1);
      }
      sb.writeln("RAW_EAPOL_DATA:$cleanHex");
    }
    Clipboard.setData(ClipboardData(text: sb.toString()));

    if (!mounted) return;
    ScaffoldMessenger.of(context).hideCurrentSnackBar();
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text("已复制 ${group.length} 条记录",
            style: const TextStyle(color: AppPalette.ink)),
        backgroundColor: AppPalette.surface,
        behavior: SnackBarBehavior.floating,
        shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(8),
            side: const BorderSide(color: AppPalette.line)),
      ),
    );
  }

  void _localQuickCrack(EapolEvent event) {
    String ssid =
        event.ssid == "UNKNOWN" ? "" : event.ssid; // 使用底层上报的 SSID
    bool isCracking = false;
    String currentAttempt = "";
    int progressIndex = 0;

    final List<String> weakPasswords = [
      "12345678",
      "88888888",
      "123456789",
      "11223344",
      "123123123",
      "password",
      "admin123",
      "66666666",
      "1234567890",
      "00000000",
      "hyc774211"
    ];

    showDialog(
        context: context,
        barrierDismissible: false,
        builder: (ctx) {
          return StatefulBuilder(builder: (context, setState) {
            return AlertDialog(
              backgroundColor: AppPalette.surface,
              shape: RoundedRectangleBorder(
                  side: const BorderSide(color: AppPalette.line),
                  borderRadius: BorderRadius.circular(12)),
              title: const Text("本地密码校验",
                  style: TextStyle(
                      color: AppPalette.ink, fontWeight: FontWeight.w600)),
              content: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text("BSSID: ${event.apMac}",
                      style: const TextStyle(
                          color: AppPalette.ink,
                          fontFamily: 'monospace',
                          fontSize: 12)),
                  const SizedBox(height: 12),
                  if (!isCracking && progressIndex == 0 && ssid.isEmpty) ...[
                    const Text("SSID is required to compute PMK:",
                        style:
                            TextStyle(color: AppPalette.muted, fontSize: 12)),
                    const SizedBox(height: 8),
                    TextField(
                      style: const TextStyle(
                          color: AppPalette.ink, fontFamily: 'monospace'),
                      decoration: const InputDecoration(
                          hintText: "Enter exact WiFi Name...",
                          hintStyle: TextStyle(color: AppPalette.muted)),
                      onChanged: (val) => ssid = val,
                    ),
                  ] else ...[
                    Text("Target SSID: ${ssid.isEmpty ? 'UNKNOWN' : ssid}",
                        style: const TextStyle(
                            color: AppPalette.ink,
                            fontSize: 13,
                            fontWeight: FontWeight.bold)),
                    const Text("\nComputing PBKDF2-HMAC-SHA1 (4096 ops)...",
                        style: TextStyle(color: AppPalette.blue, fontSize: 12)),
                    const SizedBox(height: 8),
                    LinearProgressIndicator(
                        value: progressIndex / weakPasswords.length,
                        color: AppPalette.blue,
                        backgroundColor: AppPalette.line),
                    const SizedBox(height: 8),
                    Text("Testing: $currentAttempt",
                        style: const TextStyle(
                            color: AppPalette.muted,
                            fontFamily: 'monospace',
                            fontSize: 14)),
                  ]
                ],
              ),
              actions: [
                if (!isCracking)
                  TextButton(
                      onPressed: () => Navigator.pop(ctx),
                      child: const Text("取消")),
                if (!isCracking && progressIndex == 0)
                  ElevatedButton(
                    style: ElevatedButton.styleFrom(
                        backgroundColor: AppPalette.blue,
                        foregroundColor: Colors.white),
                    onPressed: () async {
                      if (ssid.isEmpty) return;
                      setState(() => isCracking = true);

                      for (int i = 0; i < weakPasswords.length; i++) {
                        setState(() {
                          progressIndex = i + 1;
                          currentAttempt = weakPasswords[i];
                        });
                        await Future.delayed(const Duration(milliseconds: 50));
                        // 在独立 Isolate 中运行，避免阻塞 UI。
                        await compute(_isolateComputePmk,
                            {'password': currentAttempt, 'ssid': ssid});
                      }

                      setState(() {
                        isCracking = false;
                        currentAttempt = "NOT FOUND IN DICT";
                      });
                      await Future.delayed(const Duration(seconds: 2));
                      if (ctx.mounted) Navigator.pop(ctx);
                    },
                    child: const Text("开始校验"),
                  ),
              ],
            );
          });
        });
  }

  @override
  Widget build(BuildContext context) {
    return DefaultTabController(
      length: 2,
      child: Scaffold(
        backgroundColor: AppPalette.canvas,
        appBar: AppBar(
          backgroundColor: AppPalette.surface,
          title: const Text('无线记录',
              style: TextStyle(
                  fontWeight: FontWeight.w600, color: AppPalette.ink)),
          iconTheme: const IconThemeData(color: AppPalette.ink),
          bottom: const TabBar(
            indicatorColor: AppPalette.blue,
            labelColor: AppPalette.blue,
            unselectedLabelColor: AppPalette.muted,
            tabs: [
              Tab(icon: Icon(Icons.network_check), text: "设备探测"),
              Tab(icon: Icon(Icons.security), text: "认证记录"),
            ],
          ),
          actions: [
            IconButton(
              icon: const Icon(Icons.delete_sweep, color: AppPalette.ink),
              tooltip: "清除记录",
              onPressed: () => _clearAllEapolLogs(),
            ),
            IconButton(
              icon: const Icon(Icons.share, color: AppPalette.ink),
              tooltip: "导出原始记录",
              onPressed: () => _exportRawLogFile(),
            ),
          ],
        ),
        body: TabBarView(
          children: [
            _buildProbeRadar(),
            _buildEapolLoot(),
          ],
        ),
      ),
    );
  }

  Widget _buildProbeRadar() {
    if (_probeLogs.isEmpty) {
      return const Center(
          child: Text("等待设备探测数据…",
              textAlign: TextAlign.center,
              style: TextStyle(color: AppPalette.muted)));
    }
    return ListView.builder(
      padding: const EdgeInsets.all(8),
      itemCount: _probeLogs.length,
      itemBuilder: (context, index) {
        // 渲染时倒序读取，使最新记录显示在顶部
        final log = _probeLogs[_probeLogs.length - 1 - index];
        final timeStr = log.timestamp.toIso8601String().substring(11, 19);
        final bool isBroadcast = log.ssid == "<BROADCAST>";

        return Container(
          margin: const EdgeInsets.only(bottom: 6),
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: AppPalette.surface,
            border: Border(
                left: BorderSide(
                    color: isBroadcast ? AppPalette.line : AppPalette.blue,
                    width: 3)),
          ),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text("[$timeStr] ",
                  style: const TextStyle(
                      color: AppPalette.muted,
                      fontFamily: 'monospace',
                      fontSize: 12)),
              Expanded(
                child: RichText(
                  text: TextSpan(
                    style:
                        const TextStyle(fontFamily: 'monospace', fontSize: 13),
                    children: [
                      const TextSpan(
                          text: "SRC: ",
                          style: TextStyle(color: AppPalette.muted)),
                      TextSpan(
                          text: "${log.mac}\n",
                          style: const TextStyle(
                              color: AppPalette.ink,
                              fontWeight: FontWeight.bold)),
                      const TextSpan(
                          text: "REQ: ",
                          style: TextStyle(color: AppPalette.muted)),
                      TextSpan(
                          text: log.ssid,
                          style: TextStyle(
                              color: isBroadcast
                                  ? AppPalette.muted
                                  : AppPalette.blue,
                              fontWeight: isBroadcast
                                  ? FontWeight.normal
                                  : FontWeight.bold)),
                    ],
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }

  Widget _buildEapolLoot() {
    if (_eapolLogs.isEmpty) {
      return const Center(
          child: Text("暂无认证记录",
              textAlign: TextAlign.center,
              style: TextStyle(color: AppPalette.muted)));
    }

    // 按 BSSID (apMac) 将包分组
    final Map<String, List<EapolEvent>> grouped = {};
    for (var event in _eapolLogs) {
      grouped.putIfAbsent(event.apMac, () => []).add(event);
    }

    // 根据该组最新捕获的时间倒序排列网络，保证刚刚抓到包的排在最上面
    final sortedBssids = grouped.keys.toList()
      ..sort((a, b) {
        return grouped[b]!.last.timestamp.compareTo(grouped[a]!.last.timestamp);
      });

    return ListView.builder(
      padding: const EdgeInsets.all(8),
      itemCount: sortedBssids.length,
      itemBuilder: (context, index) {
        final bssid = sortedBssids[index];
        final packets = grouped[bssid]!;

        // 取最后一次更新时的 SSID 和 Client MAC
        final latestPacket = packets.last;
        final ssid = latestPacket.ssid;
        final timeStr =
            latestPacket.timestamp.toIso8601String().substring(11, 19);

        return Card(
          color: AppPalette.surface,
          margin: const EdgeInsets.only(bottom: 12),
          shape: RoundedRectangleBorder(
              side: const BorderSide(color: AppPalette.line),
              borderRadius: BorderRadius.circular(12)),
          child: Theme(
            data: Theme.of(context).copyWith(dividerColor: Colors.transparent),
            child: ExpansionTile(
              leading: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.shield_outlined,
                      color: AppPalette.blue, size: 24),
                  const SizedBox(height: 4),
                  Text("${packets.length} PKT",
                      style: const TextStyle(
                          color: AppPalette.blue,
                          fontSize: 10,
                          fontWeight: FontWeight.bold,
                          fontFamily: 'monospace')),
                ],
              ),
              title: Text(ssid == "UNKNOWN" ? "设备：$bssid" : ssid,
                  style: const TextStyle(
                      color: AppPalette.ink, fontWeight: FontWeight.w600)),
              subtitle: Text("设备：$bssid\n最近记录：$timeStr",
                  style: const TextStyle(
                      color: AppPalette.muted,
                      fontSize: 11,
                      fontFamily: 'monospace')),
              children: [
                // 数据包列表区
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: AppPalette.canvas,
                    border: Border.symmetric(
                        horizontal: BorderSide(color: AppPalette.line)),
                  ),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: packets.map((p) {
                      final t = p.timestamp.toIso8601String().substring(11, 19);
                      return Padding(
                        padding: const EdgeInsets.only(bottom: 8.0),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text("[$t] ",
                                style: const TextStyle(
                                    color: AppPalette.muted,
                                    fontSize: 10,
                                    fontFamily: 'monospace')),
                            Expanded(
                              child: SelectableText(p.hexPayload,
                                  style: TextStyle(
                                      color: AppPalette.ink,
                                      fontSize: 10,
                                      fontFamily: 'monospace')),
                            ),
                          ],
                        ),
                      );
                    }).toList(),
                  ),
                ),
                // 操作按钮区
                Padding(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      // 左侧：删除、复制和单独导出
                      Row(
                        children: [
                          IconButton(
                            icon: const Icon(Icons.delete_outline,
                                color: AppPalette.muted),
                            tooltip: "删除此设备记录",
                            onPressed: () {
                              setState(() {
                                _eapolLogs.removeWhere((e) => e.apMac == bssid);
                              });
                            },
                          ),
                          IconButton(
                            icon:
                                const Icon(Icons.copy, color: AppPalette.muted),
                            tooltip: "复制原始数据",
                            onPressed: () =>
                                _copyGroupRawData(packets, ssid, bssid),
                          ),
                          IconButton(
                            icon: const Icon(Icons.share,
                                color: AppPalette.muted),
                            tooltip: "导出此设备记录",
                            onPressed: () => _exportRawLogFile(packets),
                          ),
                        ],
                      ),
                      // 右侧：记录校验操作
                      ElevatedButton.icon(
                        style: ElevatedButton.styleFrom(
                            backgroundColor: AppPalette.blue,
                            foregroundColor: Colors.white),
                        icon: const Icon(Icons.verified_user, size: 16),
                        label: const Text("校验",
                            style: TextStyle(fontWeight: FontWeight.w600)),
                        onPressed: () => _localQuickCrack(latestPacket),
                      ),
                    ],
                  ),
                )
              ],
            ),
          ),
        );
      },
    );
  }
}
