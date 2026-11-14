@echo off
REM Script for building and uploading firmware via OTA to Smart Column S3
REM Target IP: 192.168.3.166

echo ========================================
echo Smart Column S3 - OTA Upload Script
echo ========================================
echo.
echo Target device: 192.168.3.166
echo.

REM Check if PlatformIO is available
where platformio >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: PlatformIO not found in PATH
    echo Please install PlatformIO or add it to PATH
    pause
    exit /b 1
)

echo [1/3] Building firmware...
echo.
platformio run --environment esp32s3
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo [2/3] Preparing OTA upload...
echo.

REM Check if device is reachable
ping -n 1 192.168.3.166 >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo WARNING: Device 192.168.3.166 is not responding to ping
    echo Make sure the device is online and connected to the network
    echo.
    set /p continue="Continue anyway? (y/n): "
    if /i not "%continue%"=="y" (
        echo Upload cancelled
        pause
        exit /b 1
    )
)

echo [3/3] Uploading firmware via OTA...
echo.

REM Upload via OTA using espota.py
python -m esptool --version >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Trying alternative OTA upload method...
    platformio run --target upload --upload-port 192.168.3.166
) else (
    REM Use espota.py from PlatformIO
    python "%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\espota.py" -i 192.168.3.166 -p 3232 -f ".pio\build\esp32s3\firmware.bin"
)

if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: OTA upload failed!
    echo.
    echo Possible reasons:
    echo - Device is offline or not reachable
    echo - OTA is disabled on the device
    echo - Firewall blocking connection
    echo - Device is busy or in use
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo SUCCESS: Firmware uploaded via OTA!
echo ========================================
echo.
echo Device 192.168.3.166 will reboot automatically
echo Wait 10-15 seconds for the device to restart
echo.
pause
