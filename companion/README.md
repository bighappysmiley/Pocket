# Pocket — Companion PWA

Phone-only companion for Pocket (Part D of the Pocket Spec). Sync Notes & Lists, pair and name devices, manage Pocket Cloud billing, connectors, and backup.

**Product posture:** Companion is not a desktop website app. Marketing lives at `/`; app routes require a phone browser (or `?force_phone=1` for local testing). Platform is source-available / closed — see `/developer`.

## Stack

- Vite + React 19 + TypeScript (strict)
- React Router 7
- Light theme CSS tokens (`--ink`, `--muted`, `--surface`, `--card`, `--accent`, `--danger`, `--border`)
- Service worker shell cache (`public/sw.js`) for A2HS offline open
- IndexedDB mutation queue (`src/lib/queue.ts`) — cleared on logout

## Requirements

- Node 20+
- Pocket Cloud API on `VITE_API_BASE` (default `http://localhost:8787`)

## Setup

```bash
cd companion
npm install
npm run dev
```

Dev server: **http://localhost:5173**

```bash
npm run build    # typecheck + production build
npm run preview  # serve dist on 5173
```

## Environment

| Variable | Default | Description |
| --- | --- | --- |
| `VITE_API_BASE` | `http://localhost:8787` | Cloud API origin |

Create `.env.local` if needed:

```
VITE_API_BASE=http://localhost:8787
```

All API calls use `credentials: 'include'` (httpOnly session cookie).

## Brand

- Word mark: **Pocket**
- Subscription product: **Pocket Cloud** only (never “Connect”)
- Typography: Source Serif 4 (display) + DM Sans (UI)
- Accent: deep forest green on soft warm light surface

## Routes

See Spec §5 / `IMPLEMENTATION.md`.

## Structure

```
src/
  pages/          # One screen per Spec route
  components/     # Layout, nav, A2HS, shared UI
  lib/
    api.ts        # Cloud REST client
    auth.tsx      # Session + entitlement context
    queue.ts      # IndexedDB mutation queue
    types.ts
    utils.ts
  styles/         # tokens.css + global.css
public/
  manifest.webmanifest
  sw.js
  icons/
```
