import 'package:flutter/material.dart';
import '../../widgets/watch_viewport.dart';

/// Standalone development host; the product embeds the same viewport at home.
class WatchSimulatorPage extends StatelessWidget {
  const WatchSimulatorPage({super.key});
  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('界面预览')),
        body: const WatchViewport(active: true, inspect: true),
      );
}
