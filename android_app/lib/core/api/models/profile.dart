import 'package:json_annotation/json_annotation.dart';

part 'profile.g.dart';

@JsonSerializable()
class ProfileListItem {
  final String id;
  final String name;
  final String category;
  @JsonKey(name: 'useCount')
  final int useCount;
  @JsonKey(name: 'lastUsed')
  final int lastUsed;
  @JsonKey(name: 'isBuiltin')
  final bool isBuiltin;

  ProfileListItem({
    required this.id,
    required this.name,
    required this.category,
    required this.useCount,
    required this.lastUsed,
    required this.isBuiltin,
  });

  factory ProfileListItem.fromJson(Map<String, dynamic> json) =>
      _$ProfileListItemFromJson(json);
  Map<String, dynamic> toJson() => _$ProfileListItemToJson(this);
}

@JsonSerializable()
class ProfileMetadata {
  final String name;
  final String description;
  final String category;
  final List<String> tags;
  final int created;
  final int updated;
  final String author;
  @JsonKey(name: 'isBuiltin')
  final bool isBuiltin;

  ProfileMetadata({
    required this.name,
    required this.description,
    required this.category,
    required this.tags,
    required this.created,
    required this.updated,
    required this.author,
    required this.isBuiltin,
  });

  factory ProfileMetadata.fromJson(Map<String, dynamic> json) =>
      _$ProfileMetadataFromJson(json);
  Map<String, dynamic> toJson() => _$ProfileMetadataToJson(this);
}

@JsonSerializable()
class ProfileParameters {
  final String mode;
  final int model;
  final HeaterParameters heater;
  final RectificationParameters rectification;
  final DistillationParameters distillation;
  final TemperatureParameters temperatures;
  final SafetyParameters safety;

  ProfileParameters({
    required this.mode,
    required this.model,
    required this.heater,
    required this.rectification,
    required this.distillation,
    required this.temperatures,
    required this.safety,
  });

  factory ProfileParameters.fromJson(Map<String, dynamic> json) =>
      _$ProfileParametersFromJson(json);
  Map<String, dynamic> toJson() => _$ProfileParametersToJson(this);
}

@JsonSerializable()
class HeaterParameters {
  @JsonKey(name: 'maxPower')
  final int maxPower;
  @JsonKey(name: 'autoMode')
  final bool autoMode;
  @JsonKey(name: 'pidKp')
  final double? pidKp;
  @JsonKey(name: 'pidKi')
  final double? pidKi;
  @JsonKey(name: 'pidKd')
  final double? pidKd;

  HeaterParameters({
    required this.maxPower,
    required this.autoMode,
    this.pidKp,
    this.pidKi,
    this.pidKd,
  });

  factory HeaterParameters.fromJson(Map<String, dynamic> json) =>
      _$HeaterParametersFromJson(json);
  Map<String, dynamic> toJson() => _$HeaterParametersToJson(this);
}

@JsonSerializable()
class RectificationParameters {
  @JsonKey(name: 'stabilizationMin')
  final int stabilizationMin;
  @JsonKey(name: 'headsVolume')
  final double headsVolume;
  @JsonKey(name: 'bodyVolume')
  final double bodyVolume;
  @JsonKey(name: 'tailsVolume')
  final double tailsVolume;
  @JsonKey(name: 'headsSpeed')
  final double headsSpeed;
  @JsonKey(name: 'bodySpeed')
  final double bodySpeed;
  @JsonKey(name: 'tailsSpeed')
  final double tailsSpeed;
  @JsonKey(name: 'purgeMin')
  final int purgeMin;

  RectificationParameters({
    required this.stabilizationMin,
    required this.headsVolume,
    required this.bodyVolume,
    required this.tailsVolume,
    required this.headsSpeed,
    required this.bodySpeed,
    required this.tailsSpeed,
    required this.purgeMin,
  });

  factory RectificationParameters.fromJson(Map<String, dynamic> json) =>
      _$RectificationParametersFromJson(json);
  Map<String, dynamic> toJson() => _$RectificationParametersToJson(this);
}

@JsonSerializable()
class DistillationParameters {
  @JsonKey(name: 'headsVolume')
  final double headsVolume;
  @JsonKey(name: 'targetVolume')
  final double targetVolume;
  final double speed;
  @JsonKey(name: 'endTemp')
  final double endTemp;

  DistillationParameters({
    required this.headsVolume,
    required this.targetVolume,
    required this.speed,
    required this.endTemp,
  });

  factory DistillationParameters.fromJson(Map<String, dynamic> json) =>
      _$DistillationParametersFromJson(json);
  Map<String, dynamic> toJson() => _$DistillationParametersToJson(this);
}

@JsonSerializable()
class TemperatureParameters {
  @JsonKey(name: 'maxCube')
  final double maxCube;
  @JsonKey(name: 'maxColumn')
  final double maxColumn;
  @JsonKey(name: 'headsEnd')
  final double headsEnd;
  @JsonKey(name: 'bodyStart')
  final double bodyStart;
  @JsonKey(name: 'bodyEnd')
  final double bodyEnd;

  TemperatureParameters({
    required this.maxCube,
    required this.maxColumn,
    required this.headsEnd,
    required this.bodyStart,
    required this.bodyEnd,
  });

  factory TemperatureParameters.fromJson(Map<String, dynamic> json) =>
      _$TemperatureParametersFromJson(json);
  Map<String, dynamic> toJson() => _$TemperatureParametersToJson(this);
}

@JsonSerializable()
class SafetyParameters {
  @JsonKey(name: 'maxRuntime')
  final int maxRuntime;
  @JsonKey(name: 'waterFlowMin')
  final double waterFlowMin;
  @JsonKey(name: 'pressureMax')
  final double pressureMax;

  SafetyParameters({
    required this.maxRuntime,
    required this.waterFlowMin,
    required this.pressureMax,
  });

  factory SafetyParameters.fromJson(Map<String, dynamic> json) =>
      _$SafetyParametersFromJson(json);
  Map<String, dynamic> toJson() => _$SafetyParametersToJson(this);
}

@JsonSerializable()
class ProfileStatistics {
  @JsonKey(name: 'useCount')
  final int useCount;
  @JsonKey(name: 'lastUsed')
  final int lastUsed;
  @JsonKey(name: 'avgDuration')
  final double? avgDuration;
  @JsonKey(name: 'avgYield')
  final double? avgYield;
  @JsonKey(name: 'successRate')
  final double? successRate;

  ProfileStatistics({
    required this.useCount,
    required this.lastUsed,
    this.avgDuration,
    this.avgYield,
    this.successRate,
  });

  factory ProfileStatistics.fromJson(Map<String, dynamic> json) =>
      _$ProfileStatisticsFromJson(json);
  Map<String, dynamic> toJson() => _$ProfileStatisticsToJson(this);
}

@JsonSerializable()
class Profile {
  final String id;
  final ProfileMetadata metadata;
  final ProfileParameters parameters;
  final ProfileStatistics statistics;

  Profile({
    required this.id,
    required this.metadata,
    required this.parameters,
    required this.statistics,
  });

  factory Profile.fromJson(Map<String, dynamic> json) =>
      _$ProfileFromJson(json);
  Map<String, dynamic> toJson() => _$ProfileToJson(this);
}

