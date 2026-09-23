import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession } from "../middleware/auth.js";
import { verifyDeviceApiKey } from "../lib/auth.js";
import { createPairSession, getPairSessionPublic, claimPairSession } from "../lib/pairing.js";
import { Errors } from "../lib/errors.js";
import { get, run, all } from "../db/index.js";

export const pairRoutes = new Hono<AppEnv>();

pairRoutes.post("/sessions", async (c) => {
  const auth = c.req.header("authorization") || c.req.header("x-device-key");
  if (!verifyDeviceApiKey(auth)) throw Errors.unauthorized();

  const body = await c.req.json().catch(() => ({}));
  const device_id = typeof body.device_id === "string" ? body.device_id : "";
  const code_public = typeof body.code_public === "string" ? body.code_public : "";
  const expires_at = typeof body.expires_at === "string" ? body.expires_at : undefined;

  const result = createPairSession({ device_id, code_public, expires_at });
  return c.json({ ok: true, expires_at: result.expires_at }, 201);
});

pairRoutes.get("/sessions/:code", async (c) => {
  const code = c.req.param("code");
  const result = getPairSessionPublic(code);
  return c.json(result);
});

pairRoutes.post("/claim", requireSession, async (c) => {
  const user = c.get("user")!;
  const body = await c.req.json().catch(() => ({}));
  const code = typeof body.code === "string" ? body.code : "";
  const result = claimPairSession(code, user.id);
  // device_token returned once for device to poll/fetch — PWA may ignore
  return c.json({
    device_id: result.device_id,
    device_name: result.device_name,
    linked_at: result.linked_at,
    device_token: result.device_token,
  });
});

export const deviceRoutes = new Hono<AppEnv>();

deviceRoutes.get("/", requireSession, async (c) => {
  const user = c.get("user")!;
  const rows = all<{
    id: string;
    device_id: string;
    device_name: string;
    linked_at: string;
    last_seen_at: string | null;
  }>(
    `SELECT id, device_id, device_name, linked_at, last_seen_at
     FROM device_links WHERE user_id = ? ORDER BY linked_at DESC`,
    [user.id],
  );
  return c.json({
    devices: rows.map((d) => ({
      id: d.id,
      device_id: d.device_id,
      device_name: d.device_name,
      linked_at: d.linked_at,
      last_seen_at: d.last_seen_at,
    })),
  });
});

deviceRoutes.patch("/:id", requireSession, async (c) => {
  const user = c.get("user")!;
  const id = c.req.param("id") ?? "";
  const body = await c.req.json().catch(() => ({}));
  let name = typeof body.device_name === "string" ? body.device_name.trim() : "";
  if (!name) throw Errors.badRequest("Enter a name");
  if (name.length > 20) throw Errors.badRequest("Name must be 20 characters or fewer.");
  if (!/^[\p{L}\p{N} \-']+$/u.test(name)) {
    throw Errors.badRequest("That name uses characters that aren't allowed.");
  }

  const link = get<{ id: string; user_id: string }>(
    "SELECT id, user_id FROM device_links WHERE (id = ? OR device_id = ?) AND user_id = ?",
    [id, id, user.id],
  );
  if (!link) throw Errors.notFound("That device");

  run("UPDATE device_links SET device_name = ? WHERE id = ?", [name, link.id]);
  return c.json({
    id: link.id,
    device_name: name,
  });
});

deviceRoutes.delete("/:id/link", requireSession, async (c) => {
  const user = c.get("user")!;
  const id = c.req.param("id") ?? "";
  const link = get<{ id: string }>(
    "SELECT id FROM device_links WHERE (id = ? OR device_id = ?) AND user_id = ?",
    [id, id, user.id],
  );
  if (!link) throw Errors.notFound("That device");
  run("DELETE FROM device_links WHERE id = ?", [link.id]);
  return c.json({ ok: true });
});
