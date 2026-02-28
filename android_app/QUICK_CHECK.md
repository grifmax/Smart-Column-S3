# Быстрая проверка установки Flutter

## Проблема: Flutter не найден в PATH

Если команда `flutter --version` не работает, нужно добавить Flutter в PATH.

## Решение

### Шаг 1: Найдите путь к Flutter

Где вы распаковали Flutter? Обычно это:
- `C:\src\flutter`
- `C:\flutter`
- `C:\Users\ВашеИмя\flutter`
- Или другое место

### Шаг 2: Добавьте Flutter в PATH

#### Вариант A: Через PowerShell (для текущей сессии)

```powershell
# Замените путь на ваш реальный путь к Flutter
$env:Path += ";C:\src\flutter\bin"
```

Проверьте:
```powershell
flutter --version
```

#### Вариант B: Постоянно (через системные настройки)

1. Нажмите `Win + R`
2. Введите `sysdm.cpl` и нажмите Enter
3. Вкладка "Дополнительно" → "Переменные среды"
4. В "Системные переменные" найдите `Path` → "Изменить"
5. "Создать" → добавьте путь к `flutter\bin` (например: `C:\src\flutter\bin`)
6. "ОК" во всех окнах
7. **Перезапустите терминал и Cursor**

### Шаг 3: Проверка в Cursor

1. В Cursor нажмите `Ctrl + Shift + P`
2. Введите "Flutter: Locate SDK"
3. Укажите путь к папке Flutter (не `bin`, а саму папку `flutter`)

Например:
- ✅ Правильно: `C:\src\flutter`
- ❌ Неправильно: `C:\src\flutter\bin`

### Шаг 4: Проверка готовности

После добавления в PATH:

```powershell
flutter --version
flutter doctor
```

## Если Flutter все еще не найден

1. Убедитесь, что вы распаковали Flutter (не просто скачали ZIP)
2. Проверьте, что в папке `flutter\bin` есть файл `flutter.exe`
3. Перезапустите терминал после изменения PATH
4. Перезапустите Cursor после изменения PATH

## Проверка через полный путь

Если не хотите добавлять в PATH, можно использовать полный путь:

```powershell
# Замените на ваш путь
C:\src\flutter\bin\flutter.exe --version
```

Но для удобства лучше добавить в PATH.


