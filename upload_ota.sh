#!/bin/bash
# Script for building and uploading firmware via OTA to Smart Column S3
# Target IP: 192.168.3.166

set -e  # Exit on error

echo "========================================"
echo "Smart Column S3 - OTA Upload Script"
echo "========================================"
echo ""
echo "Target device: 192.168.3.166"
echo ""

# Check if PlatformIO is available
if ! command -v platformio &> /dev/null; then
    echo "ERROR: PlatformIO not found in PATH"
    echo "Please install PlatformIO or add it to PATH"
    exit 1
fi

echo "[1/3] Building firmware..."
echo ""
platformio run --environment esp32s3

echo ""
echo "[2/3] Preparing OTA upload..."
echo ""

# Check if device is reachable
if ! ping -c 1 -W 2 192.168.3.166 > /dev/null 2>&1; then
    echo "WARNING: Device 192.168.3.166 is not responding to ping"
    echo "Make sure the device is online and connected to the network"
    echo ""
    read -p "Continue anyway? (y/n): " continue
    if [ "$continue" != "y" ] && [ "$continue" != "Y" ]; then
        echo "Upload cancelled"
        exit 1
    fi
fi

echo "[3/3] Uploading firmware via OTA..."
echo ""

# Find espota.py in PlatformIO packages
ESPOTA_PATH="$HOME/.platformio/packages/framework-arduinoespressif32/tools/espota.py"

if [ -f "$ESPOTA_PATH" ]; then
    python3 "$ESPOTA_PATH" -i 192.168.3.166 -p 3232 -f ".pio/build/esp32s3/firmware.bin"
else
    echo "Using alternative OTA upload method..."
    platformio run --target upload --upload-port 192.168.3.166
fi

echo ""
echo "========================================"
echo "SUCCESS: Firmware uploaded via OTA!"
echo "========================================"
echo ""
echo "Device 192.168.3.166 will reboot automatically"
echo "Wait 10-15 seconds for the device to restart"
echo ""
