# Pocket — deploy from `main`

Every push to **`main`** runs GitHub Actions:

1. **CI** — Pocket Cloud tests + build, Companion PWA build, firmware host tests  
2. **Deploy** — builds artifacts, then deploys whatever provider secrets are present

Agents land work on `main` (merge immediately). You should not need to merge PRs or click Deploy.

## What deploys automatically (once secrets exist)

| Surface | Provider | Trigger | Secrets / vars |
| --- | --- | --- | --- |
| Companion PWA | **GitHub Pages** | push `main` | None beyond repo Actions permission. **One-time:** Settings → Pages → Source = **GitHub Actions** |
| Companion PWA | Vercel (optional) | push `main` | `VERCEL_TOKEN`, `VERCEL_ORG_ID`, `VERCEL_PROJECT_ID_COMPANION` |
| Companion PWA | Cloudflare Pages (optional) | push `main` | `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` (+ var `CF_PAGES_PROJECT_COMPANION`) |
| Pocket Cloud API | Fly.io (optional) | push `main` | `FLY_API_TOKEN` (+ one-time `fly apps create` / volume / app secrets) |
| Pocket Cloud API | Railway (optional) | push `main` | `RAILWAY_TOKEN` (+ optional var `RAILWAY_SERVICE_ID`) |

Firmware is **not** flashed from CI (needs hardware). See firmware flash / OTA below.

## Exact GitHub Actions secret names

Add under **Repo → Settings → Secrets and variables → Actions**.

### Companion (pick one production host)

| Name | Required for | How to get |
| --- | --- | --- |
| `VERCEL_TOKEN` | Vercel deploy | [Vercel → Tokens](https://vercel.com/account/tokens) |
| `VERCEL_ORG_ID` | Vercel deploy | `.vercel/project.json` after `vercel link` in `companion/` |
| `VERCEL_PROJECT_ID_COMPANION` | Vercel deploy | same `project.json` → `projectId` |
| `CLOUDFLARE_API_TOKEN` | CF Pages | Cloudflare API token with Pages edit |
| `CLOUDFLARE_ACCOUNT_ID` | CF Pages | Cloudflare dashboard → Account ID |

### Pocket Cloud API (pick one host)

| Name | Required for | Notes |
| --- | --- | --- |
| `FLY_API_TOKEN` | Fly.io | `fly tokens create deploy` after `fly auth login` |
| `RAILWAY_TOKEN` | Railway | Railway account token |

### Shared / runtime (build + Cloud app)

| Name | Used by | Notes |
| --- | --- | --- |
| `VITE_API_BASE` | Companion build (secret **or** Actions variable) | Public Cloud origin, e.g. `https://pocket-cloud.fly.dev` |
| `SESSION_SECRET` | Cloud runtime (on Fly/Railway, not necessarily GH) | Long random string |
| `DEVICE_API_KEY` | Cloud runtime | Device `Bearer` for `POST /v1/pair/sessions` |
| `PWA_ORIGIN` | Cloud runtime | Companion origin (CORS + redirects) |
| `PUBLIC_BASE_URL` | Cloud runtime | Cloud public origin |
| `STRIPE_SECRET_KEY` | Cloud runtime (optional) | Unset = mock billing |
| `STRIPE_WEBHOOK_SECRET` | Cloud runtime (optional) | Live webhooks |
| `STRIPE_PRICE_MONTHLY_ID` | Cloud runtime (optional) | Monthly $3.99 Price |

### Optional Actions variables

| Name | Default | Purpose |
| --- | --- | --- |
| `VITE_API_BASE` | `http://localhost:8787` | Prefer variable over secret (non-sensitive URL) |
| `FLY_APP_NAME` | `pocket-cloud` | Fly app name |
| `CF_PAGES_PROJECT_COMPANION` | `pocket-companion` | Cloudflare Pages project |
| `RAILWAY_SERVICE_ID` | _(empty)_ | Railway service target |

## One-time setup (user credentials — once)

Until these are set, CI still **builds and tests** on every `main` push; optional deploy jobs print a skip notice.

1. **GitHub Pages (Companion, no extra account):** Repo → Settings → Pages → Build and deployment → Source = **GitHub Actions**. Expected URL: `https://bighappysmiley.github.io/Pocket/`
2. **Production Companion + Cloud:** Create a Vercel (or Cloudflare) project for `companion/` and a Fly (or Railway) app for `cloud/`, then paste the secret names above.
3. **Point them at each other:** set `VITE_API_BASE` / `PUBLIC_BASE_URL` / `PWA_ORIGIN` to the live origins.
4. **Stripe / OAuth (optional):** leave unset for mock billing and connector stubs.

## Firmware — merge-binary from main CI

Every `main` Deploy run builds an ESP32-S3 merge-binary and publishes it as:

- Actions artifact `pocket-firmware-esp32s3`
- Rolling release tag [`firmware-latest`](https://github.com/bighappysmiley/Pocket/releases/tag/firmware-latest)

### MacBook one-shot (preferred)

1. Download `pocket-merged.bin` from the release (or the Actions artifact).
2. Plug the Waveshare board in (data USB-C cable). Check: `ls /dev/cu.usb*`
3. From a clone of this repo:

```bash
chmod +x scripts/flash-mac.sh
./scripts/flash-mac.sh ~/Downloads/pocket-merged.bin
```

Or without the script:

```bash
python3 -m pip install --user esptool
python3 -m esptool --chip esp32s3 -p $(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null | head -1) write_flash 0x0 ~/Downloads/pocket-merged.bin
```

First flash tip: hold **BOOT**, tap **RESET**, keep holding BOOT until flashing starts.

## Expected URLs (after secrets / Pages enable)

| Surface | URL |
| --- | --- |
| Companion (Pages) | `https://bighappysmiley.github.io/Pocket/` |
| Companion (Vercel) | from Vercel project (custom domain later: `app.getpocket.device`) |
| Pocket Cloud | from Fly/Railway (`*.fly.dev` or Railway domain) |

Until provider secrets or Pages are enabled, **no live deploy URL** exists — only CI green on `main`.
