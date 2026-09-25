# Neon Function — Pocket Cloud API

Live deployable handler: **`scram-api.mjs`** (custom TLS SCRAM Postgres client; no `pg` in the zip).

## Deploy (MCP / zip)

```bash
python3 - <<'PY'
import zipfile, base64, pathlib
src = pathlib.Path('scram-api.mjs').read_bytes()
zf = pathlib.Path('/tmp/pocket-api.zip')
with zipfile.ZipFile(zf, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('index.mjs', src)
pathlib.Path('/tmp/pocket-api.b64').write_text(base64.b64encode(zf.read_bytes()).decode())
print(zf, 'bytes', zf.stat().st_size)
PY
```

Then Neon `deploy_function` with slug `api`, runtime `nodejs24`, zip = that base64.

Env (carried across deploys; set on change):

- `DEVICE_API_KEY`, `SESSION_COOKIE_NAME`, `PWA_ORIGIN`, `PUBLIC_BASE_URL`
- `ADMIN_EMAILS` — comma-separated (default includes owner)
- `DATABASE_URL` injected by Neon

Health: `{ "ok": true, "build": "scram-api-v10-firmware-ota" }`

## Stripe (Admin UI)

Admins configure billing at Companion **Admin → Stripe** (`PUT /v1/admin/stripe`). Keys are stored in
`app_settings` (masked on GET). Billing routes:

- `POST /v1/billing/checkout`
- `POST /v1/billing/portal`
- `POST /v1/billing/webhook`

Falls back to `STRIPE_*` env vars when Admin has not saved keys; unset secret → mock billing.

Also on this build: `/v1/notes`, `/v1/lists`, `/v1/music`, `/v1/device/music`, `/v1/books`,
`/v1/device/books`, `/v1/device/settings` (volume/brightness push), `/v1/connectors`, `/v1/backups`.

### Reading (eBooks)

EPUB/TXT upload via `POST /v1/books` — EPUB text is extracted server-side (minimal built-in ZIP +
`zlib.inflateRawSync`, no dependency) and stored as normalized plain text (`text_b64`); the device
always reads text, never the original EPUB container. `GET /v1/books/:id/text` serves that text to
the device (device-key + device_id, same pattern as music). Per-upload cap `BOOK_MAX_BYTES` (20 MB
original); extracted text capped at `BOOK_MAX_TEXT_BYTES` (4 MB).

### Music size limits

`MUSIC_MAX_SD_BYTES` (80 MB) applies once Pocket has reported a microSD card (`device_links.sd_present`,
set by the device's music/device pull with `x-pocket-sd: 1`); otherwise `MUSIC_MAX_INTERNAL_BYTES`
(2 MB, LittleFS-safe) applies. `GET /v1/music/limits` reports both to Companion.

`index.ts` / `slim-entry.mjs` are older Hono/`pg` sketches — **do not deploy those** while live runs the SCRAM zip.
