# Быстрая установка Flutter для сборки APK

## Шаг 1: Скачать Flutter SDK

1. Перейдите на официальный сайт: https://docs.flutter.dev/get-started/install/windows
2. Скачайте последнюю стабильную версию Flutter SDK (ZIP архив)
3. Распакуйте архив в удобное место, например:
   - `C:\src\flutter` (рекомендуется)
   - `C:\flutter`
   - Или любое другое место без пробелов в пути

## Шаг 2: Добавить Flutter в PATH

### Вариант A: Через графический интерфейс (постоянно)

1. Нажмите `Win + R`, введите `sysdm.cpl` и нажмите Enter
2. Перейдите на вкладку "Дополнительно"
3. Нажмите "Переменные среды"
4. В разделе "Системные переменные" найдите `Path` и нажмите "Изменить"
5. Нажмите "Создать" и добавьте путь к `flutter\bin`, например: `C:\src\flutter\bin`
6. Нажмите "ОК" во всех окнах
7. **Перезапустите терминал и IDE**

### Вариант B: Через PowerShell (только для текущей сессии)

```powershell
# Замените путь на ваш реальный путь к Flutter
$env:Path += ";C:\src\flutter\bin"
```

## Шаг 3: Проверить установку

Откройте новый терминал (PowerShell или CMD) и выполните:

```powershell
flutter --version
flutter doctor
```

Должна отобразиться информация о версии Flutter.

## Шаг 4: Установить Android SDK

### Вариант 1: Android Studio (рекомендуется)

1. Скачайте Android Studio: https://developer.android.com/studio
2. Установите Android Studio
3. При первом запуске выберите "Standard" установку
4. Android Studio автоматически установит Android SDK

### Вариант 2: Только Android SDK (без Android Studio)

1. Скачайте Android SDK Command Line Tools
2. Установите Android SDK
3. Настройте переменные среды:
   - `ANDROID_HOME` = путь к Android SDK (например, `C:\Users\YourName\AppData\Local\Android\Sdk`)
   - Добавьте в `Path`: `%ANDROID_HOME%\platform-tools`

## Шаг 5: Принять лицензии Android

```powershell
flutter doctor --android-licenses
```

Нажимайте `y` для принятия всех лицензий.

## Шаг 6: Проверить готовность

```powershell
flutter doctor -v
```

Должны быть установлены:
- ✅ Flutter (Channel stable)
- ✅ Android toolchain
- ✅ Android Studio (или VS Code)
- ⚠️ Connected device (не обязательно для сборки APK)

## Шаг 7: Настроить IDE

### VS Code

1. Установите расширение "Flutter" из магазина расширений
2. Нажмите `Ctrl + Shift + P`
3. Введите "Flutter: Locate SDK"
4. Укажите путь к Flutter SDK (например, `C:\src\flutter`)

### Android Studio

1. Откройте Android Studio
2. File → Settings → Languages & Frameworks → Flutter
3. Укажите путь к Flutter SDK

## Шаг 8: Собрать APK

После настройки Flutter:

```powershell
cd android_app
.\build_apk.ps1
```

Или вручную:

```powershell
cd android_app
flutter pub get
flutter pub run build_runner build --delete-conflicting-outputs
flutter build apk --release
```

## Решение проблем

### Ошибка: "Flutter SDK not found" в IDE

1. Убедитесь, что Flutter добавлен в PATH
2. Перезапустите IDE
3. В VS Code: `Ctrl + Shift + P` → "Flutter: Locate SDK" → укажите путь
4. В Android Studio: File → Settings → Flutter → укажите путь

### Ошибка: "Android licenses not accepted"

```powershell
flutter doctor --android-licenses
```

### Ошибка: "Command 'flutter' not found"

- Убедитесь, что Flutter добавлен в PATH
- Перезапустите терминал
- Проверьте путь: `where flutter` (должен показать путь к flutter.exe)

### Ошибка при сборке: "Missing .g.dart files"

```powershell
flutter pub run build_runner build --delete-conflicting-outputs
```

## Минимальные требования

- **Flutter SDK**: 3.0.0 или выше
- **Dart**: 3.0.0 или выше (входит в Flutter)
- **Android SDK**: API 21+ (Android 5.0+)
- **Java**: JDK 8 или выше

## Полезные ссылки

- Официальная документация Flutter: https://docs.flutter.dev
- Установка Flutter на Windows: https://docs.flutter.dev/get-started/install/windows
- Flutter Doctor: https://docs.flutter.dev/tools/flutter-doctor



