class ApiEndpoints {
  // Base
  static String baseUrl(String host, [String? port, bool useHttps = false]) {
    final protocol = useHttps ? 'https' : 'http';
    final portStr = port != null && port.isNotEmpty ? ':$port' : '';
    // Для HTTPS стандартный порт 443, не добавляем его явно
    if (useHttps && (port == null || port.isEmpty || port == '443')) {
      return '$protocol://$host';
    }
    return '$protocol://$host$portStr';
  }

  static String wsUrl(String host, [String? port, bool useHttps = false, bool isCloudProxy = false]) {
    final protocol = useHttps ? 'wss' : 'ws';
    final portStr = port != null && port.isNotEmpty ? ':$port' : '';
    final path = isCloudProxy ? '/client' : '/ws';
    
    // Для WSS стандартный порт 443, не добавляем его явно
    if (useHttps && (port == null || port.isEmpty || port == '443')) {
      return '$protocol://$host$path';
    }
    return '$protocol://$host$portStr$path';
  }

  // Status
  static const String status = '/api/status';
  static const String health = '/api/health';
  static const String sensors = '/api/sensors';
  static const String version = '/api/version';

  // Process Control
  static const String processStart = '/api/process/start';
  static const String processStop = '/api/process/stop';
  static const String processPause = '/api/process/pause';
  static const String processResume = '/api/process/resume';

  // Pump
  static const String pumpStart = '/api/pump/start';
  static const String pumpStop = '/api/pump/stop';
  static const String pumpStatus = '/api/pump/status';

  // Settings
  static const String settings = '/api/settings';
  static const String settingsDemo = '/api/settings/demo';

  // Calibration
  static const String calibration = '/api/calibration';
  static const String calibrationPump = '/api/calibration/pump';
  static const String calibrationTemp = '/api/calibration/temp';
  static const String calibrationHydrometer = '/api/calibration/hydrometer';
  static const String calibrationScan = '/api/calibration/scan';

  // Profiles
  static String profiles = '/api/profiles';
  static String profileDetail(String id) => '/api/profiles/$id';
  static String profileLoad(String id) => '/api/profiles/$id/load';

  // History
  static const String history = '/api/history';
  static String historyDetail(String id) => '/api/history/$id';
  static String historyExport(String id, String format) => '/api/history/$id/export?format=$format';

  // Energy
  static const String energy = '/api/energy';

  // WiFi
  static const String wifiScan = '/api/wifi/scan';
  static const String wifiStatus = '/api/wifi/status';
  static const String wifiConnect = '/api/wifi/connect';

  // System
  static const String systemReboot = '/api/reboot';
}

