import { randomBytes, createHash, timingSafeEqual, randomUUID, scryptSync } from "node:crypto";

export function newId(): string {
  return randomUUID();
}

export function sha256Hex(input: string): string {
  return createHash("sha256").update(input, "utf8").digest("hex");
}

export function randomToken(bytes = 32): string {
  return randomBytes(bytes).toString("base64url");
}

export function hashPassword(password: string): string {
  const salt = randomBytes(16).toString("hex");
  const hash = scryptSync(password, salt, 64).toString("hex");
  return `scrypt$${salt}$${hash}`;
}

export function verifyPassword(password: string, stored: string): boolean {
  const parts = stored.split("$");
  if (parts.length !== 3 || parts[0] !== "scrypt") return false;
  const salt = parts[1]!;
  const expected = parts[2]!;
  const actual = scryptSync(password, salt, 64).toString("hex");
  try {
    return timingSafeEqual(Buffer.from(actual, "hex"), Buffer.from(expected, "hex"));
  } catch {
    return false;
  }
}

export function safeEqual(a: string, b: string): boolean {
  const ba = Buffer.from(a);
  const bb = Buffer.from(b);
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}

/** Pairing alphabet: A–Z + 2–9 excluding 0/O/1/I (Spec Part D §4.1). */
export const PAIR_CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

export function generatePairCode(length = 8): string {
  const out: string[] = [];
  const alphabet = PAIR_CODE_ALPHABET;
  const bytes = randomBytes(length);
  for (let i = 0; i < length; i++) {
    out.push(alphabet[bytes[i]! % alphabet.length]!);
  }
  return out.join("");
}

export function normalizePairCode(code: string): string {
  return code.trim().toUpperCase().replace(/[^A-Z2-9]/g, "");
}

export function isValidPairCode(code: string): boolean {
  const n = normalizePairCode(code);
  if (n.length !== 8) return false;
  for (const ch of n) {
    if (!PAIR_CODE_ALPHABET.includes(ch)) return false;
  }
  return true;
}
