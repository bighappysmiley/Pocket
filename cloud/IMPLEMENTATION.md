# Pocket Cloud — implementation summary

Greenfield API under `cloud/` implementing Spec Part E + pairing from Part D §4.3.

## Stack

- TypeScript + Hono (`@hono/node-server`)
- sql.js (SQLite WASM; file-backed at `DATABASE_PATH`)
- tsx for `dev`; `tsc` for `build`
- Vitest for LWW, pairing TTL, and HTTP smoke tests
- Stripe SDK with **mock mode** when `STRIPE_SECRET_KEY` is unset

Default port **8787**. CORS: `http://localhost:5173`, `https://app.getpocket.device`.

Shared types: `packages/shared/src/index.ts`.

## Routes

| Method | Path | Auth | Status |
| --- | --- | --- | --- |
| GET | `/health` | — | Real |
| POST | `/v1/auth/magic-link` | — | Real (dev: console log link; no SMTP) |
| GET | `/v1/auth/callback` | token query | Real (httpOnly session cookie) |
| POST | `/v1/auth/logout` | cookie | Real |
| GET | `/v1/me` | session | Real |
| POST | `/v1/pair/sessions` | device API key | Real (stores code hash, TTL ≤10m) |
| GET | `/v1/pair/sessions/:code` | public | Real (no secrets) |
| POST | `/v1/pair/claim` | session | Real (single-use; returns device_token once) |
| GET | `/v1/devices` | session | Real |
| PATCH | `/v1/devices/:id` | session | Real (name ≤20) |
| DELETE | `/v1/devices/:id/link` | session | Real |
| GET | `/v1/entitlement` | session | Real |
| GET | `/v1/device/attest` | device token | Real (`cloud_entitled` mirror) |
| GET | `/v1/billing/catalog` | — | Real |
| POST | `/v1/billing/checkout` | session | Real Stripe **or** mock (auto-completes trial/active) |
| POST | `/v1/billing/portal` | session | Real Stripe **or** mock portal URL |
| POST | `/v1/billing/webhook` | Stripe sig when keyed | Real handlers; mock accepts JSON without sig |
| GET/POST | `/v1/notes` | session + entitled | Real LWW |
| GET/PATCH/DELETE | `/v1/notes/:id` | session + entitled | Real |
| POST/DELETE | `/v1/notes/:id/share` | session + entitled | Real |
| GET/POST | `/v1/lists` … | session + entitled | Real LWW + per-item merge |
| GET | `/v1/sync?since=` | session + entitled | Real |
| POST/GET | `/v1/backups` … | session + entitled | Real (JSON snapshot; retain last N≥3) |
| GET | `/s/:token` | public | Real viewer + `X-Robots-Tag: noindex` |
| GET | `/v1/connectors` | session | Real list + framing copy |
| POST | `/v1/connectors/:provider/connect` | session + entitled | **Stub OAuth** — marks Connected |
| POST | `/v1/connectors/:provider/disconnect` | session | Real (clears stub token) |

## Stubbed vs real

| Area | Real | Stubbed / deferred |
| --- | --- | --- |
| Magic link | Token mint, consume, session cookie | Email delivery (console in dev) |
| Pairing | Hash store, TTL, single-use claim, rename, unlink | Device push/poll beyond attest |
| Entitlement | State machine from subscription mirror | 72h offline grace (device-side concern) |
| Stripe | Checkout/Portal/webhook shapes + signature verify when keyed | Mock checkout when no secret; no yearly Price |
| Notes/Lists sync | LWW + tombstones + pull since | CRDT / revision log |
| Backups | Create/list/download/restore + retention | At-rest encryption beyond DB file perms |
| Share links | Token hash, 30d default, revoke, noindex viewer | — |
| Connectors | Connect/disconnect persistence + framing | Real Google/Dropbox/OneDrive OAuth + mirror jobs |

## Schema (Spec §15.1)

`users`, `sessions`, `magic_links`, `pair_sessions`, `device_links`, `subscription_mirrors`, `cloud_notes`, `cloud_lists`, `backups`, `share_links`, `connector_accounts`, `sync_cursors`, plus `stripe_events` / `mock_checkouts` for billing.
