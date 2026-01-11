import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/dashboard_provider.dart';
import '../../device_connection/providers/device_provider.dart';
import '../../device_connection/screens/device_list_screen.dart';
import '../../control/screens/control_screen.dart';
import '../../../../core/utils/helpers.dart';
import '../../../../shared/widgets/loading_indicator.dart';
import '../../../../shared/widgets/error_widget.dart';

class DashboardScreen extends ConsumerStatefulWidget {
  const DashboardScreen({super.key});

  @override
  ConsumerState<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends ConsumerState<DashboardScreen> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      ref.read(systemStateProvider.notifier).startPolling();
    });
  }

  @override
  void dispose() {
    ref.read(systemStateProvider.notifier).stopPolling();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(systemStateProvider);
    final device = ref.watch(deviceProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Smart Column S3'),
        actions: [
          IconButton(
            icon: const Icon(Icons.devices),
            onPressed: () {
              Navigator.push(
                context,
                MaterialPageRoute(
                  builder: (_) => const DeviceListScreen(),
                ),
              );
            },
          ),
        ],
      ),
      body: state.when(
        data: (systemState) => _buildContent(context, systemState, device),
        loading: () => const LoadingIndicator(message: 'Загрузка...'),
        error: (error, stack) => AppErrorWidget(
          message: error.toString(),
          onRetry: () {
            ref.read(systemStateProvider.notifier).startPolling();
          },
        ),
      ),
    );
  }

  Widget _buildContent(BuildContext context, systemState, device) {
    return RefreshIndicator(
      onRefresh: () async {
        ref.read(systemStateProvider.notifier).startPolling();
      },
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          // Статус подключения
          Card(
            child: ListTile(
              leading: const Icon(Icons.wifi, color: Colors.green),
              title: Text(device?.name ?? device?.host ?? 'Устройство'),
              subtitle: Text('${device?.host}${device?.port != null ? ':${device?.port}' : ''}'),
              trailing: IconButton(
                icon: const Icon(Icons.edit),
                onPressed: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (_) => const DeviceListScreen(),
                    ),
                  );
                },
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // Статус процесса
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Статус процесса',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 8),
                  Text('Режим: ${_getModeString(systemState.mode)}'),
                  Text('Фаза: ${_getPhaseString(systemState.phase)}'),
                  Text('Время работы: ${Helpers.formatDuration(systemState.uptime)}'),
                  if (systemState.mashing?.active == true) ...[
                    const SizedBox(height: 8),
                    Text(
                      'Затирка: ${systemState.mashing?.stepName ?? ''}'.trim(),
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                    Text('Цель: ${Helpers.formatTemperature(systemState.mashing?.targetTemp)}'),
                    Text('Осталось: ${Helpers.formatDuration(systemState.mashing?.remainingSec ?? 0)}'),
                    Text(
                      'Ступень: ${(systemState.mashing?.currentStep ?? 0) + 1}/${systemState.mashing?.stepCount ?? 0}'
                      '${systemState.mashing?.tempInRange == true ? ' (в допуске)' : ''}',
                    ),
                  ],
                  if (systemState.hold?.active == true) ...[
                    const SizedBox(height: 8),
                    const Text(
                      'Hold (выдержка)',
                      style: TextStyle(fontWeight: FontWeight.bold),
                    ),
                    Text('Цель: ${Helpers.formatTemperature(systemState.hold?.targetTemp)}'),
                    Text('Осталось: ${Helpers.formatDuration(systemState.hold?.remainingSec ?? 0)}'),
                    Text(
                      'Ступень: ${(systemState.hold?.currentStep ?? 0) + 1}/${systemState.hold?.stepCount ?? 0}'
                      '${systemState.hold?.tempInRange == true ? ' (в допуске)' : ''}',
                    ),
                  ],
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // Температуры
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
                  const SizedBox(height: 8),
                  _buildTemperatureRow('Куб', systemState.temps.cube),
                  _buildTemperatureRow('Колонна (низ)', systemState.temps.columnBottom),
                  _buildTemperatureRow('Колонна (верх)', systemState.temps.columnTop),
                  _buildTemperatureRow('Рефлюкс', systemState.temps.reflux),
                  _buildTemperatureRow('Продукт', systemState.temps.product),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // Мощность
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
                  const SizedBox(height: 8),
                  Text('Напряжение: ${systemState.power.voltage.toStringAsFixed(1)} V'),
                  Text('Ток: ${systemState.power.current.toStringAsFixed(2)} A'),
                  Text('Мощность: ${Helpers.formatPower(systemState.power.power)}'),
                  Text('Энергия: ${systemState.power.energy.toStringAsFixed(3)} kWh'),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // Объемы
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Объемы отбора',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 8),
                  Text('Головы: ${Helpers.formatVolume(systemState.volumes.heads)}'),
                  Text('Тело: ${Helpers.formatVolume(systemState.volumes.body)}'),
                  Text('Хвосты: ${Helpers.formatVolume(systemState.volumes.tails)}'),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          
          // Управление
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Управление',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 16),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                    children: [
                      ElevatedButton.icon(
                        onPressed: () {
                          Navigator.push(
                            context,
                            MaterialPageRoute(
                              builder: (_) => const ControlScreen(),
                            ),
                          );
                        },
                        icon: const Icon(Icons.play_arrow),
                        label: const Text('Управление'),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildTemperatureRow(String label, double? temp) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label),
          Text(
            Helpers.formatTemperature(temp),
            style: TextStyle(
              fontWeight: FontWeight.bold,
              color: _getTemperatureColor(temp),
            ),
          ),
        ],
      ),
    );
  }

  Color _getTemperatureColor(double? temp) {
    if (temp == null) return Colors.grey;
    if (temp < 50) return Colors.blue;
    if (temp < 80) return Colors.green;
    if (temp < 95) return Colors.orange;
    return Colors.red;
  }

  String _getModeString(int mode) {
    switch (mode) {
      case 0:
        return 'Ожидание';
      case 1:
        return 'Ректификация';
      case 2:
        return 'Дистилляция';
      case 3:
        return 'Ручная ректификация';
      case 4:
        return 'Затирка';
      case 5:
        return 'Hold';
      default:
        return 'Неизвестно';
    }
  }

  String _getPhaseString(int phase) {
    switch (phase) {
      case 0:
        return 'Ожидание';
      case 1:
        return 'Нагрев';
      case 2:
        return 'Стабилизация';
      case 3:
        return 'Головы';
      case 4:
        return 'Стабилизация после голов';
      case 5:
        return 'Тело';
      case 6:
        return 'Хвосты';
      case 7:
        return 'Продувка';
      case 8:
        return 'Завершение';
      case 9:
        return 'Завершено';
      default:
        return 'Неизвестно';
    }
  }
}

