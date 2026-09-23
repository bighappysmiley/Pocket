import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession, requireDeviceToken } from "../middleware/auth.js";
import { getEntitlementForUser } from "../lib/auth.js";

export const entitlementRoutes = new Hono<AppEnv>();

entitlementRoutes.get("/", requireSession, async (c) => {
  const user = c.get("user")!;
  const snapshot = getEntitlementForUser(user);
  return c.json({
    entitled: snapshot.entitled,
    status: snapshot.status,
    trial_ends_at: snapshot.trial_ends_at ?? null,
    current_period_end: snapshot.current_period_end ?? null,
    trial_consumed: snapshot.trial_consumed,
  });
});

export const deviceAttestRoutes = new Hono<AppEnv>();

deviceAttestRoutes.get("/attest", requireDeviceToken, async (c) => {
  const user = c.get("user")!;
  const snapshot = getEntitlementForUser(user);
  const link = c.get("deviceLink")!;
  return c.json({
    entitled: snapshot.entitled,
    status: snapshot.status,
    trial_ends_at: snapshot.trial_ends_at ?? null,
    current_period_end: snapshot.current_period_end ?? null,
    trial_consumed: snapshot.trial_consumed,
    cloud_entitled: snapshot.entitled,
    device_id: link.device_id,
    device_name: link.device_name,
  });
});
