import 'package:dio/dio.dart';
import 'package:dio/io.dart';
import 'dart:io';
import '../network/auth_interceptor.dart';
import '../utils/constants.dart';
import 'endpoints.dart';
import 'models/models.dart';

class ApiClient {
  late Dio _dio;
  final AuthInterceptor _authInterceptor = AuthInterceptor();
  String? _baseUrl;

  ApiClient() {
    _dio = Dio(
      BaseOptions(
        connectTimeout: const Duration(seconds: AppConstants.connectionTimeout),
        receiveTimeout: const Duration(seconds: AppConstants.receiveTimeout),
        headers: {
          'Content-Type': 'application/json',
          'Accept': 'application/json',
        },
        // Для самоподписанных сертификатов (в продакшене использовать валидацию)
        validateStatus: (status) => status! < 500,
      ),
    );

    // Настройка SSL для HTTPS (для самоподписанных сертификатов)
    // ВНИМАНИЕ: В продакшене нужно использовать валидные сертификаты!
    if (_dio.httpClientAdapter is IOHttpClientAdapter) {
      (_dio.httpClientAdapter as IOHttpClientAdapter).onHttpClientCreate = (client) {
        client.badCertificateCallback = (X509Certificate cert, String host, int port) {
          // Разрешаем самоподписанные сертификаты
          // В продакшене это должно быть false с валидными сертификатами
          return true; // ТОЛЬКО ДЛЯ РАЗРАБОТКИ!
        };
        return client;
      };
    }

    _dio.interceptors.add(_authInterceptor);
    _dio.interceptors.add(LogInterceptor(
      requestBody: true,
      responseBody: true,
      error: true,
    ));
  }

  void setBaseUrl(String baseUrl) {
    _baseUrl = baseUrl;
    _dio.options.baseUrl = baseUrl;
  }

  void setCredentials(String? username, String? password) {
    _authInterceptor.setCredentials(username, password);
  }

  // Status
  Future<SystemState> getStatus() async {
    final response = await _dio.get(ApiEndpoints.status);
    return SystemState.fromJson(response.data);
  }

  Future<SystemHealth> getHealth() async {
    final response = await _dio.get(ApiEndpoints.health);
    return SystemHealth.fromJson(response.data);
  }

  // Process Control
  Future<ApiResponse> startProcess({
    required String mode,
    Map<String, dynamic>? params,
  }) async {
    final response = await _dio.post(
      ApiEndpoints.processStart,
      data: {
        'mode': mode,
        if (params != null) 'params': params,
      },
    );
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> stopProcess() async {
    final response = await _dio.post(ApiEndpoints.processStop);
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> pauseProcess() async {
    final response = await _dio.post(ApiEndpoints.processPause);
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> resumeProcess() async {
    final response = await _dio.post(ApiEndpoints.processResume);
    return ApiResponse.fromJson(response.data);
  }

  // Pump
  Future<ApiResponse> startPump({required double speed}) async {
    final response = await _dio.post(
      ApiEndpoints.pumpStart,
      data: {'speed': speed},
    );
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> stopPump() async {
    final response = await _dio.post(ApiEndpoints.pumpStop);
    return ApiResponse.fromJson(response.data);
  }

  // Profiles
  Future<List<ProfileListItem>> getProfiles() async {
    final response = await _dio.get(ApiEndpoints.profiles);
    final data = response.data as Map<String, dynamic>;
    final profiles = data['profiles'] as List;
    return profiles
        .map((p) => ProfileListItem.fromJson(p as Map<String, dynamic>))
        .toList();
  }

  Future<Profile> getProfile(String id) async {
    final response = await _dio.get(ApiEndpoints.profileDetail(id));
    return Profile.fromJson(response.data);
  }

  Future<ApiResponse> loadProfile(String id) async {
    final response = await _dio.post(ApiEndpoints.profileLoad(id));
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> createProfile(Profile profile) async {
    final response = await _dio.post(
      ApiEndpoints.profiles,
      data: profile.toJson(),
    );
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> updateProfile(String id, Profile profile) async {
    final response = await _dio.put(
      ApiEndpoints.profileDetail(id),
      data: profile.toJson(),
    );
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> deleteProfile(String id) async {
    final response = await _dio.delete(ApiEndpoints.profileDetail(id));
    return ApiResponse.fromJson(response.data);
  }

  // Calibration
  Future<Map<String, dynamic>> getCalibration() async {
    final response = await _dio.get(ApiEndpoints.calibration);
    return response.data as Map<String, dynamic>;
  }

  Future<ApiResponse> calibratePump(Map<String, dynamic> data) async {
    final response = await _dio.post(
      ApiEndpoints.calibrationPump,
      data: data,
    );
    return ApiResponse.fromJson(response.data);
  }

  Future<ApiResponse> calibrateTemp(Map<String, dynamic> data) async {
    final response = await _dio.post(
      ApiEndpoints.calibrationTemp,
      data: data,
    );
    return ApiResponse.fromJson(response.data);
  }

  Future<List<Map<String, dynamic>>> scanSensors() async {
    final response = await _dio.get(ApiEndpoints.calibrationScan);
    final data = response.data as Map<String, dynamic>;
    final sensors = data['sensors'] as List;
    return sensors.cast<Map<String, dynamic>>();
  }

  // System
  Future<ApiResponse> reboot() async {
    final response = await _dio.post(ApiEndpoints.systemReboot);
    return ApiResponse.fromJson(response.data);
  }

  // Error handling
  String getErrorMessage(DioException error) {
    if (error.type == DioExceptionType.connectionTimeout ||
        error.type == DioExceptionType.receiveTimeout) {
      return 'Таймаут подключения';
    }
    if (error.type == DioExceptionType.badResponse) {
      if (error.response?.statusCode == 401) {
        return 'Неверные учетные данные';
      }
      if (error.response?.statusCode == 404) {
        return 'Ресурс не найден';
      }
      if (error.response?.statusCode == 429) {
        return 'Слишком много запросов';
      }
      return 'Ошибка сервера: ${error.response?.statusCode}';
    }
    if (error.type == DioExceptionType.connectionError) {
      return 'Ошибка подключения';
    }
    return 'Неизвестная ошибка: ${error.message}';
  }
}

