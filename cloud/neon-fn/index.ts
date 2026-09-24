/**
 * Neon Functions entry — Pocket Cloud API (auth + pairing + me).
 * Full Notes/Lists sync remains in the Node sql.js service; this host
 * unblocks companion login and device pairing on Neon.
 */
import { Hono } from "hono";
import { cors } from "hono/cors";
import { getCookie, setCookie, deleteCookie } from "hono/cookie";
import pg from "pg";
import {
  createHash,
  randomBytes,
  randomUUID,
  scryptSync,
  timingSafeEqual,
} from "node:crypto";

const pool = new pg.Pool({
  connectionString: process.env.DATABASE_URL,
  max: 5,
});

const SESSION_COOKIE = process.env.SESSION_COOKIE_NAME || "pocket_session";
const SESSION_SECRET = process.env.SESSION_SECRET || "dev-change-me";
const DEVICE_API_KEY = process.env.DEVICE_API_KEY || "dev-device-api-key";
const PWA_ORIGIN = (process.env.PWA_ORIGIN || "https://bighappysmiley.github.io/Pocket").replace(
  /\/$/,
  "",
);
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

function sha256Hex(input: string) {
  return createHash("sha256").update(input, "utf8").digest("hex");
}
function randomToken(bytes = 32) {
  return randomBytes(bytes).toString("base64url");
}
function newId() {
  return randomUUID();
}
function hashPassword(password: string) {
  const salt = randomBytes(16).toString("hex");
  const hash = scryptSync(password, salt, 64).toString("hex");
  return `scrypt$${salt}$${hash}`;
}
function verifyPassword(password: string, stored: string) {
  const parts = stored.split("$");
  if (parts.length !== 3 || parts[0] !== "scrypt") return false;
  const actual = scryptSync(password, parts[1]!, 64).toString("hex");
  try {
    return timingSafeEqual(Buffer.from(actual, "hex"), Buffer.from(parts[2]!, "hex"));
  } catch {
    return false;
  }
}
function normalizePairCode(code: string) {
  return code.trim().toUpperCase().replace(/[^A-Z2-9]/g, "");
}
function isValidPairCode(code: string) {
  const n = normalizePairCode(code);
  if (n.length !== 8) return false;
  return [...n].every((ch) => PAIR_ALPHABET.includes(ch));
}
function safeEqual(a: string, b: string) {
  const ba = Buffer.from(a);
  const bb = Buffer.from(b);
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}

async function q<T extends Record<string, unknown>>(
  text: string,
  values: unknown[] = [],
): Promise<T[]> {
  const res = await pool.query(text, values);
  return res.rows as T[];
}
async function q1<T extends Record<string, unknown>>(text: string, values: unknown[] = []) {
  const rows = await q<T>(text, values);
  return rows[0];
}

const SCHEMA = `
CREATE TABLE IF NOT EXISTS users (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  password_hash TEXT,
  email_verified_at TIMESTAMPTZ,
  created_at TIMESTAMPTZ NOT NULL,
  trial_consumed INTEGER NOT NULL DEFAULT 0,
  stripe_customer_id TEXT,
  had_subscription INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  token_hash TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ NOT NULL,
  expires_at TIMESTAMPTZ NOT NULL
);
CREATE TABLE IF NOT EXISTS email_verifications (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  token_hash TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ NOT NULL,
  expires_at TIMESTAMPTZ NOT NULL,
  consumed_at TIMESTAMPTZ
);
CREATE TABLE IF NOT EXISTS pair_sessions (
  id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  code_hash TEXT NOT NULL UNIQUE,
  code_public_hint TEXT NOT NULL,
  expires_at TIMESTAMPTZ NOT NULL,
  created_at TIMESTAMPTZ NOT NULL,
  claimed_at TIMESTAMPTZ,
  claimed_by_user_id TEXT,
  status TEXT NOT NULL DEFAULT 'pending'
);
CREATE TABLE IF NOT EXISTS device_links (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  device_id TEXT NOT NULL UNIQUE,
  device_name TEXT NOT NULL DEFAULT 'Pocket',
  linked_at TIMESTAMPTZ NOT NULL,
  last_seen_at TIMESTAMPTZ,
  device_token_hash TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS subscription_mirrors (
  user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
  stripe_subscription_id TEXT,
  status TEXT NOT NULL,
  trial_ends_at TIMESTAMPTZ,
  current_period_end TIMESTAMPTZ,
  cancel_at_period_end INTEGER NOT NULL DEFAULT 0,
  updated_at TIMESTAMPTZ NOT NULL
);
`;

let schemaReady: Promise<void> | null = null;
function ensureSchema() {
  if (!schemaReady) schemaReady = pool.query(SCHEMA).then(() => undefined);
  return schemaReady;
}

type User = {
  id: string;
  email: string;
  password_hash: string | null;
  email_verified_at: string | null;
  created_at: string;
  trial_consumed: number;
  stripe_customer_id: string | null;
  had_subscription: number;
};

