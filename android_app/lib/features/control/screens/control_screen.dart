import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../device_connection/providers/device_provider.dart';
import '../../../../shared/widgets/loading_indicator.dart';

class ControlScreen extends ConsumerStatefulWidget {
  const ControlScreen({super.key});

  @override
  ConsumerState<ControlScreen> createState() => _ControlScreenState();
}

class _ControlScreenState extends ConsumerState<ControlScreen> {
  String _selectedMode = 'rectification';
  bool _isLoading = false;

  Future<void> _startProcess() async {
    final apiClient = ref.read(currentApiClientProvider);
    if (apiClient == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Устройство не подключено')),
      );
      return;
    }

    setState(() => _isLoading = true);

    try {
      await apiClient.startProcess(mode: _selectedMode);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Процесс запущен')),
        );
        Navigator.pop(context);
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка: $e')),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  Future<void> _stopProcess() async {
    final apiClient = ref.read(currentApiClientProvider);
    if (apiClient == null) return;

    setState(() => _isLoading = true);

    try {
      await apiClient.stopProcess();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Процесс остановлен')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка: $e')),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  Future<void> _pauseProcess() async {
    final apiClient = ref.read(currentApiClientProvider);
    if (apiClient == null) return;

    setState(() => _isLoading = true);

    try {
      await apiClient.pauseProcess();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Процесс приостановлен')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка: $e')),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  Future<void> _resumeProcess() async {
    final apiClient = ref.read(currentApiClientProvider);
    if (apiClient == null) return;

    setState(() => _isLoading = true);

    try {
      await apiClient.resumeProcess();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Процесс возобновлен')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка: $e')),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_isLoading) {
      return const Scaffold(
        body: LoadingIndicator(),
      );
    }

    return Scaffold(
      appBar: AppBar(
        title: const Text('Управление'),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Выбор режима',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 16),
                  RadioListTile<String>(
                    title: const Text('Ректификация'),
                    value: 'rectification',
                    groupValue: _selectedMode,
                    onChanged: (value) {
                      setState(() => _selectedMode = value!);
                    },
                  ),
                  RadioListTile<String>(
                    title: const Text('Дистилляция'),
                    value: 'distillation',
                    groupValue: _selectedMode,
                    onChanged: (value) {
                      setState(() => _selectedMode = value!);
                    },
                  ),
                  RadioListTile<String>(
                    title: const Text('Ручная ректификация'),
                    value: 'manual',
                    groupValue: _selectedMode,
                    onChanged: (value) {
                      setState(() => _selectedMode = value!);
                    },
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          ElevatedButton.icon(
            onPressed: _startProcess,
            icon: const Icon(Icons.play_arrow),
            label: const Text('Запустить процесс'),
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.all(16),
            ),
          ),
          const SizedBox(height: 8),
          ElevatedButton.icon(
            onPressed: _pauseProcess,
            icon: const Icon(Icons.pause),
            label: const Text('Пауза'),
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.all(16),
            ),
          ),
          const SizedBox(height: 8),
          ElevatedButton.icon(
            onPressed: _resumeProcess,
            icon: const Icon(Icons.play_arrow),
            label: const Text('Возобновить'),
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.all(16),
            ),
          ),
          const SizedBox(height: 8),
          ElevatedButton.icon(
            onPressed: _stopProcess,
            icon: const Icon(Icons.stop),
            label: const Text('Остановить'),
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.all(16),
              backgroundColor: Colors.red,
              foregroundColor: Colors.white,
            ),
          ),
        ],
      ),
    );
  }
}

