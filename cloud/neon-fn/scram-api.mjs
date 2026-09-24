import { connect } from "tls";
import { createHash, createHmac, pbkdf2Sync, randomBytes, randomUUID, scryptSync, timingSafeEqual } from "crypto";

const DEVICE_API_KEY = (process.env.DEVICE_API_KEY || "dev-device-api-key").trim();
const SESSION_COOKIE = process.env.SESSION_COOKIE_NAME || "pocket_session";
const PWA_ORIGIN = (process.env.PWA_ORIGIN || "https://bighappysmiley.github.io/Pocket").replace(/\/$/, "");
const PUBLIC_BASE_URL = (process.env.PUBLIC_BASE_URL || "").replace(/\/$/, "");
const sha256Hex = (i) => createHash("sha256").update(i, "utf8").digest("hex");
const randomToken = (b = 32) => randomBytes(b).toString("base64url");
const newId = () => randomUUID();
function hashPassword(p) {
  const s = randomBytes(16).toString("hex");
  return `scrypt$${s}$${scryptSync(p, s, 64).toString("hex")}`;
}
function verifyPassword(p, st) {
  const parts = String(st || "").split("$");
  if (parts.length !== 3 || parts[0] !== "scrypt") return false;
  const a = scryptSync(p, parts[1], 64).toString("hex");
  try {
    return timingSafeEqual(Buffer.from(a, "hex"), Buffer.from(parts[2], "hex"));
  } catch {
    return false;
  }
}
const PAIR_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const normalizePairCode = (c) => c.trim().toUpperCase().replace(/[^A-Z2-9]/g, "");
const isValidPairCode = (c) => {
  const n = normalizePairCode(c);
  return n.length === 8 && [...n].every((ch) => PAIR_ALPHABET.includes(ch));
};
function safeEqual(a, b) {
  const ba = Buffer.from(String(a)), bb = Buffer.from(String(b));
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}
function cors(req) {
  const o = req.headers.get("origin") || "";
  const h = { vary: "Origin" };
  if (o) {
    h["access-control-allow-origin"] = o;
    h["access-control-allow-credentials"] = "true";
    // x-pocket-session: bearer alternative — third-party cookies often blocked on mobile Safari / ITP.
    h["access-control-allow-headers"] = "content-type,authorization,x-device-key,x-pocket-session";
    h["access-control-allow-methods"] = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
  }
  return h;
}
function json(req, body, status = 200, extra = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...cors(req), "content-type": "application/json", ...extra },
  });
}
function getCookie(req, name) {
  const c = req.headers.get("cookie") || "";
  const m = c.match(new RegExp("(?:^|;\\s*)" + name + "=([^;]+)"));
  return m ? decodeURIComponent(m[1]) : null;
}
function sessionCookie(token, maxAge) {
  return `${SESSION_COOKIE}=${encodeURIComponent(token)}; Path=/; HttpOnly; Secure; SameSite=None; Max-Age=${maxAge}`;
}
function parseDbUrl(u) {
  const x = new URL(u);
  return {
    host: x.hostname,
    port: Number(x.port || 5432),
    user: decodeURIComponent(x.username),
    pass: decodeURIComponent(x.password),
    db: x.pathname.slice(1),
  };
}
function encodeStartup({ user, db }) {
  const params = Buffer.from(`user\0${user}\0database\0${db}\0\0`);
  const len = 8 + params.length;
  const b = Buffer.alloc(len);
  b.writeInt32BE(len, 0);
  b.writeInt32BE(196608, 4);
  params.copy(b, 8);
  return b;
}
function encodePassword(pw) {
  const body = Buffer.from(pw + "\0");
  const b = Buffer.alloc(5 + body.length);
  b[0] = 0x70;
  b.writeInt32BE(4 + body.length, 1);
  body.copy(b, 5);
  return b;
}
function encodeSaslInitial(mechanism, data) {
  const mech = Buffer.from(mechanism + "\0");
  const d = Buffer.from(data, "utf8");
  const len = 4 + mech.length + 4 + d.length;
  const b = Buffer.alloc(1 + len);
  b[0] = 0x70;
  b.writeInt32BE(len, 1);
  mech.copy(b, 5);
  b.writeInt32BE(d.length, 5 + mech.length);
  d.copy(b, 5 + mech.length + 4);
  return b;
}
function encodeSaslResponse(data) {
  const d = Buffer.from(data, "utf8");
  const b = Buffer.alloc(5 + d.length);
  b[0] = 0x70;
  b.writeInt32BE(4 + d.length, 1);
  d.copy(b, 5);
  return b;
}
function encodeQuery(sql) {
  const body = Buffer.from(sql + "\0");
  const b = Buffer.alloc(5 + body.length);
  b[0] = 0x51;
  b.writeInt32BE(4 + body.length, 1);
  body.copy(b, 5);
  return b;
}
function esc(s) {
  return String(s).replace(/'/g, "''");
}
function sqlStr(s) {
  return "'" + esc(s) + "'";
}
function sqlNullable(s) {
  return s == null || s === "" ? "NULL" : sqlStr(s);
}
function scramFinal(password, clientFirstBare, serverFirst, clientNonce) {
  const kv = Object.fromEntries(
    serverFirst.split(",").map((p) => {
      const i = p.indexOf("=");
      return [p.slice(0, i), p.slice(i + 1)];
    })
  );
  if (!kv.r || !kv.r.startsWith(clientNonce)) throw new Error("scram nonce mismatch");
  const salt = Buffer.from(kv.s, "base64");
  const iter = parseInt(kv.i, 10);
  const salted = pbkdf2Sync(password, salt, iter, 32, "sha256");
  const clientKey = createHmac("sha256", salted).update("Client Key").digest();
  const storedKey = createHash("sha256").update(clientKey).digest();
  const withoutProof = `c=biws,r=${kv.r}`;
  const authMsg = `${clientFirstBare},${serverFirst},${withoutProof}`;
  const clientSig = createHmac("sha256", storedKey).update(authMsg).digest();
  const proof = Buffer.alloc(clientKey.length);
  for (let i = 0; i < clientKey.length; i++) proof[i] = clientKey[i] ^ clientSig[i];
  return `${withoutProof},p=${proof.toString("base64")}`;
}

let sock = null, buf = Buffer.alloc(0), waiters = [], q = [];
function feed(chunk) {
  buf = Buffer.concat([buf, chunk]);
  while (buf.length >= 5) {
    const type = String.fromCharCode(buf[0]);
    const len = buf.readInt32BE(1);
    if (buf.length < 1 + len) break;
    const payload = buf.subarray(5, 1 + len);
    buf = buf.subarray(1 + len);
    const msg = { type, payload };
    if (waiters.length) waiters.shift()(msg);
    else q.push(msg);
  }
}
function once() {
  return new Promise((resolve, reject) => {
    if (q.length) return resolve(q.shift());
    const t = setTimeout(() => reject(new Error("pg timeout")), 15000);
    waiters.push((msg) => {
      clearTimeout(t);
      resolve(msg);
    });
  });
}
async function ensurePg() {
  if (sock) return;
  const cfg = parseDbUrl(process.env.DATABASE_URL);
  sock = await new Promise((res, rej) => {
    const s = connect(
      { host: cfg.host, port: cfg.port, servername: cfg.host, rejectUnauthorized: false },
      () => res(s)
    );
    s.on("error", (e) => {
      sock = null;
      rej(e);
    });
  });
  sock.on("data", feed);
  sock.on("close", () => { sock = null; buf = Buffer.alloc(0); waiters = []; q = []; });
  sock.write(encodeStartup({ user: cfg.user, db: cfg.db }));
  let clientNonce = "", clientFirstBare = "";
  for (;;) {
    const { type, payload } = await once();
    if (type === "R") {
      const kind = payload.readInt32BE(0);
      if (kind === 0) continue;
      if (kind === 3) { sock.write(encodePassword(cfg.pass)); continue; }
      if (kind === 10) {
        const mechs = payload.slice(4).toString("utf8");
        if (!mechs.includes("SCRAM-SHA-256")) throw new Error("no scram: " + mechs);
        clientNonce = randomBytes(18).toString("base64");
        clientFirstBare = `n=${cfg.user},r=${clientNonce}`;
        sock.write(encodeSaslInitial("SCRAM-SHA-256", `n,,${clientFirstBare}`));
        continue;
      }
      if (kind === 11) {
        const serverFirst = payload.slice(4).toString("utf8");
        sock.write(encodeSaslResponse(scramFinal(cfg.pass, clientFirstBare, serverFirst, clientNonce)));
        continue;
      }
      if (kind === 12) continue;
      throw new Error("unsupported auth " + kind);
    }
    if (type === "Z") break;
    if (type === "E") throw new Error(payload.toString());
  }
}
async function query(sql) {
  await ensurePg();
  const rows = [];
  let fields = [];
  sock.write(encodeQuery(sql));
  for (;;) {
    const { type, payload } = await once();
    if (type === "T") {
      let n = payload.readInt16BE(0), o = 2;
      fields = [];
      for (let i = 0; i < n; i++) {
        let e = payload.indexOf(0, o);
        fields.push(payload.toString("utf8", o, e));
        o = e + 1 + 18;
      }
    } else if (type === "D") {
      let n = payload.readInt16BE(0), o = 2;
      const row = {};
      for (let i = 0; i < n; i++) {
        const l = payload.readInt32BE(o);
        o += 4;
        row[fields[i]] = l === -1 ? null : payload.toString("utf8", o, o + l);
        if (l !== -1) o += l;
      }
      rows.push(row);
    } else if (type === "C") {
    } else if (type === "Z") return rows;
    else if (type === "E") throw new Error(payload.toString("utf8"));
  }
}
async function userFromSession(req) {
  // Prefer explicit header (works cross-site when cookies are blocked on mobile).
  const headerTok = (req.headers.get("x-pocket-session") || "").trim();
  const cookieTok = getCookie(req, SESSION_COOKIE);
  const token = headerTok || cookieTok;
  if (!token) return null;
  const sessions = await query(`SELECT user_id, expires_at FROM sessions WHERE token_hash='${esc(sha256Hex(token))}'`);
  const session = sessions[0];
  if (!session) return null;
  if (Date.parse(session.expires_at) < Date.now()) {
    await query(`DELETE FROM sessions WHERE token_hash='${esc(sha256Hex(token))}'`);
    return null;
  }
  const users = await query(`SELECT * FROM users WHERE id='${esc(session.user_id)}'`);
  return users[0] || null;
}


const ADMIN_EMAILS = new Set(
  (process.env.ADMIN_EMAILS || "hillelfrankel0@icloud.com")
    .split(",")
    .map((s) => s.trim().toLowerCase())
    .filter(Boolean),
);

function isAdminUser(user) {
  if (!user) return false;
  if (String(user.role || "") === "admin") return true;
  return ADMIN_EMAILS.has(String(user.email || "").toLowerCase());
}

async function requireSessionUser(req) {
  const user = await userFromSession(req);
  if (!user) return { error: json(req, { message: "Sign in to continue." }, 401) };
  if (user.disabled_at) return { error: json(req, { message: "This account is disabled." }, 403) };
  return { user };
}

async function requireEntitledUser(req) {
  const gate = await requireSessionUser(req);
  if (gate.error) return gate;
  const subs = await query(
    `SELECT status FROM subscription_mirrors WHERE user_id='${esc(gate.user.id)}'`,
  );
  const st = subs[0]?.status;
  if (st !== "active" && st !== "trialing") {
    return { error: json(req, { message: "Pocket Cloud is required for this.", code: "not_entitled" }, 403) };
  }
  return gate;
}

async function requireAdmin(req) {
  const gate = await requireSessionUser(req);
  if (gate.error) return gate;
  if (!isAdminUser(gate.user)) return { error: json(req, { message: "Admin access required." }, 403) };
  return gate;
}

function serializeNote(row) {
  return {
    id: row.id,
    title: row.title || "",
    body: row.body || "",
    created_at: row.created_at,
    updated_at: row.updated_at,
    updated_by_device_id: row.updated_by_device_id,
    deleted_at: row.deleted_at || null,
    version: Number(row.version || 1),
  };
}

function serializeList(row) {
  let items = [];
  try {
    items = JSON.parse(row.items_json || "[]");
    if (!Array.isArray(items)) items = [];
  } catch {
    items = [];
  }
  return {
    id: row.id,
    title: row.title || "",
    items,
    created_at: row.created_at,
    updated_at: row.updated_at,
    updated_by_device_id: row.updated_by_device_id,
    deleted_at: row.deleted_at || null,
    version: Number(row.version || 1),
  };
}

function serializeMusicMeta(row) {
  return {
    id: row.id,
    title: row.title || "",
    filename: row.filename || "",
    mime: row.mime || "audio/wav",
    size: Number(row.size_bytes || 0),
    size_bytes: Number(row.size_bytes || 0),
    created_at: row.created_at,
  };
}

const MUSIC_MAX_INTERNAL_BYTES = 2 * 1024 * 1024; // LittleFS-safe
const MUSIC_MAX_SD_BYTES = 32 * 1024 * 1024; // when Pocket reports microSD
const MUSIC_MAX_BYTES = MUSIC_MAX_SD_BYTES; // cloud accepts up to SD ceiling

async function userIdForDeviceKey(req, deviceId) {
  const key = (req.headers.get("x-device-key") || "").trim();
  if (!safeEqual(key, DEVICE_API_KEY)) return null;
  const id = String(deviceId || "").trim();
  if (!id) return null;
  const rows = await query(
    `SELECT user_id FROM device_links WHERE device_id='${esc(id)}' ORDER BY linked_at DESC LIMIT 1`,
  );
  return rows[0]?.user_id || null;
}

async function audit(actorId, action, targetType, targetId, detail) {
  const now = new Date().toISOString();
  const detailText =
    detail == null ? null : typeof detail === "string" ? detail : JSON.stringify(detail);
  await query(
    `INSERT INTO admin_audit_log (id,actor_user_id,action,target_type,target_id,detail,created_at) VALUES ('${esc(newId())}','${esc(actorId || "")}','${esc(action)}','${esc(targetType || "")}','${esc(targetId || "")}',${sqlNullable(detailText)},'${now}')`,
  );
}

async function ensureAdminSchema() {
  await query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT NOT NULL DEFAULT 'user'`);
  await query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS disabled_at TIMESTAMPTZ`);
  await query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS last_login_at TIMESTAMPTZ`);
  await query(`ALTER TABLE device_links ADD COLUMN IF NOT EXISTS sd_present BOOLEAN NOT NULL DEFAULT false`);
  await query(`ALTER TABLE device_links ADD COLUMN IF NOT EXISTS pending_wifi_ssid TEXT`);
  await query(`ALTER TABLE device_links ADD COLUMN IF NOT EXISTS pending_wifi_password TEXT`);
  await query(`ALTER TABLE device_links ADD COLUMN IF NOT EXISTS pending_wifi_at TIMESTAMPTZ`);
  await query(`ALTER TABLE device_links ADD COLUMN IF NOT EXISTS parental_json TEXT NOT NULL DEFAULT '{}'`);
  await query(`CREATE TABLE IF NOT EXISTS badges (
    id TEXT PRIMARY KEY,
    key TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    icon_key TEXT NOT NULL DEFAULT 'star',
    description TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL
  )`);
  await query(`CREATE TABLE IF NOT EXISTS badge_awards (
    id TEXT PRIMARY KEY,
    badge_id TEXT NOT NULL REFERENCES badges(id) ON DELETE CASCADE,
    user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
    device_link_id TEXT REFERENCES device_links(id) ON DELETE CASCADE,
    awarded_by TEXT,
    note TEXT,
    awarded_at TIMESTAMPTZ NOT NULL
  )`);
  await query(`CREATE TABLE IF NOT EXISTS admin_audit_log (
    id TEXT PRIMARY KEY,
    actor_user_id TEXT,
    action TEXT NOT NULL,
    target_type TEXT,
    target_id TEXT,
    detail TEXT,
    created_at TIMESTAMPTZ NOT NULL
  )`);
  await query(`CREATE TABLE IF NOT EXISTS app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT '',
    updated_at TIMESTAMPTZ NOT NULL,
    updated_by TEXT
  )`);
  await query(`CREATE TABLE IF NOT EXISTS stripe_events (
    id TEXT PRIMARY KEY,
    received_at TIMESTAMPTZ NOT NULL
  )`);
  await query(`CREATE TABLE IF NOT EXISTS mock_checkouts (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    with_trial INT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL,
    completed_at TIMESTAMPTZ
  )`);
  await query(`CREATE TABLE IF NOT EXISTS cloud_notes (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title TEXT NOT NULL DEFAULT '',
    body TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL,
    updated_by_device_id TEXT NOT NULL,
    deleted_at TIMESTAMPTZ,
    version INTEGER NOT NULL DEFAULT 1
  )`);
  await query(`CREATE TABLE IF NOT EXISTS cloud_lists (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title TEXT NOT NULL DEFAULT '',
    items_json TEXT NOT NULL DEFAULT '[]',
    created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL,
    updated_by_device_id TEXT NOT NULL,
    deleted_at TIMESTAMPTZ,
    version INTEGER NOT NULL DEFAULT 1
  )`);
  await query(`CREATE TABLE IF NOT EXISTS backups (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL,
    blob_json TEXT NOT NULL,
    note_count INTEGER NOT NULL,
    list_count INTEGER NOT NULL,
    size_bytes INTEGER NOT NULL
  )`);
  await query(`CREATE TABLE IF NOT EXISTS connector_accounts (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    provider TEXT NOT NULL,
    refresh_token_enc TEXT,
    connected_at TIMESTAMPTZ NOT NULL,
    status TEXT NOT NULL DEFAULT 'connected',
    UNIQUE(user_id, provider)
  )`);
  await query(`CREATE TABLE IF NOT EXISTS music_tracks (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title TEXT NOT NULL DEFAULT '',
    filename TEXT NOT NULL DEFAULT '',
    mime TEXT NOT NULL DEFAULT 'audio/wav',
    size_bytes INTEGER NOT NULL DEFAULT 0,
    audio_b64 TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL,
    deleted_at TIMESTAMPTZ
  )`);
}

const STRIPE_SETTING_KEYS = [
  "stripe_secret_key",
  "stripe_webhook_secret",
  "stripe_price_monthly_id",
  "stripe_product_name",
];

function maskSecret(value) {
  const s = String(value || "");
  if (!s) return null;
  if (s.length <= 8) return "••••••••";
  return s.slice(0, 7) + "…" + s.slice(-4);
}

async function getAppSetting(key) {
  const rows = await query(`SELECT value FROM app_settings WHERE key='${esc(key)}'`);
  return rows[0]?.value ?? "";
}

async function setAppSetting(key, value, actorId) {
  const now = new Date().toISOString();
  const existing = await query(`SELECT key FROM app_settings WHERE key='${esc(key)}'`);
  if (existing.length) {
    await query(
      `UPDATE app_settings SET value=${sqlStr(value)}, updated_at='${now}', updated_by=${sqlNullable(actorId)} WHERE key='${esc(key)}'`,
    );
  } else {
    await query(
      `INSERT INTO app_settings (key,value,updated_at,updated_by) VALUES ('${esc(key)}',${sqlStr(value)},'${now}',${sqlNullable(actorId)})`,
    );
  }
}

async function loadStripeConfig() {
  const fromDb = {};
  for (const k of STRIPE_SETTING_KEYS) {
    fromDb[k] = await getAppSetting(k);
  }
  const secret =
    fromDb.stripe_secret_key ||
    (process.env.STRIPE_SECRET_KEY || "").trim();
  const webhook =
    fromDb.stripe_webhook_secret ||
    (process.env.STRIPE_WEBHOOK_SECRET || "").trim();
  const priceId =
    fromDb.stripe_price_monthly_id ||
    (process.env.STRIPE_PRICE_MONTHLY_ID || "").trim();
  const productName =
    fromDb.stripe_product_name ||
    (process.env.STRIPE_PRODUCT_NAME || "Pocket Cloud").trim() ||
    "Pocket Cloud";
  return {
    secretKey: secret,
    webhookSecret: webhook,
    priceMonthlyId: priceId,
    productName,
    source: fromDb.stripe_secret_key ? "admin" : secret ? "env" : "none",
    mockMode: !secret,
  };
}

function formBody(params) {
  return Object.entries(params)
    .filter(([, v]) => v !== undefined && v !== null && v !== "")
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`)
    .join("&");
}

