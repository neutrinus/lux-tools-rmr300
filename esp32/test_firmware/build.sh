#!/usr/bin/env bash
set -e

# Prerequisites:
#   ESP-IDF v5.x installed and sourced:
#     export IDF_PATH=/path/to/esp-idf
#     source $IDF_PATH/export.sh
#
# Or via PlatformIO: copy main/main.c to a new ESP32 project.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== LCD Sweep Test Firmware ==="
echo "Pins: CLK=33, MOSI=18, CS=32"
echo ""
echo "To build with ESP-IDF:"
echo "  cd $SCRIPT_DIR"
echo "  idf.py set-target esp32"
echo "  idf.py menuconfig  # optional"
echo "  idf.py build"
echo ""
echo "To flash (USB/serial):"
echo "  idf.py -p /dev/ttyUSB0 flash monitor"
echo ""
echo "Adjust /dev/ttyUSB0 to your ESP32's serial port."
echo "Press Ctrl+] to exit monitor."
