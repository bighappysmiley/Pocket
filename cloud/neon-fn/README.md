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

Health: `{ "ok": true, "build": "scram-api-v14-sd-admin" }`

## Stripe (Admin UI)

Admins configure billing at Companion **Admin → Stripe** (`PUT /v1/admin/stripe`). Keys are stored in
`app_settings` (masked on GET). Billing routes:

- `POST /v1/billing/checkout`
- `POST /v1/billing/portal`
- `POST /v1/billing/webhook`

Falls back to `STRIPE_*` env vars when Admin has not saved keys; unset secret → mock billing.

Also on this build: `/v1/notes`, `/v1/lists`, `/v1/music`, `/v1/device/music`, `/v1/books`,
`/v1/device/books`, `/v1/device/settings` (volume/brightness push), `/v1/connectors`, `/v1/backups`.

### Admin device/pairing bypasses (v14)

`PATCH /v1/admin/devices/:id` now also accepts `volume_percent`, `brightness_percent`,
`lock_message`, `parental` (object) / `unlock_parental: true`, `sd_present` (bool fix-up),
`ssid`/`password` (queue Wi‑Fi like the owner's own push), `user_id` (force re-pair to a
different account), and `reset: true` (wipe volume/brightness/lock message/parental/queued
Wi‑Fi back to defaults without unpairing). `GET /v1/admin/devices` returns all of these fields
per device so ops can see current music/library sync flags (`sd_present`) and settings state.

`POST /v1/admin/devices/force-link` `{ device_id, user_id, device_name? }` attaches (or
reassigns) a device_id straight to a user's account, bypassing the phone QR/claim flow —
works immediately because the firmware only authenticates with the shared `DEVICE_API_KEY`
plus `device_id`, not a per-device secret.

`POST /v1/admin/pair-sessions/:id/claim` `{ user_id }` force-completes a pending pairing code
for a chosen user (same effect as `/v1/pair/claim` but admin-driven, no session cookie needed).
`DELETE /v1/admin/pair-sessions/:id` cancels/expires a pending code early.

All of the above call `audit()` (visible in Admin → Activity → Admin actions).

## Deployment mechanism (important)

The Neon "Deploy Function" tool used by AI cloud agents to push this file's contents has an
unreliable ceiling on how much text can be transcribed into a single tool call: base64-encoded
zip payloads beyond roughly 15-20KB have repeatedly arrived corrupted (silently truncated or
byte-mangled) even though the same content round-trips perfectly through local tooling. Once
`scram-api.mjs` grew past that size (after the Reading/eBooks + volume/brightness work), direct
deploys became unreliable and at one point left production serving corrupted code (a mangled
SQL string literal caused every request, including `/health`, to fail with a Postgres syntax
error).

The current live deployment of the `api` function is therefore a **tiny bootstrap** file (not
this file directly). The bootstrap fetches the real, byte-identical `scram-api.mjs` from a pinned
GitHub commit at cold start, writes it to a temp file, and dynamically `import()`s it:

```js
const SRC_URL = "https://raw.githubusercontent.com/bighappysmiley/Pocket/<commit-sha>/cloud/neon-fn/scram-api.mjs";
// ...fetch, write to tmp file, import(), forward fetch(req) to it.
```

This sidesteps the tool-call transcription limit entirely (the bootstrap itself is <1KB), while
the actual served logic is always exactly what's committed to `main` at the pinned commit.

**To ship a change to this file:**
1. Edit `cloud/neon-fn/scram-api.mjs`, commit, and push to `main`.
2. Update the pinned commit SHA in the deployed bootstrap to the new commit and redeploy the
   bootstrap (small payload, safe to transcribe directly).
3. Verify `/health` — it returns `sourceSha256`, a SHA-256 of the exact bytes the running
   function loaded. Compare it to `sha256sum cloud/neon-fn/scram-api.mjs` locally; they must
   match before trusting the deploy.

If deploying from a human workstation (e.g. via the Neon console or `neonctl`, not an AI agent's
tool-call interface), this workaround is unnecessary — uploading the real file directly works
fine and is simpler. The bootstrap only exists to work around AI-agent tool-call payload
transcription risk.

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
