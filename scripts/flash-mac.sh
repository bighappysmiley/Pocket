#!/usr/bin/env bash
# Flash Pocket firmware on a MacBook (Waveshare ESP32-S3-ePaper-3.97).
# Usage:
#   ./scripts/flash-mac.sh
#   ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin
#   ./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin /dev/cu.usbmodem14101
#
# Never installs into Homebrew/system Python (avoids PEP 668 "externally-managed-environment").
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-}"
PORT="${2:-}"
VENV_DIR=""
ESPTOOL=()

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
  say "  → /dev/cu.usbmodem*     (built-in USB Serial/JTAG — most common on this board)"
  say "  → /dev/cu.wchusbserial* (external CH340 adapter — also OK)"
  say "  → /dev/cu.usbserial*    (other USB-UART chips)"
}

find_bin() {
  if [[ -n "$BIN" && -f "$BIN" ]]; then
    printf '%s\n' "$BIN"
    return
  fi
  local newest
  newest="$(ls -t "$HOME"/Downloads/pocket-merged.bin "$HOME"/Downloads/*/pocket-merged.bin 2>/dev/null | head -1 || true)"
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

list_ports() {
  ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* 2>/dev/null || true
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
  ports="$(list_ports)"
  if [[ -z "$ports" ]]; then
    return 1
  fi
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
  if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
    return 0
  fi
  if command -v esptool >/dev/null 2>&1; then
    ESPTOOL=(esptool)
    return 0
  fi
  return 1
}

ensure_esptool() {
  if resolve_existing_esptool; then
    say "Using existing esptool: ${ESPTOOL[*]}"
    return
  fi

  say "esptool is not installed yet. Installing safely (will not touch system Python)…"

  # 1) Homebrew (preferred on Mac)
  if command -v brew >/dev/null 2>&1; then
    say "→ brew install esptool"
    if brew install esptool; then
      if resolve_existing_esptool; then
        return
      fi
      # Some brew formulas put a module without a shim on PATH briefly
      if brew --prefix esptool >/dev/null 2>&1; then
        local bp
        bp="$(brew --prefix esptool 2>/dev/null || true)"
        if [[ -x "$bp/bin/esptool.py" ]]; then
          ESPTOOL=("$bp/bin/esptool.py")
          return
        fi
        if [[ -x "$bp/bin/esptool" ]]; then
          ESPTOOL=("$bp/bin/esptool")
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
      # Ensure pipx shims are on PATH for this shell
      export PATH="${HOME}/.local/bin:${PATH}"
      if resolve_existing_esptool; then
        return
      fi
      if [[ -x "${HOME}/.local/bin/esptool.py" ]]; then
        ESPTOOL=("${HOME}/.local/bin/esptool.py")
        return
      fi
      if [[ -x "${HOME}/.local/bin/esptool" ]]; then
        ESPTOOL=("${HOME}/.local/bin/esptool")
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
  ESPTOOL=("$VENV_DIR/bin/esptool.py")
  if [[ ! -x "${ESPTOOL[0]}" ]]; then
    ESPTOOL=(python -m esptool)
  fi
  say "esptool ready (temporary env)."
}

run_flash() {
  local before_mode="$1"
  say ""
  say "Flashing with --before ${before_mode} --after hard_reset …"
  "${ESPTOOL[@]}" --chip esp32s3 -p "$PORT_PATH" -b 460800 \
    --before "$before_mode" --after hard_reset \
    write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
    0x0 "$BIN_PATH"
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
  die "No /dev/cu.usb* device. Fix the cable/port, then run:  ls /dev/cu.usb*"
}

ensure_esptool

say ""
say "Firmware: $BIN_PATH"
say "Port:     $PORT_PATH"
case "$PORT_PATH" in
  *usbmodem*) say "Port type: USB Serial/JTAG (normal for this Waveshare board)." ;;
  *wchusbserial*) say "Port type: CH340 adapter (also fine)." ;;
  *usbserial*) say "Port type: USB-UART adapter." ;;
esac
print_board_help
say "Starting flash now…"

set +e
run_flash default_reset
status=$?
set -e

if [[ $status -ne 0 ]]; then
  say ""
  say "First attempt failed. Trying download-mode reset (no_reset)…"
  say "If you haven't already: hold BOOT, tap RESET, keep holding BOOT, then press Enter."
  say "(Waiting 8 seconds so you can hold BOOT…)"
  sleep 8
  set +e
  run_flash no_reset
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
  2. ls /dev/cu.usb* shows a port (usbmodem* or wchusbserial*).
  3. Hold BOOT (not PWR), tap RESET, keep holding BOOT, re-run this script.
  4. Re-download pocket-merged.bin from the firmware-latest release."
fi

say ""
say "Done. The board should reboot into Pocket."
say "If the screen stays blank, press RESET once (not PWR)."
