import 'package:json_annotation/json_annotation.dart';

part 'system_health.g.dart';

@JsonSerializable()
class SystemHealth {
  @JsonKey(name: 'overallHealth')
  final int overallHealth;
  @JsonKey(name: 'lastUpdate')
  final int lastUpdate;
  
  final TemperatureHealth temperatures;
  final SensorHealth sensors;
  final WifiHealth wifi;
  final SystemInfo system;
  final ErrorInfo errors;

  SystemHealth({
    required this.overallHealth,
    required this.lastUpdate,
    required this.temperatures,
    required this.sensors,
    required this.wifi,
    required this.system,
    required this.errors,
  });

  factory SystemHealth.fromJson(Map<String, dynamic> json) =>
      _$SystemHealthFromJson(json);
  Map<String, dynamic> toJson() => _$SystemHealthToJson(this);
}

@JsonSerializable()
class TemperatureHealth {
  final bool ok;
  final int total;

  TemperatureHealth({
    required this.ok,
    required this.total,
  });

  factory TemperatureHealth.fromJson(Map<String, dynamic> json) =>
      _$TemperatureHealthFromJson(json);
  Map<String, dynamic> toJson() => _$TemperatureHealthToJson(this);
}

@JsonSerializable()
class SensorHealth {
  final bool bmp280;
  final bool ads1115;
  final bool pzem;

  SensorHealth({
    required this.bmp280,
    required this.ads1115,
    required this.pzem,
  });

  factory SensorHealth.fromJson(Map<String, dynamic> json) =>
      _$SensorHealthFromJson(json);
  Map<String, dynamic> toJson() => _$SensorHealthToJson(this);
}

@JsonSerializable()
class WifiHealth {
  final bool connected;
  final int rssi;

  WifiHealth({
    required this.connected,
    required this.rssi,
  });

  factory WifiHealth.fromJson(Map<String, dynamic> json) =>
      _$WifiHealthFromJson(json);
  Map<String, dynamic> toJson() => _$WifiHealthToJson(this);
}

@JsonSerializable()
class SystemInfo {
  final int uptime;
  @JsonKey(name: 'freeHeap')
  final int freeHeap;
  @JsonKey(name: 'cpuTemp')
  final double cpuTemp;

  SystemInfo({
    required this.uptime,
    required this.freeHeap,
    required this.cpuTemp,
  });

  factory SystemInfo.fromJson(Map<String, dynamic> json) =>
      _$SystemInfoFromJson(json);
  Map<String, dynamic> toJson() => _$SystemInfoToJson(this);
}

@JsonSerializable()
class ErrorInfo {
  @JsonKey(name: 'pzemSpikes')
  final int pzemSpikes;
  @JsonKey(name: 'tempErrors')
  final int tempErrors;

  ErrorInfo({
    required this.pzemSpikes,
    required this.tempErrors,
  });

  factory ErrorInfo.fromJson(Map<String, dynamic> json) =>
      _$ErrorInfoFromJson(json);
  Map<String, dynamic> toJson() => _$ErrorInfoToJson(this);
}

