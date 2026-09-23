import { Hono } from "hono";
import type { AppEnv } from "../middleware/auth.js";
import { requireSession } from "../middleware/auth.js";
import {
  createCheckoutSession,
  createPortalSession,
  getStripe,
  handleStripeEvent,
  isMockBilling,
  PRODUCT_NAME,
  MONTHLY_AMOUNT_CENTS,
} from "../stripe/billing.js";
import { Errors } from "../lib/errors.js";
import { config } from "../config.js";

export const billingRoutes = new Hono<AppEnv>();

billingRoutes.get("/catalog", (c) => {
  return c.json({
    product_name: PRODUCT_NAME,
    monthly: {
      amount_cents: MONTHLY_AMOUNT_CENTS,
      currency: "usd",
      interval: "month",
      display: "$3.99/mo",
    },
    trial_days: 7,
    yearly: null,
    mock_mode: isMockBilling(),
  });
});

billingRoutes.post("/checkout", requireSession, async (c) => {
  const user = c.get("user")!;
  const result = await createCheckoutSession(user);
  return c.json({ url: result.url, product_name: PRODUCT_NAME });
});

billingRoutes.post("/portal", requireSession, async (c) => {
  const user = c.get("user")!;
  const result = await createPortalSession(user);
  return c.json({ url: result.url });
});

billingRoutes.post("/webhook", async (c) => {
  if (isMockBilling()) {
    // Accept mock webhook payloads for local testing without signature
    const body = await c.req.json().catch(() => null);
    if (body && typeof body === "object" && "type" in body && "id" in body) {
      await handleStripeEvent(body as never);
    }
    return c.json({ received: true, mock: true });
  }

  const stripe = getStripe()!;
  const signature = c.req.header("stripe-signature");
  if (!signature || !config.stripeWebhookSecret) {
    throw Errors.badRequest("Couldn't verify that request.");
  }
  const raw = await c.req.text();
  let event;
  try {
    event = stripe.webhooks.constructEvent(raw, signature, config.stripeWebhookSecret);
  } catch {
    throw Errors.badRequest("Couldn't verify that request.");
  }
  await handleStripeEvent(event);
  return c.json({ received: true });
});
