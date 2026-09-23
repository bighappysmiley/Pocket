# Pocket Cloud API

Backend for **Pocket Cloud** ($3.99/mo) — magic-link auth, device pairing, entitlement, Notes/Lists LWW sync, backups, share links, Stripe billing, and connector stubs.

Default listen: `http://localhost:8787`

## Quick start

```bash
cd cloud
cp .env.example .env
npm install
npm run dev
```

Scripts:

| Script | Purpose |
| --- | --- |
| `npm run dev` | tsx watch server |
| `npm run build` | compile to `dist/` |
| `npm start` | run compiled server |
| `npm test` | vitest (LWW, pairing TTL, API smoke) |

## Environment

See `.env.example`. Important vars:

| Variable | Notes |
| --- | --- |
| `PORT` | Default `8787` |
| `SESSION_SECRET` | Cookie signing / session entropy |
| `DEVICE_API_KEY` | Bearer key for `POST /v1/pair/sessions` |
| `DATABASE_PATH` | sql.js SQLite file (default `./data/pocket-cloud.sqlite`) |
| `PWA_ORIGIN` | Companion origin for redirects (default `http://localhost:5173`) |
| `CORS_ORIGINS` | Extra origins; `localhost:5173` and `https://app.getpocket.device` always allowed |
| `STRIPE_SECRET_KEY` | If **unset**, billing runs in **mock mode** (checkout immediately grants trial/active) |
| `STRIPE_WEBHOOK_SECRET` | Required for live webhook signature verify |
| `STRIPE_PRICE_MONTHLY_ID` | Monthly Price id ($3.99 USD) — never create a yearly Price |

## API overview

### Auth
- `POST /v1/auth/magic-link` `{ email }` — logs link in dev
- `GET /v1/auth/callback?token=` — sets httpOnly `pocket_session` cookie
- `POST /v1/auth/logout`
- `GET /v1/me` — user + entitlement + devices

### Pairing (Part D §4.3)
- `POST /v1/pair/sessions` — device auth (`Authorization: Bearer $DEVICE_API_KEY`)
- `GET /v1/pair/sessions/:code` — `{ status, device_label? }` (no secrets)
- `POST /v1/pair/claim` — user session `{ code }`
- `GET /v1/devices` · `PATCH /v1/devices/:id` · `DELETE /v1/devices/:id/link`

### Entitlement & device
- `GET /v1/entitlement`
- `GET /v1/device/attest` — `Authorization: Bearer <device_token>`

### Billing (Pocket Cloud only)
- `GET /v1/billing/catalog`
- `POST /v1/billing/checkout` → Checkout URL (`trial_period_days=7` if eligible)
- `POST /v1/billing/portal` → Customer Portal URL
- `POST /v1/billing/webhook`

### Notes / Lists / Sync (entitled)
- `GET|POST /v1/notes`, `GET|PATCH|DELETE /v1/notes/:id`
- `GET|POST /v1/lists`, `GET|PATCH|DELETE /v1/lists/:id`
- `GET /v1/sync?since=ISO`

### Backups / share / connectors
- `POST|GET /v1/backups`, `GET /v1/backups/:id/download`, `POST /v1/backups/:id/restore`
- `POST|DELETE /v1/notes/:id/share`, public `GET /s/:token` (`X-Robots-Tag: noindex`)
- `GET /v1/connectors`, `POST /v1/connectors/:provider/connect|disconnect` (`drive` \| `dropbox` \| `onedrive`, OAuth stubbed)

## Notes

- Storage: **sql.js** (WASM SQLite) — no native compile step.
- Product name in Stripe/catalog is always **Pocket Cloud** (never “Connect”).
- See `IMPLEMENTATION.md` for stub vs real matrix.