async function stripeFetch(secretKey, method, path, params) {
  const res = await fetch(`https://api.stripe.com/v1${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${secretKey}`,
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: method === "GET" ? undefined : formBody(params || {}),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const msg = data?.error?.message || `Stripe error (${res.status})`;
    const err = new Error(msg);
    err.status = res.status;
    throw err;
  }
  return data;
}

async function upsertSubscriptionMirror(userId, fields) {
  const now = new Date().toISOString();
  const existing = await query(`SELECT user_id FROM subscription_mirrors WHERE user_id='${esc(userId)}'`);
  const status = fields.status || "free";
  const subId = fields.stripe_subscription_id;
  const trialEnds = fields.trial_ends_at;
  const periodEnd = fields.current_period_end;
  const cancel = fields.cancel_at_period_end ? 1 : 0;
  if (existing.length) {
    const sets = [`status='${esc(status)}'`, `updated_at='${now}'`, `cancel_at_period_end=${cancel}`];
    if (subId !== undefined) sets.push(`stripe_subscription_id=${sqlNullable(subId)}`);
    if (trialEnds !== undefined) sets.push(`trial_ends_at=${sqlNullable(trialEnds)}`);
    if (periodEnd !== undefined) sets.push(`current_period_end=${sqlNullable(periodEnd)}`);
    await query(`UPDATE subscription_mirrors SET ${sets.join(",")} WHERE user_id='${esc(userId)}'`);
  } else {
    await query(
      `INSERT INTO subscription_mirrors (user_id,stripe_subscription_id,status,trial_ends_at,current_period_end,cancel_at_period_end,updated_at) VALUES ('${esc(userId)}',${sqlNullable(subId)},'${esc(status)}',${sqlNullable(trialEnds)},${sqlNullable(periodEnd)},${cancel},'${now}')`,
    );
  }
}

