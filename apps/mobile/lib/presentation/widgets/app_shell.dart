import 'package:flutter/material.dart';

import '../../core/theme/app_theme.dart';

/// One navigation owner; changing window size never recreates page state.
class AppShell extends StatefulWidget {
  const AppShell(
      {super.key,
      required this.pages,
      this.selectedIndex,
      this.onDestinationSelected});
  final List<Widget> pages;
  final int? selectedIndex;
  final ValueChanged<int>? onDestinationSelected;

  @override
  State<AppShell> createState() => _AppShellState();
}

class _AppShellState extends State<AppShell> {
  int _selected = 0;
  int get _index => widget.selectedIndex ?? _selected;
  void _select(int index) {
    if (widget.onDestinationSelected case final callback?) {
      callback(index);
    } else {
      setState(() => _selected = index);
    }
  }

  static const labels = ['设备', 'USB 工具', '无线记录', '设置', '调试', '时间与天气'];
  static const icons = [
    Icons.watch_outlined,
    Icons.keyboard_outlined,
    Icons.wifi_tethering_outlined,
    Icons.tune,
    Icons.terminal,
    Icons.wb_cloudy_outlined
  ];

  @override
  Widget build(BuildContext context) =>
      LayoutBuilder(builder: (context, bounds) {
        assert(widget.pages.length <= labels.length);
        final wide = bounds.maxWidth >= 920;
        final content = IndexedStack(index: _index, children: [
          for (var index = 0; index < widget.pages.length; index++)
            TickerMode(enabled: index == _index, child: widget.pages[index]),
        ]);
        return Scaffold(
          backgroundColor: AppPalette.canvas,
          body: Row(children: [
            if (wide)
              SizedBox(
                key: const ValueKey('desktop-sidebar'),
                width: 220,
                child: SafeArea(
                    child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    const Padding(
                      padding: EdgeInsets.fromLTRB(28, 30, 24, 36),
                      child: Text('Crabit',
                          style: TextStyle(
                              fontSize: 26,
                              fontWeight: FontWeight.w500,
                              letterSpacing: -0.8)),
                    ),
                    Expanded(
                        child: ListView(children: [
                      for (var index = 0; index < widget.pages.length; index++)
                        Padding(
                          padding: const EdgeInsets.fromLTRB(12, 0, 12, 6),
                          child: Material(
                            color: _index == index
                                ? AppPalette.blueSurface
                                : Colors.transparent,
                            borderRadius: BorderRadius.circular(28),
                            child: ListTile(
                              key: ValueKey('app-nav-$index'),
                              shape: const StadiumBorder(),
                              selected: _index == index,
                              selectedColor: const Color(0xFF174EA6),
                              contentPadding:
                                  const EdgeInsets.symmetric(horizontal: 20),
                              leading: Icon(icons[index], size: 22),
                              title: Text(labels[index],
                                  style: const TextStyle(fontSize: 14)),
                              onTap: () => _select(index),
                            ),
                          ),
                        ),
                    ])),
                    const Padding(
                        padding: EdgeInsets.all(28),
                        child: Text('手表工作空间',
                            style: TextStyle(
                                color: AppPalette.muted, fontSize: 12))),
                  ],
                )),
              ),
            Expanded(
                child: Padding(
              padding: wide
                  ? const EdgeInsets.fromLTRB(0, 12, 12, 12)
                  : EdgeInsets.zero,
              child: ClipRRect(
                  borderRadius: BorderRadius.circular(wide ? 24 : 0),
                  child: content),
            )),
          ]),
          bottomNavigationBar: wide ||
                  MediaQuery.viewInsetsOf(context).bottom > 0
              ? null
              : NavigationBar(
                  key: const ValueKey('mobile-navigation'),
                  selectedIndex: _index,
                  onDestinationSelected: _select,
                  destinations: [
                    for (var index = 0; index < widget.pages.length; index++)
                      NavigationDestination(
                          icon: Icon(icons[index]), label: labels[index]),
                  ],
                ),
        );
      });
}
