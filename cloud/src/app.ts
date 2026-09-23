import { Hono } from "hono";
import { cors } from "hono/cors";
import { config } from "./config.js";
import type { AppEnv } from "./middleware/auth.js";
import { handleError, optionalSession } from "./middleware/auth.js";
import { authRoutes, meRoutes } from "./routes/auth.js";
import { pairRoutes, deviceRoutes } from "./routes/pair.js";
import { entitlementRoutes, deviceAttestRoutes } from "./routes/entitlement.js";
import { billingRoutes } from "./routes/billing.js";
import { notesRoutes } from "./routes/notes.js";
import { listsRoutes } from "./routes/lists.js";
import { syncRoutes } from "./routes/sync.js";
import { backupRoutes } from "./routes/backups.js";
import { publicShareRoutes } from "./routes/share.js";
import { connectorRoutes } from "./routes/connectors.js";
import { PRODUCT_NAME, isMockBilling } from "./stripe/billing.js";

export function createApp() {
  const app = new Hono<AppEnv>();

  app.use(
    "*",
    cors({
      origin: (origin) => {
        if (!origin) return config.corsOrigins[0]!;
        return config.corsOrigins.includes(origin) ? origin : "";
      },
      credentials: true,
      allowHeaders: [
        "Content-Type",
        "Authorization",
        "X-Device-Token",
        "X-Device-Key",
        "Stripe-Signature",
      ],
      allowMethods: ["GET", "POST", "PATCH", "DELETE", "OPTIONS"],
    }),
  );

  app.onError((err, c) => handleError(err, c));
  app.use("/v1/*", optionalSession);

  app.get("/health", (c) =>
    c.json({
      ok: true,
      product: PRODUCT_NAME,
      billing: isMockBilling() ? "mock" : "stripe",
    }),
  );

  app.route("/v1/auth", authRoutes);
  app.route("/v1/me", meRoutes);
  app.route("/v1/pair", pairRoutes);
  app.route("/v1/devices", deviceRoutes);
  app.route("/v1/entitlement", entitlementRoutes);
  app.route("/v1/device", deviceAttestRoutes);
  app.route("/v1/billing", billingRoutes);
  app.route("/v1/notes", notesRoutes);
  app.route("/v1/lists", listsRoutes);
  app.route("/v1/sync", syncRoutes);
  app.route("/v1/backups", backupRoutes);
  app.route("/v1/connectors", connectorRoutes);
  app.route("/s", publicShareRoutes);

  return app;
}
