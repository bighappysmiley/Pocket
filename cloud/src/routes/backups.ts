import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession, requireEntitled } from "../middleware/auth.js";
import { all, get, run } from "../db/index.js";
import { newId } from "../lib/crypto.js";
import { Errors } from "../lib/errors.js";
import { config } from "../config.js";

export const backupRoutes = new Hono<AppEnv>();

backupRoutes.use("*", requireSession, requireEntitled);

backupRoutes.get("/", (c) => {
  const user = c.get("user")!;
  const rows = all<{
    id: string;
    created_at: string;
    note_count: number;
    list_count: number;
    size_bytes: number;
  }>(
    `SELECT id, created_at, note_count, list_count, size_bytes
     FROM backups WHERE user_id = ? ORDER BY created_at DESC`,
    [user.id],
  );
  return c.json({ backups: rows });
});

backupRoutes.post("/", (c) => {
  const user = c.get("user")!;
  const notes = all(
    `SELECT id, title, body, created_at, updated_at, updated_by_device_id, deleted_at, version
     FROM cloud_notes WHERE user_id = ? AND deleted_at IS NULL`,
    [user.id],
  );
  const listsRaw = all<{
    id: string;
    title: string;
    items_json: string;
    created_at: string;
    updated_at: string;
    updated_by_device_id: string;
    deleted_at: string | null;
    version: number;
  }>(
    `SELECT id, title, items_json, created_at, updated_at, updated_by_device_id, deleted_at, version
     FROM cloud_lists WHERE user_id = ? AND deleted_at IS NULL`,
    [user.id],
  );
  const lists = listsRaw.map((l) => ({
    ...l,
    items: JSON.parse(l.items_json || "[]"),
    items_json: undefined,
  }));

  const payload = {
    version: 1,
    created_at: new Date().toISOString(),
    notes,
    lists: lists.map(({ items_json: _, ...rest }) => rest),
  };
  const blob = JSON.stringify(payload);
  const id = newId();
  const created_at = payload.created_at;
  run(
    `INSERT INTO backups (id, user_id, created_at, blob_json, note_count, list_count, size_bytes)
     VALUES (?, ?, ?, ?, ?, ?, ?)`,
    [id, user.id, created_at, blob, notes.length, lists.length, Buffer.byteLength(blob)],
  );

  // Retention: keep at least last N
  const keep = config.backupRetentionCount;
  const old = all<{ id: string }>(
    `SELECT id FROM backups WHERE user_id = ? ORDER BY created_at DESC LIMIT -1 OFFSET ?`,
    [user.id, keep],
  );
  for (const row of old) {
    run("DELETE FROM backups WHERE id = ?", [row.id]);
  }

  return c.json(
    {
      backup: {
        id,
        created_at,
        note_count: notes.length,
        list_count: lists.length,
        size_bytes: Buffer.byteLength(blob),
      },
    },
    201,
  );
});

backupRoutes.get("/:id/download", (c) => {
  const user = c.get("user")!;
  const row = get<{ id: string; blob_json: string; created_at: string }>(
    "SELECT id, blob_json, created_at FROM backups WHERE id = ? AND user_id = ?",
    [c.req.param("id"), user.id],
  );
  if (!row) throw Errors.notFound("That backup");
  c.header("Content-Type", "application/json");
  c.header(
    "Content-Disposition",
    `attachment; filename="pocket-cloud-backup-${row.id}.json"`,
  );
  return c.body(row.blob_json);
});

backupRoutes.post("/:id/restore", (c) => {
  const user = c.get("user")!;
  const row = get<{ blob_json: string }>(
    "SELECT blob_json FROM backups WHERE id = ? AND user_id = ?",
    [c.req.param("id"), user.id],
  );
  if (!row) throw Errors.notFound("That backup");

  let payload: {
    notes?: Array<Record<string, unknown>>;
    lists?: Array<Record<string, unknown>>;
  };
  try {
    payload = JSON.parse(row.blob_json);
  } catch {
    throw Errors.badRequest("Couldn't restore backup.");
  }

  const now = new Date().toISOString();

  // Tombstone everything currently live, then restore backup as system write
  run(
    `UPDATE cloud_notes SET deleted_at = ?, updated_at = ?, updated_by_device_id = 'system' WHERE user_id = ? AND deleted_at IS NULL`,
    [now, now, user.id],
  );
  run(
    `UPDATE cloud_lists SET deleted_at = ?, updated_at = ?, updated_by_device_id = 'system' WHERE user_id = ? AND deleted_at IS NULL`,
    [now, now, user.id],
  );

  for (const note of payload.notes || []) {
    const id = String(note.id || newId());
    const existing = get("SELECT id FROM cloud_notes WHERE id = ?", [id]);
    if (existing) {
      run(
        `UPDATE cloud_notes SET user_id = ?, title = ?, body = ?, created_at = ?, updated_at = ?, updated_by_device_id = 'system', deleted_at = NULL, version = version + 1 WHERE id = ?`,
        [
          user.id,
          String(note.title ?? ""),
          String(note.body ?? ""),
          String(note.created_at ?? now),
          now,
          id,
        ],
      );
    } else {
      run(
        `INSERT INTO cloud_notes (id, user_id, title, body, created_at, updated_at, updated_by_device_id, deleted_at, version)
         VALUES (?, ?, ?, ?, ?, ?, 'system', NULL, 1)`,
        [
          id,
          user.id,
          String(note.title ?? ""),
          String(note.body ?? ""),
          String(note.created_at ?? now),
          now,
        ],
      );
    }
  }

  for (const list of payload.lists || []) {
    const id = String(list.id || newId());
    const items = Array.isArray(list.items) ? list.items : [];
    const existing = get("SELECT id FROM cloud_lists WHERE id = ?", [id]);
    if (existing) {
      run(
        `UPDATE cloud_lists SET user_id = ?, title = ?, items_json = ?, created_at = ?, updated_at = ?, updated_by_device_id = 'system', deleted_at = NULL, version = version + 1 WHERE id = ?`,
        [
          user.id,
          String(list.title ?? ""),
          JSON.stringify(items),
          String(list.created_at ?? now),
          now,
          id,
        ],
      );
    } else {
      run(
        `INSERT INTO cloud_lists (id, user_id, title, items_json, created_at, updated_at, updated_by_device_id, deleted_at, version)
         VALUES (?, ?, ?, ?, ?, ?, 'system', NULL, 1)`,
        [
          id,
          user.id,
          String(list.title ?? ""),
          JSON.stringify(items),
          String(list.created_at ?? now),
          now,
        ],
      );
    }
  }

  return c.json({
    ok: true,
    message: "Backup restored. Devices will update when online.",
    restored_at: now,
  });
});
