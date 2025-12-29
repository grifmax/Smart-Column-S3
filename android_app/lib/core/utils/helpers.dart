import 'package:intl/intl.dart';

class Helpers {
  static String formatTemperature(double? temp) {
    if (temp == null) return '--.-';
    return '${temp.toStringAsFixed(1)}°C';
  }

  static String formatPower(double? power) {
    if (power == null) return '--';
    if (power >= 1000) {
      return '${(power / 1000).toStringAsFixed(2)} kW';
    }
    return '${power.toStringAsFixed(0)} W';
  }

  static String formatVolume(double? volume) {
    if (volume == null) return '0';
    if (volume >= 1000) {
      return '${(volume / 1000).toStringAsFixed(2)} L';
    }
    return '${volume.toStringAsFixed(0)} ml';
  }

  static String formatDuration(int seconds) {
    final duration = Duration(seconds: seconds);
    final hours = duration.inHours;
    final minutes = duration.inMinutes.remainder(60);
    final secs = duration.inSeconds.remainder(60);
    
    if (hours > 0) {
      return '${hours.toString().padLeft(2, '0')}:${minutes.toString().padLeft(2, '0')}:${secs.toString().padLeft(2, '0')}';
    }
    return '${minutes.toString().padLeft(2, '0')}:${secs.toString().padLeft(2, '0')}';
  }

  static String formatDateTime(DateTime dateTime) {
    return DateFormat('dd.MM.yyyy HH:mm').format(dateTime);
  }

  static String formatDate(DateTime dateTime) {
    return DateFormat('dd.MM.yyyy').format(dateTime);
  }

  static String formatTime(DateTime dateTime) {
    return DateFormat('HH:mm').format(dateTime);
  }
}

