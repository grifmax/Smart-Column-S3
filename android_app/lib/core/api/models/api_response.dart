import 'package:json_annotation/json_annotation.dart';

part 'api_response.g.dart';

@JsonSerializable()
class ApiResponse {
  final bool success;
  final String? message;
  final String? error;
  final Map<String, dynamic>? data;

  ApiResponse({
    required this.success,
    this.message,
    this.error,
    this.data,
  });

  factory ApiResponse.fromJson(Map<String, dynamic> json) =>
      _$ApiResponseFromJson(json);
  Map<String, dynamic> toJson() => _$ApiResponseToJson(this);
}

