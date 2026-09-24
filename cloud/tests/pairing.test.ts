import { describe, expect, it, beforeEach } from "vitest";
import { openMemoryDatabase, closeDatabase } from "../src/db/index.js";
import {
  createPairSession,
  getPairSessionPublic,
  claimPairSession,
  isPairSessionActive,
  PAIR_TTL_MS,
} from "../src/lib/pairing.js";
import { getOrCreateUser } from "../src/lib/auth.js";
import { generatePairCode, isValidPairCode, PAIR_CODE_ALPHABET } from "../src/lib/crypto.js";
import { AppError } from "../src/lib/errors.js";

describe("pairing codes & TTL (Spec Part D §4)", () => {
  beforeEach(async () => {
    closeDatabase();
    await openMemoryDatabase();
  });

  it("generates 8-char codes from the locked alphabet (no 0/O/1/I)", () => {
    for (let i = 0; i < 20; i++) {
      const code = generatePairCode();
      expect(code).toHaveLength(8);
      expect(isValidPairCode(code)).toBe(true);
      for (const ch of code) {
        expect(PAIR_CODE_ALPHABET).toContain(ch);
        expect(["0", "1", "O", "I"]).not.toContain(ch);
      }
    }
  });

  it("stores hash and reports pending within TTL", () => {
    const code = generatePairCode();
    const created = createPairSession({
      device_id: "device-1",
      code_public: code,
    });
    expect(isPairSessionActive(created.expires_at)).toBe(true);
    const pub = getPairSessionPublic(code);
    expect(pub.status).toBe("pending");
    expect(pub.device_label).toBe("Pocket");
  });

  it("expires after 10 minutes", () => {
    const code = generatePairCode();
    const past = new Date(Date.now() - 1000).toISOString();
    // Force an already-expired expires_at by writing then mutating via claim path
    createPairSession({
      device_id: "device-2",
      code_public: code,
      expires_at: new Date(Date.now() + PAIR_TTL_MS).toISOString(),
    });
    // Cap ensures expires_at <= now+10m; simulate expiry via public getter after clock
    const expiredMs = Date.now() + PAIR_TTL_MS + 1;
    expect(isPairSessionActive(new Date(Date.now() + PAIR_TTL_MS).toISOString(), expiredMs)).toBe(
      false,
    );
    // Past client expires_at (unsynced device RTC) is ignored — server TTL used instead
    const code2 = generatePairCode();
    const created = createPairSession({
      device_id: "device-3",
      code_public: code2,
      expires_at: past,
    });
    expect(isPairSessionActive(created.expires_at)).toBe(true);
    expect(getPairSessionPublic(code2).status).toBe("pending");
  });

  it("claim is single-use and returns device link", () => {
    const code = generatePairCode();
    createPairSession({ device_id: "device-claim", code_public: code });
    const user = getOrCreateUser("owner@example.com");
    const result = claimPairSession(code, user.id);
    expect(result.device_id).toBe("device-claim");
    expect(result.device_name).toBe("Pocket");
    expect(result.linked_at).toBeTruthy();
    expect(result.device_token).toBeTruthy();

    expect(getPairSessionPublic(code).status).toBe("claimed");
    expect(() => claimPairSession(code, user.id)).toThrow(AppError);
  });

  it("surfaces expired status once expires_at has passed", async () => {
    const code = generatePairCode();
    createPairSession({
      device_id: "device-expire-status",
      code_public: code,
      expires_at: new Date(Date.now() + 30).toISOString(),
    });
    await new Promise((r) => setTimeout(r, 40));
    const pub = getPairSessionPublic(code);
    expect(pub.status).toBe("expired");

    const user = getOrCreateUser("ttl@example.com");
    try {
      claimPairSession(code, user.id);
      expect.unreachable("should have thrown");
    } catch (err) {
      expect(err).toBeInstanceOf(AppError);
      expect((err as AppError).message).toBe(
        "This code has expired. Generate a new one on your Pocket.",
      );
    }
  });
});
