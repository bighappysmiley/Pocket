# Pocket Version 1

Greenfield monorepo for **Pocket Version 1** device firmware, **Pocket Cloud** backend, and the companion PWA.

Authoritative Spec: Project docs `pocket-firmware-and-companion-spec.md` (Parts A–E). Part B (onboarding) and Part C (hardware) override Part A where they conflict.

## Packages

| Path | Role |
| --- | --- |
| [`firmware/`](firmware/) | ESP32-S3-ePaper firmware + host UI simulator |
| [`cloud/`](cloud/) | Pocket Cloud API (auth, pairing, sync, billing) |
| [`companion/`](companion/) | Companion PWA (`app.getpocket.device`) |
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
- Firmware is **not** flashed from CI — use ESP-IDF locally when hardware is available.

## Product constraints (short)

- Canvas **480×800** portrait, 4-level grayscale; not a touchscreen
- Controls: rotary wheel + **side button** + **power button** only — **no volume keys**
- PTT = side button hold ≥200 ms; Function long ≥800 ms → Home
- First-boot: **no** e-ink device naming (Part B); default name `"Pocket"`
- Subscription product name: **Pocket Cloud** only (never "Connect")
- No marketing landing page in this repo (deferred Spec)

## License

Proprietary — all rights reserved.
