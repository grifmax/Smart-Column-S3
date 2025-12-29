class AppConstants {
  // API
  static const String defaultApiPort = '80';
  static const String defaultWebSocketPath = '/ws';
  static const int connectionTimeout = 10; // seconds
  static const int receiveTimeout = 30; // seconds
  
  // WebSocket
  static const int websocketReconnectDelay = 3; // seconds
  static const int maxReconnectAttempts = 5;
  
  // Polling
  static const int statusPollInterval = 5; // seconds (fallback if WebSocket unavailable)
  
  // Storage keys
  static const String storageDeviceList = 'device_list';
  static const String storageCurrentDevice = 'current_device';
  static const String storageCredentials = 'credentials';
  static const String storageSettings = 'app_settings';
  
  // Cache
  static const int cacheExpirationMinutes = 5;
}

