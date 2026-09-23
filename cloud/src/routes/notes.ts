import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession, requireEntitled } from "../middleware/auth.js";
import { get, run, all } from "../db/index.js";
import { newId, randomToken, sha256Hex } from "../lib/crypto.js";
import { Errors } from "../lib/errors.js";
import { incomingWinsLww, normalizeUpdatedAt } from "../lib/lww.js";
import { config } from "../config.js";

type NoteRow = {
  id: string;
  user_id: string;
  title: string;
  body: string;
  created_at: string;
  updated_at: string;
  updated_by_device_id: string;
  deleted_at: string | null;
  version: number;
};

function serializeNote(row: NoteRow) {
  return {
    id: row.id,
    title: row.title,
    body: row.body,
    created_at: row.created_at,
    updated_at: row.updated_at,
    updated_by_device_id: row.updated_by_device_id,
    deleted_at: row.deleted_at,
    version: row.version,
  };
}

export const notesRoutes = new Hono<AppEnv>();

notesRoutes.use("*", requireSession, requireEntitled);

notesRoutes.get("/", (c) => {
  const user = c.get("user")!;
  const includeDeleted = c.req.query("include_deleted") === "1";
  const rows = all<NoteRow>(
    includeDeleted
      ? `SELECT * FROM cloud_notes WHERE user_id = ? ORDER BY updated_at DESC`
      : `SELECT * FROM cloud_notes WHERE user_id = ? AND deleted_at IS NULL ORDER BY updated_at DESC`,
    [user.id],
  );
  return c.json({ notes: rows.map(serializeNote) });
});

notesRoutes.post("/", async (c) => {
  const user = c.get("user")!;
  const body = await c.req.json().catch(() => ({}));
  const now = new Date();
  const id = typeof body.id === "string" && body.id ? body.id : newId();
  const existing = get<NoteRow>("SELECT * FROM cloud_notes WHERE id = ?", [id]);

  const updated_at = normalizeUpdatedAt(
    typeof body.updated_at === "string" ? body.updated_at : undefined,
    now,
  );
  const updated_by =
    typeof body.updated_by_device_id === "string" && body.updated_by_device_id
      ? body.updated_by_device_id
      : "pwa";
  const title = typeof body.title === "string" ? body.title : "";
  const noteBody = typeof body.body === "string" ? body.body : "";
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
      return c.json({ note: serializeNote(existing), conflict: "kept_existing" });
    }
    run(
      `UPDATE cloud_notes SET title = ?, body = ?, updated_at = ?, updated_by_device_id = ?, deleted_at = ?, version = version + 1
       WHERE id = ?`,
      [title, noteBody, updated_at, updated_by, deleted_at, id],
    );
  } else {
    run(
      `INSERT INTO cloud_notes
        (id, user_id, title, body, created_at, updated_at, updated_by_device_id, deleted_at, version)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1)`,
      [
        id,
        user.id,
        title,
        noteBody,
        typeof body.created_at === "string" ? body.created_at : now.toISOString(),
        updated_at,
        updated_by,
        deleted_at,
      ],
    );
  }
  const row = get<NoteRow>("SELECT * FROM cloud_notes WHERE id = ?", [id])!;
  return c.json({ note: serializeNote(row) }, existing ? 200 : 201);
});

notesRoutes.get("/:id", (c) => {
  const user = c.get("user")!;
  const row = get<NoteRow>("SELECT * FROM cloud_notes WHERE id = ? AND user_id = ?", [
    c.req.param("id"),
    user.id,
  ]);
  if (!row || row.deleted_at) throw Errors.notFound("That note");
  return c.json({ note: serializeNote(row) });
});

notesRoutes.patch("/:id", async (c) => {
  const user = c.get("user")!;
  const id = c.req.param("id");
  const existing = get<NoteRow>("SELECT * FROM cloud_notes WHERE id = ? AND user_id = ?", [
    id,
    user.id,
  ]);
  if (!existing || existing.deleted_at) throw Errors.notFound("That note");

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
  const noteBody = typeof body.body === "string" ? body.body : existing.body;

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
    return c.json({ note: serializeNote(existing), conflict: "kept_existing" });
  }

  run(
    `UPDATE cloud_notes SET title = ?, body = ?, updated_at = ?, updated_by_device_id = ?, version = version + 1 WHERE id = ?`,
    [title, noteBody, updated_at, updated_by, id],
  );
  const row = get<NoteRow>("SELECT * FROM cloud_notes WHERE id = ?", [id])!;
  return c.json({ note: serializeNote(row) });
});

notesRoutes.delete("/:id", async (c) => {
  const user = c.get("user")!;
  const id = c.req.param("id");
  const existing = get<NoteRow>("SELECT * FROM cloud_notes WHERE id = ? AND user_id = ?", [
    id,
    user.id,
  ]);
  if (!existing) throw Errors.notFound("That note");
  const now = new Date().toISOString();
  const body = await c.req.json().catch(() => ({}));
  const updated_by =
    typeof body.updated_by_device_id === "string" ? body.updated_by_device_id : "pwa";
  run(
    `UPDATE cloud_notes SET deleted_at = ?, updated_at = ?, updated_by_device_id = ?, version = version + 1 WHERE id = ?`,
    [now, now, updated_by, id],
  );
  return c.json({ ok: true, deleted_at: now });
});

/** Share link create — registered on notes router so /:id/share is not swallowed. */
notesRoutes.post("/:id/share", async (c) => {
  const user = c.get("user")!;
  const noteId = c.req.param("id");
  const note = get<{ id: string; deleted_at: string | null }>(
    "SELECT id, deleted_at FROM cloud_notes WHERE id = ? AND user_id = ?",
    [noteId, user.id],
  );
  if (!note || note.deleted_at) throw Errors.notFound("That note");

  const body = await c.req.json().catch(() => ({}));
  const neverExpire = body.never_expire === true;
  const now = new Date();
  const expiresAt = neverExpire
    ? null
    : new Date(now.getTime() + config.shareDefaultExpiryDays * 86_400_000).toISOString();

  run(
    `UPDATE share_links SET revoked_at = ? WHERE note_id = ? AND user_id = ? AND revoked_at IS NULL`,
    [now.toISOString(), noteId, user.id],
  );

  const token = randomToken(24);
  const id = newId();
  run(
    `INSERT INTO share_links (id, note_id, user_id, token_hash, token_public, created_at, expires_at, revoked_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, NULL)`,
    [id, noteId, user.id, sha256Hex(token), token, now.toISOString(), expiresAt],
  );

  return c.json(
    {
      share: {
        id,
        url: `${config.publicBaseUrl}/s/${token}`,
        expires_at: expiresAt,
        created_at: now.toISOString(),
      },
      message: "Link created. Anyone with the link can view this note.",
    },
    201,
  );
});

notesRoutes.delete("/:id/share", async (c) => {
  const user = c.get("user")!;
  const noteId = c.req.param("id");
  const now = new Date().toISOString();
  run(
    `UPDATE share_links SET revoked_at = ? WHERE note_id = ? AND user_id = ? AND revoked_at IS NULL`,
    [now, noteId, user.id],
  );
  return c.json({ ok: true });
});
