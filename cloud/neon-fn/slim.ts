import pg from "pg";
import {
  createHash,
  randomBytes,
  randomUUID,
  scryptSync,
  timingSafeEqual,
} from "node:crypto";

const pool = new pg.Pool({ connectionString: process.env.DATABASE_URL, max: 5 });
const SESSION_COOKIE = process.env.SESSION_COOKIE_NAME || "pocket_session";
const SESSION_SECRET = process.env.SESSION_SECRET || "dev-change-me";
const DEVICE_API_KEY = process.env.DEVICE_API_KEY || "dev-device-api-key";
const PWA_ORIGIN = (process.env.PWA_ORIGIN || "https://bighappysmiley.github.io/Pocket").replace(/\/$/, "");
const PUBLIC_BASE_URL = (process.env.PUBLIC_BASE_URL || "").replace(/\/$/, "");
const CORS_ORIGINS = new Set(
  [
    "http://localhost:5173",
    "https://app.getpocket.device",
    "https://bighappysmiley.github.io",
    ...(process.env.CORS_ORIGINS || "").split(",").map((s) => s.trim()).filter(Boolean),
  ].filter(Boolean),
);
const PAIR_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

function sha256Hex(i) {
  return createHash("sha256").update(i, "utf8").digest("hex");
}
function randomToken(b = 32) {
  return randomBytes(b).toString("base64url");
}
function newId() {
  return randomUUID();
}
function hashPassword(p) {
  const s = randomBytes(16).toString("hex");
  return `scrypt$${s}$${scryptSync(p, s, 64).toString("hex")}`;
}
function verifyPassword(p, st) {
  const parts = st.split("$");
  if (parts.length !== 3 || parts[0] !== "scrypt") return false;
  const a = scryptSync(p, parts[1], 64).toString("hex");
  try {
    return timingSafeEqual(Buffer.from(a, "hex"), Buffer.from(parts[2], "hex"));
  } catch {
    return false;
  }
}
function normalizePairCode(c) {
  return c.trim().toUpperCase().replace(/[^A-Z2-9]/g, "");
}
function isValidPairCode(c) {
  const n = normalizePairCode(c);
  return n.length === 8 && [...n].every((ch) => PAIR_ALPHABET.includes(ch));
}
function safeEqual(a, b) {
  const ba = Buffer.from(a);
  const bb = Buffer.from(b);
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}
function corsHeaders(req) {
  const o = req.headers.get("origin") || "";
  const h = { vary: "Origin" };
  if (CORS_ORIGINS.has(o) || o.endsWith(".github.io")) {
    h["access-control-allow-origin"] = o;
    h["access-control-allow-credentials"] = "true";
    h["access-control-allow-headers"] = "content-type,authorization,x-device-key";
    h["access-control-allow-methods"] = "GET,POST,OPTIONS";
  }
  return h;
}
function json(req, body, status = 200, extra = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders(req), "content-type": "application/json", ...extra },
  });
}
function getCookie(req, name) {
  const c = req.headers.get("cookie") || "";
  const m = c.match(new RegExp("(?:^|;\\s*)" + name + "=([^;]+)"));
  return m ? decodeURIComponent(m[1]) : null;
}
async function readJson(req) {
  try {
    return await req.json();
  } catch {
    return null;
  }
}
async function q(sql, params = []) {
  return pool.query(sql, params);
}
async function q1(sql, params = []) {
  const { rows } = await pool.query(sql, params);
  return rows[0];
}
async function userFromSession(req) {
  const token = getCookie(req, SESSION_COOKIE);
  if (!token) return undefined;
  const session = await q1("SELECT user_id, expires_at FROM sessions WHERE token_hash = $1", [
    sha256Hex(token),
  ]);
  if (!session) return undefined;
  if (Date.parse(session.expires_at) < Date.now()) {
    await q("DELETE FROM sessions WHERE token_hash = $1", [sha256Hex(token)]);
    return undefined;
  }
  return q1("SELECT * FROM users WHERE id = $1", [session.user_id]);
}
function sessionCookie(token, maxAge) {
  return `${SESSION_COOKIE}=${encodeURIComponent(token)}; Path=/; HttpOnly; Secure; SameSite=None; Max-Age=${maxAge}`;
}

