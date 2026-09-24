# Pocket firmware

Device firmware for **Waveshare ESP32-S3-ePaper-3.97**, per the Pocket Build Spec.

## Spec precedence

- **Part B** (onboarding v1.1) and **Part C** (hardware controls v1.2) override Part A where they conflict.
- No volume keys. No e-ink device naming on first boot.
- Logical canvas **480×800** portrait (panel rotated). Grayscale `G0`–`G3` only.
- Controls: rotary wheel (`Button_Up` / `Button_Down` / `Button_Function`) + **side button** + **power button**.
- PTT = **side button hold ≥ 200 ms**. Function long ≥ 800 ms → Home.
- GPIO silk may still say BOOT (ESP download mode); user-facing UI copy uses **side button**.

## Layout

| Path | Role |
| --- | --- |
| `main/` | ESP-IDF app entry |
| `components/pocket_ui/` | Portable UI shell, screens, apps (host-testable) |
| `components/pocket_board/` | Waveshare 3.97" e-Paper driver + Part C GPIO buttons |
| `host/` | Host simulator (g++) for UI/nav without hardware |
| `partitions.csv` | A/B OTA + NVS + LittleFS |
| `sdkconfig.defaults` | ESP32-S3 + 16MB flash + OPI PSRAM |

## Board pins (Waveshare ESP32-S3-ePaper-3.97)

| Net | GPIO | Notes |
| --- | --- | --- |
| Button_Up | 4 | Rotary, active-low |
| Button_Function | 5 | Rotary press, active-low |
| Button_Down | 6 | Rotary, active-low |
| Side button (GPIO0 / BOOT pad) | 0 | Back short / PTT hold; hold with RESET for ESP download mode |
| Power button (PWR) | 1 | Active-low |
| EPD SCLK/MOSI/CS/DC/RST/BUSY | 11/12/10/9/46/3 | SPI3 |

Boot forces a **full 4-gray refresh** so any latched factory demo image is cleared.

## Onboarding (v1.1 + companion required)

```
welcome → companion download (QR) → Link (SoftAP + /link) → connecting → Link (pair code) → pin_* → timezone → mic_test → done → home
```

**Link** is one flow in the Pocket app (`/link`): join SoftAP, send home Wi‑Fi, then link to the same account. No separate Wi‑Fi setup vs pairing.

Companion download + pairing are **required** — no Skip.

Refresh: routine focus/list updates use **partial**; full only on Spec §6 enters (Lock/Home/PIN/QR/connecting/app roots) and ghosting every 8 partials.

Default `device_name` = `"Pocket"` until renamed in the Companion PWA.

## Build (device)

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) ≥ 5.1 targeting `esp32s3`:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Host simulator

```bash
cd host && cmake -B build && cmake --build build && ./build/pocket_host
```

Keys: `j`/`k` = Down/Up, `Enter` = Function short, `H` = Function long (Home),
`b` = side button short (Back), `B` = side button hold (PTT), `p` = power, `q` = quit.

## Persistence

`DeviceConfig` (onboarding, PIN hash, Wi‑Fi SSID, timezone, etc.) is saved to **NVS** on every change
and mirrored to **`/sdcard/pocket/config.bin`** when an SD card is mounted with enough free space.
On boot, SD is preferred when present and valid; otherwise NVS. Onboarding survives unplug without an SD card.

## Cloud pairing

Pairing QR encodes `https://bighappysmiley.github.io/Pocket/pair?code={CODE}` (TTL 10 minutes).
Device registers sessions with Pocket Cloud (`POST /v1/pair/sessions` + `x-device-key: DEVICE_API_KEY`) and polls
`GET /v1/pair/sessions/:code`. Defaults: `POCKET_CLOUD_BASE=https://br-super-hill-b40yvyrj-api.compute.c-6.us-east-2.aws.neon.tech`,
`POCKET_PWA_ORIGIN=https://bighappysmiley.github.io/Pocket` (override at compile time).
