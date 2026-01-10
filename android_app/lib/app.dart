import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'shared/theme/app_theme.dart';
import 'features/dashboard/screens/main_navigation_screen.dart';
import 'features/device_connection/screens/device_list_screen.dart';
import 'features/device_connection/providers/device_provider.dart';

class SmartColumnApp extends ConsumerWidget {
  const SmartColumnApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final device = ref.watch(deviceProvider);

    return MaterialApp(
      title: 'Smart Column S3',
      theme: AppTheme.lightTheme,
      darkTheme: AppTheme.darkTheme,
      themeMode: ThemeMode.system,
      debugShowCheckedModeBanner: false,
      home: device == null
          ? const DeviceListScreen()
          : const MainNavigationScreen(),
    );
  }
}

