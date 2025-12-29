import 'package:json_annotation/json_annotation.dart';

part 'system_state.g.dart';

enum Mode {
  @JsonValue(0)
  idle,
  @JsonValue(1)
  rectification,
  @JsonValue(2)
  distillation,
  @JsonValue(3)
  manualRect,
  @JsonValue(4)
  mashing,
  @JsonValue(5)
  hold,
}

enum RectPhase {
  @JsonValue(0)
  idle,
  @JsonValue(1)
  heating,
  @JsonValue(2)
  stabilization,
  @JsonValue(3)
  heads,
  @JsonValue(4)
  postHeadsStabilization,
  @JsonValue(5)
  body,
  @JsonValue(6)
  tails,
  @JsonValue(7)
  purge,
  @JsonValue(8)
  finish,
  @JsonValue(9)
  completed,
}

@JsonSerializable()
class TemperatureData {
  final double cube;
  @JsonKey(name: 'columnBottom')
  final double columnBottom;
  @JsonKey(name: 'columnTop')
  final double columnTop;
  final double reflux;
  final double deflegmator;
  final double product;
  final double tsa;
  @JsonKey(name: 'waterIn')
  final double waterIn;
  @JsonKey(name: 'waterOut')
  final double waterOut;

  TemperatureData({
    required this.cube,
    required this.columnBottom,
    required this.columnTop,
    required this.reflux,
    required this.deflegmator,
    required this.product,
    required this.tsa,
    required this.waterIn,
    required this.waterOut,
  });

  factory TemperatureData.fromJson(Map<String, dynamic> json) =>
      _$TemperatureDataFromJson(json);
  Map<String, dynamic> toJson() => _$TemperatureDataToJson(this);
}

@JsonSerializable()
class PressureData {
  final double cube;
  final double atm;
  final double kpa;

  PressureData({
    required this.cube,
    required this.atm,
    required this.kpa,
  });

  factory PressureData.fromJson(Map<String, dynamic> json) =>
      _$PressureDataFromJson(json);
  Map<String, dynamic> toJson() => _$PressureDataToJson(this);
}

@JsonSerializable()
class PowerData {
  final double voltage;
  final double current;
  final double power;
  final double energy;
  final double frequency;
  @JsonKey(name: 'pf')
  final double powerFactor;

  PowerData({
    required this.voltage,
    required this.current,
    required this.power,
    required this.energy,
    required this.frequency,
    required this.powerFactor,
  });

  factory PowerData.fromJson(Map<String, dynamic> json) =>
      _$PowerDataFromJson(json);
  Map<String, dynamic> toJson() => _$PowerDataToJson(this);
}

@JsonSerializable()
class PumpData {
  @JsonKey(name: 'speedMlH')
  final double speedMlPerHour;
  @JsonKey(name: 'totalMl')
  final double totalVolumeMl;
  final bool running;

  PumpData({
    required this.speedMlPerHour,
    required this.totalVolumeMl,
    required this.running,
  });

  factory PumpData.fromJson(Map<String, dynamic> json) =>
      _$PumpDataFromJson(json);
  Map<String, dynamic> toJson() => _$PumpDataToJson(this);
}

@JsonSerializable()
class HydrometerData {
  final double abv;
  final double density;
  final bool valid;

  HydrometerData({
    required this.abv,
    required this.density,
    required this.valid,
  });

  factory HydrometerData.fromJson(Map<String, dynamic> json) =>
      _$HydrometerDataFromJson(json);
  Map<String, dynamic> toJson() => _$HydrometerDataToJson(this);
}

@JsonSerializable()
class VolumeData {
  final double heads;
  final double body;
  final double tails;

  VolumeData({
    required this.heads,
    required this.body,
    required this.tails,
  });

  factory VolumeData.fromJson(Map<String, dynamic> json) =>
      _$VolumeDataFromJson(json);
  Map<String, dynamic> toJson() => _$VolumeDataToJson(this);
}

@JsonSerializable()
class SystemState {
  final int mode;
  @JsonKey(name: 'modeStr')
  final String? modeStr;
  final int phase;
  @JsonKey(name: 'phaseStr')
  final String? phaseStr;
  final bool paused;
  @JsonKey(name: 'safetyOk')
  final bool safetyOk;
  final int uptime;
  final TemperatureData temps;
  final PressureData pressure;
  final PowerData power;
  final PumpData pump;
  final HydrometerData hydrometer;
  final VolumeData volumes;

  SystemState({
    required this.mode,
    this.modeStr,
    required this.phase,
    this.phaseStr,
    required this.paused,
    required this.safetyOk,
    required this.uptime,
    required this.temps,
    required this.pressure,
    required this.power,
    required this.pump,
    required this.hydrometer,
    required this.volumes,
  });

  factory SystemState.fromJson(Map<String, dynamic> json) =>
      _$SystemStateFromJson(json);
  Map<String, dynamic> toJson() => _$SystemStateToJson(this);
}

