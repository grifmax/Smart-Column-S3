import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:fl_chart/fl_chart.dart';
import '../../dashboard/providers/dashboard_provider.dart';
import '../../device_connection/providers/device_provider.dart';
import '../../../../core/utils/helpers.dart';
import '../../../../shared/widgets/loading_indicator.dart';

class MonitoringScreen extends ConsumerStatefulWidget {
  const MonitoringScreen({super.key});

  @override
  ConsumerState<MonitoringScreen> createState() => _MonitoringScreenState();
}

class _MonitoringScreenState extends ConsumerState<MonitoringScreen> {
  final List<FlSpot> _cubeTempSpots = [];
  final List<FlSpot> _columnTempSpots = [];
  final List<FlSpot> _powerSpots = [];
  final int _maxPoints = 60; // 5 минут при обновлении каждые 5 секунд

  @override
  void initState() {
    super.initState();
    _setupWebSocket();
  }

  void _setupWebSocket() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final ws = ref.read(currentWebSocketProvider);
      if (ws != null) {
        ws.stateStream.listen((state) {
          if (mounted) {
            setState(() {
              final now = DateTime.now().millisecondsSinceEpoch / 1000;
              _addDataPoint(state.temps.cube, state.temps.columnTop, state.power.power, now);
            });
          }
        });
      }
    });
  }

  void _addDataPoint(double cubeTemp, double columnTemp, double power, double timestamp) {
    _cubeTempSpots.add(FlSpot(timestamp, cubeTemp));
    _columnTempSpots.add(FlSpot(timestamp, columnTemp));
    _powerSpots.add(FlSpot(timestamp, power));

    // Ограничить количество точек
    if (_cubeTempSpots.length > _maxPoints) {
      _cubeTempSpots.removeAt(0);
      _columnTempSpots.removeAt(0);
      _powerSpots.removeAt(0);
    }
  }

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(systemStateProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Мониторинг'),
      ),
      body: state.when(
        data: (systemState) => _buildContent(context, systemState),
        loading: () => const LoadingIndicator(),
        error: (error, stack) => Center(child: Text('Ошибка: $error')),
      ),
    );
  }

  Widget _buildContent(BuildContext context, systemState) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // График температур
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Температуры',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 16),
                  SizedBox(
                    height: 200,
                    child: LineChart(
                      LineChartData(
                        gridData: const FlGridData(show: true),
                        titlesData: const FlTitlesData(
                          leftTitles: AxisTitles(
                            sideTitles: SideTitles(
                              showTitles: true,
                              reservedSize: 40,
                            ),
                          ),
                          bottomTitles: AxisTitles(
                            sideTitles: SideTitles(showTitles: false),
                          ),
                          rightTitles: AxisTitles(
                            sideTitles: SideTitles(showTitles: false),
                          ),
                          topTitles: AxisTitles(
                            sideTitles: SideTitles(showTitles: false),
                          ),
                        ),
                        lineBarsData: [
                          LineChartBarData(
                            spots: _cubeTempSpots,
                            isCurved: true,
                            color: Colors.red,
                            barWidth: 2,
                            dotData: const FlDotData(show: false),
                          ),
                          LineChartBarData(
                            spots: _columnTempSpots,
                            isCurved: true,
                            color: Colors.blue,
                            barWidth: 2,
                            dotData: const FlDotData(show: false),
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // График мощности
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Мощность',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 16),
                  SizedBox(
                    height: 200,
                    child: LineChart(
                      LineChartData(
                        gridData: const FlGridData(show: true),
                        titlesData: const FlTitlesData(
                          leftTitles: AxisTitles(
                            sideTitles: SideTitles(
                              showTitles: true,
                              reservedSize: 40,
                            ),
                          ),
                          bottomTitles: AxisTitles(
                            sideTitles: SideTitles(showTitles: false),
                          ),
                          rightTitles: AxisTitles(
                            sideTitles: SideTitles(showTitles: false),
                          ),
                          topTitles: AxisTitles(
                            sideTitles: SideTitles(showTitles: false),
                          ),
                        ),
                        lineBarsData: [
                          LineChartBarData(
                            spots: _powerSpots,
                            isCurved: true,
                            color: Colors.orange,
                            barWidth: 2,
                            dotData: const FlDotData(show: false),
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // Текущие значения
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Текущие значения',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 16),
                  _buildValueRow('Куб', Helpers.formatTemperature(systemState.temps.cube)),
                  _buildValueRow('Колонна (верх)', Helpers.formatTemperature(systemState.temps.columnTop)),
                  _buildValueRow('Мощность', Helpers.formatPower(systemState.power.power)),
                  _buildValueRow('Напряжение', '${systemState.power.voltage.toStringAsFixed(1)} V'),
                  _buildValueRow('Ток', '${systemState.power.current.toStringAsFixed(2)} A'),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildValueRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label),
          Text(
            value,
            style: const TextStyle(fontWeight: FontWeight.bold),
          ),
        ],
      ),
    );
  }
}

