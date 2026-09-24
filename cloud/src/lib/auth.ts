import { get, run, all } from "../db/index.js";
import { newId, sha256Hex, randomToken, safeEqual, hashPassword, verifyPassword } from "./crypto.js";
import { config } from "../config.js";
import { Errors } from "./errors.js";
import {
  mapSubscriptionToEntitlement,
  type EntitlementSnapshot,
} from "./entitlement.js";

export type UserRow = {
  id: string;
  email: string;
  password_hash: string | null;
  email_verified_at: string | null;
  created_at: string;
  trial_consumed: number;
  stripe_customer_id: string | null;
  had_subscription: number;
};

export type SessionRow = {
  id: string;
  user_id: string;
  token_hash: string;
  created_at: string;
  expires_at: string;
};

export function findUserByEmail(email: string): UserRow | undefined {
  return get<UserRow>("SELECT * FROM users WHERE email = ? COLLATE NOCASE", [
    email.trim().toLowerCase(),
  ]);
}

export function findUserById(id: string): UserRow | undefined {
  return get<UserRow>("SELECT * FROM users WHERE id = ?", [id]);
}

export function getOrCreateUser(email: string): UserRow {
  const normalized = email.trim().toLowerCase();
  const existing = findUserByEmail(normalized);
  if (existing) return existing;
  const id = newId();
  const now = new Date().toISOString();
  run(
    `INSERT INTO users (id, email, password_hash, email_verified_at, created_at, trial_consumed, stripe_customer_id, had_subscription)
     VALUES (?, ?, NULL, NULL, ?, 0, NULL, 0)`,
    [id, normalized, now],
  );
  return findUserById(id)!;
}

function assertValidEmail(email: string): string {
  const normalized = email.trim().toLowerCase();
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(normalized)) {
    throw Errors.badRequest("Enter a valid email address.");
  }
  return normalized;
}

function assertValidPassword(password: string): void {
  if (typeof password !== "string" || password.length < 8) {
    throw Errors.badRequest("Password must be at least 8 characters.");
  }
  if (password.length > 128) {
    throw Errors.badRequest("Password is too long.");
  }
}

/** Create account; returns verification token (caller emails the link). */
export function registerWithPassword(
  email: string,
  password: string,
): { user: UserRow; verifyToken: string; verifyUrl: string } {
  const normalized = assertValidEmail(email);
  assertValidPassword(password);
  if (findUserByEmail(normalized)) {
    throw Errors.badRequest("An account with that email already exists. Sign in instead.");
  }
  const id = newId();
  const now = new Date();
  run(
    `INSERT INTO users (id, email, password_hash, email_verified_at, created_at, trial_consumed, stripe_customer_id, had_subscription)
     VALUES (?, ?, ?, NULL, ?, 0, NULL, 0)`,
    [id, normalized, hashPassword(password), now.toISOString()],
  );
  const user = findUserById(id)!;
  const verifyToken = createEmailVerification(user.id);
  const verifyUrl = `${config.publicBaseUrl}/v1/auth/verify-email?token=${encodeURIComponent(verifyToken)}`;
  return { user, verifyToken, verifyUrl };
}

export function createEmailVerification(userId: string): string {
  const token = randomToken(32);
  const id = newId();
  const now = new Date();
  const expires = new Date(now.getTime() + config.magicLinkTtlMinutes * 60_000);
  run(
    `INSERT INTO email_verifications (id, user_id, token_hash, created_at, expires_at, consumed_at)
     VALUES (?, ?, ?, ?, ?, NULL)`,
    [id, userId, sha256Hex(token), now.toISOString(), expires.toISOString()],
  );
  return token;
}

export function verifyEmailToken(token: string): UserRow {
  const hash = sha256Hex(token);
  const row = get<{
    id: string;
    user_id: string;
    expires_at: string;
    consumed_at: string | null;
  }>("SELECT * FROM email_verifications WHERE token_hash = ?", [hash]);
  if (!row) throw Errors.badRequest("This verification link is invalid.");
  if (row.consumed_at) throw Errors.badRequest("This verification link was already used.");
  if (Date.parse(row.expires_at) < Date.now()) {
    throw Errors.badRequest("This verification link has expired.");
  }
  const now = new Date().toISOString();
  run("UPDATE email_verifications SET consumed_at = ? WHERE id = ?", [now, row.id]);
  run("UPDATE users SET email_verified_at = ? WHERE id = ? AND email_verified_at IS NULL", [
    now,
    row.user_id,
  ]);
  return findUserById(row.user_id)!;
}

export function loginWithPassword(email: string, password: string): UserRow {
  const normalized = assertValidEmail(email);
  assertValidPassword(password);
  const user = findUserByEmail(normalized);
  if (!user?.password_hash) {
    throw Errors.unauthorized("Wrong email or password.");
  }
  if (!verifyPassword(password, user.password_hash)) {
    throw Errors.unauthorized("Wrong email or password.");
  }
  if (!user.email_verified_at) {
    throw Errors.badRequest("Verify your email before signing in. Check your inbox for the link.");
  }
  return user;
}