const app = new Hono();

app.use("*", async (c, next) => {
  await ensureSchema();
  await next();
});

app.use(
  "*",
  cors({
    origin: (origin) => {
      if (!origin) return [...CORS_ORIGINS][0]!;
      return CORS_ORIGINS.has(origin) ? origin : "";
    },
    credentials: true,
    allowHeaders: ["Content-Type", "Authorization", "X-Device-Key"],
    allowMethods: ["GET", "POST", "PATCH", "DELETE", "OPTIONS"],
  }),
);

app.get("/health", (c) => c.json({ ok: true, product: "Pocket Cloud", host: "neon" }));

function setSession(c: Parameters<typeof app.fetch>[0] extends never ? never : any, token: string, expiresAt: Date) {
  const maxAge = Math.max(0, Math.floor((expiresAt.getTime() - Date.now()) / 1000));
  setCookie(c, SESSION_COOKIE, token, {
    httpOnly: true,
    sameSite: "None",
    secure: true,
    path: "/",
    maxAge,
  });
}

async function userFromSession(c: any): Promise<User | undefined> {
  const token = getCookie(c, SESSION_COOKIE);
  if (!token) return undefined;
  const session = await q1<{ user_id: string; expires_at: string }>(
    "SELECT user_id, expires_at FROM sessions WHERE token_hash = $1",
    [sha256Hex(token)],
  );
  if (!session) return undefined;
  if (Date.parse(session.expires_at) < Date.now()) {
    await q("DELETE FROM sessions WHERE token_hash = $1", [sha256Hex(token)]);
    return undefined;
  }
  return q1<User>("SELECT * FROM users WHERE id = $1", [session.user_id]);
}

app.post("/v1/auth/register", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const email = typeof body.email === "string" ? body.email.trim().toLowerCase() : "";
  const password = typeof body.password === "string" ? body.password : "";
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    return c.json({ message: "Enter a valid email address." }, 400);
  }
  if (password.length < 8) {
    return c.json({ message: "Password must be at least 8 characters." }, 400);
  }
  const existing = await q1("SELECT id FROM users WHERE email = $1", [email]);
  if (existing) {
    return c.json({ message: "An account with that email already exists. Sign in instead." }, 400);
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
    [
      newId(),
      id,
      sha256Hex(verifyToken),
      now.toISOString(),
      new Date(now.getTime() + 15 * 60_000).toISOString(),
    ],
  );
  const base = PUBLIC_BASE_URL || new URL(c.req.url).origin;
  const verifyUrl = `${base}/v1/auth/verify-email?token=${encodeURIComponent(verifyToken)}`;
  console.log(`[verify-email] ${email} → ${verifyUrl}`);
  console.log(`[session-secret-set]=${Boolean(SESSION_SECRET)}`);
  return c.json({ ok: true, message: "Check your email for a verification link, then sign in." });
});

app.get("/v1/auth/verify-email", async (c) => {
  const token = c.req.query("token");
  if (!token) return c.json({ message: "This verification link is invalid." }, 400);
  const row = await q1<{ id: string; user_id: string; expires_at: string; consumed_at: string | null }>(
    "SELECT * FROM email_verifications WHERE token_hash = $1",
    [sha256Hex(token)],
  );
  if (!row || row.consumed_at) return c.redirect(`${PWA_ORIGIN}/login?verified=0`, 302);
  if (Date.parse(row.expires_at) < Date.now()) return c.redirect(`${PWA_ORIGIN}/login?verified=0`, 302);
  const now = new Date().toISOString();
  await q("UPDATE email_verifications SET consumed_at = $1 WHERE id = $2", [now, row.id]);
  await q("UPDATE users SET email_verified_at = $1 WHERE id = $2 AND email_verified_at IS NULL", [
    now,
    row.user_id,
  ]);
  return c.redirect(`${PWA_ORIGIN}/login?verified=1`, 302);
});

app.post("/v1/auth/login", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const email = typeof body.email === "string" ? body.email.trim().toLowerCase() : "";
  const password = typeof body.password === "string" ? body.password : "";
  const user = await q1<User>("SELECT * FROM users WHERE email = $1", [email]);
  if (!user?.password_hash || !verifyPassword(password, user.password_hash)) {
    return c.json({ message: "Wrong email or password." }, 401);
  }
  if (!user.email_verified_at) {
    return c.json(
      { message: "Verify your email before signing in. Check your inbox for the link." },
      400,
    );
  }
  const token = randomToken(32);
  const expires = new Date(Date.now() + 30 * 86_400_000);
  await q(
    `INSERT INTO sessions (id, user_id, token_hash, created_at, expires_at) VALUES ($1,$2,$3,$4,$5)`,
    [newId(), user.id, sha256Hex(token), new Date().toISOString(), expires.toISOString()],
  );
  setSession(c, token, expires);
  return c.json({ ok: true, user: { id: user.id, email: user.email } });
});

