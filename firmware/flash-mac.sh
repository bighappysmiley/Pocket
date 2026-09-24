#!/usr/bin/env bash
# Flash Pocket firmware on a MacBook (Waveshare ESP32-S3-ePaper-3.97).
# Usage:
#   ./scripts/flash-mac.sh
#   ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin
#   ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin /dev/cu.usbmodem1101
#
# Never installs into Homebrew/system Python (avoids PEP 668 "externally-managed-environment").
# Compatible with esptool v4 (write_flash) and v5 (write-flash).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-}"
PORT="${2:-}"
VENV_DIR=""
ESPTOOL=()
WRITE_FLASH="write_flash"
FLASH_MODE_OPT="--flash_mode"
FLASH_SIZE_OPT="--flash_size"
FLASH_FREQ_OPT="--flash_freq"
BEFORE_DEFAULT="default_reset"
BEFORE_NO="no_reset"
AFTER_HARD="hard_reset"

say() { printf '%s\n' "$*"; }
die() { printf '\nError: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [[ -n "${VENV_DIR}" && -d "${VENV_DIR}" ]]; then
    rm -rf "${VENV_DIR}"
  fi
}
trap cleanup EXIT

print_board_help() {
  say ""
  say "Waveshare ESP32-S3-ePaper-3.97 buttons:"
  say "  • BOOT  — small button labeled BOOT (near the USB-C port on most boards)."
  say "  • RESET — small button labeled RESET / RST (not PWR)."
  say "  • PWR   — power button. Holding PWR does nothing for flashing — use BOOT + RESET."
  say ""
  say "Enter download mode (when flash can't connect):"
  say "  1. Unplug USB, wait 2 seconds, plug back in (use a data cable, not charge-only)."
  say "  2. Hold BOOT."
  say "  3. While still holding BOOT, press and release RESET (or unplug/replug USB)."
  say "  4. Keep holding BOOT for 2 more seconds, then release."
  say "  5. Run this script again immediately."
  say ""
  say "Ports you should see after plugging in:"
  say "  ls /dev/cu.usb*"
  say "  → /dev/cu.usbmodem…     (built-in USB Serial/JTAG — most common on this board)"
  say "  → /dev/cu.wchusbserial… (external CH340 adapter — also OK)"
  say "  → /dev/cu.usbserial…    (other USB-UART chips)"
}

