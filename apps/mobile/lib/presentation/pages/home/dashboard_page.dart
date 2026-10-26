import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../../../core/battery/battery_history_controller.dart';
import '../../../core/device/device_center_controller.dart';
import '../../../core/device/device_link.dart';
import '../../../core/managers/ble_manager.dart';
import '../../widgets/device_connection_panel.dart';
import '../../widgets/battery_history_panel.dart';
import '../../widgets/device_reading_card.dart';
import '../../widgets/watch_preview_stage.dart';
import '../../widgets/watch_viewport.dart';
import '../ble/sniffer_page.dart';
import '../misc/transport_diagnostics_page.dart';
import '../settings/settings_page.dart';
import '../tools/badusb_page.dart';
import '../weather/weather_page.dart';
import 'watch_3d_page.dart';

typedef WatchViewportBuilder = Widget Function(
    BuildContext context, bool active, bool inspect);

class DashboardPage extends StatefulWidget {
  const DashboardPage(
      {super.key,
      this.controller,
      this.onNavigate,
      this.previewBuilder,
      this.batteryController});
  final BatteryHistoryController? batteryController;
  final DeviceCenterController? controller;
  final ValueChanged<int>? onNavigate;
  final WatchViewportBuilder? previewBuilder;
  @override
  State<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  late DeviceCenterController _controller;
  BleDeviceLink? _ownedLink;
  final _scroll = ScrollController();
  final _bodyKey = GlobalKey(), _previewKey = GlobalKey();
  bool _expanded = false,
      _visible = true,
      _covered = false,
      _measurePending = false;

  void _attach() {
    if (widget.controller case final controller?) {
      _controller = controller;
    } else {
      _ownedLink = BleDeviceLink(BleManager());
      _controller = DeviceCenterController(link: _ownedLink!);
    }
  }

  void _detach() {
    if (_ownedLink != null) {
      _controller.dispose();
      _ownedLink!.dispose();
      _ownedLink = null;
    }
  }

  @override
  void initState() {
    super.initState();
    _attach();
    _scroll.addListener(_measure);
  }

