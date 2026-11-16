@echo off
REM Полная прошивка по USB с разметкой из partitions.csv.
REM
REM Почему не `pio run -t upload`: загрузчик PlatformIO пишет приложение по
REM историческим адресам 0x10000 и 0xE000, а в этой разметке app0 лежит на
REM 0x20000, otadata — на 0x19000. После такой прошивки плата уходит в цикл
REM перезагрузки, потому что по 0x20000 заголовка приложения нет.
REM
REM Использование:  scripts\flash-usb.cmd [COM11]

setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM11

set ROOT=%~dp0..
set PIO_PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe
set ESPTOOL=%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py
set BOOT_APP0=%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin
set BUILD=%ROOT%\.pio\build\esp32s3

if not exist "%PIO_PY%" (
  echo Не найден python PlatformIO: %PIO_PY%
  exit /b 1
)

echo Прошивка через %PORT%
"%PIO_PY%" "%ESPTOOL%" --port %PORT% --baud 460800 write_flash ^
  --flash_mode dio --flash_freq 80m --flash_size 16MB ^
  0x0      "%BUILD%\bootloader.bin" ^
  0x8000   "%BUILD%\partitions.bin" ^
  0x19000  "%BOOT_APP0%" ^
  0x20000  "%BUILD%\firmware.bin"
if errorlevel 1 exit /b 1

echo Файловая система
"%PIO_PY%" "%ESPTOOL%" --port %PORT% --baud 460800 write_flash ^
  --flash_mode dio --flash_freq 80m --flash_size 16MB ^
  0x820000 "%BUILD%\littlefs.bin"
if errorlevel 1 exit /b 1

echo Готово.
endlocal