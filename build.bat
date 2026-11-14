@echo off
REM Скрипт компиляции Smart Column S3
REM Использование: build.bat

echo ==========================================
echo Smart Column S3 - Compilation Script
echo ==========================================
echo.

REM Проверка PlatformIO
where platformio >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: PlatformIO not found
    echo Install: pip install platformio
    pause
    exit /b 1
)

echo [OK] PlatformIO found
echo.

REM Очистка предыдущей сборки (опционально)
set /p clean="Clean previous build? (y/n): "
if /i "%clean%"=="y" (
    echo Cleaning...
    platformio run --target clean
    echo.
)

REM Компиляция
echo Building firmware...
echo.
platformio run --environment esp32s3

if %ERRORLEVEL% equ 0 (
    echo.
    echo ==========================================
    echo [SUCCESS] Firmware compiled!
    echo ==========================================
    echo.
    echo Firmware location: .pio\build\esp32s3\firmware.bin
    echo.
    echo Next steps:
    echo 1. Test compilation errors: DONE
    echo 2. Upload via OTA: upload_ota.bat
    echo 3. Or upload via USB: platformio run --target upload
    echo.
    pause
) else (
    echo.
    echo ==========================================
    echo [FAILED] Compilation errors
    echo ==========================================
    echo.
    echo Check errors above and fix them
    pause
    exit /b 1
)
