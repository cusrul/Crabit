import 'package:flutter/material.dart';
import 'package:flutter_3d_controller/flutter_3d_controller.dart';

/// Independent, offline model preview. No BLE connection is opened here.
class Watch3DPage extends StatefulWidget {
  const Watch3DPage({super.key});

  @override
  State<Watch3DPage> createState() => _Watch3DPageState();
}

class _Watch3DPageState extends State<Watch3DPage> {
  String? _error;

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('手表模型')),
        body: Stack(children: [
          const Positioned.fill(child: ColoredBox(color: Color(0xFFF3F5F7))),
          Positioned.fill(
            child: Flutter3DViewer(
              src: 'assets/crabit_collection.glb',
              activeGestureInterceptor: false,
              onError: (error) {
                if (mounted) setState(() => _error = error);
              },
            ),
          ),
          if (_error case final error?) Center(child: Text('模型无法显示：$error')),
          const Positioned(
            left: 20,
            bottom: 20,
            child: Text('3D 预览 · 拖动旋转 · 滚轮或双指缩放',
                style: TextStyle(color: Color(0xFF5F6872))),
          ),
        ]),
      );
}
