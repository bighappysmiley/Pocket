/**
 * Neon Function bootstrap for Pocket Cloud `api`.
 * Tiny loader that fetches pinned scram-api.mjs from GitHub (avoids MCP zip size limits).
 *
 * Hard rules for the deployed zip (MCP transcription corruption):
 * - never use the substring `fetch(req` (use `fetch(r)` / dynamic import)
 * - avoid top-level `throw` in the bootstrap itself
 */
export default {
  async fetch(r) {
    const u = new URL(r.url);
    const P = "9c0f689a4f54bba185703c8e6cbe69556f38d1bc";
    const U =
      "https://raw.githubusercontent.com/bighappysmiley/Pocket/" +
      P +
      "/cloud/neon-fn/scram-api.mjs?cb=v16stt8";
    if (u.pathname === "/v1/bootstrap") {
      return Response.json({ ok: true, pin: P, src: U });
    }
    try {
      const fs = await import("node:fs");
      const os = await import("node:os");
      const path = await import("node:path");
      const res = await globalThis.fetch(U, { cache: "no-store" });
      if (!res.ok) return Response.json({ error: "fetch " + res.status }, { status: 500 });
      const body = await res.text();
      if (!body.includes("scram-api-v16-stt") || !body.includes("stt_proxy_url")) {
        return Response.json({ error: "bad source", len: body.length }, { status: 500 });
      }
      const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fn-"));
      const file = path.join(dir, "i.mjs");
      fs.writeFileSync(file, body);
      const mod = await import("file://" + file);
      return mod.default.fetch.bind(mod.default)(r);
    } catch (e) {
      return Response.json(
        { error: "bootstrap_failed", message: String(e && e.message || e) },
        { status: 500 },
      );
    }
  },
};
