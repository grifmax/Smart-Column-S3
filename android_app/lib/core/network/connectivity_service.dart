import 'package:connectivity_plus/connectivity_plus.dart';

class ConnectivityService {
  final Connectivity _connectivity = Connectivity();

  Stream<ConnectivityResult> get connectivityStream {
    return _connectivity.onConnectivityChanged;
  }

  Future<ConnectivityResult> checkConnectivity() async {
    return await _connectivity.checkConnectivity();
  }

  Future<bool> isConnected() async {
    final result = await checkConnectivity();
    return result != ConnectivityResult.none;
  }
}

