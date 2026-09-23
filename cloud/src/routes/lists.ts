import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession, requireEntitled } from "../middleware/auth.js";
import { get, run, all } from "../db/index.js";
import { newId } from "../lib/crypto.js";
import { Errors } from "../lib/errors.js";
import {
  incomingWinsLww,
  normalizeUpdatedAt,
  mergeListItems,
  type ListItem,
} from "../lib/lww.js";

type ListRow = {
  id: string;
  user_id: string;
  title: string;
  items_json: string;
  created_at: string;
  updated_at: string;
  updated_by_device_id: string;
  deleted_at: string | null;
  version: number;
};

function parseItems(json: string): ListItem[] {
  try {
    const v = JSON.parse(json);
    return Array.isArray(v) ? v : [];
  } catch {
    return [];
  }
}

function serializeList(row: ListRow) {
  return {
    id: row.id,
    title: row.title,
    items: parseItems(row.items_json),
    created_at: row.created_at,
    updated_at: row.updated_at,
    updated_by_device_id: row.updated_by_device_id,
    deleted_at: row.deleted_at,
    version: row.version,
  };
}

export const listsRoutes = new Hono<AppEnv>();

listsRoutes.use("*", requireSession, requireEntitled);

listsRoutes.get("/", (c) => {
  const user = c.get("user")!;
  const includeDeleted = c.req.query("include_deleted") === "1";
  const rows = all<ListRow>(
    includeDeleted
      ? `SELECT * FROM cloud_lists WHERE user_id = ? ORDER BY updated_at DESC`
      : `SELECT * FROM cloud_lists WHERE user_id = ? AND deleted_at IS NULL ORDER BY updated_at DESC`,
    [user.id],
  );
  return c.json({ lists: rows.map(serializeList) });
});

listsRoutes.post("/", async (c) => {
  const user = c.get("user")!;
  const body = await c.req.json().catch(() => ({}));
  const now = new Date();
  const id = typeof body.id === "string" && body.id ? body.id : newId();
  const existing = get<ListRow>("SELECT * FROM cloud_lists WHERE id = ?", [id]);

  const updated_at = normalizeUpdatedAt(
    typeof body.updated_at === "string" ? body.updated_at : undefined,
    now,
  );
  const updated_by =
    typeof body.updated_by_device_id === "string" && body.updated_by_device_id
      ? body.updated_by_device_id
      : "pwa";
  const title = typeof body.title === "string" ? body.title : "";
  const incomingItems: ListItem[] = Array.isArray(body.items) ? body.items : [];
  const deleted_at =
    body.deleted_at === null
      ? null
      : typeof body.deleted_at === "string"
        ? body.deleted_at
        : null;

  if (existing) {
    if (existing.user_id !== user.id) throw Errors.forbidden();
    if (
      !incomingWinsLww(
        { updated_at, updated_by_device_id: updated_by, deleted_at },
        {
          updated_at: existing.updated_at,
          updated_by_device_id: existing.updated_by_device_id,
          deleted_at: existing.deleted_at,
        },
      )
    ) {
      // Still merge items with per-item LWW if parent loses? Spec: parent LWW; if lose, keep existing.
      return c.json({ list: serializeList(existing), conflict: "kept_existing" });
    }
    const merged = mergeListItems(parseItems(existing.items_json), incomingItems);
    run(
      `UPDATE cloud_lists SET title = ?, items_json = ?, updated_at = ?, updated_by_device_id = ?, deleted_at = ?, version = version + 1
       WHERE id = ?`,
      [title, JSON.stringify(merged), updated_at, updated_by, deleted_at, id],
    );
  } else {
    run(
      `INSERT INTO cloud_lists
        (id, user_id, title, items_json, created_at, updated_at, updated_by_device_id, deleted_at, version)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1)`,
      [
        id,
        user.id,
        title,
        JSON.stringify(incomingItems),
        typeof body.created_at === "string" ? body.created_at : now.toISOString(),
        updated_at,
        updated_by,
        deleted_at,
      ],
    );
  }
  const row = get<ListRow>("SELECT * FROM cloud_lists WHERE id = ?", [id])!;
  return c.json({ list: serializeList(row) }, existing ? 200 : 201);
});

listsRoutes.get("/:id", (c) => {
  const user = c.get("user")!;
  const row = get<ListRow>("SELECT * FROM cloud_lists WHERE id = ? AND user_id = ?", [
    c.req.param("id"),
    user.id,
  ]);
  if (!row || row.deleted_at) throw Errors.notFound("That list");
  return c.json({ list: serializeList(row) });
});

listsRoutes.patch("/:id", async (c) => {
  const user = c.get("user")!;
  const id = c.req.param("id");
  const existing = get<ListRow>("SELECT * FROM cloud_lists WHERE id = ? AND user_id = ?", [
    id,
    user.id,
  ]);
  if (!existing || existing.deleted_at) throw Errors.notFound("That list");

  const body = await c.req.json().catch(() => ({}));
  const now = new Date();
  const updated_at = normalizeUpdatedAt(
    typeof body.updated_at === "string" ? body.updated_at : now.toISOString(),
    now,
  );
  const updated_by =
    typeof body.updated_by_device_id === "string" && body.updated_by_device_id
      ? body.updated_by_device_id
      : "pwa";
  const title = typeof body.title === "string" ? body.title : existing.title;
  const incomingItems: ListItem[] = Array.isArray(body.items)
    ? body.items
    : parseItems(existing.items_json);

  if (
    !incomingWinsLww(
      { updated_at, updated_by_device_id: updated_by },
      {
        updated_at: existing.updated_at,
        updated_by_device_id: existing.updated_by_device_id,
        deleted_at: existing.deleted_at,
      },
    )
  ) {
    return c.json({ list: serializeList(existing), conflict: "kept_existing" });
  }

  const merged = mergeListItems(parseItems(existing.items_json), incomingItems);
  run(
    `UPDATE cloud_lists SET title = ?, items_json = ?, updated_at = ?, updated_by_device_id = ?, version = version + 1 WHERE id = ?`,
    [title, JSON.stringify(merged), updated_at, updated_by, id],
  );
  const row = get<ListRow>("SELECT * FROM cloud_lists WHERE id = ?", [id])!;
  return c.json({ list: serializeList(row) });
});

listsRoutes.delete("/:id", async (c) => {
  const user = c.get("user")!;
  const id = c.req.param("id");
  const existing = get<ListRow>("SELECT * FROM cloud_lists WHERE id = ? AND user_id = ?", [
    id,
    user.id,
  ]);
  if (!existing) throw Errors.notFound("That list");
  const now = new Date().toISOString();
  const body = await c.req.json().catch(() => ({}));
  const updated_by =
    typeof body.updated_by_device_id === "string" ? body.updated_by_device_id : "pwa";
  run(
    `UPDATE cloud_lists SET deleted_at = ?, updated_at = ?, updated_by_device_id = ?, version = version + 1 WHERE id = ?`,
    [now, now, updated_by, id],
  );
  return c.json({ ok: true, deleted_at: now });
});