export function createMagicLink(email: string): { token: string; expiresAt: string } {
  const normalized = email.trim().toLowerCase();
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(normalized)) {
    throw Errors.badRequest("Enter a valid email address.");
  }
  const token = randomToken(32);
  const id = newId();
  const now = new Date();
  const expires = new Date(now.getTime() + config.magicLinkTtlMinutes * 60_000);
  run(
    `INSERT INTO magic_links (id, email, token_hash, created_at, expires_at, consumed_at)
     VALUES (?, ?, ?, ?, ?, NULL)`,
    [id, normalized, sha256Hex(token), now.toISOString(), expires.toISOString()],
  );
  return { token, expiresAt: expires.toISOString() };
}

export function consumeMagicLink(token: string): UserRow {
  const hash = sha256Hex(token);
  const row = get<{
    id: string;
    email: string;
    expires_at: string;
    consumed_at: string | null;
  }>("SELECT * FROM magic_links WHERE token_hash = ?", [hash]);
  if (!row) throw Errors.badRequest("This sign-in link is invalid.");
  if (row.consumed_at) throw Errors.badRequest("This sign-in link was already used.");
  if (Date.parse(row.expires_at) < Date.now()) {
    throw Errors.badRequest("This sign-in link has expired.");
  }
  run("UPDATE magic_links SET consumed_at = ? WHERE id = ?", [
    new Date().toISOString(),
    row.id,
  ]);
  return getOrCreateUser(row.email);
}

export function createSession(userId: string): { token: string; expiresAt: string } {
  const token = randomToken(32);
  const id = newId();
  const now = new Date();
  const expires = new Date(now.getTime() + config.sessionTtlDays * 86_400_000);
  run(
    `INSERT INTO sessions (id, user_id, token_hash, created_at, expires_at)
     VALUES (?, ?, ?, ?, ?)`,
    [id, userId, sha256Hex(token), now.toISOString(), expires.toISOString()],
  );
  return { token, expiresAt: expires.toISOString() };
}

export function findSessionUser(token: string): UserRow | undefined {
  const hash = sha256Hex(token);
  const session = get<SessionRow>(
    "SELECT * FROM sessions WHERE token_hash = ?",
    [hash],
  );
  if (!session) return undefined;
  if (Date.parse(session.expires_at) < Date.now()) {
    run("DELETE FROM sessions WHERE id = ?", [session.id]);
    return undefined;
  }
  return findUserById(session.user_id);
}

export function destroySession(token: string): void {
  run("DELETE FROM sessions WHERE token_hash = ?", [sha256Hex(token)]);
}

export function getEntitlementForUser(user: UserRow): EntitlementSnapshot {
  const sub = get<{
    status: string;
    trial_ends_at: string | null;
    current_period_end: string | null;
    cancel_at_period_end: number;
  }>("SELECT * FROM subscription_mirrors WHERE user_id = ?", [user.id]);
  return mapSubscriptionToEntitlement(
    sub ?? null,
    Boolean(user.trial_consumed),
    Boolean(user.had_subscription),
  );
}

export function setStripeCustomerId(userId: string, customerId: string): void {
  run("UPDATE users SET stripe_customer_id = ? WHERE id = ?", [customerId, userId]);
}

export function markTrialConsumed(userId: string): void {
  run("UPDATE users SET trial_consumed = 1 WHERE id = ?", [userId]);
}

export function upsertSubscriptionMirror(
  userId: string,
  data: {
    stripe_subscription_id?: string | null;
    status: string;
    trial_ends_at?: string | null;
    current_period_end?: string | null;
    cancel_at_period_end?: boolean;
  },
): void {
  const now = new Date().toISOString();
  const existing = get("SELECT user_id FROM subscription_mirrors WHERE user_id = ?", [
    userId,
  ]);
  if (existing) {
    run(
      `UPDATE subscription_mirrors SET
        stripe_subscription_id = COALESCE(?, stripe_subscription_id),
        status = ?,
        trial_ends_at = ?,
        current_period_end = ?,
        cancel_at_period_end = ?,
        updated_at = ?
       WHERE user_id = ?`,
      [
        data.stripe_subscription_id ?? null,
        data.status,
        data.trial_ends_at ?? null,
        data.current_period_end ?? null,
        data.cancel_at_period_end ? 1 : 0,
        now,
        userId,
      ],
    );
  } else {
    run(
      `INSERT INTO subscription_mirrors
        (user_id, stripe_subscription_id, status, trial_ends_at, current_period_end, cancel_at_period_end, updated_at)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
      [
        userId,
        data.stripe_subscription_id ?? null,
        data.status,
        data.trial_ends_at ?? null,
        data.current_period_end ?? null,
        data.cancel_at_period_end ? 1 : 0,
        now,
      ],
    );
  }
  if (data.status === "trialing" || data.status === "active" || data.status === "past_due") {
    run("UPDATE users SET had_subscription = 1 WHERE id = ?", [userId]);
  }
  if (data.status === "trialing") {
    markTrialConsumed(userId);
  }
}

export function verifyDeviceApiKey(header: string | undefined): boolean {
  if (!header) return false;
  const raw = header.startsWith("Bearer ") ? header.slice(7) : header;
  return safeEqual(raw, config.deviceApiKey);
}

export function listDeviceLinks(userId: string) {
  return all<{
    id: string;
    user_id: string;
    device_id: string;
    device_name: string;
    linked_at: string;
    last_seen_at: string | null;
  }>(
    `SELECT id, user_id, device_id, device_name, linked_at, last_seen_at
     FROM device_links WHERE user_id = ? ORDER BY linked_at DESC`,
    [userId],
  );
}
