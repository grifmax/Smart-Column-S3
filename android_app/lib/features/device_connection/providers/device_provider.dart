import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../../core/api/api_client.dart';
import '../../../../core/api/websocket_client.dart';
import '../../../../core/api/endpoints.dart';
import '../../../../core/storage/local_storage.dart';
import '../../../../core/utils/constants.dart';

final apiClientProvider = Provider<ApiClient>((ref) => ApiClient());

final websocketClientProvider = Provider<WebSocketClient>((ref) => WebSocketClient());

class DeviceConnection {
  final String host;
  final String? port;
  final String? username;
  final String? password;
  final String? name;
  final bool useHttps;
  final String? deviceId; // Для облачного прокси
  final String? proxyToken; // Токен для облачного прокси

  DeviceConnection({
    required this.host,
    this.port,
    this.username,
    this.password,
    this.name,
    this.useHttps = false,
    this.deviceId,
    this.proxyToken,
  });

  String get baseUrl => ApiEndpoints.baseUrl(host, port, useHttps);
  String get wsUrl => ApiEndpoints.wsUrl(host, port, useHttps, deviceId != null && proxyToken != null);
  
  bool get isCloudProxy => deviceId != null && proxyToken != null;

  Map<String, dynamic> toJson() => {
        'host': host,
        'port': port,
        'username': username,
        'password': password,
        'name': name,
        'useHttps': useHttps,
        'deviceId': deviceId,
        'proxyToken': proxyToken,
      };

  factory DeviceConnection.fromJson(Map<String, dynamic> json) => DeviceConnection(
        host: json['host'] as String,
        port: json['port'] as String?,
        username: json['username'] as String?,
        password: json['password'] as String?,
        name: json['name'] as String?,
        useHttps: json['useHttps'] as bool? ?? false,
        deviceId: json['deviceId'] as String?,
        proxyToken: json['proxyToken'] as String?,
      );
}

class DeviceNotifier extends StateNotifier<DeviceConnection?> {
  DeviceNotifier() : super(null) {
    _loadCurrentDevice();
  }

  Future<void> _loadCurrentDevice() async {
    final deviceJson = LocalStorage.getJson(AppConstants.storageCurrentDevice);
    if (deviceJson != null) {
      state = DeviceConnection.fromJson(deviceJson);
    }
  }

  Future<void> setDevice(DeviceConnection device) async {
    state = device;
    await LocalStorage.setJson(AppConstants.storageCurrentDevice, device.toJson());
  }

  Future<void> clearDevice() async {
    state = null;
    await LocalStorage.remove(AppConstants.storageCurrentDevice);
  }
}

final deviceProvider = StateNotifierProvider<DeviceNotifier, DeviceConnection?>((ref) {
  return DeviceNotifier();
});

final currentApiClientProvider = Provider<ApiClient?>((ref) {
  final device = ref.watch(deviceProvider);
  if (device == null) return null;

  final client = ref.read(apiClientProvider);
  client.setBaseUrl(device.baseUrl);
  client.setCredentials(device.username, device.password);
  return client;
});

final currentWebSocketProvider = Provider<WebSocketClient?>((ref) {
  final device = ref.watch(deviceProvider);
  if (device == null) return null;

  final ws = ref.read(websocketClientProvider);
  // Если это облачный прокси, используем токен и deviceId
  if (device.proxyToken != null && device.deviceId != null) {
    ws.connect(device.wsUrl, token: device.proxyToken, deviceId: device.deviceId);
  } else {
    ws.connect(device.wsUrl);
  }
  return ws;
});

