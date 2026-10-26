import 'package:flutter/material.dart';
import '../../../core/managers/ble_manager.dart';

class AuditSyncAction extends StatelessWidget {
  const AuditSyncAction({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = BleManager();
    return ValueListenableBuilder<String>(
      valueListenable: ble.auditSyncStatus,
      builder: (context, status, _) => IconButton(
        tooltip: status,
        icon: const Icon(Icons.sync),
        onPressed: () async {
          if (!ble.isReady) {
            ScaffoldMessenger.of(context)
                .showSnackBar(const SnackBar(content: Text('请先连接并验证手表蓝牙')));
            return;
          }
          await ble.syncAuditRecords();
          if (context.mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(content: Text(ble.auditSyncStatus.value)));
          }
        },
      ),
    );
  }
}
