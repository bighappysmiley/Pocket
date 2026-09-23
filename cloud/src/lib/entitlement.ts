export type EntitlementStatus = "free" | "trialing" | "active" | "lapsed";

export type EntitlementSnapshot = {
  entitled: boolean;
  status: EntitlementStatus;
  trial_ends_at?: string | null;
  current_period_end?: string | null;
  trial_consumed: boolean;
  cancel_at_period_end?: boolean;
};

export type SubscriptionRow = {
  status: string;
  trial_ends_at: string | null;
  current_period_end: string | null;
  cancel_at_period_end: number;
};

/**
 * Spec §7.5: entitled = status in {trialing, active}; past_due / unpaid / canceled => false.
 * Lapsed = had subscription before AND not entitled (caller sets via UX umbrella).
 */
export function mapSubscriptionToEntitlement(
  sub: SubscriptionRow | null | undefined,
  trialConsumed: boolean,
  hadSubscriptionBefore: boolean,
): EntitlementSnapshot {
  if (!sub) {
    return {
      entitled: false,
      status: hadSubscriptionBefore ? "lapsed" : "free",
      trial_consumed: trialConsumed,
      trial_ends_at: null,
      current_period_end: null,
      cancel_at_period_end: false,
    };
  }

  const stripeStatus = sub.status;
  const entitled = stripeStatus === "trialing" || stripeStatus === "active";

  let status: EntitlementStatus;
  if (stripeStatus === "trialing") status = "trialing";
  else if (stripeStatus === "active") status = "active";
  else if (
    stripeStatus === "past_due" ||
    stripeStatus === "unpaid" ||
    stripeStatus === "canceled" ||
    stripeStatus === "incomplete" ||
    stripeStatus === "incomplete_expired" ||
    stripeStatus === "paused"
  ) {
    status = hadSubscriptionBefore || trialConsumed ? "lapsed" : "free";
  } else {
    status = entitled ? "active" : hadSubscriptionBefore ? "lapsed" : "free";
  }

  if (!entitled && (hadSubscriptionBefore || trialConsumed)) {
    status = "lapsed";
  }

  return {
    entitled,
    status: entitled ? status : status === "trialing" || status === "active" ? "lapsed" : status,
    trial_ends_at: sub.trial_ends_at,
    current_period_end: sub.current_period_end,
    trial_consumed: trialConsumed,
    cancel_at_period_end: Boolean(sub.cancel_at_period_end),
  };
}
