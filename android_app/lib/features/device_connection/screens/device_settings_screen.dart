import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/device_provider.dart';
import '../../../../core/api/api_client.dart';

class DeviceSettingsScreen extends ConsumerStatefulWidget {
  const DeviceSettingsScreen({super.key});

  @override
  ConsumerState<DeviceSettingsScreen> createState() => _DeviceSettingsScreenState();
}

class _DeviceSettingsScreenState extends ConsumerState<DeviceSettingsScreen> {
  final _formKey = GlobalKey<FormState>();
  final _hostController = TextEditingController();
  final _portController = TextEditingController(text: '80');
  final _usernameController = TextEditingController();
  final _passwordController = TextEditingController();
  final _nameController = TextEditingController();
  final _deviceIdController = TextEditingController();
  final _proxyTokenController = TextEditingController();
  bool _isLoading = false;
  bool _useHttps = false;
  bool _useCloudProxy = false;

  @override
  void initState() {
    super.initState();
    final device = ref.read(deviceProvider);
    if (device != null) {
      _hostController.text = device.host;
      _portController.text = device.port ?? '80';
      _usernameController.text = device.username ?? '';
      _passwordController.text = device.password ?? '';
      _nameController.text = device.name ?? '';
      _useHttps = device.useHttps;
      _deviceIdController.text = device.deviceId ?? '';
      _proxyTokenController.text = device.proxyToken ?? '';
      _useCloudProxy = device.deviceId != null && device.proxyToken != null;
    }
  }

  @override
  void dispose() {
    _hostController.dispose();
    _portController.dispose();
    _usernameController.dispose();
    _passwordController.dispose();
    _nameController.dispose();
    _deviceIdController.dispose();
    _proxyTokenController.dispose();
    super.dispose();
  }

