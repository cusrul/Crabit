import 'package:flutter/material.dart';

// Layout tests do not load a native WebView/DLL or pretend this is model QA.
Widget buildPreviewStub(BuildContext context, bool active, bool inspect) =>
    const ColoredBox(
        color: Color(0xFFF3F5F7), child: Center(child: Text('模型渲染占位')));
