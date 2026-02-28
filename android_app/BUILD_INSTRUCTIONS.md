# Инструкции по сборке Android приложения

## Предварительные требования

1. Flutter SDK 3.0.0 или выше
2. Android Studio или Android SDK
3. Dart 3.0.0 или выше

## Установка зависимостей

```bash
cd android_app
flutter pub get
```

## Генерация JSON сериализации

Перед первой сборкой необходимо сгенерировать файлы сериализации:

```bash
flutter pub run build_runner build --delete-conflicting-outputs
```

Или используйте скрипт:

```bash
chmod +x build_runner.sh
./build_runner.sh
```

## Сборка приложения

### Debug версия

```bash
flutter build apk --debug
```

### Release версия

```bash
flutter build apk --release
```

## Запуск на устройстве

```bash
flutter run
```

## Важные замечания

1. Приложение скомпилировано только для 64-bit архитектуры (arm64-v8a)
2. Минимальная версия Android: 5.0 (API 21)
3. Рекомендуемая версия Android: 8.0+ (API 26)

## Структура проекта

- `lib/core/` - Основная логика (API, storage, network)
- `lib/features/` - Функциональные модули
- `lib/shared/` - Общие компоненты (widgets, theme)

## Разработка

Для разработки используйте:

```bash
flutter run --debug
```

Для hot reload нажмите `r` в консоли, для hot restart - `R`.

## Тестирование

```bash
flutter test
```

