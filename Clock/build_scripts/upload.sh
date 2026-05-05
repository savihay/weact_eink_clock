#!/bin/bash
# Upload firmware via OTA (WiFi) using espota.py.
# Usage: ./upload.sh [device-ip]
# If device-ip is omitted, falls back to DEVICE_IP from config.env.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.env"

if [ -n "$1" ]; then
    DEVICE_IP="$1"
fi

if [ -z "$DEVICE_IP" ]; then
    echo "❌ No device IP specified."
    echo "   Pass it as an argument: ./upload.sh 192.168.x.y"
    echo "   Or set DEVICE_IP in build_scripts/config.env"
    exit 1
fi

PROJECT_PATH="$SCRIPT_DIR/$PROJECT_DIR"
BUILD_DIR="$PROJECT_PATH/build"
BIN_FILE="$BUILD_DIR/$SKETCH_NAME.bin"

# Path to espota.py from the ESP32 Arduino core 3.3.5
ESPOTA="$HOME/Library/Arduino15/packages/esp32/hardware/esp32/3.3.5/tools/espota.py"

echo "========================================"
echo "  ESP32-C6 OTA Upload"
echo "========================================"
echo "Binary:    $BIN_FILE"
echo "Device IP: $DEVICE_IP"
echo "========================================"

if [ ! -f "$BIN_FILE" ]; then
    echo "❌ Binary not found at $BIN_FILE"
    echo "   Run compile.sh first."
    exit 1
fi

if [ ! -f "$ESPOTA" ]; then
    echo "❌ espota.py not found at $ESPOTA"
    echo "   Check your ESP32 core install path."
    exit 1
fi

echo "Checking device connectivity..."
if ! ping -c 1 -W 2 "$DEVICE_IP" > /dev/null 2>&1; then
    echo "❌ Device at $DEVICE_IP is not reachable"
    exit 1
fi
echo "✅ Device is reachable"

echo ""
echo "Uploading firmware via OTA..."
START_TIME=$(date +%s)
python3 "$ESPOTA" -i "$DEVICE_IP" -p 3232 -f "$BIN_FILE"
END_TIME=$(date +%s)
echo "⏱️  Upload completed in $((END_TIME - START_TIME)) seconds"

# Increment version in version.h
VERSION_FILE="$PROJECT_PATH/version.h"
if [ -f "$VERSION_FILE" ]; then
    echo ""
    echo "Incrementing version..."
    CURRENT_VERSION=$(grep -o 'FIRMWARE_VERSION "[0-9]*\.[0-9]*\.[0-9]*"' "$VERSION_FILE" | grep -o '[0-9]*\.[0-9]*\.[0-9]*')
    if [ -n "$CURRENT_VERSION" ]; then
        MAJOR=$(echo "$CURRENT_VERSION" | cut -d. -f1)
        MINOR=$(echo "$CURRENT_VERSION" | cut -d. -f2)
        PATCH=$(echo "$CURRENT_VERSION" | cut -d. -f3)
        NEW_VERSION="$MAJOR.$MINOR.$((PATCH + 1))"
        sed -i '' "s/FIRMWARE_VERSION \"$CURRENT_VERSION\"/FIRMWARE_VERSION \"$NEW_VERSION\"/" "$VERSION_FILE"
        echo "   Version: $CURRENT_VERSION → $NEW_VERSION"
    fi
fi

echo ""
echo "✅ Upload successful! Device should reboot now."
