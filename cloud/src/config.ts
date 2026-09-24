import { config as loadEnv } from "./load-env.js";

loadEnv();

function env(name: string, fallback?: string): string {
  const v = process.env[name] ?? fallback;
  if (v === undefined) {
    throw new Error(`Missing required env: ${name}`);
  }
  return v;
}

function envInt(name: string, fallback: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === "") return fallback;
  const n = Number.parseInt(raw, 10);
  return Number.isFinite(n) ? n : fallback;
}

const defaultCors = [
  "http://localhost:5173",
  "https://app.getpocket.device",
  "https://bighappysmiley.github.io",
];

export const config = {
  port: envInt("PORT", 8787),
  host: process.env.HOST || "0.0.0.0",
  nodeEnv: process.env.NODE_ENV || "development",
  isDev: (process.env.NODE_ENV || "development") !== "production",
  publicBaseUrl: process.env.PUBLIC_BASE_URL || "http://localhost:8787",
  // Companion origin including path prefix for GitHub project Pages
  pwaOrigin: process.env.PWA_ORIGIN || "http://localhost:5173",
  corsOrigins: Array.from(
    new Set([
      ...defaultCors,
      ...(process.env.CORS_ORIGINS || "")
        .split(",")
        .map((s) => s.trim())
        .filter(Boolean),
    ]),
  ),
  sessionSecret: env("SESSION_SECRET", "dev-change-me-to-a-long-random-string"),
  sessionCookieName: process.env.SESSION_COOKIE_NAME || "pocket_session",
  sessionTtlDays: envInt("SESSION_TTL_DAYS", 30),
  magicLinkTtlMinutes: envInt("MAGIC_LINK_TTL_MINUTES", 15),
  deviceApiKey: env("DEVICE_API_KEY", "dev-device-api-key"),
  databasePath: process.env.DATABASE_PATH || "./data/pocket-cloud.sqlite",
  stripeSecretKey: process.env.STRIPE_SECRET_KEY || "",
  stripeWebhookSecret: process.env.STRIPE_WEBHOOK_SECRET || "",
  stripePriceMonthlyId: process.env.STRIPE_PRICE_MONTHLY_ID || "",
  stripeProductName: process.env.STRIPE_PRODUCT_NAME || "Pocket Cloud",
  backupRetentionCount: Math.max(3, envInt("BACKUP_RETENTION_COUNT", 3)),
  shareDefaultExpiryDays: envInt("SHARE_DEFAULT_EXPIRY_DAYS", 30),
  get stripeMockMode(): boolean {
    return !this.stripeSecretKey;
  },
} as const;

export type AppConfig = typeof config;
