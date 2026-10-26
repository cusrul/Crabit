import 'package:flutter/material.dart';

/// Shared page bounds; the caller retains ownership of scrolling and controls.
class AppPage extends StatelessWidget {
  const AppPage({super.key, required this.title, required this.child});
  final String title;
  final Widget child;

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: Text(title)),
        body: SafeArea(
            top: false,
            child: Center(
              child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 1000),
                  child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 24),
                      child: child)),
            )),
      );
}
