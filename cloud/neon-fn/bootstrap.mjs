/**
 * Neon Function bootstrap for Pocket Cloud `api`.
 * Tiny loader that fetches pinned scram-api.mjs from GitHub (avoids MCP zip size limits).
 *
 * Hard rules for the deployed zip (MCP transcription corruption):
 * - never use the substring `fetch(req` (use `fetch(r)` / dynamic import)
 * - avoid top-level `throw` in the bootstrap itself
 */
let modPromise = null;

export default {
  async fetch(r) {
    const u = new URL(r.url);
    const P = "961f8990dd8c998f36f9a1c97d249ff260cbd9cc";
    const U =
      "https://raw.githubusercontent.com/bighappysmiley/Pocket/" +
      P +
      "/cloud/neon-fn/scram-api.mjs?cb=v56ota1";
    if (u.pathname === "/v1/bootstrap") {
      return Response.json({ ok: true, pin: P, src: U });
    }
    try {
      if (!modPromise) {
        modPromise = (async () => {
          const fs = await import("node:fs");
          const os = await import("node:os");
          const path = await import("node:path");
          const res = await globalThis.fetch(U, { cache: "no-store" });
          if (!res.ok) throw new Error("fetch " + res.status);
          const body = await res.text();
          if (!body.includes("scram-api-v16-stt") || !body.includes("stt_proxy_url")) {
            throw new Error("bad source len=" + body.length);
          }
          if (!body.includes("bighappysmiley.github.io/Pocket/firmware-manifest.json")) {
            throw new Error("missing ota pages mirror");
          }
          const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fn-"));
          const file = path.join(dir, "i.mjs");
          fs.writeFileSync(file, body);
          return import("file://" + file);
        })();
      }
      const mod = await modPromise;
      return mod.default.fetch.bind(mod.default)(r);
    } catch (e) {
      modPromise = null;
      return Response.json(
        { error: "bootstrap_failed", message: String(e && e.message || e) },
        { status: 500 },
      );
    }
  },
};
