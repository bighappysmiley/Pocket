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

Health: `{ "ok": true, "build": "scram-api-v9-session-header" }`

## Stripe (Admin UI)

Admins configure billing at Companion **Admin → Stripe** (`PUT /v1/admin/stripe`). Keys are stored in
`app_settings` (masked on GET). Billing routes:

- `POST /v1/billing/checkout`
- `POST /v1/billing/portal`
- `POST /v1/billing/webhook`

Falls back to `STRIPE_*` env vars when Admin has not saved keys; unset secret → mock billing.

Also on this build: `/v1/notes`, `/v1/lists`, `/v1/music`, `/v1/device/music`, `/v1/connectors`, `/v1/backups`.

`index.ts` / `slim-entry.mjs` are older Hono/`pg` sketches — **do not deploy those** while live runs the SCRAM zip.