  @override
  void didUpdateWidget(DashboardPage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != widget.controller) {
      _detach();
      _attach();
    }
  }

  @override
  void dispose() {
    _scroll.dispose();
    _detach();
    super.dispose();
  }

  void _measure() {
    if (_measurePending) return;
    _measurePending = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _measurePending = false;
      if (!mounted) return;
      final view = _previewKey.currentContext?.findRenderObject();
      final body = _bodyKey.currentContext?.findRenderObject();
      if (view is! RenderBox ||
          body is! RenderBox ||
          !view.hasSize ||
          !body.hasSize) {
        return;
      }
      final rect = view.localToGlobal(Offset.zero, ancestor: body) & view.size;
      final shown = rect.overlaps(Offset.zero & body.size);
      if (shown != _visible) setState(() => _visible = shown);
    });
  }

  void _toggleExpanded() {
    if (_scroll.hasClients) _scroll.jumpTo(0);
    setState(() {
      _expanded = !_expanded;
      _visible = true;
    });
    _measure();
  }

  void _scrollBy(double delta) {
    if (!_scroll.hasClients || _expanded) return;
    _scroll.jumpTo((_scroll.offset + delta).clamp(
        _scroll.position.minScrollExtent, _scroll.position.maxScrollExtent));
  }

  Future<void> _open(Widget page) async {
    setState(() => _covered = true);
    try {
      await Navigator.of(context)
          .push(MaterialPageRoute<void>(builder: (_) => page));
    } finally {
      if (mounted) setState(() => _covered = false);
    }
  }

  void _navigate(int index) {
    if (widget.onNavigate case final callback?) {
      callback(index);
    } else {
      unawaited(_open(switch (index) {
        1 => const BadUsbPage(),
        2 => const SnifferPage(),
        5 => const WeatherPage(),
        _ => const SettingsPage()
      }));
    }
  }

  @override
  Widget build(BuildContext context) {
    _measure();
    return PopScope(
        canPop: !_expanded || !TickerMode.valuesOf(context).enabled,
        onPopInvokedWithResult: (didPop, _) {
          if (!didPop && _expanded) _toggleExpanded();
        },
        child: Scaffold(
          appBar: AppBar(
              title: Text(_expanded ? '界面预览' : '设备'),
              leading: _expanded
                  ? IconButton(
                      tooltip: '收起预览',
                      onPressed: _toggleExpanded,
                      icon: const Icon(Icons.arrow_back))
                  : null,
              actions: [
                if (!_expanded)
                  ListenableBuilder(
                      listenable: _controller,
                      builder: (context, child) => PopupMenuButton<String>(
                          tooltip: '更多',
                          onSelected: (value) {
                            if (value == 'diagnostics' &&
                                (_controller.busy ||
                                    _controller.disconnecting)) {
                              return;
                            }
                            unawaited(_open(value == 'model'
                                ? const Watch3DPage()
                                : const TransportDiagnosticsPage()));
                          },
                          itemBuilder: (_) => [
                                const PopupMenuItem(
                                    value: 'model', child: Text('查看模型')),
                                PopupMenuItem(
                                    enabled: !_controller.busy &&
                                        !_controller.disconnecting,
                                    value: 'diagnostics',
                                    child: Text('链路诊断'))
                              ]))
              ]),
          body: LayoutBuilder(key: _bodyKey, builder: _body),
        ));
  }

  Widget _body(BuildContext context, BoxConstraints bounds) {
    _measure(); // LayoutBuilder also runs on resize without rebuilding State.
    final scale = MediaQuery.textScalerOf(context).scale(1);
    final padding = _expanded
        ? 12.0
        : bounds.maxWidth >= 800
            ? 28.0
            : 20.0;
    final width = math.min(1100.0, bounds.maxWidth - padding * 2);
    final wide = !_expanded && width >= 760 && scale <= 1.3;
    final stageWidth = wide ? width - 332 : width;
    final stageHeight = _expanded
        ? math.max(240.0, bounds.maxHeight - padding * 2)
        : wide
            ? 440.0
            : 340.0;
    final active = _visible && !_covered;
    final viewport = widget.previewBuilder?.call(context, active, _expanded) ??
        WatchViewport(
            active: active,
            inspect: _expanded,
            onScroll: _scrollBy,
            deviceController: _controller);
    return SingleChildScrollView(
        key: const ValueKey('device-scroll'),
        controller: _scroll,
        physics: _expanded ? const NeverScrollableScrollPhysics() : null,
        child: Center(
            child: SizedBox(
                width: width,
                child: Padding(
                    padding: EdgeInsets.symmetric(vertical: padding),
                    child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          Offstage(
                              offstage: _expanded,
                              child: Padding(
                                  padding: const EdgeInsets.only(bottom: 24),
                                  child: Column(
                                      crossAxisAlignment:
                                          CrossAxisAlignment.start,
                                      children: [
                                        Text('我的 Crabit',
                                            style: Theme.of(context)
                                                .textTheme
                                                .headlineLarge),
                                        const SizedBox(height: 6),
                                        const Text('体验手表界面，或连接设备查看真实状态。'),
                                      ]))),
                          // A single Flex retains the viewport element across column/row/fullscreen.
                          Flex(
                              direction: wide ? Axis.horizontal : Axis.vertical,
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                SizedBox(
                                    key: _previewKey,
                                    width: stageWidth,
                                    height: stageHeight,
                                    child: WatchPreviewStage(
                                        interactive: Platform.isWindows,
                                        expanded: _expanded,
                                        onExpand: _toggleExpanded,
                                        child: viewport)),
                                Offstage(
                                    offstage: _expanded,
                                    child: SizedBox(
                                        width: wide ? 32 : 0,
                                        height: wide ? 0 : 24)),
                                Offstage(
                                    offstage: _expanded,
                                    child: SizedBox(
                                        width: wide ? 300 : width,
                                        child: ListenableBuilder(
                                            listenable: _controller,
                                            builder: (context, child) =>
                                                DeviceConnectionPanel(
                                                    controller: _controller)))),
                              ]),
                          Offstage(offstage: _expanded, child: _features(wide)),
                        ])))));
  }

  Widget _features(bool wide) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        const SizedBox(height: 28),
        Wrap(spacing: 12, runSpacing: 10, children: [
          _shortcut('USB 工具', Icons.keyboard_outlined, 1),
          _shortcut('无线功能', Icons.wifi_tethering_outlined, 2),
          _shortcut('手表设置', Icons.tune, 3),
          _shortcut('时间与天气', Icons.wb_cloudy_outlined, 5),
        ]),
        const SizedBox(height: 32),
        Text('设备读数', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 6),
        const Text('来自已连接的实体手表，按需读取。'),
        const SizedBox(height: 16),
        ListenableBuilder(
            listenable: _controller,
            builder: (context, child) {
              final status = DeviceReadingCard(
                  controller: _controller,
                  topic: 'diag/snapshot',
                  title: '手表状态',
                  description: '设备返回的最新状态快照。',
                  action: '读取手表状态',
                  icon: Icons.watch_outlined,
                  buttonKey: 'diag-snapshot');
              final wifi = DeviceReadingCard(
                  controller: _controller,
                  topic: 'diag/wifi',
                  title: 'Wi-Fi',
                  description: '查看网络状态，不切换无线模式。',
                  action: '读取 Wi-Fi 状态',
                  icon: Icons.wifi,
                  buttonKey: 'diag-wifi');
              return wide
                  ? Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                          Expanded(child: status),
                          const SizedBox(width: 16),
                          Expanded(child: wifi),
                        ])
                  : Column(
                      children: [status, const SizedBox(height: 12), wifi]);
            }),
        const SizedBox(height: 16),
        if (widget.batteryController case final battery?) ...[
          BatteryHistoryPanel(controller: battery),
          const SizedBox(height: 16),
        ],
        ExpansionTile(
            key: const ValueKey('device-diagnostics'),
            title: const Text('连接诊断'),
            tilePadding: EdgeInsets.zero,
            children: [
              ListenableBuilder(
                  listenable: _controller,
                  builder: (context, child) => DeviceReadingCard(
                      controller: _controller,
                      topic: 'diag/echo',
                      title: '链路检查',
                      description: '检查一条短消息的 STM32 往返。',
                      action: '检查回显',
                      icon: Icons.swap_horiz,
                      buttonKey: 'diag-echo'))
            ]),
        const SizedBox(height: 20),
      ]);

  Widget _shortcut(String label, IconData icon, int index) =>
      OutlinedButton.icon(
          key: ValueKey('device-shortcut-$index'),
          onPressed: () => _navigate(index),
          icon: Icon(icon, size: 18),
          label: Text(label));
}
