# Скрипт для автоматической сборки APK
# Использование: .\build_apk.ps1 [--release|--debug]

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("release", "debug")]
    [string]$BuildType = "release"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Сборка Smart Column S3 APK" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Проверка Flutter
Write-Host "Проверка Flutter..." -ForegroundColor Yellow
try {
    $flutterVersion = flutter --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Flutter не найден"
    }
    Write-Host "✓ Flutter найден" -ForegroundColor Green
    Write-Host $flutterVersion[0] -ForegroundColor Gray
} catch {
    Write-Host "✗ Ошибка: Flutter не установлен или не в PATH" -ForegroundColor Red
    Write-Host ""
    Write-Host "Установите Flutter SDK:" -ForegroundColor Yellow
    Write-Host "1. Скачайте с https://flutter.dev/docs/get-started/install/windows" -ForegroundColor Gray
    Write-Host "2. Добавьте flutter\bin в PATH" -ForegroundColor Gray
    Write-Host "3. Перезапустите терминал" -ForegroundColor Gray
    exit 1
}

# Проверка Flutter Doctor
Write-Host ""
Write-Host "Проверка окружения Flutter..." -ForegroundColor Yellow
$doctorOutput = flutter doctor 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "⚠ Предупреждения в Flutter Doctor:" -ForegroundColor Yellow
    Write-Host $doctorOutput -ForegroundColor Gray
} else {
    Write-Host "✓ Окружение настроено" -ForegroundColor Green
}

# Переход в директорию приложения
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptPath

# Установка зависимостей
Write-Host ""
Write-Host "Установка зависимостей..." -ForegroundColor Yellow
flutter pub get
if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Ошибка при установке зависимостей" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Зависимости установлены" -ForegroundColor Green

# Генерация JSON сериализации
Write-Host ""
Write-Host "Генерация файлов JSON сериализации..." -ForegroundColor Yellow
flutter pub run build_runner build --delete-conflicting-outputs
if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Ошибка при генерации файлов" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Файлы сгенерированы" -ForegroundColor Green

# Сборка APK
Write-Host ""
Write-Host "Сборка APK ($BuildType)..." -ForegroundColor Yellow
if ($BuildType -eq "release") {
    flutter build apk --release
} else {
    flutter build apk --debug
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Ошибка при сборке APK" -ForegroundColor Red
    exit 1
}

# Определение пути к APK
$apkPath = if ($BuildType -eq "release") {
    "build\app\outputs\flutter-apk\app-release.apk"
} else {
    "build\app\outputs\flutter-apk\app-debug.apk"
}

if (Test-Path $apkPath) {
    $apkSize = (Get-Item $apkPath).Length / 1MB
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  ✓ APK успешно собран!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Файл: $apkPath" -ForegroundColor Cyan
    Write-Host "Размер: $([math]::Round($apkSize, 2)) MB" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Для установки на устройство:" -ForegroundColor Yellow
    Write-Host "  flutter install" -ForegroundColor Gray
    Write-Host "  или" -ForegroundColor Gray
    Write-Host "  adb install $apkPath" -ForegroundColor Gray
} else {
    Write-Host "✗ APK файл не найден по пути: $apkPath" -ForegroundColor Red
    exit 1
}




