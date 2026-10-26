import 'package:flutter/material.dart';

/// Presentation chrome only; the child retains the native model/session.
class WatchPreviewStage extends StatelessWidget {
  const WatchPreviewStage(
      {super.key,
      required this.child,
      required this.expanded,
      required this.onExpand,
      this.interactive = true});
  final Widget child;
  final bool expanded;
  final bool interactive;
  final VoidCallback onExpand;
  @override
  Widget build(BuildContext context) => ClipRRect(
        borderRadius: BorderRadius.circular(24),
        child: ColoredBox(
            color: const Color(0xFFF3F5F7),
            child: Column(children: [
              Padding(
                  padding: const EdgeInsets.fromLTRB(16, 6, 8, 0),
                  child: Row(children: [
                    Tooltip(
                        message: '本地界面或模型预览，不是实体手表的实时屏幕',
                        child: Row(mainAxisSize: MainAxisSize.min, children: [
                          Icon(Icons.visibility_outlined, size: 16),
                          SizedBox(width: 6),
                          Text(interactive ? '界面预览' : '模型预览'),
                        ])),
                    const Spacer(),
                    IconButton(
                        key: const ValueKey('watch-expand'),
                        tooltip: expanded ? '收起预览' : '放大操作',
                        onPressed: onExpand,
                        icon: Icon(expanded
                            ? Icons.fullscreen_exit
                            : Icons.fullscreen)),
                  ])),
              Expanded(child: child),
              Padding(
                  padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
                  child: Text(
                      !interactive
                          ? '模型预览 · NanoUI 交互暂限 Windows'
                          : expanded
                              ? '点击按键操作 · 右侧长按返回 · 拖动旋转'
                              : '点击模型上的按键体验界面',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodySmall)),
            ])),
      );
}
