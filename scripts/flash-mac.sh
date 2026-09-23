#!/usr/bin/env bash
# Flash Pocket firmware on a MacBook (Waveshare ESP32-S3-ePaper-3.97).
# Usage:
#   ./scripts/flash-mac.sh
#   ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin
#   ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin /dev/cu.usbmodem14101
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-}"
PORT="${2:-}"

say() { printf '%s\n' "$*"; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

find_bin() {
  if [[ -n "$BIN" && -f "$BIN" ]]; then
    printf '%s\n' "$BIN"
    return
  fi
  local candidates=(
    "$HOME/Downloads/pocket-merged.bin"
    "$HOME/Downloads/pocket-firmware-esp32s3/pocket-merged.bin"
    "$ROOT/firmware/build/pocket-merged.bin"
    "$ROOT/pocket-merged.bin"
  )
  # Newest matching download
  local newest
  newest="$(ls -t "$HOME"/Downloads/pocket-merged.bin "$HOME"/Downloads/*/pocket-merged.bin 2>/dev/null | head -1 || true)"
  if [[ -n "$newest" && -f "$newest" ]]; then
    printf '%s\n' "$newest"
    return
  fi
  for c in "${candidates[@]}"; do
    if [[ -f "$c" ]]; then
      printf '%s\n' "$c"
      return
    fi
  done
  return 1
}

find_port() {
  if [[ -n "$PORT" ]]; then
    printf '%s\n' "$PORT"
    return
  fi
  local ports
  ports="$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null || true)"
  if [[ -z "$ports" ]]; then
    return 1
  fi
  printf '%s\n' "$ports" | head -1
}

ensure_esptool() {
  if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
    return
  fi
  if command -v esptool >/dev/null 2>&1; then
    ESPTOOL=(esptool)
    return
  fi
  if python3 -c 'import esptool' >/dev/null 2>&1; then
    ESPTOOL=(python3 -m esptool)
    return
  fi
  say "Installing esptool (one-time)…"
  if command -v pipx >/dev/null 2>&1; then
    pipx install esptool >/dev/null
    ESPTOOL=(esptool.py)
    return
  fi
  python3 -m pip install --user esptool >/dev/null
  if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
  else
    ESPTOOL=(python3 -m esptool)
  fi
}

say "=== Pocket Mac flash (ESP32-S3 ePaper 3.97) ==="
say ""

BIN_PATH="$(find_bin)" || die "Could not find pocket-merged.bin.
Download it from:
  https://github.com/bighappysmiley/Pocket/releases/tag/firmware-latest
or from the latest Deploy Action artifact \"pocket-firmware-esp32s3\",
then re-run:
  ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin"

PORT_PATH="$(find_port)" || {
  say "No USB serial port found."
  say "1. Plug the Waveshare board into your Mac with a data USB-C cable."
  say "2. If it still does not appear: hold BOOT, tap RESET (or unplug/replug while holding BOOT), then release BOOT."
  say "3. Check ports with:  ls /dev/cu.usb*"
  die "No /dev/cu.usb* device. Plug in the board and try again."
}

ensure_esptool

say "Firmware: $BIN_PATH"
say "Port:     $PORT_PATH"
say ""
say "If this is the first flash and it fails, hold BOOT, tap RESET, keep holding BOOT,"
say "run this script again, then release BOOT when erasing/writing starts."
say ""
say "Flashing…"

"${ESPTOOL[@]}" --chip esp32s3 -p "$PORT_PATH" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 "$BIN_PATH"

say ""
say "Done. The board should reboot into Pocket."
