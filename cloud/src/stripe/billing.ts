import Stripe from "stripe";
import { config } from "../config.js";
import { newId } from "../lib/crypto.js";
import { get, run } from "../db/index.js";
import {
  findUserById,
  markTrialConsumed,
  setStripeCustomerId,
  upsertSubscriptionMirror,
  type UserRow,
} from "../lib/auth.js";
import { Errors } from "../lib/errors.js";

let stripeClient: Stripe | null = null;

export function getStripe(): Stripe | null {
  if (config.stripeMockMode) return null;
  if (!stripeClient) {
    stripeClient = new Stripe(config.stripeSecretKey);
  }
  return stripeClient;
}

export function isMockBilling(): boolean {
  return config.stripeMockMode;
}

/** Ensure we never create a yearly Price (Spec §3.1). */
export const MONTHLY_AMOUNT_CENTS = 399;
export const PRODUCT_NAME = "Pocket Cloud";

export async function createCheckoutSession(user: UserRow): Promise<{ url: string }> {
  const withTrial = !user.trial_consumed;
  const successUrl = `${config.pwaOrigin}/billing/success?session_id={CHECKOUT_SESSION_ID}`;
  const cancelUrl = `${config.pwaOrigin}/billing/cancel`;

  if (isMockBilling()) {
    const id = `mock_cs_${newId().replace(/-/g, "").slice(0, 24)}`;
    run(
      `INSERT INTO mock_checkouts (id, user_id, with_trial, created_at, completed_at)
       VALUES (?, ?, ?, ?, NULL)`,
      [id, user.id, withTrial ? 1 : 0, new Date().toISOString()],
    );
    // Simulate immediate success in mock mode (sets trialing/active entitlement)
    completeMockCheckout(id);
    return { url: `${config.pwaOrigin}/billing/success?session_id=${id}` };
  }

  const stripe = getStripe()!;
  let customerId = user.stripe_customer_id;
  if (!customerId) {
    const customer = await stripe.customers.create({
      email: user.email,
      metadata: { pocket_user_id: user.id },
    });
    customerId = customer.id;
    setStripeCustomerId(user.id, customerId);
  }

  const priceId = config.stripePriceMonthlyId;
  if (!priceId) {
    throw Errors.badRequest("Billing isn't configured yet. Try again later.");
  }

  const sessionParams: Stripe.Checkout.SessionCreateParams = {
    mode: "subscription",
    customer: customerId,
    client_reference_id: user.id,
    success_url: successUrl,
    cancel_url: cancelUrl,
    line_items: [{ price: priceId, quantity: 1 }],
    allow_promotion_codes: false,
  };
  if (withTrial) {
    sessionParams.subscription_data = { trial_period_days: 7 };
  }

  const session = await stripe.checkout.sessions.create(sessionParams);
  if (!session.url) throw Errors.badRequest("Couldn't start checkout. Try again.");
  return { url: session.url };
}

export async function createPortalSession(user: UserRow): Promise<{ url: string }> {
  if (isMockBilling()) {
    if (!user.stripe_customer_id && !user.had_subscription && !user.trial_consumed) {
      throw Errors.badRequest("No billing account yet. Start a subscription first.");
    }
    return { url: `${config.pwaOrigin}/billing?portal=mock` };
  }

  const stripe = getStripe()!;
  if (!user.stripe_customer_id) {
    throw Errors.badRequest("No billing account yet. Start a subscription first.");
  }
  const session = await stripe.billingPortal.sessions.create({
    customer: user.stripe_customer_id,
    return_url: `${config.pwaOrigin}/billing`,
  });
  return { url: session.url };
}

export function completeMockCheckout(checkoutId: string): void {
  const row = get<{
    id: string;
    user_id: string;
    with_trial: number;
    completed_at: string | null;
  }>("SELECT * FROM mock_checkouts WHERE id = ?", [checkoutId]);
  if (!row || row.completed_at) return;

  const now = new Date();
  const trialEnds = new Date(now.getTime() + 7 * 86_400_000);
  const periodEnd = row.with_trial
    ? trialEnds
    : new Date(now.getTime() + 30 * 86_400_000);

  const mockCustomer = `cus_mock_${row.user_id.replace(/-/g, "").slice(0, 14)}`;
  const mockSub = `sub_mock_${newId().replace(/-/g, "").slice(0, 14)}`;

  setStripeCustomerId(row.user_id, mockCustomer);
  if (row.with_trial) markTrialConsumed(row.user_id);

  upsertSubscriptionMirror(row.user_id, {
    stripe_subscription_id: mockSub,
    status: row.with_trial ? "trialing" : "active",
    trial_ends_at: row.with_trial ? trialEnds.toISOString() : null,
    current_period_end: periodEnd.toISOString(),
    cancel_at_period_end: false,
  });

  run("UPDATE mock_checkouts SET completed_at = ? WHERE id = ?", [
    now.toISOString(),
    checkoutId,
  ]);
}

