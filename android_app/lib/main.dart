import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'core/storage/local_storage.dart';
import 'core/storage/cache_manager.dart';
import 'app.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  
  // Инициализация хранилища
  await LocalStorage.init();
  await CacheManager.init();
  
  runApp(
    const ProviderScope(
      child: SmartColumnApp(),
    ),
  );
}

