#!/bin/bash
# Open serial monitor on the ESP32-C6 USB-CDC port.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.env"

USB_PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -z "$USB_PORT" ]; then
  USB_PORT=$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)
fi

if [ -z "$USB_PORT" ]; then
  echo "❌ No USB serial port found. Plug the board in and try again."
  exit 1
fi

echo "Monitoring $USB_PORT @ 115200 (Ctrl-C to exit)"
arduino-cli monitor --port "$USB_PORT" --config baudrate=115200