async function completeMockCheckout(checkoutId) {
  const rows = await query(`SELECT * FROM mock_checkouts WHERE id='${esc(checkoutId)}'`);
  const row = rows[0];
  if (!row || row.completed_at) return;
  const now = new Date();
  const trialEnds = new Date(now.getTime() + 7 * 86400000);
  const withTrial = Number(row.with_trial) === 1;
  const periodEnd = withTrial ? trialEnds : new Date(now.getTime() + 30 * 86400000);
  const mockCustomer = `cus_mock_${String(row.user_id).replace(/-/g, "").slice(0, 14)}`;
  const mockSub = `sub_mock_${newId().replace(/-/g, "").slice(0, 14)}`;
  await query(`UPDATE users SET stripe_customer_id='${esc(mockCustomer)}'${withTrial ? ", trial_consumed=1" : ""} WHERE id='${esc(row.user_id)}'`);
  await upsertSubscriptionMirror(row.user_id, {
    stripe_subscription_id: mockSub,
    status: withTrial ? "trialing" : "active",
    trial_ends_at: withTrial ? trialEnds.toISOString() : null,
    current_period_end: periodEnd.toISOString(),
    cancel_at_period_end: false,
  });
  await query(`UPDATE mock_checkouts SET completed_at='${now.toISOString()}' WHERE id='${esc(checkoutId)}'`);
}

async function resolveUserIdFromCustomer(customerId) {
  if (!customerId) return null;
  const rows = await query(`SELECT id FROM users WHERE stripe_customer_id='${esc(customerId)}'`);
  return rows[0]?.id || null;
}

async function handleStripeWebhookEvent(event) {
  const existing = await query(`SELECT id FROM stripe_events WHERE id='${esc(event.id)}'`);
  if (existing.length) return;
  const type = event.type;
  const obj = event.data?.object || {};
  if (type === "checkout.session.completed") {
    const userId = obj.client_reference_id;
    if (userId) {
      if (typeof obj.customer === "string") {
        await query(`UPDATE users SET stripe_customer_id='${esc(obj.customer)}' WHERE id='${esc(userId)}'`);
      }
      if (obj.mode === "subscription") {
        await upsertSubscriptionMirror(userId, {
          stripe_subscription_id: typeof obj.subscription === "string" ? obj.subscription : null,
          status: "trialing",
          trial_ends_at: new Date(Date.now() + 7 * 86400000).toISOString(),
          current_period_end: new Date(Date.now() + 7 * 86400000).toISOString(),
        });
        await query(`UPDATE users SET trial_consumed=1 WHERE id='${esc(userId)}'`);
      }
    }
  } else if (type === "customer.subscription.created" || type === "customer.subscription.updated") {
    const customerId = typeof obj.customer === "string" ? obj.customer : null;
    const userId = await resolveUserIdFromCustomer(customerId);
    if (userId) {
      await upsertSubscriptionMirror(userId, {
        stripe_subscription_id: obj.id,
        status: obj.status,
        trial_ends_at: obj.trial_end ? new Date(obj.trial_end * 1000).toISOString() : null,
        current_period_end: obj.current_period_end
          ? new Date(obj.current_period_end * 1000).toISOString()
          : null,
        cancel_at_period_end: Boolean(obj.cancel_at_period_end),
      });
      if (obj.status === "trialing") {
        await query(`UPDATE users SET trial_consumed=1 WHERE id='${esc(userId)}'`);
      }
    }
  } else if (type === "customer.subscription.deleted") {
    const customerId = typeof obj.customer === "string" ? obj.customer : null;
    const userId = await resolveUserIdFromCustomer(customerId);
    if (userId) {
      await upsertSubscriptionMirror(userId, {
        stripe_subscription_id: obj.id,
        status: "canceled",
        trial_ends_at: null,
        current_period_end: obj.current_period_end
          ? new Date(obj.current_period_end * 1000).toISOString()
          : null,
        cancel_at_period_end: false,
      });
    }
  } else if (type === "invoice.paid" || type === "invoice.payment_succeeded") {
    const customerId = typeof obj.customer === "string" ? obj.customer : null;
    const userId = await resolveUserIdFromCustomer(customerId);
    const subId = typeof obj.subscription === "string" ? obj.subscription : null;
    if (userId && subId) {
      const periodEnd = obj.lines?.data?.[0]?.period?.end;
      await upsertSubscriptionMirror(userId, {
        stripe_subscription_id: subId,
        status: "active",
        current_period_end: periodEnd ? new Date(periodEnd * 1000).toISOString() : undefined,
      });
    }
  } else if (type === "invoice.payment_failed" || type === "invoice.payment_action_required") {
    const customerId = typeof obj.customer === "string" ? obj.customer : null;
    const userId = await resolveUserIdFromCustomer(customerId);
    if (userId) {
      await upsertSubscriptionMirror(userId, {
        stripe_subscription_id: typeof obj.subscription === "string" ? obj.subscription : null,
        status: "past_due",
      });
    }
  }
  const now = new Date().toISOString();
  await query(`INSERT INTO stripe_events (id, received_at) VALUES ('${esc(event.id)}','${now}')`);
}

function verifyStripeWebhookSignature(payload, header, secret) {
  if (!secret || !header) return false;
  const parts = Object.fromEntries(
    String(header)
      .split(",")
      .map((p) => p.trim().split("="))
      .filter((p) => p.length === 2),
  );
  const ts = parts.t;
  const v1 = parts.v1;
  if (!ts || !v1) return false;
  const age = Math.abs(Date.now() / 1000 - Number(ts));
  if (!Number.isFinite(age) || age > 300) return false;
  const signed = createHmac("sha256", secret).update(`${ts}.${payload}`, "utf8").digest("hex");
  try {
    return timingSafeEqual(Buffer.from(signed, "hex"), Buffer.from(v1, "hex"));
  } catch {
    return false;
  }
}

let schemaReady = null;
function readySchema() {
  if (!schemaReady) schemaReady = ensureAdminSchema().catch((e) => { schemaReady = null; throw e; });
  return schemaReady;
}


