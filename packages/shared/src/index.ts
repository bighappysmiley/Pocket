/** Shared Pocket Cloud API shapes (Spec Part E). */

export type EntitlementStatus = "free" | "trialing" | "active" | "lapsed";

export type EntitlementSnapshot = {
  entitled: boolean;
  status: EntitlementStatus;
  trial_ends_at?: string | null;
  current_period_end?: string | null;
  trial_consumed: boolean;
};

export type DeviceLink = {
  id: string;
  device_id: string;
  device_name: string;
  linked_at: string;
  last_seen_at?: string | null;
};

export type CloudNote = {
  id: string;
  title: string;
  body: string;
  created_at: string;
  updated_at: string;
  updated_by_device_id: string;
  deleted_at: string | null;
  version?: number;
};

export type CloudListItem = {
  id: string;
  text: string;
  checked: boolean;
  updated_at: string;
};

export type CloudList = {
  id: string;
  title: string;
  items: CloudListItem[];
  created_at: string;
  updated_at: string;
  updated_by_device_id: string;
  deleted_at: string | null;
  version?: number;
};

export type PairSessionStatus = "pending" | "claimed" | "expired";

/** Product subscription name — never "Connect". */
export const POCKET_CLOUD_PRODUCT_NAME = "Pocket Cloud" as const;
export const POCKET_CLOUD_MONTHLY_CENTS = 399 as const;