export default {
  async fetch(req) {
    if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: corsHeaders(req) });
    const u = new URL(req.url);
    const path = u.pathname;
    try {
      if (path === "/" || path === "/health" || path === "/v1/health") {
        return json(req, { ok: true, product: "Pocket Cloud", host: "neon" });
      }

      if (req.method === "POST" && path === "/v1/auth/register") {
        const body = await readJson(req);
        const email = typeof body?.email === "string" ? body.email.trim().toLowerCase() : "";
        const password = typeof body?.password === "string" ? body.password : "";
        if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
          return json(req, { message: "Enter a valid email address." }, 400);
        }
        if (password.length < 8) {
          return json(req, { message: "Password must be at least 8 characters." }, 400);
        }
        const existing = await q1("SELECT id FROM users WHERE email = $1", [email]);
        if (existing) {
          return json(req, { message: "An account with that email already exists. Sign in instead." }, 400);
        }
        const id = newId();
        const now = new Date();
        await q(
          `INSERT INTO users (id, email, password_hash, email_verified_at, created_at, trial_consumed, stripe_customer_id, had_subscription)
           VALUES ($1,$2,$3,NULL,$4,0,NULL,0)`,
          [id, email, hashPassword(password), now.toISOString()],
        );
        const verifyToken = randomToken(32);
        await q(
          `INSERT INTO email_verifications (id, user_id, token_hash, created_at, expires_at, consumed_at)
           VALUES ($1,$2,$3,$4,$5,NULL)`,
          [newId(), id, sha256Hex(verifyToken), now.toISOString(), new Date(now.getTime() + 15 * 60_000).toISOString()],
        );
        const base = PUBLIC_BASE_URL || u.origin;
        const verifyUrl = `${base}/v1/auth/verify-email?token=${encodeURIComponent(verifyToken)}`;
        console.log(`[verify-email] ${email} → ${verifyUrl}`);
        return json(req, { ok: true, message: "Check your email for a verification link, then sign in." });
      }

      if (req.method === "GET" && path === "/v1/auth/verify-email") {
        const token = u.searchParams.get("token");
        if (!token) return json(req, { message: "This verification link is invalid." }, 400);
        const row = await q1("SELECT * FROM email_verifications WHERE token_hash = $1", [sha256Hex(token)]);
        if (!row || row.consumed_at) {
          return Response.redirect(`${PWA_ORIGIN}/login?verified=0`, 302);
        }
        if (Date.parse(row.expires_at) < Date.now()) {
          return Response.redirect(`${PWA_ORIGIN}/login?verified=0`, 302);
        }
        const now = new Date().toISOString();
        await q("UPDATE email_verifications SET consumed_at = $1 WHERE id = $2", [now, row.id]);
        await q("UPDATE users SET email_verified_at = $1 WHERE id = $2 AND email_verified_at IS NULL", [
          now,
          row.user_id,
        ]);
        return Response.redirect(`${PWA_ORIGIN}/login?verified=1`, 302);
      }

      if (req.method === "POST" && path === "/v1/auth/login") {
        const body = await readJson(req);
        const email = typeof body?.email === "string" ? body.email.trim().toLowerCase() : "";
        const password = typeof body?.password === "string" ? body.password : "";
        const user = await q1("SELECT * FROM users WHERE email = $1", [email]);
        if (!user?.password_hash || !verifyPassword(password, user.password_hash)) {
          return json(req, { message: "Wrong email or password." }, 401);
        }
        if (!user.email_verified_at) {
          return json(req, {
            message: "Verify your email before signing in. Check your inbox for the link.",
          }, 400);
        }
        const token = randomToken(32);
        const expires = new Date(Date.now() + 30 * 86_400_000);
        await q(
          `INSERT INTO sessions (id, user_id, token_hash, created_at, expires_at) VALUES ($1,$2,$3,$4,$5)`,
          [newId(), user.id, sha256Hex(token), new Date().toISOString(), expires.toISOString()],
        );
        return json(req, { ok: true, user: { id: user.id, email: user.email } }, 200, {
          "set-cookie": sessionCookie(token, 30 * 86400),
        });
      }

      if (req.method === "POST" && path === "/v1/auth/logout") {
        const token = getCookie(req, SESSION_COOKIE);
        if (token) await q("DELETE FROM sessions WHERE token_hash = $1", [sha256Hex(token)]);
        return json(req, { ok: true }, 200, {
          "set-cookie": `${SESSION_COOKIE}=; Path=/; HttpOnly; Secure; SameSite=None; Max-Age=0`,
        });
      }

      if (req.method === "GET" && path === "/v1/me") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const { rows: devices } = await q(
          "SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id = $1 ORDER BY linked_at DESC",
          [user.id],
        );
        const sub = await q1(
          "SELECT status, trial_ends_at, current_period_end FROM subscription_mirrors WHERE user_id = $1",
          [user.id],
        );
        const entitled = sub?.status === "active" || sub?.status === "trialing";
        return json(req, {
          user: {
            id: user.id,
            email: user.email,
            created_at: user.created_at,
            trial_consumed: Boolean(user.trial_consumed),
            email_verified: Boolean(user.email_verified_at),
          },
          entitlement: {
            status: sub?.status || "free",
            entitled: Boolean(entitled),
            trial_consumed: Boolean(user.trial_consumed),
            trial_ends_at: sub?.trial_ends_at || null,
            current_period_end: sub?.current_period_end || null,
            has_customer: Boolean(user.stripe_customer_id),
          },
          devices,
        });
      }

      if (req.method === "POST" && path === "/v1/pair/sessions") {
        const auth = req.headers.get("authorization") || req.headers.get("x-device-key") || "";
        const key = auth.toLowerCase().startsWith("bearer ") ? auth.slice(7) : auth;
        if (!safeEqual(key, DEVICE_API_KEY)) return json(req, { message: "Sign in to continue." }, 401);
        const body = await readJson(req);
        const device_id = typeof body?.device_id === "string" ? body.device_id : "";
        const code = normalizePairCode(typeof body?.code_public === "string" ? body.code_public : "");
        if (!device_id.trim() || !isValidPairCode(code)) {
          return json(req, { message: "That pairing code isn't valid." }, 400);
        }
        const now = new Date();
        const expires = new Date(now.getTime() + 10 * 60_000);
        await q(`UPDATE pair_sessions SET status = 'expired' WHERE device_id = $1 AND status = 'pending'`, [
          device_id,
        ]);
        await q(
          `INSERT INTO pair_sessions (id, device_id, code_hash, code_public_hint, expires_at, created_at, status)
           VALUES ($1,$2,$3,$4,$5,$6,'pending')`,
          [
            newId(),
            device_id,
            sha256Hex(code),
            code.slice(0, 2) + "******",
            expires.toISOString(),
            now.toISOString(),
          ],
        );
        return json(req, { ok: true, expires_at: expires.toISOString() }, 201);
      }

      const pairMatch = path.match(/^\/v1\/pair\/sessions\/([^/]+)$/);
      if (req.method === "GET" && pairMatch) {
        const code = normalizePairCode(decodeURIComponent(pairMatch[1]));
        if (!isValidPairCode(code)) return json(req, { status: "expired" });
        const row = await q1("SELECT status, expires_at FROM pair_sessions WHERE code_hash = $1", [
          sha256Hex(code),
        ]);
        if (!row) return json(req, { status: "expired" });
        if (row.status === "claimed") return json(req, { status: "claimed", device_label: "Pocket" });
        if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
          return json(req, { status: "expired" });
        }
        return json(req, { status: "pending", device_label: "Pocket" });
      }

      if (req.method === "POST" && path === "/v1/pair/claim") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const body = await readJson(req);
        const code = normalizePairCode(typeof body?.code === "string" ? body.code : "");
        if (!isValidPairCode(code)) return json(req, { message: "We couldn't find that code." }, 404);
        const row = await q1("SELECT * FROM pair_sessions WHERE code_hash = $1", [sha256Hex(code)]);
        if (!row) return json(req, { message: "We couldn't find that code." }, 404);
        if (row.status === "claimed") return json(req, { message: "That code was already used." }, 400);
        if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
          return json(req, { message: "This code has expired. Generate a new one on your Pocket." }, 410);
        }
        const existing = await q1("SELECT user_id, id FROM device_links WHERE device_id = $1", [row.device_id]);
        if (existing && existing.user_id !== user.id) {
          return json(
            req,
            {
              message: "This Pocket is linked to another account. Unlink it there first.",
              code: "pair_other_account",
            },
            409,
          );
        }
        const now = new Date().toISOString();
        const deviceToken = randomToken(32);
        if (existing) {
          await q(
            `UPDATE device_links SET user_id=$1, device_name='Pocket', linked_at=$2, device_token_hash=$3, last_seen_at=$2 WHERE id=$4`,
            [user.id, now, sha256Hex(deviceToken), existing.id],
          );
        } else {
          await q(
            `INSERT INTO device_links (id, user_id, device_id, device_name, linked_at, last_seen_at, device_token_hash)
             VALUES ($1,$2,$3,'Pocket',$4,$4,$5)`,
            [newId(), user.id, row.device_id, now, sha256Hex(deviceToken)],
          );
        }
        await q(`UPDATE pair_sessions SET status='claimed', claimed_at=$1, claimed_by_user_id=$2 WHERE id=$3`, [
          now,
          user.id,
          row.id,
        ]);
        return json(req, {
          device_id: row.device_id,
          device_name: "Pocket",
          linked_at: now,
          device_token: deviceToken,
        });
      }

      if (req.method === "GET" && path === "/v1/devices") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const { rows: devices } = await q(
          `SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id = $1 ORDER BY linked_at DESC`,
          [user.id],
        );
        return json(req, { devices });
      }

      return json(req, { error: "not_found", path }, 404);
    } catch (e) {
      console.error(e);
      return json(req, { error: "internal", message: String(e?.message || e) }, 500);
    }
  },
};
