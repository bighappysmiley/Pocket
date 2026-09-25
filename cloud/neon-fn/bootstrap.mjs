// This is what is actually deployed to the Neon Function `api` right now.
// See the "Deployment mechanism" section in README.md for why.
//
// To ship a change to scram-api.mjs:
//   1. Edit, commit, and push scram-api.mjs to `main`.
//   2. Update SRC_URL below to the new commit SHA.
//   3. Zip just this file and deploy it via the Neon "Deploy Function" tool
//      (it's tiny, so it transcribes reliably even through AI tool calls).
//   4. Verify GET /health returns a sourceSha256 matching
//      `sha256sum cloud/neon-fn/scram-api.mjs` locally.
import { writeFileSync, mkdtempSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

const SRC_URL = "https://raw.githubusercontent.com/bighappysmiley/Pocket/0a2a733d7b79afdec81c338ed454ee4125d64ae9/cloud/neon-fn/scram-api.mjs";

let modPromise = null;
async function load() {
  if (modPromise) return modPromise;
  modPromise = (async () => {
    const res = await fetch(SRC_URL);
    if (!res.ok) throw new Error("bootstrap fetch failed: " + res.status);
    const src = await res.text();
    const dir = mkdtempSync(join(tmpdir(), "fn-"));
    const file = join(dir, "index.mjs");
    writeFileSync(file, src);
    return import("file://" + file);
  })();
  return modPromise;
}

export default {
  async fetch(req) {
    try {
      const m = await load();
      return m.default.fetch(req);
    } catch (e) {
      modPromise = null;
      return new Response(JSON.stringify({ error: "bootstrap_failed", message: String(e && e.message || e) }), {
        status: 500,
        headers: { "content-type": "application/json" },
      });
    }
  },
};
