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
    h["access-control-allow-headers"] = "content-type,authorization,x-device-key";
    h["access-control-allow-methods"] = "GET,POST,PATCH,DELETE,OPTIONS";
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
  const token = getCookie(req, SESSION_COOKIE);
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

async function requireAdmin(req) {
  const user = await userFromSession(req);
  if (!user) return { error: json(req, { message: "Sign in to continue." }, 401) };
  if (user.disabled_at) return { error: json(req, { message: "This account is disabled." }, 403) };
  if (!isAdminUser(user)) return { error: json(req, { message: "Admin access required." }, 403) };
  return { user };
}

async function audit(actorId, action, targetType, targetId, detail) {
  const now = new Date().toISOString();
  const d = detail == null ? "NULL" : `'${esc(typeof detail === "string" ? detail : JSON.stringify(detail))}'`;
  await query(
    `INSERT INTO admin_audit_log (id,actor_user_id,action,target_type,target_id,detail,created_at) VALUES ('${esc(newId())}','${esc(actorId || "")}','${esc(action)}','${esc(targetType || "")}','${esc(targetId || "")}',${d},'${now}')`,
  );
}

async function ensureAdminSchema() {
  await query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT NOT NULL DEFAULT 'user'`);
  await query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS disabled_at TIMESTAMPTZ`);
  await query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS last_login_at TIMESTAMPTZ`);
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
        return json(req, { ok: true, build: "scram-api-v5-admin" });
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
        await query(`INSERT INTO users (id,email,password_hash,email_verified_at,created_at,trial_consumed,stripe_customer_id,had_subscription) VALUES ('${esc(id)}','${esc(email)}','${esc(ph)}','${now}','${now}',0,NULL,0)`);
        const verifyToken = randomToken(32);
        await query(`INSERT INTO email_verifications (id,user_id,token_hash,created_at,expires_at,consumed_at) VALUES ('${esc(newId())}','${esc(id)}','${esc(sha256Hex(verifyToken))}','${now}','${new Date(Date.now() + 15 * 60e3).toISOString()}',NULL)`);
        const base = PUBLIC_BASE_URL || u.origin;
        console.log(`[verify-email] ${email} -> ${base}/v1/auth/verify-email?token=${encodeURIComponent(verifyToken)}`);
        return json(req, { ok: true, message: "Account created. You can sign in now." });
      }

      if (req.method === "GET" && path === "/v1/auth/verify-email") {
        const token = u.searchParams.get("token");
        if (!token) return json(req, { message: "This verification link is invalid." }, 400);
        const rows = await query(`SELECT * FROM email_verifications WHERE token_hash='${esc(sha256Hex(token))}'`);
        const row = rows[0];
        if (!row || row.consumed_at) return Response.redirect(`${PWA_ORIGIN}/login?verified=0`, 302);
        if (Date.parse(row.expires_at) < Date.now()) return Response.redirect(`${PWA_ORIGIN}/login?verified=0`, 302);
        const now = new Date().toISOString();
        await query(`UPDATE email_verifications SET consumed_at='${now}' WHERE id='${esc(row.id)}'`);
        await query(`UPDATE users SET email_verified_at='${now}' WHERE id='${esc(row.user_id)}' AND email_verified_at IS NULL`);
        return Response.redirect(`${PWA_ORIGIN}/login?verified=1`, 302);
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
        if (!user.email_verified_at) {
          return json(req, { message: "Verify your email before signing in. Check your inbox for the link." }, 400);
        }
        if (user.disabled_at) {
          return json(req, { message: "This account is disabled." }, 403);
        }
        const token = randomToken(32);
        const nowIso = new Date().toISOString();
        const expires = new Date(Date.now() + 30 * 86400e3).toISOString();
        await query(`INSERT INTO sessions (id,user_id,token_hash,created_at,expires_at) VALUES ('${esc(newId())}','${esc(user.id)}','${esc(sha256Hex(token))}','${nowIso}','${expires}')`);
        await query(`UPDATE users SET last_login_at='${nowIso}' WHERE id='${esc(user.id)}'`);
        return json(req, { ok: true, user: { id: user.id, email: user.email, role: user.role || "user", is_admin: isAdminUser(user) } }, 200, {
          "set-cookie": sessionCookie(token, 30 * 86400),
        });
      }

      if (req.method === "POST" && path === "/v1/auth/logout") {
        const token = getCookie(req, SESSION_COOKIE);
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


      // ---- Admin ----
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
            if (existing[0]) {
              await query(`UPDATE subscription_mirrors SET status='${esc(st)}', trial_ends_at=${trialEnd ? `'${trialEnd}'` : "NULL"}, updated_at='${now}' WHERE user_id='${esc(id)}'`);
            } else {
              await query(`INSERT INTO subscription_mirrors (user_id,stripe_subscription_id,status,trial_ends_at,current_period_end,cancel_at_period_end,updated_at) VALUES ('${esc(id)}',NULL,'${esc(st)}',${trialEnd ? `'${trialEnd}'` : "NULL"},NULL,0,'${now}')`);
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
        await query(`INSERT INTO badge_awards (id,badge_id,user_id,device_link_id,awarded_by,note,awarded_at) VALUES ('${esc(id)}','${esc(badgeId)}',${userId ? `'${esc(userId)}'` : "NULL"},${deviceLinkId ? `'${esc(deviceLinkId)}'` : "NULL"},'${esc(gate.user.id)}',${note ? `'${esc(note)}'` : "NULL"},'${now}')`);
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