#!/bin/bash
# Скрипт компиляции Smart Column S3
# Использование: ./build.sh

set -e  # Выход при ошибке

echo "=========================================="
echo "Smart Column S3 - Compilation Script"
echo "=========================================="
echo ""

# Проверка PlatformIO
if ! command -v platformio &> /dev/null; then
    echo "❌ ERROR: PlatformIO not found"
    echo "Install: pip install platformio"
    exit 1
fi

echo "✅ PlatformIO found"
echo ""

# Очистка предыдущей сборки (опционально)
read -p "Clean previous build? (y/n): " clean
if [ "$clean" == "y" ] || [ "$clean" == "Y" ]; then
    echo "🧹 Cleaning..."
    platformio run --target clean
    echo ""
fi

# Компиляция
echo "🔨 Building firmware..."
echo ""
platformio run --environment esp32s3

if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "✅ SUCCESS: Firmware compiled!"
    echo "=========================================="
    echo ""
    echo "Firmware location: .pio/build/esp32s3/firmware.bin"
    echo ""
    echo "Next steps:"
    echo "1. Test compilation errors: DONE ✅"
    echo "2. Upload via OTA: ./upload_ota.sh"
    echo "3. Or upload via USB: platformio run --target upload"
    echo ""
else
    echo ""
    echo "=========================================="
    echo "❌ FAILED: Compilation errors"
    echo "=========================================="
    echo ""
    echo "Check errors above and fix them"
    exit 1
fi
