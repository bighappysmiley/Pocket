# Stripe payment setup (Pocket Cloud)

Pocket Cloud billing is **$3.99/month** only (no yearly plan). The companion Billing page calls the Cloud API; Stripe Checkout and Customer Portal handle payment.

If `STRIPE_SECRET_KEY` is **unset**, Cloud runs in **mock billing** (checkout instantly grants trial/active). Leave secrets empty for local/dev.

## 1. Create the product in Stripe

1. Open [Stripe Dashboard](https://dashboard.stripe.com) → **Products** → **Add product**.
2. Name: **Pocket Cloud** (do not use “Connect”).
3. Pricing:
   - Model: **Recurring**
   - Price: **$3.99 USD**
   - Billing period: **Monthly**
   - Do **not** create a yearly price.
4. Save and copy the **Price ID** (`price_…`).

Optional: set env `STRIPE_PRODUCT_NAME=Pocket Cloud` (this is already the default).

## 2. API keys

1. Stripe → **Developers** → **API keys**.
2. Copy the **Secret key** (`sk_test_…` for test, `sk_live_…` for production).
3. Preferred: open Companion **Admin → Stripe** and paste the secret key, monthly **Price ID**, and webhook
   signing secret. Keys are stored server-side (never paste them into chat).
4. Alternative (host env on Cloud / Neon Function):

```bash
STRIPE_SECRET_KEY=sk_test_…
STRIPE_PRICE_MONTHLY_ID=price_…
STRIPE_WEBHOOK_SECRET=whsec_…
```

Admin-saved settings override env when present. If neither has a secret key, Cloud runs **mock billing**.

## 3. Webhook

1. Stripe → **Developers** → **Webhooks** → **Add endpoint**.
2. Endpoint URL:

```text
https://<your-cloud-origin>/v1/billing/webhook
```

Example: `https://pocket-cloud.fly.dev/v1/billing/webhook`

3. Subscribe to at least:
   - `checkout.session.completed`
   - `customer.subscription.created`
   - `customer.subscription.updated`
   - `customer.subscription.deleted`
   - `invoice.paid` (or `invoice.payment_succeeded`)
   - `invoice.payment_failed`
   - `invoice.payment_action_required` (if offered)

4. Copy the endpoint **Signing secret** (`whsec_…`) and set:

```bash
STRIPE_WEBHOOK_SECRET=whsec_…
```

Events are deduped in the `stripe_events` table.

## 4. Customer Portal

1. Stripe → **Settings** → **Billing** → **Customer portal**.
2. Enable the portal (customers manage payment method / cancel).
3. Cloud creates portal sessions via `POST /v1/billing/portal` and returns users to `{PWA_ORIGIN}/billing`.

## 5. Origins (Checkout redirects)

Set these on Cloud so Checkout success/cancel and portal return URLs work:

| Variable | Value |
| --- | --- |
| `PUBLIC_BASE_URL` | Cloud public origin (webhook + API) |
| `PWA_ORIGIN` | Companion origin |

Checkout uses:

- Success: `{PWA_ORIGIN}/billing/success?session_id={CHECKOUT_SESSION_ID}`
- Cancel: `{PWA_ORIGIN}/billing/cancel`
- Portal return: `{PWA_ORIGIN}/billing`

Companion build needs `VITE_API_BASE` pointing at the same Cloud origin.

## 6. Trial behavior

- First-time eligible users get **7 days** trial (`trial_period_days=7`) when `trial_consumed` is false.
- Entitlement is **trialing** or **active** (see `subscription_mirrors`).
- Product catalog in code: monthly **399¢**, yearly **null**.

## 7. Verify

1. Deploy Cloud with the three Stripe env vars above.
2. Open companion → sign in → **Billing** → Start checkout.
3. Use a Stripe test card (`4242…`) in test mode.
4. Confirm:
   - Webhook deliveries succeed in Stripe Dashboard
   - `GET /v1/entitlement` shows entitled after trial/active
   - Portal opens and returns to `/billing`

## 8. Local / CI without Stripe

Leave `STRIPE_SECRET_KEY` empty. `POST /v1/billing/checkout` completes a mock session and grants entitlement immediately. No webhook needed.

## Env checklist

```bash
STRIPE_SECRET_KEY=sk_…
STRIPE_WEBHOOK_SECRET=whsec_…
STRIPE_PRICE_MONTHLY_ID=price_…
STRIPE_PRODUCT_NAME=Pocket Cloud   # optional
PUBLIC_BASE_URL=https://…
PWA_ORIGIN=https://…
```

See also `cloud/.env.example`, `cloud/README.md`, and `docs/deploy.md`.
