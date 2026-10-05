# Pocket

Greenfield monorepo for **Pocket** device firmware, **Pocket Cloud** backend, and the phone **Companion** PWA.

Authoritative Spec: Project docs `pocket-firmware-and-companion-spec.md` (Parts A–E). Part B (onboarding) and Part C (hardware) override Part A where they conflict.

## Platform

Pocket is **source-available with a closed platform**:

- Buy and use the device as shipped.
- Casual alternate OSes / unrestricted custom firmware are out of scope.
- Tweaks and extensions go through an approved **developer** path (Companion `/developer` describes the model; submission UI coming).
- Official firmware updates and owner recovery remain via `firmware-latest` / OTA — not marketed as an open modding playground.

Companion is **phone-only**. The marketing site may be browsed on a computer; the Companion app routes are blocked on desktop.

## Packages

| Path | Role |
| --- | --- |
| [`firmware/`](firmware/) | ESP32-S3-ePaper firmware + host UI simulator |
| [`cloud/`](cloud/) | Pocket Cloud API (auth, pairing, sync, billing) |
| [`companion/`](companion/) | Marketing site + Companion PWA (`app.getpocket.device`) |
| [`packages/shared/`](packages/shared/) | Shared TypeScript types |

## Quick start

```bash
# Cloud (port 8787)
cd cloud && cp .env.example .env && npm install && npm run dev

# Companion PWA (port 5173)
cd companion && npm install && npm run dev

# Firmware host tests / simulator
cd firmware/host && cmake -B build && cmake --build build && ctest --test-dir build
```

## Deploy (always from `main`)

Pushing to **`main`** runs CI and auto-deploy workflows. Agents merge to `main`; you do not need to merge or click Deploy.

- Companion + Pocket Cloud: GitHub Actions (`.github/workflows/`). Provider secrets unlock live deploys.
- Full secret checklist and URLs: [`docs/deploy.md`](docs/deploy.md)
- Firmware is **not** flashed from CI — use ESP-IDF locally when hardware is available (owners / recovery).

## Product constraints (short)

- Canvas **480×800** portrait, 4-level grayscale; not a touchscreen
- Controls: rotary wheel + **side button** + **power button** only — **no volume keys**
- PTT = side button hold ≥200 ms; Function long ≥800 ms → Home
- First-boot: **no** e-ink device naming (Part B); default name `"Pocket"`
- Subscription product name: **Pocket Cloud** only (never "Connect")
- Companion CTA: **Download Companion** (phone); not a desktop web app

## License

Proprietary — all rights reserved. Source may be visible for transparency; the platform remains controlled.
