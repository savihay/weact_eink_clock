#!/bin/bash
# Build + flash via OTA (WiFi) using espota.py.
# Usage: ./upload.sh [device-ip] [--no-build]
# If device-ip is omitted, falls back to DEVICE_IP from config.env.
# Pass --no-build (or set NO_BUILD=1) to skip compilation.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.env"

# Parse args — IP is positional, --no-build is a flag in any position.
NO_BUILD="${NO_BUILD:-0}"
for arg in "$@"; do
    case "$arg" in
        --no-build)  NO_BUILD=1 ;;
        *)           DEVICE_IP="$arg" ;;
    esac
done

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
echo "Device IP: $DEVICE_IP"
echo "========================================"

# Reachability check first — no point burning a build if the device is gone.
echo "Checking device connectivity..."
if ! ping -c 1 -W 2 "$DEVICE_IP" > /dev/null 2>&1; then
    echo "❌ Device at $DEVICE_IP is not reachable"
    exit 1
fi
echo "✅ Device is reachable"
echo ""

# Build now (after reachability) — keeps the on-device version field accurate
# (binary contains the current version.h; the post-upload bump applies to the next build).
if [[ "$NO_BUILD" != "1" ]]; then
    "$SCRIPT_DIR/compile.sh"
    echo ""
fi

if [ ! -f "$BIN_FILE" ]; then
    echo "❌ Binary not found at $BIN_FILE"
    echo "   Run compile.sh first (or omit --no-build)."
    exit 1
fi

if [ ! -f "$ESPOTA" ]; then
    echo "❌ espota.py not found at $ESPOTA"
    echo "   Check your ESP32 core install path."
    exit 1
fi

echo "Uploading firmware via OTA..."
START_TIME=$(date +%s)
python3 "$ESPOTA" -i "$DEVICE_IP" -p 3232 -f "$BIN_FILE"
END_TIME=$(date +%s)
echo "⏱️  Upload completed in $((END_TIME - START_TIME)) seconds"

echo ""
echo "Incrementing version..."
source "$SCRIPT_DIR/bump_version.sh"
bump_version "$PROJECT_PATH/version.h"

echo ""
echo "✅ Upload successful! Device should reboot now."
