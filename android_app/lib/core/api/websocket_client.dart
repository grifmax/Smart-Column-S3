import 'dart:async';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import '../utils/constants.dart';
import 'models/models.dart';

enum WebSocketStatus {
  disconnected,
  connecting,
  connected,
  error,
}

class WebSocketClient {
  WebSocketChannel? _channel;
  WebSocketStatus _status = WebSocketStatus.disconnected;
  StreamController<SystemState>? _stateController;
  StreamController<Map<String, dynamic>>? _eventController;
  Timer? _reconnectTimer;
  int _reconnectAttempts = 0;
  String? _url;
  bool _shouldReconnect = false;

  Stream<SystemState> get stateStream => _stateController?.stream ?? const Stream.empty();
  Stream<Map<String, dynamic>> get eventStream => _eventController?.stream ?? const Stream.empty();
  WebSocketStatus get status => _status;

  WebSocketClient() {
    _stateController = StreamController<SystemState>.broadcast();
    _eventController = StreamController<Map<String, dynamic>>.broadcast();
  }

  Future<void> connect(String url, {String? token, String? deviceId}) async {
    if (_status == WebSocketStatus.connected || _status == WebSocketStatus.connecting) {
      return;
    }

    // Добавляем параметры для облачного прокси
    if (token != null && deviceId != null) {
      final uri = Uri.parse(url);
      _url = uri.replace(queryParameters: {
        ...uri.queryParameters,
        'token': token,
        'device': deviceId,
      }).toString();
    } else {
      _url = url;
    }
    
    _shouldReconnect = true;
    await _connectInternal();
  }

  Future<void> _connectInternal() async {
    try {
      _status = WebSocketStatus.connecting;
      _channel = WebSocketChannel.connect(Uri.parse(_url!));

      _channel!.stream.listen(
        _onMessage,
        onError: _onError,
        onDone: _onDone,
        cancelOnError: false,
      );

      _status = WebSocketStatus.connected;
      _reconnectAttempts = 0;
    } catch (e) {
      _status = WebSocketStatus.error;
      _scheduleReconnect();
    }
  }

  void _onMessage(dynamic message) {
    try {
      final data = message as String;
      final json = jsonDecode(data) as Map<String, dynamic>;

      // Check if it's an event
      if (json.containsKey('type') && json['type'] == 'event') {
        _eventController?.add(json);
        return;
      }

      // Try to parse as SystemState (WebSocket sends flat structure)
      try {
        final state = _parseWebSocketState(json);
        _stateController?.add(state);
      } catch (e) {
        // Not a state message, treat as event
        _eventController?.add(json);
      }
    } catch (e) {
      // Ignore parsing errors
    }
  }

  SystemState _parseWebSocketState(Map<String, dynamic> json) {
    // WebSocket sends flat structure, convert to SystemState format
    return SystemState(
      mode: json['mode'] as int? ?? 0,
      modeStr: null,
      phase: json['phase'] as int? ?? 0,
      phaseStr: null,
      paused: false,
      safetyOk: true,
      uptime: json['uptime'] as int? ?? 0,
      temps: TemperatureData(
        cube: (json['t_cube'] as num?)?.toDouble() ?? 0.0,
        columnBottom: (json['t_column_bottom'] as num?)?.toDouble() ?? 0.0,
        columnTop: (json['t_column_top'] as num?)?.toDouble() ?? 0.0,
        reflux: (json['t_reflux'] as num?)?.toDouble() ?? 0.0,
        deflegmator: (json['t_reflux'] as num?)?.toDouble() ?? 0.0,
        product: 0.0,
        tsa: (json['t_tsa'] as num?)?.toDouble() ?? 0.0,
        waterIn: (json['t_water_in'] as num?)?.toDouble() ?? 0.0,
        waterOut: (json['t_water_out'] as num?)?.toDouble() ?? 0.0,
      ),
      pressure: PressureData(
        cube: (json['p_cube'] as num?)?.toDouble() ?? 0.0,
        atm: (json['p_atm'] as num?)?.toDouble() ?? 0.0,
        kpa: 0.0,
      ),
      power: PowerData(
        voltage: (json['voltage'] as num?)?.toDouble() ?? 0.0,
        current: (json['current'] as num?)?.toDouble() ?? 0.0,
        power: (json['power'] as num?)?.toDouble() ?? 0.0,
        energy: (json['energy'] as num?)?.toDouble() ?? 0.0,
        frequency: (json['frequency'] as num?)?.toDouble() ?? 0.0,
        powerFactor: (json['pf'] as num?)?.toDouble() ?? 0.0,
      ),
      pump: PumpData(
        speedMlPerHour: (json['pump_speed'] as num?)?.toDouble() ?? 0.0,
        totalVolumeMl: (json['pump_volume'] as num?)?.toDouble() ?? 0.0,
        running: false,
      ),
      hydrometer: HydrometerData(
        abv: (json['abv'] as num?)?.toDouble() ?? 0.0,
        density: 0.0,
        valid: false,
      ),
      volumes: VolumeData(
        heads: 0.0,
        body: 0.0,
        tails: 0.0,
      ),
    );
  }

  void _onError(dynamic error) {
    _status = WebSocketStatus.error;
    _scheduleReconnect();
  }

  void _onDone() {
    _status = WebSocketStatus.disconnected;
    if (_shouldReconnect) {
      _scheduleReconnect();
    }
  }

  void _scheduleReconnect() {
    if (!_shouldReconnect) return;
    if (_reconnectAttempts >= AppConstants.maxReconnectAttempts) {
      _shouldReconnect = false;
      return;
    }

    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(
      Duration(seconds: AppConstants.websocketReconnectDelay * (_reconnectAttempts + 1)),
      () {
        _reconnectAttempts++;
        _connectInternal();
      },
    );
  }

  void sendMessage(Map<String, dynamic> message) {
    if (_status == WebSocketStatus.connected && _channel != null) {
      _channel!.sink.add(jsonEncode(message));
    }
  }

  Future<void> disconnect() async {
    _shouldReconnect = false;
    _reconnectTimer?.cancel();
    await _channel?.sink.close();
    _status = WebSocketStatus.disconnected;
  }

  void dispose() {
    disconnect();
    _stateController?.close();
    _eventController?.close();
  }
}

