import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/device_provider.dart';
import 'device_settings_screen.dart';

class DeviceListScreen extends ConsumerWidget {
  const DeviceListScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final device = ref.watch(deviceProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Устройства'),
      ),
      body: device == null
          ? Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.devices, size: 64),
                  const SizedBox(height: 16),
                  const Text('Устройство не подключено'),
                  const SizedBox(height: 24),
                  ElevatedButton.icon(
                    onPressed: () {
                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (_) => const DeviceSettingsScreen(),
                        ),
                      );
                    },
                    icon: const Icon(Icons.add),
                    label: const Text('Добавить устройство'),
                  ),
                ],
              ),
            )
          : ListTile(
              title: Text(device.name ?? device.host),
              subtitle: Text('${device.host}${device.port != null ? ':${device.port}' : ''}'),
              trailing: IconButton(
                icon: const Icon(Icons.edit),
                onPressed: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (_) => const DeviceSettingsScreen(),
                    ),
                  );
                },
              ),
            ),
      floatingActionButton: FloatingActionButton(
        onPressed: () {
          Navigator.push(
            context,
            MaterialPageRoute(
              builder: (_) => const DeviceSettingsScreen(),
            ),
          );
        },
        child: const Icon(Icons.add),
      ),
    );
  }
}

