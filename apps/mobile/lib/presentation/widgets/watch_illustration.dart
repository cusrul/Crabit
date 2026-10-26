import 'package:flutter/material.dart';

/// Decorative, not a live display or a source of synthetic device statistics.
class WatchIllustration extends StatelessWidget {
  const WatchIllustration({super.key});

  @override
  Widget build(BuildContext context) => Semantics(
        label: '手表示意图，不显示实时数据',
        image: true,
        child: SizedBox(
          width: 178,
          height: 200,
          child: Stack(alignment: Alignment.center, children: [
            Container(
                width: 70,
                height: 200,
                decoration: BoxDecoration(
                    color: const Color(0xFFA4AEC0),
                    borderRadius: BorderRadius.circular(22))),
            Container(
              width: 164,
              height: 112,
              decoration: BoxDecoration(
                  color: const Color(0xFF747E8B),
                  borderRadius: BorderRadius.circular(24),
                  border: Border.all(color: const Color(0xFF5F6A79), width: 3)),
              padding: const EdgeInsets.all(7),
              child: Container(
                decoration: BoxDecoration(
                    color: const Color(0xFF20252C),
                    borderRadius: BorderRadius.circular(15)),
                child: const Center(
                    child: Text('CRABIT',
                        style: TextStyle(
                            color: Color(0xFFD3E3FD),
                            fontSize: 16,
                            fontWeight: FontWeight.w500,
                            letterSpacing: 2))),
              ),
            ),
          ]),
        ),
      );
}
