import 'package:dio/dio.dart';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'api_client.dart';
import 'websocket_client.dart';
import 'models/models.dart';

/// Облачный прокси-клиент для работы через промежуточный сервер
/// Используется когда ESP32 подключен к облачному серверу,
/// а не доступен напрямую из интернета
class CloudProxyClient {
  final String proxyUrl;
  final String deviceId;
  final String? authToken;
  late Dio _dio;
  WebSocketChannel? _wsChannel;

  CloudProxyClient({
    required this.proxyUrl,
    required this.deviceId,
    this.authToken,
  }) {
    _dio = Dio(
      BaseOptions(
        baseUrl: proxyUrl,
        connectTimeout: const Duration(seconds: 10),
        receiveTimeout: const Duration(seconds: 30),
        headers: {
          'Content-Type': 'application/json',
          'Accept': 'application/json',
          if (authToken != null) 'Authorization': 'Bearer $authToken',
          'X-Device-Id': deviceId,
        },
      ),
    );
  }

  /// Получить статус устройства через прокси
  Future<SystemState> getStatus() async {
    final response = await _dio.get('/api/device/$deviceId/status');
    return SystemState.fromJson(response.data);
  }

  /// Отправить команду через прокси
  Future<ApiResponse> sendCommand(String command, Map<String, dynamic>? data) async {
    final response = await _dio.post(
      '/api/device/$deviceId/command',
      data: {
        'command': command,
        'data': data,
      },
    );
    return ApiResponse.fromJson(response.data);
  }

  /// Подключиться к WebSocket через прокси
  Stream<SystemState> connectWebSocket() {
    final wsUrl = Uri.parse(proxyUrl.replaceFirst('http', 'ws'))
        .replace(path: '/ws/device/$deviceId')
        .replace(queryParameters: {
          if (authToken != null) 'token': authToken!,
        });

    _wsChannel = WebSocketChannel.connect(wsUrl);

    return _wsChannel!.stream.map((message) {
      final json = jsonDecode(message as String) as Map<String, dynamic>;
      return SystemState.fromJson(json);
    });
  }

  void disconnect() {
    _wsChannel?.sink.close();
  }
}

