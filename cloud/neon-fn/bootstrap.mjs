import { writeFileSync, mkdtempSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

const SRC_URL = "https://raw.githubusercontent.com/bighappysmiley/Pocket/7062439f81550fe8f37b655362d549ca48a78b37/cloud/neon-fn/scram-api.mjs?cb=v16stt2";
const PIN = "7062439f81550fe8f37b655362d549ca48a78b37";

let modPromise = null;
async function load() {
  if (modPromise) return modPromise;
  modPromise = (async () => {
    const res = await fetch(SRC_URL, { cache: "no-store" });
    if (!res.ok) throw new Error("bootstrap fetch failed: " + res.status + " url=" + SRC_URL);
    const src = await res.text();
    if (!src.includes("scram-api-v16-stt")) {
      throw new Error("unexpected source (pin=" + PIN + ", len=" + src.length + ")");
    }
    const dir = mkdtempSync(join(tmpdir(), "fn-"));
    const file = join(dir, "index.mjs");
    writeFileSync(file, src);
    return import("file://" + file);
  })();
  return modPromise;
}

export default {
  async fetch(req) {
    const u = new URL(req.url);
    if (u.pathname === "/v1/bootstrap") {
      return new Response(JSON.stringify({ ok: true, pin: PIN, src: SRC_URL }), {
        headers: { "content-type": "application/json", "cache-control": "no-store" },
      });
    }
    try {
      const m = await load();
      return m.default.fetch(req);
    } catch (e) {
      modPromise = null;
      return new Response(JSON.stringify({ error: "bootstrap_failed", message: String(e && e.message || e), pin: PIN }), {
        status: 500,
        headers: { "content-type": "application/json" },
      });
    }
  },
};
