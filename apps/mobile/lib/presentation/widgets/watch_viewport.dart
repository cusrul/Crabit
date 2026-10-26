import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_inappwebview/flutter_inappwebview.dart';
import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import '../../core/device/device_center_controller.dart';

import '../../core/pose/imu_pose_poller.dart';
import '../../core/preview/watch_preview_session.dart';

/// One persistent model surface and NanoUI session. Resize this element for
/// inspection rather than launching a second native instance.
class WatchViewport extends StatefulWidget {
  const WatchViewport({
    super.key,
    required this.active,
    this.inspect = false,
    this.onScroll,
    this.deviceController,
  });
  final bool active;
  final bool inspect;
  final ValueChanged<double>? onScroll;
  final DeviceCenterController? deviceController;
  @override
  State<WatchViewport> createState() => _WatchViewportState();
}

class _WatchViewportState extends State<WatchViewport>
    with WidgetsBindingObserver {
  WatchPreviewSession? _session;
  InAppWebViewController? _web;
  String? _error;
  bool _ready = false, _tabVisible = true, _appVisible = true;
  Timer? _deadline;
  late final ImuPosePoller _posePoller;
  bool get _active => widget.active && _tabVisible && _appVisible;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    final ble = BleManager();
    _posePoller = ImuPosePoller(
      read: () async =>
          (await ble.requestDiagnosticChecked('diag/imu', '')).value,
      // Sample only between foreground operations on the verified product link.
      canReadBackground: () => ble.canReadBackground,
    )..addListener(_poseChanged);
    widget.deviceController?.addListener(_deviceChanged);
    _appVisible = !_hidden(WidgetsBinding.instance.lifecycleState);
    unawaited(_open());
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _tabVisible = TickerMode.valuesOf(context).enabled;
    _sync();
  }

  @override
  void didUpdateWidget(WatchViewport oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.active != widget.active ||
        oldWidget.inspect != widget.inspect ||
        oldWidget.deviceController != widget.deviceController) {
      oldWidget.deviceController?.removeListener(_deviceChanged);
      widget.deviceController?.addListener(_deviceChanged);
      _sync();
    }
  }

  static bool _hidden(AppLifecycleState? state) =>
      state == AppLifecycleState.hidden ||
      state == AppLifecycleState.paused ||
      state == AppLifecycleState.detached;
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // A focused native WebView reports inactive on Windows while still usable.
    _appVisible = !_hidden(state);
    _sync();
  }

  Future<void> _open() async {
    _deadline = Timer(const Duration(seconds: 45), () => _fail('预览加载超时'));
    try {
      final session = await WatchPreviewSession.open(
        simulate: Platform.isWindows,
      );
      if (!mounted) {
        await session.close();
        return;
      }
      setState(() => _session = session);
      _sync();
    } catch (error) {
      _fail('$error');
    }
  }

  void _sync() {
    final session = _session;
    if (session == null) return;
    if (_active) {
      session.resume();
    } else {
      session.pause();
    }
    final web = _web;
    _posePoller.setActive(
      _active && _ready && (widget.deviceController?.isReady ?? false),
    );
    if (!_ready || web == null) return;
    unawaited(
      web
          .evaluateJavascript(
        source:
            'window.crabitPreview?.setPresentation({active:$_active,inspect:${widget.inspect},follow:${_posePoller.enabled}})',
      )
          .catchError((Object error) {
        if (mounted && _web == web) _fail('预览已中断，请重试');
        return null;
      }),
    );
  }

  void _deviceChanged() {
    if (!mounted) return;
    if (!(widget.deviceController?.isReady ?? false) && _posePoller.enabled) {
      _posePoller.setEnabled(false);
    }
    setState(() {});
    _sync();
  }

  void _poseChanged({bool snap = false}) {
    if (!mounted) return;
    setState(() {});
    _sync();
    final pose = _posePoller.pose;
    final web = _web;
    if (!_ready || web == null) return;
    final quaternion = pose?.quaternion ?? const [0.0, 0.0, 0.0, 1.0];
    unawaited(
      web
          .evaluateJavascript(
        source:
            'window.crabitPreview?.setPose(${quaternion.join(',')},${snap || pose == null})',
      )
          .catchError((Object error) {
        if (mounted && _web == web) _fail('预览已中断，请重试');
        return null;
      }),
    );
  }

  void _fail(String error) {
    if (!mounted || _error != null) return;
    _deadline?.cancel();
    final session = _session;
    setState(() {
      _session = null;
      _web = null;
      _error = error;
      _ready = false;
    });
    if (session != null) unawaited(session.close());
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    widget.deviceController?.removeListener(_deviceChanged);
    _posePoller.removeListener(_poseChanged);
    _posePoller.dispose();
    _deadline?.cancel();
    _web = null;
    final session = _session;
    _session = null;
    if (session != null) unawaited(session.close());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.watch_outlined, size: 48),
              const SizedBox(height: 12),
              const Text('界面预览暂不可用'),
              const SizedBox(height: 6),
              Text(
                _error!,
                style: Theme.of(context).textTheme.bodySmall,
                textAlign: TextAlign.center,
              ),
              TextButton(
                onPressed: () {
                  setState(() => _error = null);
                  unawaited(_open());
                },
                child: const Text('重新加载'),
              ),
            ],
          ),
        ),
      );
    }
    return Stack(
      children: [
        if (_session case final session?)
          Positioned.fill(
            child: InAppWebView(
              initialUrlRequest: URLRequest(
                url: WebUri(session.uri.toString()),
              ),
              initialSettings: InAppWebViewSettings(
                transparentBackground: true,
                useShouldOverrideUrlLoading: true,
              ),
              onWebViewCreated: (controller) {
                _web = controller;
                controller.addJavaScriptHandler(
                  handlerName: 'nanoButton',
                  callback: (args) {
                    if (_active &&
                        args.length == 2 &&
                        args[0] is int &&
                        args[1] is int) {
                      session.button(args[0] as int, args[1] as int);
                    }
                    return null;
                  },
                );
                controller.addJavaScriptHandler(
                  handlerName: 'nanoReady',
                  callback: (_) {
                    _deadline?.cancel();
                    if (mounted && _web == controller) {
                      setState(() => _ready = true);
                      _sync();
                    }
                    return null;
                  },
                );
                controller.addJavaScriptHandler(
                  handlerName: 'nanoError',
                  callback: (args) {
                    _fail(args.firstOrNull?.toString() ?? '模型加载失败');
                    return null;
                  },
                );
                controller.addJavaScriptHandler(
                  handlerName: 'nanoScroll',
                  callback: (args) {
                    if (_active &&
                        !widget.inspect &&
                        args.length == 1 &&
                        args[0] is num) {
                      final delta = (args[0] as num).toDouble();
                      if (delta.isFinite) {
                        widget.onScroll?.call(delta.clamp(-1000, 1000));
                      }
                    }
                    return null;
                  },
                );
              },
              shouldOverrideUrlLoading: (_, action) async =>
                  session.permits(action.request.url)
                      ? NavigationActionPolicy.ALLOW
                      : NavigationActionPolicy.CANCEL,
              onReceivedError: (_, request, error) {
                if (request.isForMainFrame == true) _fail(error.description);
              },
            ),
          ),
        if (!_ready) const Center(child: CircularProgressIndicator()),
        Positioned(
          left: 8,
          right: 8,
          bottom: 8,
          child: Material(
            color: Theme.of(
              context,
            ).colorScheme.surface.withValues(alpha: 0.94),
            borderRadius: BorderRadius.circular(12),
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
              child: Wrap(
                alignment: WrapAlignment.center,
                crossAxisAlignment: WrapCrossAlignment.center,
                spacing: 4,
                children: [
                  TextButton.icon(
                    onPressed: !_active ||
                            !(widget.deviceController?.isReady ?? false)
                        ? null
                        : () => _posePoller.setEnabled(!_posePoller.enabled),
                    icon: Icon(
                      _posePoller.enabled
                          ? Icons.pause_circle_outline
                          : Icons.screen_rotation_outlined,
                      size: 17,
                    ),
                    label: Text(_posePoller.enabled ? '停止倾斜' : 'IMU 倾斜'),
                  ),
                  TextButton(
                    onPressed: _posePoller.pose == null
                        ? null
                        : () {
                            _posePoller.calibrate();
                            _poseChanged(snap: true);
                          },
                    child: const Text('校准零位'),
                  ),
                  Text(
                    _posePoller.status + (_posePoller.stale ? ' · 过期' : ''),
                    style: Theme.of(context).textTheme.labelSmall,
                  ),
                ],
              ),
            ),
          ),
        ),
      ],
    );
  }
}
