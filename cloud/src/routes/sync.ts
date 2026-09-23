import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession, requireEntitled } from "../middleware/auth.js";
import { all } from "../db/index.js";

export const syncRoutes = new Hono<AppEnv>();

syncRoutes.get("/", requireSession, requireEntitled, (c) => {
  const user = c.get("user")!;
  const since = c.req.query("since");
  const sinceIso = since && Number.isFinite(Date.parse(since)) ? since : "1970-01-01T00:00:00.000Z";

  const notes = all<Record<string, unknown>>(
    `SELECT id, title, body, created_at, updated_at, updated_by_device_id, deleted_at, version
     FROM cloud_notes WHERE user_id = ? AND updated_at > ? ORDER BY updated_at ASC`,
    [user.id, sinceIso],
  );
  const lists = all<Record<string, unknown>>(
    `SELECT id, title, items_json, created_at, updated_at, updated_by_device_id, deleted_at, version
     FROM cloud_lists WHERE user_id = ? AND updated_at > ? ORDER BY updated_at ASC`,
    [user.id, sinceIso],
  );

  const parsedLists = lists.map((row) => {
    let items: unknown[] = [];
    try {
      items = JSON.parse(String(row.items_json ?? "[]"));
    } catch {
      items = [];
    }
    const { items_json: _, ...rest } = row;
    return { ...rest, items };
  });

  const serverTime = new Date().toISOString();
  return c.json({
    since: sinceIso,
    server_time: serverTime,
    notes,
    lists: parsedLists,
  });
});
