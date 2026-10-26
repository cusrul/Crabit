import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';

/// Shared presentation tokens. Device/protocol code must not depend on these.
abstract final class AppPalette {
  static const canvas = Color(0xFFF8F9FA);
  static const surface = Color(0xFFFFFFFF);
  static const ink = Color(0xFF202124);
  static const muted = Color(0xFF5F6368);
  static const blue = Color(0xFF1A73E8);
  static const blueSurface = Color(0xFFE8F0FE);
  static const line = Color(0xFFDADCE0);
  static const green = Color(0xFF188038);
  static const greenSurface = Color(0xFFE6F4EA);
  static const error = Color(0xFFB3261E);
  static const errorSurface = Color(0xFFFCE8E6);
  static const amber = Color(0xFF8A5A00);
}

abstract final class AppTheme {
  static ThemeData get lightTheme => _build(Brightness.light);
  static ThemeData get darkTheme => _build(Brightness.dark);

  static ThemeData _build(Brightness brightness) {
    final light = brightness == Brightness.light;
    final scheme = ColorScheme.fromSeed(
      seedColor: AppPalette.blue,
      brightness: brightness,
      primary: light ? AppPalette.blue : const Color(0xFFA8C7FA),
      onPrimary: light ? Colors.white : const Color(0xFF062E6F),
      surface: light ? AppPalette.surface : const Color(0xFF202124),
      onSurface: light ? AppPalette.ink : const Color(0xFFE8EAED),
      onSurfaceVariant: light ? AppPalette.muted : const Color(0xFFBDC1C6),
      primaryContainer:
          light ? AppPalette.blueSurface : const Color(0xFF243B5A),
      onPrimaryContainer:
          light ? const Color(0xFF174EA6) : const Color(0xFFD3E3FD),
      outlineVariant: light ? AppPalette.line : const Color(0xFF444746),
    );
    final theme = ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      fontFamily:
          defaultTargetPlatform == TargetPlatform.windows ? 'Segoe UI' : null,
      fontFamilyFallback: const [
        'Microsoft YaHei UI',
        'Microsoft YaHei',
        'Noto Sans CJK SC',
        'sans-serif'
      ],
    );
    return theme.copyWith(
      scaffoldBackgroundColor: scheme.surface,
      dividerColor: scheme.outlineVariant,
      textTheme: theme.textTheme.copyWith(
        headlineLarge: theme.textTheme.headlineLarge?.copyWith(
            fontSize: 34, fontWeight: FontWeight.w500, letterSpacing: -0.7),
        headlineSmall: theme.textTheme.headlineSmall?.copyWith(
            fontSize: 24, fontWeight: FontWeight.w500, letterSpacing: -0.3),
        titleLarge:
            theme.textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w500),
        bodyMedium: theme.textTheme.bodyMedium?.copyWith(height: 1.6),
        bodySmall: theme.textTheme.bodySmall?.copyWith(height: 1.5),
      ),
      appBarTheme: AppBarTheme(
        backgroundColor: scheme.surface,
        foregroundColor: scheme.onSurface,
        surfaceTintColor: Colors.transparent,
        scrolledUnderElevation: 0,
        elevation: 0,
        centerTitle: false,
        titleTextStyle: theme.textTheme.titleLarge?.copyWith(
            color: scheme.onSurface, fontSize: 20, fontWeight: FontWeight.w500),
      ),
      cardTheme: CardThemeData(
        color: scheme.surface,
        surfaceTintColor: Colors.transparent,
        elevation: 0,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(20),
            side: BorderSide(color: scheme.outlineVariant)),
      ),
      filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(
        minimumSize: const Size(48, 44),
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
        textStyle: theme.textTheme.labelLarge
            ?.copyWith(fontSize: 14, fontWeight: FontWeight.w600),
      )),
      outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
        minimumSize: const Size(48, 44),
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
        side: BorderSide(color: scheme.outlineVariant),
      )),
      elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
        backgroundColor: scheme.primary,
        foregroundColor: scheme.onPrimary,
        elevation: 0,
        minimumSize: const Size(48, 44),
      )),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: light ? AppPalette.canvas : const Color(0xFF292A2D),
        contentPadding: const EdgeInsets.all(16),
        border: OutlineInputBorder(
            borderRadius: BorderRadius.circular(12),
            borderSide: BorderSide(color: scheme.outlineVariant)),
        enabledBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(12),
            borderSide: BorderSide(color: scheme.outlineVariant)),
      ),
      navigationBarTheme: NavigationBarThemeData(
        elevation: 0,
        backgroundColor: scheme.surface,
        indicatorColor: scheme.primaryContainer,
        labelTextStyle: WidgetStateProperty.all(
            TextStyle(fontSize: 12, color: scheme.onSurface)),
      ),
      tabBarTheme: TabBarThemeData(
        labelColor: scheme.primary,
        unselectedLabelColor: scheme.onSurfaceVariant,
        indicatorColor: scheme.primary,
        dividerColor: scheme.outlineVariant,
      ),
      snackBarTheme: SnackBarThemeData(
        backgroundColor: scheme.inverseSurface,
        contentTextStyle: TextStyle(color: scheme.onInverseSurface),
        behavior: SnackBarBehavior.floating,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      ),
    );
  }
}
