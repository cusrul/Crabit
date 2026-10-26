import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

/// Keeps Material Slider's value-indicator portal inside its own semantics
/// subtree on Windows. On Flutter 3.41.6 a route transition/hidden page can
/// otherwise submit an orphan node and crash the native accessibility bridge.
/// See flutter/flutter#190357 and #193410. Retain until the SDK fix is verified
/// by slider_semantics_regression_test, not merely until a newer SDK is installed.
class SliderOverlayScope extends StatelessWidget {
  const SliderOverlayScope({super.key, required this.child});

  final Slider child;

  @override
  Widget build(BuildContext context) =>
      defaultTargetPlatform == TargetPlatform.windows
          ? Overlay.wrap(clipBehavior: Clip.none, child: child)
          : child;
}