  Future<void> _testConnection() async {
    if (!_formKey.currentState!.validate()) return;

    setState(() => _isLoading = true);

    try {
      final device = DeviceConnection(
        host: _hostController.text.trim(),
        port: _portController.text.trim().isEmpty ? null : _portController.text.trim(),
        username: _usernameController.text.trim().isEmpty ? null : _usernameController.text.trim(),
        password: _passwordController.text.trim().isEmpty ? null : _passwordController.text.trim(),
        name: _nameController.text.trim().isEmpty ? null : _nameController.text.trim(),
        useHttps: _useHttps,
        deviceId: _useCloudProxy ? _deviceIdController.text.trim() : null,
        proxyToken: _useCloudProxy ? _proxyTokenController.text.trim() : null,
      );

      final apiClient = ApiClient();
      apiClient.setBaseUrl(device.baseUrl);
      apiClient.setCredentials(device.username, device.password);

      await apiClient.getStatus();

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Подключение успешно')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка подключения: $e')),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  Future<void> _saveDevice() async {
    if (!_formKey.currentState!.validate()) return;

    final device = DeviceConnection(
      host: _hostController.text.trim(),
      port: _portController.text.trim().isEmpty ? null : _portController.text.trim(),
      username: _usernameController.text.trim().isEmpty ? null : _usernameController.text.trim(),
      password: _passwordController.text.trim().isEmpty ? null : _passwordController.text.trim(),
      name: _nameController.text.trim().isEmpty ? null : _nameController.text.trim(),
      useHttps: _useHttps,
      deviceId: _useCloudProxy ? _deviceIdController.text.trim() : null,
      proxyToken: _useCloudProxy ? _proxyTokenController.text.trim() : null,
    );

    await ref.read(deviceProvider.notifier).setDevice(device);

    if (mounted) {
      Navigator.pop(context);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Настройки устройства'),
      ),
      body: Form(
        key: _formKey,
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            TextFormField(
              controller: _nameController,
              decoration: const InputDecoration(
                labelText: 'Название (опционально)',
                hintText: 'Моя колонна',
              ),
            ),
            const SizedBox(height: 16),
            TextFormField(
              controller: _hostController,
              decoration: const InputDecoration(
                labelText: 'IP адрес или доменное имя',
                hintText: '192.168.1.100, mydevice.ddns.net, или cloud-proxy.com',
                helperText: 'IP адрес, доменное имя (DDNS), или облачный прокси',
              ),
              validator: (value) {
                if (value == null || value.trim().isEmpty) {
                  return 'Введите IP адрес или доменное имя';
                }
                return null;
              },
            ),
            const SizedBox(height: 8),
            const Text(
              '💡 Для работы без настройки роутера используйте облачный прокси',
              style: TextStyle(fontSize: 12, fontStyle: FontStyle.italic),
            ),
            const SizedBox(height: 16),
            SwitchListTile(
              title: const Text('Облачный прокси'),
              subtitle: const Text('Использовать облачный прокси-сервер (не требует настройки роутера)'),
              value: _useCloudProxy,
              onChanged: (value) {
                setState(() {
                  _useCloudProxy = value;
                  if (value) {
                    _useHttps = true;
                    if (_portController.text == '80') {
                      _portController.text = '443';
                    }
                  }
                });
              },
            ),
            if (_useCloudProxy) ...[
              const SizedBox(height: 16),
              TextFormField(
                controller: _deviceIdController,
                decoration: const InputDecoration(
                  labelText: 'Device ID',
                  hintText: 'esp32-001',
                  helperText: 'Уникальный идентификатор устройства (MAC адрес или произвольный)',
                ),
                validator: (value) {
                  if (_useCloudProxy && (value == null || value.trim().isEmpty)) {
                    return 'Введите Device ID';
                  }
                  return null;
                },
              ),
              const SizedBox(height: 16),
              TextFormField(
                controller: _proxyTokenController,
                decoration: const InputDecoration(
                  labelText: 'Токен прокси',
                  hintText: 'your_client_token',
                  helperText: 'Токен для подключения к облачному прокси',
                ),
                obscureText: true,
                validator: (value) {
                  if (_useCloudProxy && (value == null || value.trim().isEmpty)) {
                    return 'Введите токен прокси';
                  }
                  return null;
                },
              ),
            ],
            const SizedBox(height: 16),
            SwitchListTile(
              title: const Text('Использовать HTTPS'),
              subtitle: Text(_useCloudProxy 
                  ? 'Автоматически включено для облачного прокси'
                  : 'Включите для безопасного подключения через интернет'),
              value: _useHttps,
              onChanged: _useCloudProxy ? null : (value) {
                setState(() {
                  _useHttps = value;
                  if (value && _portController.text == '80') {
                    _portController.text = '443';
                  } else if (!value && _portController.text == '443') {
                    _portController.text = '80';
                  }
                });
              },
            ),
            const SizedBox(height: 16),
            TextFormField(
              controller: _portController,
              decoration: InputDecoration(
                labelText: 'Порт',
                hintText: _useHttps ? '443' : '80',
                helperText: _useHttps 
                    ? 'Стандартный порт HTTPS: 443'
                    : 'Стандартный порт HTTP: 80',
              ),
              keyboardType: TextInputType.number,
            ),
            const SizedBox(height: 16),
            TextFormField(
              controller: _usernameController,
              decoration: const InputDecoration(
                labelText: 'Имя пользователя (опционально)',
              ),
            ),
            const SizedBox(height: 16),
            TextFormField(
              controller: _passwordController,
              decoration: const InputDecoration(
                labelText: 'Пароль (опционально)',
              ),
              obscureText: true,
            ),
            const SizedBox(height: 24),
            ElevatedButton.icon(
              onPressed: _isLoading ? null : _testConnection,
              icon: _isLoading
                  ? const SizedBox(
                      width: 16,
                      height: 16,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.wifi_find),
              label: const Text('Проверить подключение'),
            ),
            const SizedBox(height: 16),
            ElevatedButton.icon(
              onPressed: _saveDevice,
              icon: const Icon(Icons.save),
              label: const Text('Сохранить'),
            ),
          ],
        ),
      ),
    );
  }
}

