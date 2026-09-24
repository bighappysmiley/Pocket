import { describe, expect, it, beforeEach } from "vitest";
import { openMemoryDatabase, closeDatabase } from "../src/db/index.js";
import { createApp } from "../src/app.js";
import {
  getOrCreateUser,
  createSession,
  upsertSubscriptionMirror,
  markTrialConsumed,
} from "../src/lib/auth.js";
import { generatePairCode } from "../src/lib/crypto.js";

describe("HTTP smoke — auth, pair, entitlement gate", () => {
  beforeEach(async () => {
    closeDatabase();
    await openMemoryDatabase();
  });

  it("register → verify email → login → me", async () => {
    const app = createApp();
    const { registerWithPassword, verifyEmailToken } = await import("../src/lib/auth.js");

    const { verifyToken } = registerWithPassword("new@example.com", "password123");

    const beforeVerify = await app.request("http://localhost/v1/auth/login", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ email: "new@example.com", password: "password123" }),
    });
    expect(beforeVerify.status).toBe(400);

    verifyEmailToken(verifyToken);

    const login = await app.request("http://localhost/v1/auth/login", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ email: "new@example.com", password: "password123" }),
    });
    expect(login.status).toBe(200);
    const cookie = login.headers.get("set-cookie") || "";
    expect(cookie).toContain("pocket_session=");

    const me = await app.request("http://localhost/v1/me", {
      headers: { cookie: cookie.split(";")[0]! },
    });
    expect(me.status).toBe(200);
    const body = await me.json();
    expect(body.user.email).toBe("new@example.com");
    expect(body.user.email_verified).toBe(true);
  });

  it("magic-link → session → me", async () => {
    const app = createApp();
    const send = await app.request("http://localhost/v1/auth/magic-link", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ email: "ada@example.com" }),
    });
    expect(send.status).toBe(200);

    // Consume via createSession after getOrCreate for test (callback needs token from DB)
    const user = getOrCreateUser("ada@example.com");
    const session = createSession(user.id);
    const me = await app.request("http://localhost/v1/me", {
      headers: { cookie: `pocket_session=${session.token}` },
    });
    expect(me.status).toBe(200);
    const body = await me.json();
    expect(body.user.email).toBe("ada@example.com");
    expect(body.entitlement.entitled).toBe(false);
  });

  it("gates notes without entitlement", async () => {
    const app = createApp();
    const user = getOrCreateUser("bob@example.com");
    const session = createSession(user.id);
    const res = await app.request("http://localhost/v1/notes", {
      headers: { cookie: `pocket_session=${session.token}` },
    });
    expect(res.status).toBe(403);
  });

  it("allows notes when trialing and mock checkout works", async () => {
    const app = createApp();
    const user = getOrCreateUser("cara@example.com");
    const session = createSession(user.id);

    const checkout = await app.request("http://localhost/v1/billing/checkout", {
      method: "POST",
      headers: { cookie: `pocket_session=${session.token}` },
    });
    expect(checkout.status).toBe(200);
    const { url } = await checkout.json();
    expect(url).toContain("/billing/success");

    const notes = await app.request("http://localhost/v1/notes", {
      headers: { cookie: `pocket_session=${session.token}` },
    });
    expect(notes.status).toBe(200);

    const create = await app.request("http://localhost/v1/notes", {
      method: "POST",
      headers: {
        cookie: `pocket_session=${session.token}`,
        "content-type": "application/json",
      },
      body: JSON.stringify({ title: "Hello", body: "World", updated_by_device_id: "pwa" }),
    });
    expect(create.status).toBe(201);
  });

  it("device pair session → claim", async () => {
    const app = createApp();
    const code = generatePairCode();
    const create = await app.request("http://localhost/v1/pair/sessions", {
      method: "POST",
      headers: {
        "content-type": "application/json",
        authorization: "Bearer dev-device-api-key",
      },
      body: JSON.stringify({
        device_id: "hw-001",
        code_public: code,
        expires_at: new Date(Date.now() + 600_000).toISOString(),
      }),
    });
    expect(create.status).toBe(201);

    const status = await app.request(`http://localhost/v1/pair/sessions/${code}`);
    expect((await status.json()).status).toBe("pending");

    const user = getOrCreateUser("pair@example.com");
    const session = createSession(user.id);
    const claim = await app.request("http://localhost/v1/pair/claim", {
      method: "POST",
      headers: {
        cookie: `pocket_session=${session.token}`,
        "content-type": "application/json",
      },
      body: JSON.stringify({ code }),
    });
    expect(claim.status).toBe(200);
    const body = await claim.json();
    expect(body.device_id).toBe("hw-001");
    expect(body.device_name).toBe("Pocket");
  });

  it("maps entitlement after subscription mirror upsert", async () => {
    const user = getOrCreateUser("ent@example.com");
    markTrialConsumed(user.id);
    upsertSubscriptionMirror(user.id, {
      status: "active",
      current_period_end: new Date(Date.now() + 86400000).toISOString(),
    });
    const app = createApp();
    const session = createSession(user.id);
    const res = await app.request("http://localhost/v1/entitlement", {
      headers: { cookie: `pocket_session=${session.token}` },
    });
    const body = await res.json();
    expect(body.entitled).toBe(true);
    expect(body.status).toBe("active");
    expect(body.trial_consumed).toBe(true);
  });
});