app.post("/v1/auth/logout", async (c) => {
  const token = getCookie(c, SESSION_COOKIE);
  if (token) await q("DELETE FROM sessions WHERE token_hash = $1", [sha256Hex(token)]);
  deleteCookie(c, SESSION_COOKIE, { path: "/" });
  return c.json({ ok: true });
});

app.get("/v1/me", async (c) => {
  const user = await userFromSession(c);
  if (!user) return c.json({ message: "Sign in to continue." }, 401);
  const devices = await q<{
    id: string;
    device_id: string;
    device_name: string;
    linked_at: string;
    last_seen_at: string | null;
  }>("SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id = $1 ORDER BY linked_at DESC", [
    user.id,
  ]);
  const sub = await q1<{ status: string; trial_ends_at: string | null; current_period_end: string | null }>(
    "SELECT status, trial_ends_at, current_period_end FROM subscription_mirrors WHERE user_id = $1",
    [user.id],
  );
  const entitled = sub?.status === "active" || sub?.status === "trialing";
  return c.json({
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
});

app.post("/v1/pair/sessions", async (c) => {
  const auth = c.req.header("authorization") || c.req.header("x-device-key") || "";
  const key = auth.toLowerCase().startsWith("bearer ") ? auth.slice(7) : auth;
  if (!safeEqual(key, DEVICE_API_KEY)) return c.json({ message: "Sign in to continue." }, 401);
  const body = await c.req.json().catch(() => ({}));
  const device_id = typeof body.device_id === "string" ? body.device_id : "";
  const code_public = typeof body.code_public === "string" ? body.code_public : "";
  const code = normalizePairCode(code_public);
  if (!device_id.trim() || !isValidPairCode(code)) {
    return c.json({ message: "That pairing code isn't valid." }, 400);
  }
  const now = new Date();
  const expires = new Date(now.getTime() + 10 * 60_000);
  await q(`UPDATE pair_sessions SET status = 'expired' WHERE device_id = $1 AND status = 'pending'`, [
    device_id,
  ]);
  await q(
    `INSERT INTO pair_sessions (id, device_id, code_hash, code_public_hint, expires_at, created_at, status)
     VALUES ($1,$2,$3,$4,$5,$6,'pending')`,
    [newId(), device_id, sha256Hex(code), code.slice(0, 2) + "******", expires.toISOString(), now.toISOString()],
  );
  return c.json({ ok: true, expires_at: expires.toISOString() }, 201);
});

app.get("/v1/pair/sessions/:code", async (c) => {
  const code = normalizePairCode(c.req.param("code"));
  if (!isValidPairCode(code)) return c.json({ status: "expired" });
  const row = await q1<{ status: string; expires_at: string }>(
    "SELECT status, expires_at FROM pair_sessions WHERE code_hash = $1",
    [sha256Hex(code)],
  );
  if (!row) return c.json({ status: "expired" });
  if (row.status === "claimed") return c.json({ status: "claimed", device_label: "Pocket" });
  if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
    return c.json({ status: "expired" });
  }
  return c.json({ status: "pending", device_label: "Pocket" });
});

app.post("/v1/pair/claim", async (c) => {
  const user = await userFromSession(c);
  if (!user) return c.json({ message: "Sign in to continue." }, 401);
  const body = await c.req.json().catch(() => ({}));
  const code = normalizePairCode(typeof body.code === "string" ? body.code : "");
  if (!isValidPairCode(code)) return c.json({ message: "We couldn't find that code." }, 404);
  const row = await q1<{ id: string; device_id: string; status: string; expires_at: string }>(
    "SELECT * FROM pair_sessions WHERE code_hash = $1",
    [sha256Hex(code)],
  );
  if (!row) return c.json({ message: "We couldn't find that code." }, 404);
  if (row.status === "claimed") return c.json({ message: "That code was already used." }, 400);
  if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
    return c.json({ message: "This code has expired. Generate a new one on your Pocket." }, 410);
  }
  const existing = await q1<{ user_id: string; id: string }>(
    "SELECT user_id, id FROM device_links WHERE device_id = $1",
    [row.device_id],
  );
  if (existing && existing.user_id !== user.id) {
    return c.json(
      { message: "This Pocket is linked to another account. Unlink it there first.", code: "pair_other_account" },
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
  return c.json({
    device_id: row.device_id,
    device_name: "Pocket",
    linked_at: now,
    device_token: deviceToken,
  });
});

app.get("/v1/devices", async (c) => {
  const user = await userFromSession(c);
  if (!user) return c.json({ message: "Sign in to continue." }, 401);
  const devices = await q(
    `SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id = $1 ORDER BY linked_at DESC`,
    [user.id],
  );
  return c.json({ devices });
});

export default app;
