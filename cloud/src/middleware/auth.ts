import type { Context, Next } from "hono";
import { getCookie, setCookie, deleteCookie } from "hono/cookie";
import { config } from "../config.js";
import { findSessionUser, type UserRow, getEntitlementForUser } from "../lib/auth.js";
import { Errors, AppError } from "../lib/errors.js";
import { get } from "../db/index.js";
import { sha256Hex } from "../lib/crypto.js";
import { run } from "../db/index.js";
import type { EntitlementSnapshot } from "../lib/entitlement.js";

export type AppVariables = {
  user?: UserRow;
  entitlement?: EntitlementSnapshot;
  deviceLink?: {
    id: string;
    user_id: string;
    device_id: string;
    device_name: string;
  };
};

export type AppEnv = { Variables: AppVariables };

export function readSessionToken(c: Context<AppEnv>): string | undefined {
  return getCookie(c, config.sessionCookieName) || undefined;
}

export function setSessionCookie(c: Context<AppEnv>, token: string, expiresAt: string): void {
  const maxAge = Math.max(
    0,
    Math.floor((Date.parse(expiresAt) - Date.now()) / 1000),
  );
  setCookie(c, config.sessionCookieName, token, {
    httpOnly: true,
    sameSite: "Lax",
    path: "/",
    secure: !config.isDev,
    maxAge,
  });
}

export function clearSessionCookie(c: Context<AppEnv>): void {
  deleteCookie(c, config.sessionCookieName, { path: "/" });
}

export async function optionalSession(c: Context<AppEnv>, next: Next) {
  const token = readSessionToken(c);
  if (token) {
    const user = findSessionUser(token);
    if (user) {
      c.set("user", user);
      c.set("entitlement", getEntitlementForUser(user));
    }
  }
  await next();
}

export async function requireSession(c: Context<AppEnv>, next: Next) {
  const token = readSessionToken(c);
  if (!token) throw Errors.unauthorized();
  const user = findSessionUser(token);
  if (!user) throw Errors.unauthorized();
  c.set("user", user);
  c.set("entitlement", getEntitlementForUser(user));
  await next();
}

export async function requireEntitled(c: Context<AppEnv>, next: Next) {
  const user = c.get("user");
  if (!user) throw Errors.unauthorized();
  const entitlement = c.get("entitlement") ?? getEntitlementForUser(user);
  c.set("entitlement", entitlement);
  if (!entitlement.entitled) throw Errors.notEntitled();
  await next();
}

export async function requireDeviceToken(c: Context<AppEnv>, next: Next) {
  const auth = c.req.header("authorization");
  const token =
    auth?.startsWith("Bearer ") ? auth.slice(7) : c.req.header("x-device-token") || "";
  if (!token) throw Errors.unauthorized();
  const link = get<{
    id: string;
    user_id: string;
    device_id: string;
    device_name: string;
  }>("SELECT id, user_id, device_id, device_name FROM device_links WHERE device_token_hash = ?", [
    sha256Hex(token),
  ]);
  if (!link) throw Errors.unauthorized();
  run("UPDATE device_links SET last_seen_at = ? WHERE id = ?", [
    new Date().toISOString(),
    link.id,
  ]);
  c.set("deviceLink", link);
  const user = get<{
    id: string;
    email: string;
    created_at: string;
    trial_consumed: number;
    stripe_customer_id: string | null;
    had_subscription: number;
  }>("SELECT * FROM users WHERE id = ?", [link.user_id]);
  if (!user) throw Errors.unauthorized();
  c.set("user", user as UserRow);
  c.set("entitlement", getEntitlementForUser(user as UserRow));
  await next();
}

export function handleError(err: unknown, c: Context) {
  if (err instanceof AppError) {
    return c.json({ error: { code: err.code, message: err.message } }, err.status as 400);
  }
  console.error(err);
  return c.json(
    { error: { code: "internal", message: "Something went wrong. Try again." } },
    500,
  );
}
