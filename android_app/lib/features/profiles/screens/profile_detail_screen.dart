import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/profiles_provider.dart';
import '../../../../shared/widgets/loading_indicator.dart';
import '../../../../shared/widgets/error_widget.dart';

class ProfileDetailScreen extends ConsumerWidget {
  final String profileId;

  const ProfileDetailScreen({
    super.key,
    required this.profileId,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profile = ref.watch(profileDetailProvider(profileId));

    return Scaffold(
      appBar: AppBar(
        title: const Text('Профиль'),
        actions: [
          IconButton(
            icon: const Icon(Icons.download),
            onPressed: () async {
              final success = await ref.read(profileDetailProvider(profileId).notifier).loadProfile();
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    content: Text(success ? 'Профиль загружен' : 'Ошибка загрузки профиля'),
                  ),
                );
              }
            },
          ),
        ],
      ),
      body: profile.when(
        data: (profileData) => SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        profileData.metadata.name,
                        style: Theme.of(context).textTheme.titleLarge,
                      ),
                      const SizedBox(height: 8),
                      Text(profileData.metadata.description),
                      const SizedBox(height: 8),
                      Text('Категория: ${profileData.metadata.category}'),
                      Text('Использовано: ${profileData.statistics.useCount} раз'),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Параметры',
                        style: Theme.of(context).textTheme.titleMedium,
                      ),
                      const SizedBox(height: 8),
                      Text('Режим: ${profileData.parameters.mode}'),
                      Text('Модель: ${profileData.parameters.model}'),
                      Text('Макс. мощность: ${profileData.parameters.heater.maxPower} W'),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
        loading: () => const LoadingIndicator(),
        error: (error, stack) => AppErrorWidget(
          message: error.toString(),
          onRetry: () => ref.refresh(profileDetailProvider(profileId)),
        ),
      ),
    );
  }
}

