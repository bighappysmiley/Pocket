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
| `main/` | ESP-IDF app entry, board bring-up, Wi‑Fi, OTA hooks |
| `components/pocket_ui/` | Portable UI shell, screens, apps (host-testable) |
| `host/` | Host simulator (g++) for UI/nav without hardware |
| `partitions.csv` | A/B OTA + NVS + LittleFS |
| `sdkconfig.defaults` | ESP32-S3 defaults |

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

Pairing QR encodes `https://app.getpocket.device/pair?code={CODE}` (TTL 10 minutes).
Device registers sessions with Pocket Cloud (`POST /v1/pair/sessions`) and polls status.