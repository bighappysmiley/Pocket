# Companion PWA — Implementation summary

Implements **Part D — Companion PWA Specification v1.0** under `/workspace/companion`.

## Screens

| Screen ID | Route | Status |
| --- | --- | --- |
| `pwa.login` | `/login` | Done — exact Spec copy; magic link; `return_to` |
| `pwa` auth callback | `/auth/callback` | Done — consumes `token`, redirects |
| `pwa.home` | `/` | Done — entitled summary / Free–Lapsed upsell |
| `pwa.notes.list` | `/notes` | Done — gated; empty/error copy |
| `pwa.notes.detail` | `/notes/:id` | Done — edit, share, delete, sync hints |
| `pwa.lists.list` | `/lists` | Done — gated |
| `pwa.lists.detail` | `/lists/:id` | Done — checkbox LWW fields |
| `pwa.passes.list` | `/passes` | Done — Coming soon placeholder (§6.6) |
| `pwa.devices` | `/devices` | Done — link help sheet |
| `pwa.devices.detail` | `/devices/:id` | Done — rename ≤20, unlink; no PIN |
| `pwa.device.setup_name` | `/devices/:id/setup` | Done — Name your Pocket |
| `pwa.pair` | `/pair` | Done — claim + expiry copy; auth + `return_to` |
| `pwa.billing` | `/billing` | Done — $3.99/mo, 7 days free, yearly Coming soon |
| `pwa.billing.success` | `/billing/success` | Done |
| `pwa.billing.cancel` | `/billing/cancel` | Done |
| `pwa.connectors` | `/connectors` | Done — headline exact; Drive/Dropbox/OneDrive |
| `pwa.backup` | `/backup` | Done — create / download / restore copy |
| `pwa.gated` | `/upgrade` | Done — exact gated + past_due variant |
| `pwa.a2hs` | banner | Done — non-blocking; 14-day dismiss |
| `pwa.account` | `/account` | Done — email, sign out; queue cleared |

## Behavior locked to Spec

- Entitlement gate: Notes / Lists / Backup / Connectors → `/upgrade` when Free/Lapsed
- Always available when logged in: Pair, name, Devices, Billing, Account, Passes (placeholder), Home
- Mutations set `updated_at` + `updated_by_device_id: "pwa"`; offline queue in IndexedDB; **clear on logout**
- Network failures surface `"You're offline or the server is unreachable."` / `"Try again"` without crashing
- `manifest.webmanifest`: name Pocket Cloud, short_name Pocket, standalone, light theme colors
- Service worker caches shell for A2HS offline open

## API surface (client)

Base: `VITE_API_BASE` → default `http://localhost:8787`, `credentials: 'include'`.

Auth `/v1/auth/*`, `/v1/me`, notes/lists CRUD, `/v1/pair/*`, `/v1/devices/*`, `/v1/billing/*`, `/v1/connectors/*`, `/v1/backups/*`.

## Out of scope / deferred

- Pass create / Pass detail (Cloud does not sync Passes in v1)
- Yearly Checkout (UI Coming soon only)
- Device PIN collection (forbidden)
