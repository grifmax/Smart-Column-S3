import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../../core/api/api_client.dart';
import '../../../../core/api/models/models.dart';
import '../../device_connection/providers/device_provider.dart';

class ProfilesNotifier extends StateNotifier<AsyncValue<List<ProfileListItem>>> {
  ProfilesNotifier(this._apiClient) : super(const AsyncValue.loading()) {
    _loadProfiles();
  }

  final ApiClient? _apiClient;

  Future<void> _loadProfiles() async {
    if (_apiClient == null) {
      state = const AsyncValue.error('Устройство не подключено', StackTrace.empty);
      return;
    }

    try {
      final profiles = await _apiClient!.getProfiles();
      state = AsyncValue.data(profiles);
    } catch (e, stack) {
      state = AsyncValue.error(e, stack);
    }
  }

  Future<void> refresh() => _loadProfiles();

  Future<bool> deleteProfile(String id) async {
    if (_apiClient == null) return false;
    try {
      await _apiClient!.deleteProfile(id);
      await _loadProfiles();
      return true;
    } catch (e) {
      return false;
    }
  }
}

final profilesProvider = StateNotifierProvider<ProfilesNotifier, AsyncValue<List<ProfileListItem>>>((ref) {
  final apiClient = ref.watch(currentApiClientProvider);
  return ProfilesNotifier(apiClient);
});

class ProfileDetailNotifier extends StateNotifier<AsyncValue<Profile>> {
  ProfileDetailNotifier(this._apiClient, this._profileId) : super(const AsyncValue.loading()) {
    _loadProfile();
  }

  final ApiClient? _apiClient;
  final String _profileId;

  Future<void> _loadProfile() async {
    if (_apiClient == null) {
      state = const AsyncValue.error('Устройство не подключено', StackTrace.empty);
      return;
    }

    try {
      final profile = await _apiClient!.getProfile(_profileId);
      state = AsyncValue.data(profile);
    } catch (e, stack) {
      state = AsyncValue.error(e, stack);
    }
  }

  Future<bool> loadProfile() async {
    if (_apiClient == null) return false;
    try {
      await _apiClient!.loadProfile(_profileId);
      return true;
    } catch (e) {
      return false;
    }
  }
}

final profileDetailProvider = StateNotifierProvider.family<ProfileDetailNotifier, AsyncValue<Profile>, String>((ref, profileId) {
  final apiClient = ref.watch(currentApiClientProvider);
  return ProfileDetailNotifier(apiClient, profileId);
});

