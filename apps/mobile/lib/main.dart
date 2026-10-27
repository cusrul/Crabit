import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:crabit_watch_app/core/managers/storage_manager.dart';
import 'package:crabit_watch_app/presentation/pages/home/home_page.dart';
import 'package:crabit_watch_app/core/theme/app_theme.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // 1. 初始化本地存储 (Hive 持久化引擎)
  await StorageManager().init();

  // 开启 Edge-to-Edge 全面屏绘制。
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
  SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
    systemNavigationBarColor: Colors.transparent,
    systemNavigationBarIconBrightness: Brightness.dark,
    statusBarColor: Colors.transparent,
    statusBarIconBrightness: Brightness.dark,
  ));

  // 2. 启动 App
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key, this.home});

  final Widget? home;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Crabit',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.lightTheme,
      home: home ?? const HomePage(),
    );
  }
}
