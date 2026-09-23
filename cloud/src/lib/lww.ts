/**
 * Last-write-wins with updated_at + updated_by_device_id tie-break.
 * Spec Part E §9.3:
 * 1. Greater updated_at wins
 * 2. Equal timestamps → higher lexicographic updated_by_device_id wins
 * 3. Tombstone wins if its updated_at/deleted_at is newer than competing update
 */

export type LwwFields = {
  updated_at: string;
  updated_by_device_id: string;
  deleted_at?: string | null;
};

/** Returns true if `incoming` should replace `existing`. */
export function incomingWinsLww(incoming: LwwFields, existing: LwwFields): boolean {
  const incomingKey = conflictInstant(incoming);
  const existingKey = conflictInstant(existing);
  if (incomingKey > existingKey) return true;
  if (incomingKey < existingKey) return false;
  return incoming.updated_by_device_id > existing.updated_by_device_id;
}

/** Prefer deleted_at when present for tombstone conflict key (§9.3 rule 5). */
function conflictInstant(row: LwwFields): number {
  const tombstone = row.deleted_at ? Date.parse(row.deleted_at) : NaN;
  const updated = Date.parse(row.updated_at);
  if (Number.isFinite(tombstone) && Number.isFinite(updated)) {
    return Math.max(tombstone, updated);
  }
  if (Number.isFinite(tombstone)) return tombstone;
  if (Number.isFinite(updated)) return updated;
  return 0;
}

/**
 * Clamp future client clocks: if updated_at is >1h ahead of server, use server now.
 * Spec §9.3 clock skew guidance.
 */
export function normalizeUpdatedAt(clientIso: string | undefined, serverNow: Date): string {
  if (!clientIso) return serverNow.toISOString();
  const t = Date.parse(clientIso);
  if (!Number.isFinite(t)) return serverNow.toISOString();
  const skewMs = t - serverNow.getTime();
  if (skewMs > 60 * 60 * 1000) return serverNow.toISOString();
  return new Date(t).toISOString();
}

export type ListItem = {
  id: string;
  text: string;
  checked: boolean;
  updated_at: string;
};

/** Merge list items with per-item LWW on updated_at. */
export function mergeListItems(existing: ListItem[], incoming: ListItem[]): ListItem[] {
  const map = new Map<string, ListItem>();
  for (const item of existing) map.set(item.id, item);
  for (const item of incoming) {
    const prev = map.get(item.id);
    if (!prev) {
      map.set(item.id, item);
      continue;
    }
    const a = Date.parse(item.updated_at);
    const b = Date.parse(prev.updated_at);
    if (a > b || (a === b && item.id > prev.id)) {
      map.set(item.id, item);
    }
  }
  return Array.from(map.values());
}
