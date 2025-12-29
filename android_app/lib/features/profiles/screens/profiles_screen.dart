import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/profiles_provider.dart';
import '../../../../shared/widgets/loading_indicator.dart';
import '../../../../shared/widgets/error_widget.dart';
import 'profile_detail_screen.dart';

class ProfilesScreen extends ConsumerWidget {
  const ProfilesScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profiles = ref.watch(profilesProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Профили'),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: () {
              ref.read(profilesProvider.notifier).refresh();
            },
          ),
        ],
      ),
      body: profiles.when(
        data: (profileList) => profileList.isEmpty
            ? const Center(child: Text('Нет профилей'))
            : RefreshIndicator(
                onRefresh: () => ref.read(profilesProvider.notifier).refresh(),
                child: ListView.builder(
                  itemCount: profileList.length,
                  itemBuilder: (context, index) {
                    final profile = profileList[index];
                    return ListTile(
                      title: Text(profile.name),
                      subtitle: Text('${profile.category} • Использовано: ${profile.useCount}'),
                      trailing: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          IconButton(
                            icon: const Icon(Icons.delete),
                            onPressed: () async {
                              final confirmed = await showDialog<bool>(
                                context: context,
                                builder: (context) => AlertDialog(
                                  title: const Text('Удалить профиль?'),
                                  content: Text('Вы уверены, что хотите удалить профиль "${profile.name}"?'),
                                  actions: [
                                    TextButton(
                                      onPressed: () => Navigator.pop(context, false),
                                      child: const Text('Отмена'),
                                    ),
                                    TextButton(
                                      onPressed: () => Navigator.pop(context, true),
                                      child: const Text('Удалить'),
                                    ),
                                  ],
                                ),
                              );
                              if (confirmed == true) {
                                await ref.read(profilesProvider.notifier).deleteProfile(profile.id);
                              }
                            },
                          ),
                        ],
                      ),
                      onTap: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (_) => ProfileDetailScreen(profileId: profile.id),
                          ),
                        );
                      },
                    );
                  },
                ),
              ),
        loading: () => const LoadingIndicator(),
        error: (error, stack) => AppErrorWidget(
          message: error.toString(),
          onRetry: () => ref.read(profilesProvider.notifier).refresh(),
        ),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: () {
          // TODO: Create new profile
        },
        child: const Icon(Icons.add),
      ),
    );
  }
}

