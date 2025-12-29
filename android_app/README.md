# Smart Column S3 - Android App

Android приложение для управления ректификационной колонной Smart-Column S3.

## Требования

- Flutter SDK 3.0.0 или выше
- Android SDK 21+ (только 64-bit устройства - arm64-v8a)
- Dart 3.0.0 или выше

## Установка

1. Установите зависимости:
```bash
flutter pub get
```

2. Запустите приложение:
```bash
flutter run
```

## Сборка

### Debug
```bash
flutter build apk --debug
```

### Release
```bash
flutter build apk --release
```

## Архитектура

Приложение использует:
- **Flutter** для UI
- **Riverpod** для state management
- **Dio** для HTTP запросов
- **WebSocket** для real-time обновлений
- **SharedPreferences** и **Hive** для локального хранилища

## Структура проекта

```
lib/
├── main.dart                 # Точка входа
├── app.dart                  # Главный виджет приложения
├── core/                     # Основная логика
│   ├── api/                 # API клиенты
│   ├── storage/             # Локальное хранилище
│   ├── network/             # Сетевые утилиты
│   └── utils/               # Вспомогательные функции
├── features/                # Функциональные модули
│   ├── dashboard/          # Главный экран
│   ├── monitoring/          # Мониторинг
│   ├── control/             # Управление
│   ├── profiles/            # Профили
│   ├── history/             # История
│   ├── settings/            # Настройки
│   ├── calibration/         # Калибровка
│   ├── charts/              # Графики
│   └── device_connection/   # Подключение к устройству
└── shared/                  # Общие компоненты
    ├── widgets/             # Переиспользуемые виджеты
    └── theme/               # Тема приложения
```

## Лицензия

См. основной проект Smart-Column S3.

