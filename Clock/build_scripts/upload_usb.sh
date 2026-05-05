#!/bin/bash
# Upload firmware via USB-CDC (ESP32-C6 native USB / JTAG-Serial).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.env"

PROJECT_PATH="$SCRIPT_DIR/$PROJECT_DIR"
BUILD_DIR="$PROJECT_PATH/build"

# ESP32-C6 DevKitC-1 enumerates as a USB-CDC device on macOS:
#   - /dev/cu.usbmodem*  (native USB-Serial-JTAG)
#   - /dev/cu.usbserial-*  (only if you use the older UART port via a separate USB-UART chip)
USB_PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -z "$USB_PORT" ]; then
  USB_PORT=$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)
fi

if [ -z "$USB_PORT" ]; then
  echo "❌ No USB serial port found."
  echo "   Looked for /dev/cu.usbmodem* and /dev/cu.usbserial-*"
  echo "   Plug the ESP32-C6 into the USB-C port labeled USB (not UART) and try again."
  exit 1
fi

echo "========================================"
echo "  Arduino CLI USB Upload (ESP32-C6)"
echo "========================================"
echo "Port:   $USB_PORT"
echo "Binary: $BUILD_DIR/$SKETCH_NAME.bin"
echo "========================================"

START_TIME=$(date +%s)

arduino-cli upload \
    --fqbn "$FQBN" \
    --port "$USB_PORT" \
    --input-dir "$BUILD_DIR" \
    "$PROJECT_PATH/$SKETCH_NAME"

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "✅ USB upload successful!"
echo "⏱️  Upload time: ${ELAPSED}s"
