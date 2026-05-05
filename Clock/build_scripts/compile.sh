#!/bin/bash
# Compile HelloWorld for ESP32-C6 with arduino-cli.
set -e

START_TIME=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.env"

PROJECT_PATH="$SCRIPT_DIR/$PROJECT_DIR"
SKETCH_PATH="$PROJECT_PATH/$SKETCH_NAME"
BUILD_DIR="$PROJECT_PATH/build"

echo "========================================"
echo "  Arduino CLI Compile (ESP32-C6)"
echo "========================================"
echo "Sketch: $SKETCH_PATH"
echo "FQBN:   $FQBN"
echo "Build:  $BUILD_DIR"
echo "========================================"

mkdir -p "$BUILD_DIR"

arduino-cli compile \
    --fqbn "$FQBN" \
    --build-path "$BUILD_DIR" \
    --warnings default \
    --jobs 0 \
    "$SKETCH_PATH"

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "✅ Compilation successful!"
echo "Binary: $BUILD_DIR/$SKETCH_NAME.bin"
echo "⏱️  Build time: ${ELAPSED}s"
