#!/usr/bin/env bash
# Copy all 5 firmware binaries to the punkhazard web public folder for online flashing.
# Run from I2S-ESP32-SD project root, after: pio run && pio run -t buildfs
#
# Usage: ./scripts/copy_firmware_to_web.sh
# Or:    bash scripts/copy_firmware_to_web.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/.pio/build/featheresp32"
# Default: from Electronics/Vincent/I2S-ESP32-SD go up to Documents then Web/punkhazard/...
WEB_DIR="${WEB_DIR:-$PROJECT_DIR/../../../Web/punkhazard/punkhazard/public/firmwares/i2s-esp32-sd}"

# Override destination with env if needed:
#   export WEB_DIR=/path/to/punkhazard/public/firmwares/i2s-esp32-sd
#   ./scripts/copy_firmware_to_web.sh

echo "Build dir:  $BUILD_DIR"
echo "Web dir:    $WEB_DIR"

mkdir -p "$WEB_DIR"

# 1) Bins produced by this project's build
for name in firmware.bin partitions.bin spiffs.bin; do
  if [ ! -f "$BUILD_DIR/$name" ]; then
    echo "Missing $BUILD_DIR/$name — run: pio run && pio run -t buildfs"
    exit 1
  fi
  cp -v "$BUILD_DIR/$name" "$WEB_DIR/$name"
done

# 2) bootloader.bin — in build dir if build ran the bootloader target, else from framework
if [ -f "$BUILD_DIR/bootloader.bin" ]; then
  cp -v "$BUILD_DIR/bootloader.bin" "$WEB_DIR/bootloader.bin"
else
  # Resolve framework path (PlatformIO default)
  FRAMEWORK_DIR="${PLATFORMIO_PACKAGES_DIR:-$HOME/.platformio/packages}/framework-arduinoespressif32"
  BOOTLOADER_ELF="$FRAMEWORK_DIR/tools/sdk/esp32/bin/bootloader_dio_80m.elf"
  if [ ! -f "$BOOTLOADER_ELF" ]; then
    echo "Missing bootloader: not in build and not found: $BOOTLOADER_ELF"
    echo "Run a full build once: pio run"
    exit 1
  fi
  # Generate bootloader.bin from .elf using esptool (from PlatformIO or system)
  PY=""
  for candidate in \
    "$HOME/.platformio/penv/bin/python" \
    "$(which python3 2>/dev/null)" \
    "$(which python 2>/dev/null)"; do
    [ -z "$candidate" ] && continue
    if "$candidate" -c "import esptool" 2>/dev/null; then
      PY="$candidate"
      break
    fi
  done
  if [ -z "$PY" ]; then
    echo "Install esptool to generate bootloader.bin: pip install esptool"
    echo "Then run: python -m esptool --chip esp32 elf2image --flash_mode dio --flash_freq 80m --flash_size 4MB -o $WEB_DIR/bootloader.bin $BOOTLOADER_ELF"
    exit 1
  fi
  "$PY" -m esptool --chip esp32 elf2image --flash_mode dio --flash_freq 80m --flash_size 4MB -o "$WEB_DIR/bootloader.bin" "$BOOTLOADER_ELF"
  echo "Generated and copied bootloader.bin"
fi

# 3) boot_app0.bin — from framework (same for all ESP32 Arduino builds)
FRAMEWORK_DIR="${PLATFORMIO_PACKAGES_DIR:-$HOME/.platformio/packages}/framework-arduinoespressif32"
BOOT_APP0="$FRAMEWORK_DIR/tools/partitions/boot_app0.bin"
if [ ! -f "$BOOT_APP0" ]; then
  echo "Missing $BOOT_APP0 — is framework-arduinoespressif32 installed?"
  exit 1
fi
cp -v "$BOOT_APP0" "$WEB_DIR/boot_app0.bin"

echo "Done. All 5 files are in $WEB_DIR"
