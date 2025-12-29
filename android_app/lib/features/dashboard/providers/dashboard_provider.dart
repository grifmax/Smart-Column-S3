import 'dart:async';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../../core/api/api_client.dart';
import '../../../../core/api/models/models.dart';
import '../../device_connection/providers/device_provider.dart';

class SystemStateNotifier extends StateNotifier<AsyncValue<SystemState>> {
  SystemStateNotifier(this._apiClient) : super(const AsyncValue.loading()) {
    _loadStatus();
  }

  final ApiClient? _apiClient;
  Timer? _pollTimer;

  Future<void> _loadStatus() async {
    if (_apiClient == null) {
      state = const AsyncValue.error('Устройство не подключено', StackTrace.empty);
      return;
    }

    try {
      final status = await _apiClient!.getStatus();
      state = AsyncValue.data(status);
    } catch (e, stack) {
      state = AsyncValue.error(e, stack);
    }
  }

  void startPolling() {
    _pollTimer?.cancel();
    _pollTimer = Timer.periodic(const Duration(seconds: 5), (_) => _loadStatus());
  }

  void stopPolling() {
    _pollTimer?.cancel();
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }
}

final systemStateProvider = StateNotifierProvider<SystemStateNotifier, AsyncValue<SystemState>>((ref) {
  final apiClient = ref.watch(currentApiClientProvider);
  return SystemStateNotifier(apiClient);
});

class SystemHealthNotifier extends StateNotifier<AsyncValue<SystemHealth>> {
  SystemHealthNotifier(this._apiClient) : super(const AsyncValue.loading()) {
    _loadHealth();
  }

  final ApiClient? _apiClient;

  Future<void> _loadHealth() async {
    if (_apiClient == null) {
      state = const AsyncValue.error('Устройство не подключено', StackTrace.empty);
      return;
    }

    try {
      final health = await _apiClient!.getHealth();
      state = AsyncValue.data(health);
    } catch (e, stack) {
      state = AsyncValue.error(e, stack);
    }
  }

  Future<void> refresh() => _loadHealth();
}

final systemHealthProvider = StateNotifierProvider<SystemHealthNotifier, AsyncValue<SystemHealth>>((ref) {
  final apiClient = ref.watch(currentApiClientProvider);
  return SystemHealthNotifier(apiClient);
});
