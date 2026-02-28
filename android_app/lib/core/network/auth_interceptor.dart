import 'package:dio/dio.dart';
import 'dart:convert';

class AuthInterceptor extends Interceptor {
  String? username;
  String? password;

  AuthInterceptor({this.username, this.password});

  void setCredentials(String? user, String? pass) {
    username = user;
    password = pass;
  }

  @override
  void onRequest(RequestOptions options, RequestInterceptorHandler handler) {
    if (username != null && password != null && username!.isNotEmpty) {
      final credentials = '$username:$password';
      final encoded = base64Encode(utf8.encode(credentials));
      options.headers['Authorization'] = 'Basic $encoded';
    }
    super.onRequest(options, handler);
  }
}

