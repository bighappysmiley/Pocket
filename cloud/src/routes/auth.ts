import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import {
  clearSessionCookie,
  optionalSession,
  requireSession,
  setSessionCookie,
} from "../middleware/auth.js";
import {
  createMagicLink,
  createSession,
  consumeMagicLink,
  destroySession,
  getEntitlementForUser,
  listDeviceLinks,
  registerWithPassword,
  loginWithPassword,
  verifyEmailToken,
} from "../lib/auth.js";
import { readSessionToken } from "../middleware/auth.js";
import { config } from "../config.js";
import { Errors } from "../lib/errors.js";

export const authRoutes = new Hono<AppEnv>();

authRoutes.post("/register", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const email = typeof body.email === "string" ? body.email : "";
  const password = typeof body.password === "string" ? body.password : "";
  const { verifyUrl } = registerWithPassword(email, password);
  if (config.isDev || !process.env.SMTP_URL) {
    console.log(`[verify-email] ${email.trim().toLowerCase()} → ${verifyUrl}`);
  }
  // SMTP delivery can be wired later via SMTP_URL / Resend; link is always created.
  return c.json({
    ok: true,
    message: "Check your email for a verification link, then sign in.",
  });
});

authRoutes.post("/login", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const email = typeof body.email === "string" ? body.email : "";
  const password = typeof body.password === "string" ? body.password : "";
  const user = loginWithPassword(email, password);
  const session = createSession(user.id);
  setSessionCookie(c, session.token, session.expiresAt);
  return c.json({
    ok: true,
    user: { id: user.id, email: user.email },
  });
});

authRoutes.get("/verify-email", async (c) => {
  const token = c.req.query("token");
  if (!token) throw Errors.badRequest("This verification link is invalid.");
  verifyEmailToken(token);
  const dest = `${config.pwaOrigin}/login?verified=1`;
  return c.redirect(dest, 302);
});

/** Legacy passwordless magic link (kept for older clients / tests). */
authRoutes.post("/magic-link", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const email = typeof body.email === "string" ? body.email : "";
  const { token } = createMagicLink(email);
  const callbackUrl = `${config.publicBaseUrl}/v1/auth/callback?token=${encodeURIComponent(token)}`;
  if (config.isDev) {
    console.log(`[magic-link] ${email.trim().toLowerCase()} → ${callbackUrl}`);
  }
  return c.json({
    ok: true,
    message: "Check your email for a sign-in link.",
  });
});

authRoutes.get("/callback", async (c) => {
  const token = c.req.query("token");
  if (!token) throw Errors.badRequest("This sign-in link is invalid.");
  const user = consumeMagicLink(token);
  const session = createSession(user.id);
  setSessionCookie(c, session.token, session.expiresAt);
  const redirect = c.req.query("return_to") || `${config.pwaOrigin}/`;
  const safe =
    redirect.startsWith("/") || redirect.startsWith(config.pwaOrigin)
      ? redirect.startsWith("/")
        ? `${config.pwaOrigin}${redirect}`
        : redirect
      : `${config.pwaOrigin}/`;
  return c.redirect(safe, 302);
});

authRoutes.post("/logout", optionalSession, async (c) => {
  const token = readSessionToken(c);
  if (token) destroySession(token);
  clearSessionCookie(c);
  return c.json({ ok: true });
});

export const meRoutes = new Hono<AppEnv>();

meRoutes.get("/", requireSession, async (c) => {
  const user = c.get("user")!;
  const entitlement = getEntitlementForUser(user);
  const devices = listDeviceLinks(user.id);
  return c.json({
    user: {
      id: user.id,
      email: user.email,
      created_at: user.created_at,
      trial_consumed: Boolean(user.trial_consumed),
      email_verified: Boolean(user.email_verified_at),
    },
    entitlement,
    devices: devices.map((d) => ({
      id: d.id,
      device_id: d.device_id,
      device_name: d.device_name,
      linked_at: d.linked_at,
      last_seen_at: d.last_seen_at,
    })),
  });
});
