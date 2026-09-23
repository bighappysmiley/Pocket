import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession, requireEntitled } from "../middleware/auth.js";
import { all, get, run } from "../db/index.js";
import { newId } from "../lib/crypto.js";
import { Errors } from "../lib/errors.js";

const PROVIDERS = ["drive", "dropbox", "onedrive"] as const;
type Provider = (typeof PROVIDERS)[number];

function isProvider(v: string): v is Provider {
  return (PROVIDERS as readonly string[]).includes(v);
}

const providerLabels: Record<Provider, string> = {
  drive: "Google Drive",
  dropbox: "Dropbox",
  onedrive: "OneDrive",
};

export const connectorRoutes = new Hono<AppEnv>();

connectorRoutes.get("/", requireSession, async (c) => {
  const user = c.get("user")!;
  const entitlement = c.get("entitlement");
  const rows = all<{
    provider: string;
    status: string;
    connected_at: string;
  }>(
    `SELECT provider, status, connected_at FROM connector_accounts WHERE user_id = ?`,
    [user.id],
  );
  const byProvider = new Map(rows.map((r) => [r.provider, r]));

  return c.json({
    framing: {
      title: "Keep a copy where you already work",
      helper:
        "We'll sync copies of your Notes & Lists to a folder in your account. This is not a document library inside Pocket.",
    },
    entitled: Boolean(entitlement?.entitled),
    connectors: PROVIDERS.map((provider) => {
      const row = byProvider.get(provider);
      return {
        provider,
        label: providerLabels[provider],
        status: row?.status === "connected" ? "connected" : "disconnected",
        connected_at: row?.connected_at ?? null,
      };
    }),
  });
});

connectorRoutes.post("/:provider/connect", requireSession, requireEntitled, async (c) => {
  const user = c.get("user")!;
  const providerParam = c.req.param("provider") ?? "";
  if (!isProvider(providerParam)) throw Errors.notFound("That connector");
  const provider = providerParam;

  // Stub OAuth: mark Connected without real provider round-trip
  const now = new Date().toISOString();
  const existing = get<{ id: string }>(
    "SELECT id FROM connector_accounts WHERE user_id = ? AND provider = ?",
    [user.id, provider],
  );
  if (existing) {
    run(
      `UPDATE connector_accounts SET status = 'connected', connected_at = ?, refresh_token_enc = ? WHERE id = ?`,
      [now, `stub_enc_${provider}_${user.id}`, existing.id],
    );
  } else {
    run(
      `INSERT INTO connector_accounts (id, user_id, provider, refresh_token_enc, connected_at, status)
       VALUES (?, ?, ?, ?, ?, 'connected')`,
      [newId(), user.id, provider, `stub_enc_${provider}_${user.id}`, now],
    );
  }

  return c.json({
    provider,
    label: providerLabels[provider],
    status: "connected",
    connected_at: now,
    oauth: "stub",
  });
});

connectorRoutes.post("/:provider/disconnect", requireSession, async (c) => {
  const user = c.get("user")!;
  const providerParam = c.req.param("provider") ?? "";
  if (!isProvider(providerParam)) throw Errors.notFound("That connector");
  const provider = providerParam;

  run(
    `UPDATE connector_accounts SET status = 'disconnected', refresh_token_enc = NULL WHERE user_id = ? AND provider = ?`,
    [user.id, provider],
  );
  return c.json({
    provider,
    label: providerLabels[provider],
    status: "disconnected",
  });
});
