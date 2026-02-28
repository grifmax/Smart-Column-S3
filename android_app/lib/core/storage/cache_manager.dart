import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import '../utils/constants.dart';

class CacheManager {
  static SharedPreferences? _prefs;

  static Future<void> init() async {
    _prefs = await SharedPreferences.getInstance();
  }

  static Future<void> cacheData(String key, Map<String, dynamic> data) async {
    final cacheEntry = {
      'data': data,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
    };
    await _prefs?.setString('cache_$key', jsonEncode(cacheEntry));
  }

  static Map<String, dynamic>? getCachedData(String key) {
    final cached = _prefs?.getString('cache_$key');
    if (cached == null) return null;

    try {
      final entry = jsonDecode(cached) as Map<String, dynamic>;
      final timestamp = entry['timestamp'] as int;
      final now = DateTime.now().millisecondsSinceEpoch;
      final ageMinutes = (now - timestamp) / 1000 / 60;

      if (ageMinutes > AppConstants.cacheExpirationMinutes) {
        _prefs?.remove('cache_$key');
        return null;
      }

      return entry['data'] as Map<String, dynamic>;
    } catch (e) {
      return null;
    }
  }

  static Future<void> clearCache(String key) async {
    await _prefs?.remove('cache_$key');
  }

  static Future<void> clearAllCache() async {
    final keys = _prefs?.getKeys() ?? {};
    for (final key in keys) {
      if (key.startsWith('cache_')) {
        await _prefs?.remove(key);
      }
    }
  }
}