export function alreadyProcessedEvent(eventId: string): boolean {
  return Boolean(get("SELECT id FROM stripe_events WHERE id = ?", [eventId]));
}

export function markEventProcessed(eventId: string): void {
  run("INSERT OR IGNORE INTO stripe_events (id, received_at) VALUES (?, ?)", [
    eventId,
    new Date().toISOString(),
  ]);
}

export async function handleStripeEvent(event: Stripe.Event): Promise<void> {
  if (alreadyProcessedEvent(event.id)) return;

  switch (event.type) {
    case "checkout.session.completed": {
      const session = event.data.object as Stripe.Checkout.Session;
      const userId = session.client_reference_id;
      if (userId) {
        const user = findUserById(userId);
        if (user) {
          if (typeof session.customer === "string") {
            setStripeCustomerId(userId, session.customer);
          }
          // Trial started via checkout → consume trial; entitlement updated when subscription webhook arrives
          if (session.mode === "subscription") {
            // If subscription expands later; optimistic trialing if subscription present
            upsertSubscriptionMirror(userId, {
              stripe_subscription_id:
                typeof session.subscription === "string" ? session.subscription : null,
              status: "trialing",
              trial_ends_at: new Date(Date.now() + 7 * 86_400_000).toISOString(),
              current_period_end: new Date(Date.now() + 7 * 86_400_000).toISOString(),
            });
            markTrialConsumed(userId);
          }
        }
      }
      break;
    }
    case "customer.subscription.created":
    case "customer.subscription.updated": {
      const sub = event.data.object as Stripe.Subscription;
      const userId = await resolveUserIdFromSubscription(sub);
      if (userId) {
        upsertSubscriptionMirror(userId, {
          stripe_subscription_id: sub.id,
          status: sub.status,
          trial_ends_at: sub.trial_end
            ? new Date(sub.trial_end * 1000).toISOString()
            : null,
          current_period_end: new Date(sub.current_period_end * 1000).toISOString(),
          cancel_at_period_end: Boolean(sub.cancel_at_period_end),
        });
        if (sub.status === "trialing") markTrialConsumed(userId);
      }
      break;
    }
    case "customer.subscription.deleted": {
      const sub = event.data.object as Stripe.Subscription;
      const userId = await resolveUserIdFromSubscription(sub);
      if (userId) {
        upsertSubscriptionMirror(userId, {
          stripe_subscription_id: sub.id,
          status: "canceled",
          trial_ends_at: null,
          current_period_end: sub.current_period_end
            ? new Date(sub.current_period_end * 1000).toISOString()
            : null,
          cancel_at_period_end: false,
        });
      }
      break;
    }
    case "invoice.paid": {
      const invoice = event.data.object as Stripe.Invoice;
      const subId =
        typeof invoice.subscription === "string" ? invoice.subscription : null;
      if (subId) {
        const userId = await resolveUserIdFromCustomer(
          typeof invoice.customer === "string" ? invoice.customer : null,
        );
        if (userId) {
          upsertSubscriptionMirror(userId, {
            stripe_subscription_id: subId,
            status: "active",
            current_period_end: invoice.lines?.data?.[0]?.period?.end
              ? new Date(invoice.lines.data[0].period.end * 1000).toISOString()
              : undefined,
          });
        }
      }
      break;
    }
    case "invoice.payment_failed":
    case "invoice.payment_action_required": {
      const invoice = event.data.object as Stripe.Invoice;
      const userId = await resolveUserIdFromCustomer(
        typeof invoice.customer === "string" ? invoice.customer : null,
      );
      if (userId) {
        upsertSubscriptionMirror(userId, {
          stripe_subscription_id:
            typeof invoice.subscription === "string" ? invoice.subscription : null,
          status: "past_due",
        });
      }
      break;
    }
    case "customer.subscription.trial_will_end":
    case "customer.updated":
      // Log-only / optional UX flags — no entitlement change
      break;
    default:
      break;
  }

  markEventProcessed(event.id);
}

async function resolveUserIdFromSubscription(
  sub: Stripe.Subscription,
): Promise<string | undefined> {
  const customerId = typeof sub.customer === "string" ? sub.customer : sub.customer?.id;
  return resolveUserIdFromCustomer(customerId ?? null);
}

async function resolveUserIdFromCustomer(
  customerId: string | null,
): Promise<string | undefined> {
  if (!customerId) return undefined;
  const user = get<{ id: string }>(
    "SELECT id FROM users WHERE stripe_customer_id = ?",
    [customerId],
  );
  return user?.id;
}