export default {
  async fetch(req) {
    if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: cors(req) });
    const u = new URL(req.url), path = u.pathname;
    try {
      await readySchema();
      if (path === "/health" || path === "/" || path === "/v1/health") {
        return json(req, { ok: true, build: "scram-api-v11-v1-polish" });
      }

      if (req.method === "POST" && path === "/v1/auth/register") {
        const body = await req.json().catch(() => ({}));
        const email = typeof body.email === "string" ? body.email.trim().toLowerCase() : "";
        const password = typeof body.password === "string" ? body.password : "";
        if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) return json(req, { message: "Enter a valid email address." }, 400);
        if (password.length < 8) return json(req, { message: "Password must be at least 8 characters." }, 400);
        const existing = await query(`SELECT id FROM users WHERE email='${esc(email)}'`);
        if (existing.length) return json(req, { message: "An account with that email already exists. Sign in instead." }, 400);
        const id = newId(), now = new Date().toISOString(), ph = hashPassword(password);
        // For now: no email verification — mark verified at create so sign-in works immediately.
        await query(`INSERT INTO users (id,email,password_hash,email_verified_at,created_at,trial_consumed,stripe_customer_id,had_subscription) VALUES ('${esc(id)}','${esc(email)}','${esc(ph)}','${now}','${now}',0,NULL,0)`);
        return json(req, { ok: true, message: "Account created. You can sign in now." });
      }

      if (req.method === "GET" && path === "/v1/auth/verify-email") {
        // Legacy links: email verification is not required. Send them to sign in.
        return Response.redirect(`${PWA_ORIGIN}/login`, 302);
      }

      if (req.method === "POST" && path === "/v1/auth/login") {
        const body = await req.json().catch(() => ({}));
        const email = typeof body.email === "string" ? body.email.trim().toLowerCase() : "";
        const password = typeof body.password === "string" ? body.password : "";
        const users = await query(`SELECT * FROM users WHERE email='${esc(email)}'`);
        const user = users[0];
        if (!user?.password_hash || !verifyPassword(password, user.password_hash)) {
          return json(req, { message: "Wrong email or password." }, 401);
        }
        if (user.disabled_at) {
          return json(req, { message: "This account is disabled." }, 403);
        }
        const token = randomToken(32);
        const nowIso = new Date().toISOString();
        const expires = new Date(Date.now() + 30 * 86400e3).toISOString();
        // Backfill verified_at for any older accounts that never clicked a link.
        if (!user.email_verified_at) {
          await query(`UPDATE users SET email_verified_at='${nowIso}' WHERE id='${esc(user.id)}'`);
        }
        await query(`INSERT INTO sessions (id,user_id,token_hash,created_at,expires_at) VALUES ('${esc(newId())}','${esc(user.id)}','${esc(sha256Hex(token))}','${nowIso}','${expires}')`);
        await query(`UPDATE users SET last_login_at='${nowIso}' WHERE id='${esc(user.id)}'`);
        // session_token in body: required on mobile where cross-site cookies are blocked.
        return json(
          req,
          {
            ok: true,
            session_token: token,
            user: { id: user.id, email: user.email, role: user.role || "user", is_admin: isAdminUser(user) },
          },
          200,
          { "set-cookie": sessionCookie(token, 30 * 86400) },
        );
      }

      if (req.method === "POST" && path === "/v1/auth/logout") {
        const headerTok = (req.headers.get("x-pocket-session") || "").trim();
        const cookieTok = getCookie(req, SESSION_COOKIE);
        const token = headerTok || cookieTok;
        if (token) await query(`DELETE FROM sessions WHERE token_hash='${esc(sha256Hex(token))}'`);
        return json(req, { ok: true }, 200, {
          "set-cookie": `${SESSION_COOKIE}=; Path=/; HttpOnly; Secure; SameSite=None; Max-Age=0`,
        });
      }

      if (req.method === "GET" && path === "/v1/me") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        if (user.disabled_at) return json(req, { message: "This account is disabled." }, 403);
        const devices = await query(`SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id='${esc(user.id)}' ORDER BY linked_at DESC`);
        const subs = await query(`SELECT status, trial_ends_at, current_period_end FROM subscription_mirrors WHERE user_id='${esc(user.id)}'`);
        const sub = subs[0];
        const entitled = sub?.status === "active" || sub?.status === "trialing";
        return json(req, {
          user: {
            id: user.id,
            email: user.email,
            created_at: user.created_at,
            trial_consumed: Boolean(Number(user.trial_consumed)),
            email_verified: Boolean(user.email_verified_at),
            role: user.role || "user",
            is_admin: isAdminUser(user),
            disabled: Boolean(user.disabled_at),
            last_login_at: user.last_login_at || null,
          },
          entitlement: {
            status: sub?.status || "free",
            entitled: Boolean(entitled),
            trial_consumed: Boolean(Number(user.trial_consumed)),
            trial_ends_at: sub?.trial_ends_at || null,
            current_period_end: sub?.current_period_end || null,
            has_customer: Boolean(user.stripe_customer_id),
          },
          devices,
        });
      }

      if (req.method === "POST" && path === "/v1/pair/sessions") {
        // Prefer x-device-key — Neon Functions may strip Authorization.
        const key = (req.headers.get("x-device-key") || "").trim();
        if (!safeEqual(key, DEVICE_API_KEY)) return json(req, { message: "Sign in to continue." }, 401);
        const body = await req.json().catch(() => ({}));
        const device_id = typeof body.device_id === "string" ? body.device_id : "";
        const code_public = typeof body.code_public === "string" ? body.code_public : typeof body.code === "string" ? body.code : "";
        const code = normalizePairCode(code_public);
        if (!device_id.trim() || !isValidPairCode(code)) return json(req, { message: "That pairing code isn't valid." }, 400);
        const now = new Date(), expires = new Date(now.getTime() + 10 * 60e3);
        await query(`UPDATE pair_sessions SET status='expired' WHERE device_id='${esc(device_id)}' AND status='pending'`);
        await query(`INSERT INTO pair_sessions (id,device_id,code_hash,code_public_hint,expires_at,created_at,status) VALUES ('${esc(newId())}','${esc(device_id)}','${esc(sha256Hex(code))}','${esc(code.slice(0, 2) + "******")}','${expires.toISOString()}','${now.toISOString()}','pending')`);
        return json(req, { ok: true, expires_at: expires.toISOString() }, 201);
      }

      const pairMatch = path.match(/^\/v1\/pair\/sessions\/([^/]+)$/);
      if (req.method === "GET" && pairMatch) {
        const code = normalizePairCode(decodeURIComponent(pairMatch[1]));
        if (!isValidPairCode(code)) return json(req, { status: "expired" });
        const rows = await query(`SELECT status, expires_at FROM pair_sessions WHERE code_hash='${esc(sha256Hex(code))}'`);
        const row = rows[0];
        if (!row) return json(req, { status: "expired" });
        if (row.status === "claimed") return json(req, { status: "claimed", device_label: "Pocket" });
        if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) return json(req, { status: "expired" });
        return json(req, { status: "pending", device_label: "Pocket" });
      }

      if (req.method === "POST" && path === "/v1/pair/claim") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const body = await req.json().catch(() => ({}));
        const code = normalizePairCode(typeof body.code === "string" ? body.code : "");
        if (!isValidPairCode(code)) return json(req, { message: "We couldn't find that code." }, 404);
        const rows = await query(`SELECT * FROM pair_sessions WHERE code_hash='${esc(sha256Hex(code))}'`);
        const row = rows[0];
        if (!row) return json(req, { message: "We couldn't find that code." }, 404);
        if (row.status === "claimed") return json(req, { message: "That code was already used." }, 400);
        if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
          return json(req, { message: "This code has expired. Generate a new one on your Pocket." }, 410);
        }
        const existingRows = await query(`SELECT user_id, id FROM device_links WHERE device_id='${esc(row.device_id)}'`);
        const existing = existingRows[0];
        if (existing && existing.user_id !== user.id) {
          return json(req, { message: "This Pocket is linked to another account. Unlink it there first.", code: "pair_other_account" }, 409);
        }
        const now = new Date().toISOString();
        const deviceToken = randomToken(32);
        if (existing) {
          await query(`UPDATE device_links SET user_id='${esc(user.id)}', device_name='Pocket', linked_at='${now}', device_token_hash='${esc(sha256Hex(deviceToken))}', last_seen_at='${now}' WHERE id='${esc(existing.id)}'`);
        } else {
          await query(`INSERT INTO device_links (id,user_id,device_id,device_name,linked_at,last_seen_at,device_token_hash) VALUES ('${esc(newId())}','${esc(user.id)}','${esc(row.device_id)}','Pocket','${now}','${now}','${esc(sha256Hex(deviceToken))}')`);
        }
        await query(`UPDATE pair_sessions SET status='claimed', claimed_at='${now}', claimed_by_user_id='${esc(user.id)}' WHERE id='${esc(row.id)}'`);
        return json(req, { device_id: row.device_id, device_name: "Pocket", linked_at: now, device_token: deviceToken });
      }

      if (req.method === "GET" && path === "/v1/devices") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const devices = await query(`SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id='${esc(user.id)}' ORDER BY linked_at DESC`);
        return json(req, { devices });
      }

      const deviceIdMatch = path.match(/^\/v1\/devices\/([^/]+)$/);
      if (deviceIdMatch && (req.method === "GET" || req.method === "PATCH")) {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const id = decodeURIComponent(deviceIdMatch[1]);
        const rows = await query(
          `SELECT id, device_id, device_name, linked_at, last_seen_at, parental_json FROM device_links WHERE (id='${esc(id)}' OR device_id='${esc(id)}') AND user_id='${esc(user.id)}' LIMIT 1`,
        );
        const row = rows[0];
        if (!row) return json(req, { message: "That device wasn't found." }, 404);
        if (req.method === "GET") {
          let parental = {};
          try { parental = JSON.parse(row.parental_json || "{}") || {}; } catch { parental = {}; }
          return json(req, {
            id: row.id,
            device_id: row.device_id,
            device_name: row.device_name,
            linked_at: row.linked_at,
            last_seen_at: row.last_seen_at,
            parental,
          });
        }
        const body = await req.json().catch(() => ({}));
        let name = typeof body.device_name === "string" ? body.device_name.trim() : "";
        if (!name) return json(req, { message: "Enter a name" }, 400);
        if (name.length > 20) return json(req, { message: "Name must be 20 characters or fewer." }, 400);
        if (!/^[\p{L}\p{N} \-']+$/u.test(name)) {
          return json(req, { message: "That name uses characters that aren't allowed." }, 400);
        }
        await query(`UPDATE device_links SET device_name='${esc(name)}' WHERE id='${esc(row.id)}'`);
        return json(req, {
          id: row.id,
          device_id: row.device_id,
          device_name: name,
          linked_at: row.linked_at,
          last_seen_at: row.last_seen_at,
        });
      }

      const deviceLinkMatch = path.match(/^\/v1\/devices\/([^/]+)\/link$/);
      if (req.method === "DELETE" && deviceLinkMatch) {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const id = decodeURIComponent(deviceLinkMatch[1]);
        const rows = await query(
          `SELECT id FROM device_links WHERE (id='${esc(id)}' OR device_id='${esc(id)}') AND user_id='${esc(user.id)}' LIMIT 1`,
        );
        if (!rows[0]) return json(req, { message: "That device wasn't found." }, 404);
        await query(`DELETE FROM device_links WHERE id='${esc(rows[0].id)}'`);
        return json(req, { ok: true });
      }

      const deviceWifiMatch = path.match(/^\/v1\/devices\/([^/]+)\/wifi$/);
      if (req.method === "POST" && deviceWifiMatch) {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const id = decodeURIComponent(deviceWifiMatch[1]);
        const rows = await query(
          `SELECT id FROM device_links WHERE (id='${esc(id)}' OR device_id='${esc(id)}') AND user_id='${esc(user.id)}' LIMIT 1`,
        );
        if (!rows[0]) return json(req, { message: "That device wasn't found." }, 404);
        const body = await req.json().catch(() => ({}));
        const ssid = typeof body.ssid === "string" ? body.ssid.trim().slice(0, 32) : "";
        const password = typeof body.password === "string" ? body.password.slice(0, 64) : "";
        if (!ssid) return json(req, { message: "Enter a network name." }, 400);
        const now = new Date().toISOString();
        await query(
          `UPDATE device_links SET pending_wifi_ssid=${sqlStr(ssid)}, pending_wifi_password=${sqlStr(password)}, pending_wifi_at='${now}' WHERE id='${esc(rows[0].id)}'`,
        );
        return json(req, { ok: true, queued: true });
      }

      const deviceParentalMatch = path.match(/^\/v1\/devices\/([^/]+)\/parental$/);
      if (deviceParentalMatch && (req.method === "GET" || req.method === "PATCH")) {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const id = decodeURIComponent(deviceParentalMatch[1]);
        const rows = await query(
          `SELECT id, parental_json FROM device_links WHERE (id='${esc(id)}' OR device_id='${esc(id)}') AND user_id='${esc(user.id)}' LIMIT 1`,
        );
        if (!rows[0]) return json(req, { message: "That device wasn't found." }, 404);
        if (req.method === "GET") {
          let parental = {};
          try { parental = JSON.parse(rows[0].parental_json || "{}") || {}; } catch { parental = {}; }
          return json(req, { parental });
        }
        const body = await req.json().catch(() => ({}));
        const pin_gated_apps = Array.isArray(body.pin_gated_apps)
          ? body.pin_gated_apps.map((a) => String(a).trim().toLowerCase()).filter(Boolean).slice(0, 16)
          : [];
        const hide_pass_share = body.hide_pass_share === true;
        const block_connectors = body.block_connectors === true;
        const parental = { pin_gated_apps, hide_pass_share, block_connectors };
        await query(
          `UPDATE device_links SET parental_json=${sqlStr(JSON.stringify(parental))} WHERE id='${esc(rows[0].id)}'`,
        );
        return json(req, { parental });
      }

      // Device heartbeat + entitlement + pending Wi‑Fi / parental (x-device-key + device_id)
      if (req.method === "GET" && path === "/v1/device/attest") {
        const deviceId = u.searchParams.get("device_id") || req.headers.get("x-device-id") || "";
        const userId = await userIdForDeviceKey(req, deviceId);
        if (!userId) return json(req, { message: "Sign in to continue." }, 401);
        const now = new Date().toISOString();
        const links = await query(
          `SELECT id, device_id, device_name, pending_wifi_ssid, pending_wifi_password, pending_wifi_at, parental_json
           FROM device_links WHERE device_id='${esc(deviceId)}' ORDER BY linked_at DESC LIMIT 1`,
        );
        const link = links[0];
        if (!link) return json(req, { message: "That device wasn't found." }, 404);
        await query(`UPDATE device_links SET last_seen_at='${now}' WHERE id='${esc(link.id)}'`);
        const subs = await query(`SELECT status, trial_ends_at, current_period_end FROM subscription_mirrors WHERE user_id='${esc(userId)}'`);
        const sub = subs[0];
        const entitled = sub?.status === "active" || sub?.status === "trialing";
        let parental = {};
        try { parental = JSON.parse(link.parental_json || "{}") || {}; } catch { parental = {}; }
        let pending_wifi = null;
        if (link.pending_wifi_ssid) {
          pending_wifi = {
            ssid: link.pending_wifi_ssid,
            password: link.pending_wifi_password || "",
            queued_at: link.pending_wifi_at || null,
          };
          await query(
            `UPDATE device_links SET pending_wifi_ssid=NULL, pending_wifi_password=NULL, pending_wifi_at=NULL WHERE id='${esc(link.id)}'`,
          );
        }
        return json(req, {
          entitled,
          status: sub?.status || "free",
          trial_ends_at: sub?.trial_ends_at || null,
          current_period_end: sub?.current_period_end || null,
          cloud_entitled: entitled,
          device_id: link.device_id,
          device_name: link.device_name,
          parental,
          pin_gated_apps: Array.isArray(parental.pin_gated_apps) ? parental.pin_gated_apps : [],
          hide_pass_share: parental.hide_pass_share === true,
          block_connectors: parental.block_connectors === true,
          pending_wifi,
        });
      }

      // ---- Music (Companion manage + device pull) ----
      if (req.method === "GET" && path === "/v1/music") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const rows = await query(
          `SELECT id, title, filename, mime, size_bytes, created_at FROM music_tracks WHERE user_id='${esc(gate.user.id)}' AND deleted_at IS NULL ORDER BY created_at DESC`,
        );
        return json(req, { tracks: rows.map(serializeMusicMeta) });
      }

      if (req.method === "POST" && path === "/v1/music") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const body = await req.json().catch(() => ({}));
        const title = typeof body.title === "string" ? body.title.trim().slice(0, 120) : "";
        let filename = typeof body.filename === "string" ? body.filename.trim().slice(0, 180) : "";
        const mime = typeof body.mime === "string" ? body.mime.trim().slice(0, 80) : "audio/wav";
        const audioB64 = typeof body.audio_b64 === "string" ? body.audio_b64.replace(/\s+/g, "") : "";
        if (!audioB64) return json(req, { message: "Choose a WAV file to upload." }, 400);
        let decoded;
        try {
          decoded = Buffer.from(audioB64, "base64");
        } catch {
          return json(req, { message: "Upload data was invalid." }, 400);
        }
        if (!decoded.length || decoded.length > MUSIC_MAX_BYTES) {
          return json(
            req,
            {
              message: `Track must be a WAV under ${MUSIC_MAX_SD_BYTES / (1024 * 1024)} MB (or ${MUSIC_MAX_INTERNAL_BYTES / (1024 * 1024)} MB without an SD card).`,
            },
            400,
          );
        }
        if (decoded.length < 12 || decoded.toString("ascii", 0, 4) !== "RIFF" || decoded.toString("ascii", 8, 12) !== "WAVE") {
          return json(req, { message: "Only WAV audio is supported on Pocket." }, 400);
        }
        if (!filename) filename = "track.wav";
        if (!/\.wav$/i.test(filename)) filename = `${filename.replace(/\.[^.]+$/, "") || "track"}.wav`;
        filename = filename.replace(/[/\\]/g, "_");
        const id = newId();
        const now = new Date().toISOString();
        const safeTitle = title || filename.replace(/\.wav$/i, "");
        await query(
          `INSERT INTO music_tracks (id,user_id,title,filename,mime,size_bytes,audio_b64,created_at,deleted_at) VALUES ('${esc(id)}','${esc(gate.user.id)}',${sqlStr(safeTitle)},${sqlStr(filename)},${sqlStr(mime || "audio/wav")},${decoded.length},${sqlStr(audioB64)},'${now}',NULL)`,
        );
        const rows = await query(
          `SELECT id, title, filename, mime, size_bytes, created_at FROM music_tracks WHERE id='${esc(id)}'`,
        );
        return json(req, { track: serializeMusicMeta(rows[0]) }, 201);
      }

      const musicAudioMatch = path.match(/^\/v1\/music\/([^/]+)\/audio$/);
      if (req.method === "GET" && musicAudioMatch) {
        const id = decodeURIComponent(musicAudioMatch[1]);
        // Companion session OR device key + device_id
        let userId = null;
        const gate = await requireEntitledUser(req);
        if (!gate.error) {
          userId = gate.user.id;
        } else {
          const deviceId = u.searchParams.get("device_id") || req.headers.get("x-device-id") || "";
          userId = await userIdForDeviceKey(req, deviceId);
          if (!userId) return gate.error;
        }
        const rows = await query(
          `SELECT mime, audio_b64, filename FROM music_tracks WHERE id='${esc(id)}' AND user_id='${esc(userId)}' AND deleted_at IS NULL`,
        );
        if (!rows[0]) return json(req, { message: "Track not found." }, 404);
        const bytes = Buffer.from(String(rows[0].audio_b64 || ""), "base64");
        return new Response(bytes, {
          status: 200,
          headers: {
            ...cors(req),
            "content-type": rows[0].mime || "audio/wav",
            "content-length": String(bytes.length),
            "content-disposition": `attachment; filename="${String(rows[0].filename || "track.wav").replace(/"/g, "")}"`,
          },
        });
      }

      const musicIdMatch = path.match(/^\/v1\/music\/([^/]+)$/);
      if (musicIdMatch) {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(musicIdMatch[1]);
        if (req.method === "DELETE") {
          const existing = await query(
            `SELECT id FROM music_tracks WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}' AND deleted_at IS NULL`,
          );
          if (!existing[0]) return json(req, { message: "Track not found." }, 404);
          const now = new Date().toISOString();
          await query(`UPDATE music_tracks SET deleted_at='${now}' WHERE id='${esc(id)}'`);
          return new Response(null, { status: 204, headers: cors(req) });
        }
        if (req.method === "GET") {
          const rows = await query(
            `SELECT id, title, filename, mime, size_bytes, created_at FROM music_tracks WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}' AND deleted_at IS NULL`,
          );
          if (!rows[0]) return json(req, { message: "Track not found." }, 404);
          return json(req, serializeMusicMeta(rows[0]));
        }
      }

      // Device library pull (x-device-key + device_id); reports microSD for Companion limits
      if (req.method === "GET" && path === "/v1/device/music") {
        const deviceId = u.searchParams.get("device_id") || req.headers.get("x-device-id") || "";
        const userId = await userIdForDeviceKey(req, deviceId);
        if (!userId) return json(req, { message: "Sign in to continue." }, 401);
        const sdHeader = (req.headers.get("x-pocket-sd") || "").trim();
        const sdQuery = u.searchParams.get("sd") === "1";
        const sdPresent = sdHeader === "1" || sdQuery;
        const now = new Date().toISOString();
        await query(
          `UPDATE device_links SET last_seen_at='${now}', sd_present=${sdPresent ? "true" : "false"} WHERE device_id='${esc(deviceId)}'`,
        );
        const rows = await query(
          `SELECT id, title, filename, mime, size_bytes, created_at FROM music_tracks WHERE user_id='${esc(userId)}' AND deleted_at IS NULL ORDER BY created_at DESC`,
        );
        return json(req, { tracks: rows.map(serializeMusicMeta), sd_present: sdPresent });
      }

      if (req.method === "GET" && path === "/v1/music/limits") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const rows = await query(
          `SELECT COALESCE(BOOL_OR(sd_present), false) AS sd FROM device_links WHERE user_id='${esc(gate.user.id)}'`,
        );
        const sd = Boolean(rows[0]?.sd === true || rows[0]?.sd === "t" || rows[0]?.sd === "true");
        return json(req, {
          sd_present: sd,
          max_upload_bytes: sd ? MUSIC_MAX_SD_BYTES : MUSIC_MAX_INTERNAL_BYTES,
          internal_max_bytes: MUSIC_MAX_INTERNAL_BYTES,
          sd_max_bytes: MUSIC_MAX_SD_BYTES,
        });
      }

      // Device OTA discovery — points at firmware-latest app image (A/B OTA, not merged USB bin).
      if (req.method === "GET" && path === "/v1/firmware/latest") {
        const manifestUrl =
          "https://github.com/bighappysmiley/Pocket/releases/download/firmware-latest/firmware-manifest.json";
        try {
          const r = await fetch(manifestUrl, { headers: { Accept: "application/json" } });
          if (r.ok) {
            const body = await r.json();
            if (body && body.url) return json(req, { ok: true, ...body });
          }
        } catch (_) {
          /* fall through */
        }
        return json(req, {
          ok: true,
          build_id: "POCKET-LIVE-unknown",
          version: "firmware-latest",
          url: "https://github.com/bighappysmiley/Pocket/releases/download/firmware-latest/pocket.bin",
          size: 0,
          channel: "stable",
        });
      }

      // ---- Notes ----
      if (req.method === "GET" && path === "/v1/notes") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const includeDeleted = u.searchParams.get("include_deleted") === "1";
        const rows = await query(
          includeDeleted
            ? `SELECT * FROM cloud_notes WHERE user_id='${esc(gate.user.id)}' ORDER BY updated_at DESC`
            : `SELECT * FROM cloud_notes WHERE user_id='${esc(gate.user.id)}' AND deleted_at IS NULL ORDER BY updated_at DESC`,
        );
        return json(req, { notes: rows.map(serializeNote) });
      }

      if (req.method === "POST" && path === "/v1/notes") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const body = await req.json().catch(() => ({}));
        const now = new Date().toISOString();
        const id = typeof body.id === "string" && body.id ? body.id : newId();
        const title = typeof body.title === "string" ? body.title : "";
        const noteBody = typeof body.body === "string" ? body.body : "";
        const updated_at = typeof body.updated_at === "string" ? body.updated_at : now;
        const updated_by =
          typeof body.updated_by_device_id === "string" && body.updated_by_device_id
            ? body.updated_by_device_id
            : "pwa";
        const created_at = typeof body.created_at === "string" ? body.created_at : now;
        const deleted_at =
          body.deleted_at === null
            ? null
            : typeof body.deleted_at === "string"
              ? body.deleted_at
              : null;
        const existing = await query(`SELECT * FROM cloud_notes WHERE id='${esc(id)}'`);
        if (existing[0]) {
          if (existing[0].user_id !== gate.user.id) return json(req, { message: "Forbidden." }, 403);
          await query(
            `UPDATE cloud_notes SET title=${sqlStr(title)}, body=${sqlStr(noteBody)}, updated_at=${sqlStr(updated_at)}, updated_by_device_id=${sqlStr(updated_by)}, deleted_at=${sqlNullable(deleted_at)}, version=version+1 WHERE id='${esc(id)}'`,
          );
        } else {
          await query(
            `INSERT INTO cloud_notes (id,user_id,title,body,created_at,updated_at,updated_by_device_id,deleted_at,version) VALUES ('${esc(id)}','${esc(gate.user.id)}',${sqlStr(title)},${sqlStr(noteBody)},${sqlStr(created_at)},${sqlStr(updated_at)},${sqlStr(updated_by)},${sqlNullable(deleted_at)},1)`,
          );
        }
        const rows = await query(`SELECT * FROM cloud_notes WHERE id='${esc(id)}'`);
        return json(req, { note: serializeNote(rows[0]) }, existing[0] ? 200 : 201);
      }

      const noteMatch = path.match(/^\/v1\/notes\/([^/]+)$/);
      if (noteMatch) {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(noteMatch[1]);
        if (req.method === "GET") {
          const rows = await query(
            `SELECT * FROM cloud_notes WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}'`,
          );
          if (!rows[0]) return json(req, { message: "Note not found." }, 404);
          return json(req, serializeNote(rows[0]));
        }
        if (req.method === "PATCH") {
          const body = await req.json().catch(() => ({}));
          const existing = await query(
            `SELECT * FROM cloud_notes WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}'`,
          );
          if (!existing[0]) return json(req, { message: "Note not found." }, 404);
          const title = typeof body.title === "string" ? body.title : existing[0].title;
          const noteBody = typeof body.body === "string" ? body.body : existing[0].body;
          const updated_at = typeof body.updated_at === "string" ? body.updated_at : new Date().toISOString();
          const updated_by =
            typeof body.updated_by_device_id === "string" && body.updated_by_device_id
              ? body.updated_by_device_id
              : "pwa";
          await query(
            `UPDATE cloud_notes SET title=${sqlStr(title)}, body=${sqlStr(noteBody)}, updated_at=${sqlStr(updated_at)}, updated_by_device_id=${sqlStr(updated_by)}, version=version+1 WHERE id='${esc(id)}'`,
          );
          const rows = await query(`SELECT * FROM cloud_notes WHERE id='${esc(id)}'`);
          return json(req, serializeNote(rows[0]));
        }
        if (req.method === "DELETE") {
          const body = await req.json().catch(() => ({}));
          const existing = await query(
            `SELECT * FROM cloud_notes WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}'`,
          );
          if (!existing[0]) return json(req, { message: "Note not found." }, 404);
          const updated_at = typeof body.updated_at === "string" ? body.updated_at : new Date().toISOString();
          const updated_by =
            typeof body.updated_by_device_id === "string" && body.updated_by_device_id
              ? body.updated_by_device_id
              : "pwa";
          await query(
            `UPDATE cloud_notes SET deleted_at=${sqlStr(updated_at)}, updated_at=${sqlStr(updated_at)}, updated_by_device_id=${sqlStr(updated_by)}, version=version+1 WHERE id='${esc(id)}'`,
          );
          return new Response(null, { status: 204, headers: cors(req) });
        }
      }

      // ---- Lists ----
      if (req.method === "GET" && path === "/v1/lists") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const includeDeleted = u.searchParams.get("include_deleted") === "1";
        const rows = await query(
          includeDeleted
            ? `SELECT * FROM cloud_lists WHERE user_id='${esc(gate.user.id)}' ORDER BY updated_at DESC`
            : `SELECT * FROM cloud_lists WHERE user_id='${esc(gate.user.id)}' AND deleted_at IS NULL ORDER BY updated_at DESC`,
        );
        return json(req, { lists: rows.map(serializeList) });
      }

      if (req.method === "POST" && path === "/v1/lists") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const body = await req.json().catch(() => ({}));
        const now = new Date().toISOString();
        const id = typeof body.id === "string" && body.id ? body.id : newId();
        const title = typeof body.title === "string" ? body.title : "";
        const itemsJson = JSON.stringify(Array.isArray(body.items) ? body.items : []);
        const updated_at = typeof body.updated_at === "string" ? body.updated_at : now;
        const updated_by =
          typeof body.updated_by_device_id === "string" && body.updated_by_device_id
            ? body.updated_by_device_id
            : "pwa";
        const created_at = typeof body.created_at === "string" ? body.created_at : now;
        const existing = await query(`SELECT * FROM cloud_lists WHERE id='${esc(id)}'`);
        if (existing[0]) {
          if (existing[0].user_id !== gate.user.id) return json(req, { message: "Forbidden." }, 403);
          await query(
            `UPDATE cloud_lists SET title=${sqlStr(title)}, items_json=${sqlStr(itemsJson)}, updated_at=${sqlStr(updated_at)}, updated_by_device_id=${sqlStr(updated_by)}, version=version+1 WHERE id='${esc(id)}'`,
          );
        } else {
          await query(
            `INSERT INTO cloud_lists (id,user_id,title,items_json,created_at,updated_at,updated_by_device_id,deleted_at,version) VALUES ('${esc(id)}','${esc(gate.user.id)}',${sqlStr(title)},${sqlStr(itemsJson)},${sqlStr(created_at)},${sqlStr(updated_at)},${sqlStr(updated_by)},NULL,1)`,
          );
        }
        const rows = await query(`SELECT * FROM cloud_lists WHERE id='${esc(id)}'`);
        return json(req, { list: serializeList(rows[0]) }, existing[0] ? 200 : 201);
      }

      const listMatch = path.match(/^\/v1\/lists\/([^/]+)$/);
      if (listMatch) {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(listMatch[1]);
        if (req.method === "GET") {
          const rows = await query(
            `SELECT * FROM cloud_lists WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}'`,
          );
          if (!rows[0]) return json(req, { message: "List not found." }, 404);
          return json(req, serializeList(rows[0]));
        }
        if (req.method === "PATCH") {
          const body = await req.json().catch(() => ({}));
          const existing = await query(
            `SELECT * FROM cloud_lists WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}'`,
          );
          if (!existing[0]) return json(req, { message: "List not found." }, 404);
          const title = typeof body.title === "string" ? body.title : existing[0].title;
          const itemsJson = Array.isArray(body.items)
            ? JSON.stringify(body.items)
            : existing[0].items_json;
          const updated_at = typeof body.updated_at === "string" ? body.updated_at : new Date().toISOString();
          const updated_by =
            typeof body.updated_by_device_id === "string" && body.updated_by_device_id
              ? body.updated_by_device_id
              : "pwa";
          await query(
            `UPDATE cloud_lists SET title=${sqlStr(title)}, items_json=${sqlStr(itemsJson)}, updated_at=${sqlStr(updated_at)}, updated_by_device_id=${sqlStr(updated_by)}, version=version+1 WHERE id='${esc(id)}'`,
          );
          const rows = await query(`SELECT * FROM cloud_lists WHERE id='${esc(id)}'`);
          return json(req, serializeList(rows[0]));
        }
        if (req.method === "DELETE") {
          const body = await req.json().catch(() => ({}));
          const existing = await query(
            `SELECT * FROM cloud_lists WHERE id='${esc(id)}' AND user_id='${esc(gate.user.id)}'`,
          );
          if (!existing[0]) return json(req, { message: "List not found." }, 404);
          const updated_at = typeof body.updated_at === "string" ? body.updated_at : new Date().toISOString();
          const updated_by =
            typeof body.updated_by_device_id === "string" && body.updated_by_device_id
              ? body.updated_by_device_id
              : "pwa";
          await query(
            `UPDATE cloud_lists SET deleted_at=${sqlStr(updated_at)}, updated_at=${sqlStr(updated_at)}, updated_by_device_id=${sqlStr(updated_by)}, version=version+1 WHERE id='${esc(id)}'`,
          );
          return new Response(null, { status: 204, headers: cors(req) });
        }
      }

      // ---- Connectors / backups (empty until wired) ----
      if (req.method === "GET" && path === "/v1/connectors") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const rows = await query(
          `SELECT provider, status, connected_at FROM connector_accounts WHERE user_id='${esc(gate.user.id)}'`,
        );
        const byProv = Object.fromEntries(rows.map((r) => [r.provider, r]));
        const connectors = ["drive", "dropbox", "onedrive"].map((provider) => ({
          provider,
          status: byProv[provider]?.status === "connected" ? "connected" : "disconnected",
          connected_at: byProv[provider]?.connected_at || null,
        }));
        return json(req, { connectors });
      }

      if (req.method === "GET" && path === "/v1/backups") {
        const gate = await requireEntitledUser(req);
        if (gate.error) return gate.error;
        const rows = await query(
          `SELECT id, created_at, note_count, list_count, size_bytes FROM backups WHERE user_id='${esc(gate.user.id)}' ORDER BY created_at DESC`,
        );
        return json(req, { backups: rows });
      }

      // ---- Billing (Stripe via Admin-configured keys or env) ----
      if (req.method === "POST" && path === "/v1/billing/checkout") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        if (user.disabled_at) return json(req, { message: "This account is disabled." }, 403);
        const cfg = await loadStripeConfig();
        const withTrial = !Number(user.trial_consumed);
        if (cfg.mockMode) {
          const id = `mock_cs_${newId().replace(/-/g, "").slice(0, 24)}`;
          const now = new Date().toISOString();
          await query(
            `INSERT INTO mock_checkouts (id,user_id,with_trial,created_at,completed_at) VALUES ('${esc(id)}','${esc(user.id)}',${withTrial ? 1 : 0},'${now}',NULL)`,
          );
          await completeMockCheckout(id);
          return json(req, { url: `${PWA_ORIGIN}/billing/success?session_id=${id}` });
        }
        if (!cfg.priceMonthlyId) {
          return json(req, { message: "Billing isn't configured yet. Try again later." }, 400);
        }
        let customerId = user.stripe_customer_id;
        if (!customerId) {
          const customer = await stripeFetch(cfg.secretKey, "POST", "/customers", {
            email: user.email,
            "metadata[pocket_user_id]": user.id,
          });
          customerId = customer.id;
          await query(`UPDATE users SET stripe_customer_id='${esc(customerId)}' WHERE id='${esc(user.id)}'`);
        }
        const params = {
          mode: "subscription",
          customer: customerId,
          client_reference_id: user.id,
          success_url: `${PWA_ORIGIN}/billing/success?session_id={CHECKOUT_SESSION_ID}`,
          cancel_url: `${PWA_ORIGIN}/billing/cancel`,
          "line_items[0][price]": cfg.priceMonthlyId,
          "line_items[0][quantity]": "1",
          allow_promotion_codes: "false",
        };
        if (withTrial) params["subscription_data[trial_period_days]"] = "7";
        const session = await stripeFetch(cfg.secretKey, "POST", "/checkout/sessions", params);
        if (!session.url) return json(req, { message: "Couldn't start checkout. Try again." }, 400);
        return json(req, { url: session.url });
      }

      if (req.method === "POST" && path === "/v1/billing/portal") {
        const user = await userFromSession(req);
        if (!user) return json(req, { message: "Sign in to continue." }, 401);
        const cfg = await loadStripeConfig();
        if (cfg.mockMode) {
          if (!user.stripe_customer_id && !Number(user.had_subscription) && !Number(user.trial_consumed)) {
            return json(req, { message: "No billing account yet. Start a subscription first." }, 400);
          }
          return json(req, { url: `${PWA_ORIGIN}/billing?portal=mock` });
        }
        if (!user.stripe_customer_id) {
          return json(req, { message: "No billing account yet. Start a subscription first." }, 400);
        }
        const session = await stripeFetch(cfg.secretKey, "POST", "/billing_portal/sessions", {
          customer: user.stripe_customer_id,
          return_url: `${PWA_ORIGIN}/billing`,
        });
        return json(req, { url: session.url });
      }

      if (req.method === "POST" && path === "/v1/billing/webhook") {
        const cfg = await loadStripeConfig();
        const raw = await req.text();
        if (cfg.mockMode) {
          return json(req, { received: true, mock: true });
        }
        const sig = req.headers.get("stripe-signature") || "";
        if (!verifyStripeWebhookSignature(raw, sig, cfg.webhookSecret)) {
          return json(req, { message: "Invalid webhook signature." }, 400);
        }
        let event;
        try {
          event = JSON.parse(raw);
        } catch {
          return json(req, { message: "Invalid payload." }, 400);
        }
        await handleStripeWebhookEvent(event);
        return json(req, { received: true });
      }

      // ---- Admin ----
      if (req.method === "GET" && path === "/v1/admin/stripe") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const cfg = await loadStripeConfig();
        const updated = await query(
          `SELECT key, updated_at, updated_by FROM app_settings WHERE key LIKE 'stripe_%' ORDER BY key`,
        );
        return json(req, {
          configured: !cfg.mockMode,
          mock_mode: cfg.mockMode,
          source: cfg.source,
          secret_key_set: Boolean(cfg.secretKey),
          secret_key_masked: maskSecret(cfg.secretKey),
          webhook_secret_set: Boolean(cfg.webhookSecret),
          webhook_secret_masked: maskSecret(cfg.webhookSecret),
          price_monthly_id: cfg.priceMonthlyId || null,
          product_name: cfg.productName,
          webhook_url: `${PUBLIC_BASE_URL || u.origin}/v1/billing/webhook`,
          updated,
        });
      }

      if (req.method === "PUT" && path === "/v1/admin/stripe") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const body = await req.json().catch(() => ({}));
        const updates = [];
        if (typeof body.secret_key === "string" && body.secret_key.trim()) {
          const sk = body.secret_key.trim();
          if (!sk.startsWith("sk_")) {
            return json(req, { message: "Secret key should start with sk_test_ or sk_live_." }, 400);
          }
          await setAppSetting("stripe_secret_key", sk, gate.user.id);
          updates.push("secret_key");
        }
        if (typeof body.webhook_secret === "string" && body.webhook_secret.trim()) {
          const wh = body.webhook_secret.trim();
          if (!wh.startsWith("whsec_")) {
            return json(req, { message: "Webhook secret should start with whsec_." }, 400);
          }
          await setAppSetting("stripe_webhook_secret", wh, gate.user.id);
          updates.push("webhook_secret");
        }
        if (typeof body.price_monthly_id === "string") {
          const price = body.price_monthly_id.trim();
          if (price && !price.startsWith("price_")) {
            return json(req, { message: "Price id should start with price_." }, 400);
          }
          await setAppSetting("stripe_price_monthly_id", price, gate.user.id);
          updates.push("price_monthly_id");
        }
        if (typeof body.product_name === "string") {
          const name = body.product_name.trim() || "Pocket Cloud";
          if (/connect/i.test(name)) {
            return json(req, { message: 'Product name must not contain "Connect".' }, 400);
          }
          await setAppSetting("stripe_product_name", name, gate.user.id);
          updates.push("product_name");
        }
        if (body.clear === true) {
          for (const k of STRIPE_SETTING_KEYS) {
            await query(`DELETE FROM app_settings WHERE key='${esc(k)}'`);
          }
          updates.push("cleared");
        }
        if (!updates.length) {
          return json(req, { message: "Nothing to update. Paste a key, price id, or clear." }, 400);
        }
        await audit(gate.user.id, "stripe.configure", "stripe", null, { updates });
        const cfg = await loadStripeConfig();
        return json(req, {
          ok: true,
          updates,
          configured: !cfg.mockMode,
          mock_mode: cfg.mockMode,
          source: cfg.source,
          secret_key_masked: maskSecret(cfg.secretKey),
          webhook_secret_masked: maskSecret(cfg.webhookSecret),
          price_monthly_id: cfg.priceMonthlyId || null,
          product_name: cfg.productName,
        });
      }

      if (req.method === "GET" && path === "/v1/admin/overview") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const users = await query(`SELECT COUNT(*)::text AS c FROM users`);
        const devices = await query(`SELECT COUNT(*)::text AS c FROM device_links`);
        const pending = await query(`SELECT COUNT(*)::text AS c FROM pair_sessions WHERE status='pending' AND expires_at > NOW()`);
        const claimed = await query(`SELECT COUNT(*)::text AS c FROM pair_sessions WHERE status='claimed'`);
        const entitled = await query(`SELECT COUNT(*)::text AS c FROM subscription_mirrors WHERE status IN ('active','trialing')`);
        const badges = await query(`SELECT COUNT(*)::text AS c FROM badges`);
        const awards = await query(`SELECT COUNT(*)::text AS c FROM badge_awards`);
        return json(req, {
          users: Number(users[0]?.c || 0),
          devices: Number(devices[0]?.c || 0),
          pair_pending: Number(pending[0]?.c || 0),
          pair_claimed: Number(claimed[0]?.c || 0),
          subscriptions_active: Number(entitled[0]?.c || 0),
          badges: Number(badges[0]?.c || 0),
          badge_awards: Number(awards[0]?.c || 0),
        });
      }

      if (req.method === "GET" && path === "/v1/admin/users") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const q = (u.searchParams.get("q") || "").trim().toLowerCase();
        const limit = Math.min(100, Math.max(1, Number(u.searchParams.get("limit") || 50)));
        let sql = `SELECT u.id, u.email, u.role, u.email_verified_at, u.disabled_at, u.created_at, u.last_login_at, u.trial_consumed, u.had_subscription,
          sm.status AS sub_status, sm.trial_ends_at, sm.current_period_end,
          (SELECT COUNT(*)::text FROM device_links d WHERE d.user_id=u.id) AS device_count
          FROM users u
          LEFT JOIN subscription_mirrors sm ON sm.user_id=u.id`;
        if (q) sql += ` WHERE u.email LIKE '%${esc(q)}%' OR u.id='${esc(q)}'`;
        sql += ` ORDER BY u.created_at DESC LIMIT ${limit}`;
        const rows = await query(sql);
        return json(req, {
          users: rows.map((r) => ({
            id: r.id,
            email: r.email,
            role: r.role || "user",
            email_verified: Boolean(r.email_verified_at),
            disabled: Boolean(r.disabled_at),
            created_at: r.created_at,
            last_login_at: r.last_login_at || null,
            trial_consumed: Boolean(Number(r.trial_consumed)),
            had_subscription: Boolean(Number(r.had_subscription)),
            entitlement_status: r.sub_status || "free",
            trial_ends_at: r.trial_ends_at || null,
            current_period_end: r.current_period_end || null,
            device_count: Number(r.device_count || 0),
          })),
        });
      }

      const userMatch = path.match(/^\/v1\/admin\/users\/([^/]+)$/);
      if (req.method === "GET" && userMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(userMatch[1]);
        const rows = await query(`SELECT * FROM users WHERE id='${esc(id)}'`);
        const user = rows[0];
        if (!user) return json(req, { message: "User not found." }, 404);
        const devices = await query(`SELECT id, device_id, device_name, linked_at, last_seen_at FROM device_links WHERE user_id='${esc(id)}' ORDER BY linked_at DESC`);
        const subs = await query(`SELECT * FROM subscription_mirrors WHERE user_id='${esc(id)}'`);
        const awards = await query(`SELECT a.id, a.badge_id, a.note, a.awarded_at, b.key, b.name, b.icon_key FROM badge_awards a JOIN badges b ON b.id=a.badge_id WHERE a.user_id='${esc(id)}' ORDER BY a.awarded_at DESC`);
        const sub = subs[0];
        return json(req, {
          user: {
            id: user.id,
            email: user.email,
            role: user.role || "user",
            email_verified: Boolean(user.email_verified_at),
            disabled: Boolean(user.disabled_at),
            created_at: user.created_at,
            last_login_at: user.last_login_at || null,
            trial_consumed: Boolean(Number(user.trial_consumed)),
            had_subscription: Boolean(Number(user.had_subscription)),
            stripe_customer_id: user.stripe_customer_id || null,
          },
          entitlement: {
            status: sub?.status || "free",
            entitled: sub?.status === "active" || sub?.status === "trialing",
            trial_ends_at: sub?.trial_ends_at || null,
            current_period_end: sub?.current_period_end || null,
          },
          devices,
          badges: awards,
        });
      }

      if (req.method === "PATCH" && userMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(userMatch[1]);
        const rows = await query(`SELECT * FROM users WHERE id='${esc(id)}'`);
        const user = rows[0];
        if (!user) return json(req, { message: "User not found." }, 404);
        const body = await req.json().catch(() => ({}));
        const now = new Date().toISOString();
        if (body.verify_email === true) {
          await query(`UPDATE users SET email_verified_at=COALESCE(email_verified_at,'${now}') WHERE id='${esc(id)}'`);
          await audit(gate.user.id, "verify_email", "user", id, null);
        }
        if (body.disabled === true) {
          await query(`UPDATE users SET disabled_at='${now}' WHERE id='${esc(id)}'`);
          await query(`DELETE FROM sessions WHERE user_id='${esc(id)}'`);
          await audit(gate.user.id, "disable_user", "user", id, null);
        } else if (body.disabled === false) {
          await query(`UPDATE users SET disabled_at=NULL WHERE id='${esc(id)}'`);
          await audit(gate.user.id, "enable_user", "user", id, null);
        }
        if (body.role === "admin" || body.role === "user") {
          if (id === gate.user.id && body.role !== "admin") {
            return json(req, { message: "You can't remove your own admin role." }, 400);
          }
          await query(`UPDATE users SET role='${esc(body.role)}' WHERE id='${esc(id)}'`);
          await audit(gate.user.id, "set_role", "user", id, { role: body.role });
        }
        if (typeof body.entitlement_status === "string") {
          const st = body.entitlement_status;
          if (!["free", "trialing", "active", "past_due", "canceled", "lapsed"].includes(st)) {
            return json(req, { message: "Invalid entitlement status." }, 400);
          }
          if (st === "free" || st === "lapsed") {
            await query(`DELETE FROM subscription_mirrors WHERE user_id='${esc(id)}'`);
            if (st === "lapsed") await query(`UPDATE users SET had_subscription=1 WHERE id='${esc(id)}'`);
          } else {
            const existing = await query(`SELECT user_id FROM subscription_mirrors WHERE user_id='${esc(id)}'`);
            const trialEnd = st === "trialing" ? new Date(Date.now() + 7 * 86400e3).toISOString() : null;
            // active = permanent/comped grant (no Stripe period end). Trial keeps 7-day end.
            if (existing[0]) {
              await query(
                `UPDATE subscription_mirrors SET status='${esc(st)}', trial_ends_at=${sqlNullable(trialEnd)}, current_period_end=${st === "active" ? "NULL" : "current_period_end"}, updated_at='${now}' WHERE user_id='${esc(id)}'`,
              );
            } else {
              await query(`INSERT INTO subscription_mirrors (user_id,stripe_subscription_id,status,trial_ends_at,current_period_end,cancel_at_period_end,updated_at) VALUES ('${esc(id)}',NULL,'${esc(st)}',${sqlNullable(trialEnd)},NULL,0,'${now}')`);
            }
            if (st === "active" || st === "trialing") {
              await query(`UPDATE users SET had_subscription=1${st === "trialing" ? ", trial_consumed=1" : ""} WHERE id='${esc(id)}'`);
            }
          }
          await audit(gate.user.id, "set_entitlement", "user", id, { status: st });
        }
        const fresh = await query(`SELECT * FROM users WHERE id='${esc(id)}'`);
        const f = fresh[0];
        const subs = await query(`SELECT status, trial_ends_at, current_period_end FROM subscription_mirrors WHERE user_id='${esc(id)}'`);
        const sub = subs[0];
        return json(req, {
          user: {
            id: f.id,
            email: f.email,
            role: f.role || "user",
            email_verified: Boolean(f.email_verified_at),
            disabled: Boolean(f.disabled_at),
            created_at: f.created_at,
            last_login_at: f.last_login_at || null,
            trial_consumed: Boolean(Number(f.trial_consumed)),
          },
          entitlement: {
            status: sub?.status || "free",
            entitled: sub?.status === "active" || sub?.status === "trialing",
            trial_ends_at: sub?.trial_ends_at || null,
            current_period_end: sub?.current_period_end || null,
          },
        });
      }

      if (req.method === "GET" && path === "/v1/admin/devices") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const userId = (u.searchParams.get("user_id") || "").trim();
        let sql = `SELECT d.id, d.device_id, d.device_name, d.linked_at, d.last_seen_at, d.user_id, u.email AS user_email
          FROM device_links d JOIN users u ON u.id=d.user_id`;
        if (userId) sql += ` WHERE d.user_id='${esc(userId)}'`;
        sql += ` ORDER BY d.linked_at DESC LIMIT 200`;
        const devices = await query(sql);
        return json(req, { devices });
      }

      const deviceMatch = path.match(/^\/v1\/admin\/devices\/([^/]+)$/);
      if (req.method === "PATCH" && deviceMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(deviceMatch[1]);
        const body = await req.json().catch(() => ({}));
        const name = typeof body.device_name === "string" ? body.device_name.trim() : "";
        if (!name || name.length > 64) return json(req, { message: "Enter a device label (1–64 characters)." }, 400);
        const rows = await query(`SELECT id FROM device_links WHERE id='${esc(id)}'`);
        if (!rows[0]) return json(req, { message: "Device not found." }, 404);
        await query(`UPDATE device_links SET device_name='${esc(name)}' WHERE id='${esc(id)}'`);
        await audit(gate.user.id, "rename_device", "device", id, { device_name: name });
        const devices = await query(`SELECT d.id, d.device_id, d.device_name, d.linked_at, d.last_seen_at, d.user_id, u.email AS user_email FROM device_links d JOIN users u ON u.id=d.user_id WHERE d.id='${esc(id)}'`);
        return json(req, { device: devices[0] });
      }

      if (req.method === "DELETE" && deviceMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(deviceMatch[1]);
        const rows = await query(`SELECT id, device_id, user_id FROM device_links WHERE id='${esc(id)}'`);
        if (!rows[0]) return json(req, { message: "Device not found." }, 404);
        await query(`DELETE FROM badge_awards WHERE device_link_id='${esc(id)}'`);
        await query(`DELETE FROM device_links WHERE id='${esc(id)}'`);
        await audit(gate.user.id, "unlink_device", "device", id, { device_id: rows[0].device_id, user_id: rows[0].user_id });
        return json(req, { ok: true });
      }

      if (req.method === "GET" && path === "/v1/admin/pair-sessions") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const status = (u.searchParams.get("status") || "").trim();
        let sql = `SELECT id, device_id, code_public_hint, status, created_at, expires_at, claimed_at, claimed_by_user_id FROM pair_sessions`;
        if (status === "pending") sql += ` WHERE status='pending'`;
        else if (status === "claimed") sql += ` WHERE status='claimed'`;
        else if (status === "expired") sql += ` WHERE status='expired' OR (status='pending' AND expires_at < NOW())`;
        sql += ` ORDER BY created_at DESC LIMIT 100`;
        const sessions = await query(sql);
        return json(req, { sessions });
      }

      if (req.method === "GET" && path === "/v1/admin/badges") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const badges = await query(`SELECT b.*, (SELECT COUNT(*)::text FROM badge_awards a WHERE a.badge_id=b.id) AS award_count FROM badges b ORDER BY b.name ASC`);
        const awards = await query(`SELECT a.id, a.badge_id, a.user_id, a.device_link_id, a.note, a.awarded_at, a.awarded_by, b.name AS badge_name, b.key AS badge_key, b.icon_key, u.email AS user_email
          FROM badge_awards a
          JOIN badges b ON b.id=a.badge_id
          LEFT JOIN users u ON u.id=a.user_id
          ORDER BY a.awarded_at DESC LIMIT 100`);
        return json(req, {
          badges: badges.map((b) => ({ ...b, award_count: Number(b.award_count || 0) })),
          awards,
        });
      }

      if (req.method === "POST" && path === "/v1/admin/badges") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const body = await req.json().catch(() => ({}));
        const name = typeof body.name === "string" ? body.name.trim() : "";
        const key = typeof body.key === "string" ? body.key.trim().toLowerCase().replace(/[^a-z0-9_]+/g, "_") : "";
        const icon_key = typeof body.icon_key === "string" && body.icon_key.trim() ? body.icon_key.trim() : "star";
        const description = typeof body.description === "string" ? body.description.trim() : "";
        if (!name || !key) return json(req, { message: "Name and key are required." }, 400);
        const exists = await query(`SELECT id FROM badges WHERE key='${esc(key)}'`);
        if (exists[0]) return json(req, { message: "A badge with that key already exists." }, 400);
        const id = newId(), now = new Date().toISOString();
        await query(`INSERT INTO badges (id,key,name,icon_key,description,created_at,updated_at) VALUES ('${esc(id)}','${esc(key)}','${esc(name)}','${esc(icon_key)}','${esc(description)}','${now}','${now}')`);
        await audit(gate.user.id, "create_badge", "badge", id, { key, name });
        return json(req, { badge: { id, key, name, icon_key, description, created_at: now, updated_at: now } }, 201);
      }

      const badgeMatch = path.match(/^\/v1\/admin\/badges\/([^/]+)$/);
      if (req.method === "PATCH" && badgeMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(badgeMatch[1]);
        const rows = await query(`SELECT * FROM badges WHERE id='${esc(id)}'`);
        if (!rows[0]) return json(req, { message: "Badge not found." }, 404);
        const body = await req.json().catch(() => ({}));
        const name = typeof body.name === "string" ? body.name.trim() : rows[0].name;
        const icon_key = typeof body.icon_key === "string" ? body.icon_key.trim() : rows[0].icon_key;
        const description = typeof body.description === "string" ? body.description.trim() : rows[0].description;
        const now = new Date().toISOString();
        await query(`UPDATE badges SET name='${esc(name)}', icon_key='${esc(icon_key)}', description='${esc(description)}', updated_at='${now}' WHERE id='${esc(id)}'`);
        await audit(gate.user.id, "edit_badge", "badge", id, { name });
        const fresh = await query(`SELECT * FROM badges WHERE id='${esc(id)}'`);
        return json(req, { badge: fresh[0] });
      }

      const awardMatch = path.match(/^\/v1\/admin\/badges\/([^/]+)\/award$/);
      if (req.method === "POST" && awardMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const badgeId = decodeURIComponent(awardMatch[1]);
        const badges = await query(`SELECT id FROM badges WHERE id='${esc(badgeId)}'`);
        if (!badges[0]) return json(req, { message: "Badge not found." }, 404);
        const body = await req.json().catch(() => ({}));
        const userId = typeof body.user_id === "string" ? body.user_id.trim() : "";
        const deviceLinkId = typeof body.device_link_id === "string" ? body.device_link_id.trim() : "";
        const note = typeof body.note === "string" ? body.note.trim() : "";
        if (!userId && !deviceLinkId) return json(req, { message: "Choose a user or device to award." }, 400);
        if (userId) {
          const urows = await query(`SELECT id FROM users WHERE id='${esc(userId)}'`);
          if (!urows[0]) return json(req, { message: "User not found." }, 404);
        }
        if (deviceLinkId) {
          const drows = await query(`SELECT id FROM device_links WHERE id='${esc(deviceLinkId)}'`);
          if (!drows[0]) return json(req, { message: "Device not found." }, 404);
        }
        const id = newId(), now = new Date().toISOString();
        await query(`INSERT INTO badge_awards (id,badge_id,user_id,device_link_id,awarded_by,note,awarded_at) VALUES ('${esc(id)}','${esc(badgeId)}',${sqlNullable(userId)},${sqlNullable(deviceLinkId)},'${esc(gate.user.id)}',${sqlNullable(note)},'${now}')`);
        await audit(gate.user.id, "award_badge", "badge_award", id, { badge_id: badgeId, user_id: userId || null, device_link_id: deviceLinkId || null });
        return json(req, { award: { id, badge_id: badgeId, user_id: userId || null, device_link_id: deviceLinkId || null, note: note || null, awarded_at: now } }, 201);
      }

      const awardIdMatch = path.match(/^\/v1\/admin\/badge-awards\/([^/]+)$/);
      if (req.method === "DELETE" && awardIdMatch) {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const id = decodeURIComponent(awardIdMatch[1]);
        const rows = await query(`SELECT id FROM badge_awards WHERE id='${esc(id)}'`);
        if (!rows[0]) return json(req, { message: "Award not found." }, 404);
        await query(`DELETE FROM badge_awards WHERE id='${esc(id)}'`);
        await audit(gate.user.id, "revoke_badge", "badge_award", id, null);
        return json(req, { ok: true });
      }

      if (req.method === "GET" && path === "/v1/admin/activity") {
        const gate = await requireAdmin(req);
        if (gate.error) return gate.error;
        const logins = await query(`SELECT id, email, last_login_at FROM users WHERE last_login_at IS NOT NULL ORDER BY last_login_at DESC LIMIT 25`);
        const claims = await query(`SELECT p.id, p.device_id, p.claimed_at, p.claimed_by_user_id, u.email AS user_email
          FROM pair_sessions p LEFT JOIN users u ON u.id=p.claimed_by_user_id
          WHERE p.status='claimed' ORDER BY p.claimed_at DESC NULLS LAST LIMIT 25`);
        const audits = await query(`SELECT a.*, u.email AS actor_email FROM admin_audit_log a LEFT JOIN users u ON u.id=a.actor_user_id ORDER BY a.created_at DESC LIMIT 40`);
        return json(req, { logins, pair_claims: claims, audits });
      }


      return json(req, { error: "not_found", path }, 404);
    } catch (e) {
      try { if (sock) sock.destroy(); } catch {}
      sock = null;
      return json(req, { error: "internal", message: String(e.message || e) }, 500);
    }
  },
};