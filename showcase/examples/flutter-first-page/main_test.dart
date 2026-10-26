import 'package:flutter_test/flutter_test.dart';
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'main.dart';

void main() {
  testWidgets('the page shows and increases its value', (tester) async {
    final output = Platform.environment['CRABIT_FIRST_PAGE_PREVIEW'];
    final boundary = GlobalKey();
    if (output != null) {
      tester.view.physicalSize = const Size(390, 360);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      await tester.runAsync(() async {
        await (FontLoader('Roboto')..addFont(
              File(
                'C:/Windows/Fonts/segoeui.ttf',
              ).readAsBytes().then(ByteData.sublistView),
            ))
            .load();
      });
    }
    await tester.pumpWidget(
      RepaintBoundary(key: boundary, child: const FirstPageApp()),
    );

    expect(find.text('My first page'), findsOneWidget);
    expect(find.text('Current value'), findsOneWidget);
    expect(find.text('Value: 0'), findsOneWidget);
    expect(find.text('Add one'), findsOneWidget);

    await tester.tap(find.text('Add one'));
    await tester.pump();

    expect(find.text('Value: 1'), findsOneWidget);
    expect(find.text('Value: 0'), findsNothing);
    if (output != null) {
      await tester.pumpAndSettle();
      await tester.runAsync(() async {
        final image =
            await (boundary.currentContext!.findRenderObject()!
                    as RenderRepaintBoundary)
                .toImage();
        final png = await image.toByteData(format: ui.ImageByteFormat.png);
        await File(output).writeAsBytes(png!.buffer.asUint8List());
        image.dispose();
      });
    }
  });
}