find_bin() {
  if [[ -n "$BIN" && -f "$BIN" ]]; then
    printf '%s\n' "$BIN"
    return
  fi
  local newest=""
  shopt -s nullglob
  local downloads=("$HOME"/Downloads/pocket-merged.bin "$HOME"/Downloads/*/pocket-merged.bin)
  shopt -u nullglob
  if ((${#downloads[@]})); then
    # shellcheck disable=SC2012
    newest="$(ls -t "${downloads[@]}" 2>/dev/null | head -1 || true)"
  fi
  if [[ -n "$newest" && -f "$newest" ]]; then
    printf '%s\n' "$newest"
    return
  fi
  local candidates=(
    "$HOME/Downloads/pocket-merged.bin"
    "$HOME/Downloads/pocket-firmware-esp32s3/pocket-merged.bin"
    "$ROOT/firmware/build/pocket-merged.bin"
    "$ROOT/pocket-merged.bin"
  )
  for c in "${candidates[@]}"; do
    if [[ -f "$c" ]]; then
      printf '%s\n' "$c"
      return
    fi
  done
  return 1
}

# Bash nullglob — never leave literal unmatched patterns (and never use zsh).
list_ports() {
  local ports=()
  shopt -s nullglob
  ports=(/dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial*)
  shopt -u nullglob
  if ((${#ports[@]} == 0)); then
    return 1
  fi
  printf '%s\n' "${ports[@]}"
}

find_port() {
  if [[ -n "$PORT" ]]; then
    if [[ ! -e "$PORT" ]]; then
      die "Port $PORT was not found. Unplug/replug the board and run:  ls /dev/cu.usb*"
    fi
    printf '%s\n' "$PORT"
    return
  fi
  local ports
  ports="$(list_ports)" || return 1
  # Prefer native usbmodem (ESP32-S3 USB Serial/JTAG) over CH340 when both exist.
  local preferred
  preferred="$(printf '%s\n' "$ports" | grep 'cu.usbmodem' | head -1 || true)"
  if [[ -n "$preferred" ]]; then
    printf '%s\n' "$preferred"
    return
  fi
  printf '%s\n' "$ports" | head -1
}

resolve_existing_esptool() {
  # Prefer `esptool` (Homebrew v5) over legacy `esptool.py`.
  if command -v esptool >/dev/null 2>&1; then
    ESPTOOL=(esptool)
    return 0
  fi
  if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
    return 0
  fi
  return 1
}

detect_esptool_cli() {
  local help_out
  help_out="$("${ESPTOOL[@]}" -h 2>&1 || true)"
  if printf '%s\n' "$help_out" | grep -qE '(^|[[:space:]])write-flash([[:space:]]|$)'; then
    WRITE_FLASH="write-flash"
    FLASH_MODE_OPT="--flash-mode"
    FLASH_SIZE_OPT="--flash-size"
    FLASH_FREQ_OPT="--flash-freq"
    BEFORE_DEFAULT="default-reset"
    BEFORE_NO="no-reset"
    say "Detected esptool v5+ style (write-flash)."
  else
    WRITE_FLASH="write_flash"
    FLASH_MODE_OPT="--flash_mode"
    FLASH_SIZE_OPT="--flash_size"
    FLASH_FREQ_OPT="--flash_freq"
    BEFORE_DEFAULT="default_reset"
    BEFORE_NO="no_reset"
    say "Detected esptool v4 style (write_flash)."
  fi
}

ensure_esptool() {
  if resolve_existing_esptool; then
    say "Using existing esptool: ${ESPTOOL[*]}"
    detect_esptool_cli
    return
  fi

  say "esptool is not installed yet. Installing safely (will not touch system Python)…"

  # 1) Homebrew (preferred on Mac)
  if command -v brew >/dev/null 2>&1; then
    say "→ brew install esptool"
    if brew install esptool; then
      if resolve_existing_esptool; then
        detect_esptool_cli
        return
      fi
      if brew --prefix esptool >/dev/null 2>&1; then
        local bp
        bp="$(brew --prefix esptool 2>/dev/null || true)"
        if [[ -x "$bp/bin/esptool" ]]; then
          ESPTOOL=("$bp/bin/esptool")
          detect_esptool_cli
          return
        fi
        if [[ -x "$bp/bin/esptool.py" ]]; then
          ESPTOOL=("$bp/bin/esptool.py")
          detect_esptool_cli
          return
        fi
      fi
    else
      say "Homebrew install did not finish. Trying the next option…"
    fi
  else
    say "Homebrew not found. (Optional: install from https://brew.sh then re-run.)"
  fi

  # 2) pipx
  if command -v pipx >/dev/null 2>&1; then
    say "→ pipx install esptool"
    if pipx install esptool; then
      export PATH="${HOME}/.local/bin:${PATH}"
      if resolve_existing_esptool; then
        detect_esptool_cli
        return
      fi
      if [[ -x "${HOME}/.local/bin/esptool" ]]; then
        ESPTOOL=("${HOME}/.local/bin/esptool")
        detect_esptool_cli
        return
      fi
      if [[ -x "${HOME}/.local/bin/esptool.py" ]]; then
        ESPTOOL=("${HOME}/.local/bin/esptool.py")
        detect_esptool_cli
        return
      fi
    else
      say "pipx install did not finish. Trying the next option…"
    fi
  elif command -v brew >/dev/null 2>&1; then
    say "→ brew install pipx && pipx install esptool"
    if brew install pipx && pipx ensurepath && pipx install esptool; then
      export PATH="${HOME}/.local/bin:${PATH}"
      if resolve_existing_esptool; then
        detect_esptool_cli
        return
      fi
    fi
  fi

  # 3) Temporary venv (never touches system/Homebrew Python site-packages)
  if ! command -v python3 >/dev/null 2>&1; then
    die "Python 3 is missing. Install Homebrew from https://brew.sh then run:  brew install python esptool"
  fi
  say "→ creating a temporary Python environment and installing esptool there"
  VENV_DIR="$(mktemp -d -t pocket-esptool.XXXXXX)"
  python3 -m venv "$VENV_DIR"
  # shellcheck disable=SC1091
  source "$VENV_DIR/bin/activate"
  python -m pip install --upgrade pip >/dev/null
  python -m pip install esptool >/dev/null
  if [[ -x "$VENV_DIR/bin/esptool" ]]; then
    ESPTOOL=("$VENV_DIR/bin/esptool")
  elif [[ -x "$VENV_DIR/bin/esptool.py" ]]; then
    ESPTOOL=("$VENV_DIR/bin/esptool.py")
  else
    ESPTOOL=(python -m esptool)
  fi
  say "esptool ready (temporary env)."
  detect_esptool_cli
}

run_flash() {
  local before_mode="$1"
  local cmd=(
    "${ESPTOOL[@]}"
    --chip esp32s3
    -p "$PORT_PATH"
    -b 460800
    --before "$before_mode"
    --after hard_reset
    "$WRITE_FLASH"
    "$FLASH_MODE_OPT" dio
    "$FLASH_SIZE_OPT" 16MB
    "$FLASH_FREQ_OPT" 80m
    0x0 "$BIN_PATH"
  )
  say ""
  say "Running:"
  # Print a copy-pasteable line
  printf '  '
  printf '%q ' "${cmd[@]}"
  printf '\n'
  "${cmd[@]}"
}

say "=== Pocket Mac flash (Waveshare ESP32-S3-ePaper-3.97) ==="
say ""

BIN_PATH="$(find_bin)" || {
  print_board_help
  die "Could not find pocket-merged.bin in Downloads.
Download it from:
  https://github.com/bighappysmiley/Pocket/releases/tag/firmware-latest
Save it to Downloads, then run:
  ./flash-mac.sh ~/Downloads/pocket-merged.bin"
}

PORT_PATH="$(find_port)" || {
  say "No USB serial port found on this Mac."
  say ""
  say "Most common causes:"
  say "  • Charge-only USB-C cable (try another cable / port)."
  say "  • Board not plugged in, or Mac didn't enumerate it yet."
  say "  • Holding PWR instead of BOOT (PWR will not help flashing)."
  print_board_help
  die "No /dev/cu.usb… device. Fix the cable/port, then run:  ls /dev/cu.usb*"
}

ensure_esptool

say ""
say "Firmware: $BIN_PATH"
BIN_BYTES="$(wc -c <"$BIN_PATH" | tr -d ' ')"
say "Size:     $BIN_BYTES bytes"
# v10+ builds are larger than the broken 454912/456720 interim bins.
if [[ "$BIN_BYTES" -lt 450000 ]]; then
  die "Firmware file looks too small ($BIN_BYTES bytes). Re-download pocket-merged.bin from firmware-latest."
fi
if ! strings "$BIN_PATH" 2>/dev/null | grep -q 'POCKET-LIVE-v23'; then
  say "WARNING: bin does not contain POCKET-LIVE-v23 — you may have an old download."
  say "Re-download from https://github.com/bighappysmiley/Pocket/releases/tag/firmware-latest"
fi
say "Port:     $PORT_PATH"
case "$PORT_PATH" in
  *usbmodem*) say "Port type: USB Serial/JTAG (normal for this Waveshare board)." ;;
  *wchusbserial*) say "Port type: CH340 adapter (also fine)." ;;
  *usbserial*) say "Port type: USB-UART adapter." ;;
esac
print_board_help
say "Starting flash now…"

set +e
run_flash "$BEFORE_DEFAULT"
status=$?
set -e

if [[ $status -ne 0 ]]; then
  say ""
  say "First attempt failed. Trying download-mode reset (${BEFORE_NO})…"
  say "Hold BOOT now, tap RESET, keep holding BOOT…"
  say "(Waiting 8 seconds so you can hold BOOT…)"
  sleep 8
  set +e
  run_flash "$BEFORE_NO"
  status=$?
  set -e
fi

if [[ $status -ne 0 ]]; then
  say ""
  say "Flash still failed."
  print_board_help
  die "Could not write firmware.
Checklist:
  1. Data USB-C cable (not charge-only).
  2. ls /dev/cu.usb* shows a port (usbmodem… or wchusbserial…).
  3. Hold BOOT (not PWR), tap RESET, keep holding BOOT, re-run this script.
  4. Re-download pocket-merged.bin + flash-mac.sh from the firmware-latest release.

Manual one-liner (your port, Homebrew esptool v5):
  esptool --chip esp32s3 -p /dev/cu.usbmodem1101 write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 ~/Downloads/pocket-merged.bin"
fi

say ""
say "Done. Unplug USB for 2 seconds, plug back in, wait up to ~20s."
say "Serial MUST show:  *** POCKET-LIVE-v23-skip-wifi ***"
say "Then: Welcome with larger Pocket wordmark; Wi-Fi scan lists real SSIDs."
say "If you only see a PSRAM line, you flashed an old UART build — re-download."
say "Rotary Up/Down moves focus; press selects Continue."
say "No SD card or battery is required (USB power is enough)."
say "If the Chinese demo is still stuck, re-run with a full erase:"
say "  esptool --chip esp32s3 -p $PORT_PATH erase-flash"
say "  then flash again with this script."
