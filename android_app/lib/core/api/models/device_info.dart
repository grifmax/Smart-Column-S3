import 'package:json_annotation/json_annotation.dart';

part 'device_info.g.dart';

@JsonSerializable()
class DeviceInfo {
  @JsonKey(name: 'firmware_version')
  final String? firmwareVersion;
  final String? hardware;
  final int? uptime;
  @JsonKey(name: 'free_heap')
  final int? freeHeap;
  @JsonKey(name: 'wifi_rssi')
  final int? wifiRssi;
  @JsonKey(name: 'ip_address')
  final String? ipAddress;

  DeviceInfo({
    this.firmwareVersion,
    this.hardware,
    this.uptime,
    this.freeHeap,
    this.wifiRssi,
    this.ipAddress,
  });

  factory DeviceInfo.fromJson(Map<String, dynamic> json) =>
      _$DeviceInfoFromJson(json);
  Map<String, dynamic> toJson() => _$DeviceInfoToJson(this);
}

@JsonSerializable()
class DeviceConnection {
  final String host;
  final String? port;
  final String? username;
  final String? password;
  final String? name;

  DeviceConnection({
    required this.host,
    this.port,
    this.username,
    this.password,
    this.name,
  });

  String get baseUrl {
    return 'http://$host${port != null && port!.isNotEmpty ? ':$port' : ''}';
  }

  String get wsUrl {
    return 'ws://$host${port != null && port!.isNotEmpty ? ':$port' : ''}/ws';
  }

  factory DeviceConnection.fromJson(Map<String, dynamic> json) =>
      _$DeviceConnectionFromJson(json);
  Map<String, dynamic> toJson() => _$DeviceConnectionToJson(this);
}

