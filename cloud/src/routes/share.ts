import { Hono } from "hono";
import { get } from "../db/index.js";
import { sha256Hex } from "../lib/crypto.js";

export const publicShareRoutes = new Hono();

publicShareRoutes.get("/:token", (c) => {
  c.header("X-Robots-Tag", "noindex, nofollow");
  const token = c.req.param("token") ?? "";
  const link = get<{
    note_id: string;
    expires_at: string | null;
    revoked_at: string | null;
  }>("SELECT note_id, expires_at, revoked_at FROM share_links WHERE token_hash = ?", [
    sha256Hex(token),
  ]);

  if (!link || link.revoked_at) {
    return c.html(viewerPage("This link is no longer available."), 410);
  }
  if (link.expires_at && Date.parse(link.expires_at) < Date.now()) {
    return c.html(viewerPage("This link has expired."), 410);
  }

  const note = get<{ title: string; body: string; deleted_at: string | null }>(
    "SELECT title, body, deleted_at FROM cloud_notes WHERE id = ?",
    [link.note_id],
  );
  if (!note || note.deleted_at) {
    return c.html(viewerPage("This link is no longer available."), 410);
  }

  return c.html(viewerPage(null, note.title, note.body));
});

function viewerPage(
  error: string | null,
  title?: string,
  body?: string,
): string {
  const content = error
    ? `<p class="err">${escapeHtml(error)}</p>`
    : `<h1>${escapeHtml(title || "")}</h1><pre class="body">${escapeHtml(body || "")}</pre>`;
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="robots" content="noindex,nofollow" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${error ? "Pocket" : escapeHtml(title || "Note")} · Pocket</title>
  <style>
    :root { color-scheme: light; --ink:#1a1a1a; --muted:#6b6b6b; --bg:#f7f4ef; }
    body { margin:0; font-family: "Source Serif 4", Georgia, serif; background: var(--bg); color: var(--ink); }
    main { max-width: 40rem; margin: 0 auto; padding: 2.5rem 1.25rem 4rem; }
    .mark { font-family: system-ui, sans-serif; font-size: 0.85rem; letter-spacing: 0.04em; color: var(--muted); margin-bottom: 2rem; }
    h1 { font-size: 1.75rem; font-weight: 600; margin: 0 0 1rem; }
    .body { white-space: pre-wrap; font-family: inherit; font-size: 1.05rem; line-height: 1.55; margin: 0; }
    .err { color: var(--muted); font-size: 1.1rem; }
  </style>
</head>
<body>
  <main>
    <div class="mark">Pocket</div>
    ${content}
  </main>
</body>
</html>`;
}

function escapeHtml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}
