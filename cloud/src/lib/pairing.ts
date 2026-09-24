import { get, run } from "../db/index.js";
import {
  newId,
  sha256Hex,
  randomToken,
  normalizePairCode,
  isValidPairCode,
} from "./crypto.js";
import { Errors } from "./errors.js";

export const PAIR_TTL_MS = 10 * 60 * 1000;

export type PairStatus = "pending" | "claimed" | "expired";

export type PairSessionRow = {
  id: string;
  device_id: string;
  code_hash: string;
  code_public_hint: string;
  expires_at: string;
  created_at: string;
  claimed_at: string | null;
  claimed_by_user_id: string | null;
  status: string;
};

export function createPairSession(input: {
  device_id: string;
  code_public: string;
  expires_at?: string;
}): { id: string; expires_at: string } {
  if (!input.device_id?.trim()) {
    throw Errors.badRequest("A device id is required.");
  }
  const code = normalizePairCode(input.code_public);
  if (!isValidPairCode(code)) {
    throw Errors.badRequest("That pairing code isn't valid.");
  }
  const now = new Date();
  // Prefer server TTL — ignore client expires_at when missing/invalid/past (unsynced device RTC).
  let expiresAt = new Date(now.getTime() + PAIR_TTL_MS);
  if (input.expires_at) {
    const parsed = new Date(input.expires_at);
    if (!Number.isNaN(parsed.getTime()) && parsed.getTime() > now.getTime()) {
      expiresAt = parsed;
    }
  }
  // Cap TTL at 10 minutes from now
  const maxExpiry = new Date(now.getTime() + PAIR_TTL_MS);
  const effectiveExpiry = expiresAt > maxExpiry ? maxExpiry : expiresAt;
  if (effectiveExpiry.getTime() <= now.getTime()) {
    throw Errors.badRequest("That pairing code has already expired.");
  }

  // Replace any pending sessions for this device
  run(
    `UPDATE pair_sessions SET status = 'expired' WHERE device_id = ? AND status = 'pending'`,
    [input.device_id],
  );

  const id = newId();
  run(
    `INSERT INTO pair_sessions
      (id, device_id, code_hash, code_public_hint, expires_at, created_at, claimed_at, claimed_by_user_id, status)
     VALUES (?, ?, ?, ?, ?, ?, NULL, NULL, 'pending')`,
    [
      id,
      input.device_id,
      sha256Hex(code),
      code.slice(0, 2) + "******",
      effectiveExpiry.toISOString(),
      now.toISOString(),
    ],
  );
  return { id, expires_at: effectiveExpiry.toISOString() };
}

export function getPairSessionPublic(codeRaw: string): {
  status: PairStatus;
  device_label?: string;
} {
  const code = normalizePairCode(codeRaw);
  if (!isValidPairCode(code)) {
    return { status: "expired" };
  }
  const row = get<PairSessionRow>("SELECT * FROM pair_sessions WHERE code_hash = ?", [
    sha256Hex(code),
  ]);
  if (!row) return { status: "expired" };

  if (row.status === "claimed") {
    return { status: "claimed", device_label: "Pocket" };
  }

  if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
    if (row.status === "pending") {
      run(`UPDATE pair_sessions SET status = 'expired' WHERE id = ?`, [row.id]);
    }
    return { status: "expired" };
  }

  return { status: "pending", device_label: "Pocket" };
}

export function claimPairSession(
  codeRaw: string,
  userId: string,
): { device_id: string; device_name: string; linked_at: string; device_token: string } {
  const code = normalizePairCode(codeRaw);
  if (!isValidPairCode(code)) throw Errors.pairInvalid();

  const row = get<PairSessionRow>("SELECT * FROM pair_sessions WHERE code_hash = ?", [
    sha256Hex(code),
  ]);
  if (!row) throw Errors.pairInvalid();

  if (row.status === "claimed") {
    throw Errors.badRequest("That code was already used.");
  }
  if (row.status !== "pending" || Date.parse(row.expires_at) < Date.now()) {
    run(`UPDATE pair_sessions SET status = 'expired' WHERE id = ?`, [row.id]);
    throw Errors.pairExpired();
  }

  const existingLink = get<{ user_id: string; id: string }>(
    "SELECT user_id, id FROM device_links WHERE device_id = ?",
    [row.device_id],
  );
  if (existingLink && existingLink.user_id !== userId) {
    throw Errors.pairOtherAccount();
  }

  const now = new Date().toISOString();
  const deviceToken = randomToken(32);
  const deviceName = "Pocket";

  if (existingLink) {
    run(
      `UPDATE device_links SET user_id = ?, device_name = ?, linked_at = ?, device_token_hash = ?, last_seen_at = ? WHERE id = ?`,
      [userId, deviceName, now, sha256Hex(deviceToken), now, existingLink.id],
    );
  } else {
    run(
      `INSERT INTO device_links
        (id, user_id, device_id, device_name, linked_at, last_seen_at, device_token_hash)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
      [newId(), userId, row.device_id, deviceName, now, now, sha256Hex(deviceToken)],
    );
  }

  run(
    `UPDATE pair_sessions SET status = 'claimed', claimed_at = ?, claimed_by_user_id = ? WHERE id = ?`,
    [now, userId, row.id],
  );

  return {
    device_id: row.device_id,
    device_name: deviceName,
    linked_at: now,
    device_token: deviceToken,
  };
}

/** True if a pending session is still within TTL (for tests). */
export function isPairSessionActive(expiresAtIso: string, nowMs = Date.now()): boolean {
  return Date.parse(expiresAtIso) > nowMs;
}
