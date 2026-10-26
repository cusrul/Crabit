import 'package:flutter/material.dart';

class SyncPage extends StatelessWidget {
  const SyncPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('数据中心')),
      body: const Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.sync, size: 64, color: Colors.grey),
            SizedBox(height: 16),
            Text("单词本同步功能开发中...")
          ],
        ),
      ),
    );
  }
}
