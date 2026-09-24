# Pocket firmware

Device firmware for **Waveshare ESP32-S3-ePaper-3.97**, per the Pocket Build Spec.

## Spec precedence

- **Part B** (onboarding v1.1) and **Part C** (hardware controls v1.2) override Part A where they conflict.
- No volume keys. No e-ink device naming on first boot.
- Logical canvas **480×800** portrait (panel rotated). Grayscale `G0`–`G3` only.
- Controls: rotary `Button_Up` / `Button_Down` / `Button_Function` + **BOOT** + **PWR**.
- PTT = **BOOT hold ≥ 200 ms**. Function long ≥ 800 ms → Home.

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
| BOOT | 0 | Back short / PTT hold |
| PWR | 1 | Active-low |
| EPD SCLK/MOSI/CS/DC/RST/BUSY | 11/12/10/9/46/3 | SPI3 |

Boot forces a **full 4-gray refresh** so any latched factory demo image is cleared.

## Onboarding (v1.1)

```
welcome → wifi_* → companion_qr → pin_* → timezone → mic_test → done → home
```

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
`b` = BOOT short (Back), `B` = BOOT hold (PTT), `p` = PWR, `q` = quit.

## Cloud pairing

Pairing QR encodes `https://bighappysmiley.github.io/Pocket/pair?code={CODE}` (TTL 10 minutes).
Device registers sessions with Pocket Cloud (`POST /v1/pair/sessions` + Bearer `DEVICE_API_KEY`) and polls
`GET /v1/pair/sessions/:code`. Defaults: `POCKET_CLOUD_BASE=https://pocket-cloud.fly.dev`,
`POCKET_PWA_ORIGIN=https://bighappysmiley.github.io/Pocket` (override at compile time).
