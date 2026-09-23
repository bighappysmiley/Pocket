import { describe, expect, it } from "vitest";
import { incomingWinsLww, mergeListItems, normalizeUpdatedAt } from "../src/lib/lww.js";

describe("LWW conflict policy (Spec §9.3)", () => {
  it("prefers greater updated_at", () => {
    const existing = {
      updated_at: "2026-01-01T10:00:00.000Z",
      updated_by_device_id: "device-b",
    };
    const incoming = {
      updated_at: "2026-01-01T11:00:00.000Z",
      updated_by_device_id: "device-a",
    };
    expect(incomingWinsLww(incoming, existing)).toBe(true);
    expect(incomingWinsLww(existing, incoming)).toBe(false);
  });

  it("tie-breaks equal timestamps by higher updated_by_device_id", () => {
    const t = "2026-01-01T10:00:00.000Z";
    const a = { updated_at: t, updated_by_device_id: "aaaa" };
    const b = { updated_at: t, updated_by_device_id: "bbbb" };
    expect(incomingWinsLww(b, a)).toBe(true);
    expect(incomingWinsLww(a, b)).toBe(false);
  });

  it("compares pwa literal against device ids", () => {
    const t = "2026-01-01T10:00:00.000Z";
    const pwa = { updated_at: t, updated_by_device_id: "pwa" };
    const device = {
      updated_at: t,
      updated_by_device_id: "zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz",
    };
    expect(incomingWinsLww(device, pwa)).toBe(true);
  });

  it("lets a newer tombstone win over an older update", () => {
    const existing = {
      updated_at: "2026-01-01T10:00:00.000Z",
      updated_by_device_id: "device-a",
      deleted_at: null,
    };
    const tombstone = {
      updated_at: "2026-01-01T12:00:00.000Z",
      updated_by_device_id: "pwa",
      deleted_at: "2026-01-01T12:00:00.000Z",
    };
    expect(incomingWinsLww(tombstone, existing)).toBe(true);
  });

  it("merges list items with per-item LWW", () => {
    const existing = [
      { id: "1", text: "Milk", checked: false, updated_at: "2026-01-01T10:00:00.000Z" },
      { id: "2", text: "Eggs", checked: false, updated_at: "2026-01-01T10:00:00.000Z" },
    ];
    const incoming = [
      { id: "1", text: "Milk", checked: true, updated_at: "2026-01-01T11:00:00.000Z" },
      { id: "3", text: "Bread", checked: false, updated_at: "2026-01-01T11:00:00.000Z" },
    ];
    const merged = mergeListItems(existing, incoming);
    expect(merged).toHaveLength(3);
    expect(merged.find((i) => i.id === "1")?.checked).toBe(true);
    expect(merged.find((i) => i.id === "2")?.text).toBe("Eggs");
    expect(merged.find((i) => i.id === "3")?.text).toBe("Bread");
  });

  it("clamps updated_at more than 1h in the future", () => {
    const serverNow = new Date("2026-01-01T12:00:00.000Z");
    const farFuture = "2026-01-01T15:00:00.000Z";
    expect(normalizeUpdatedAt(farFuture, serverNow)).toBe(serverNow.toISOString());
    const nearFuture = "2026-01-01T12:30:00.000Z";
    expect(normalizeUpdatedAt(nearFuture, serverNow)).toBe(nearFuture);
  });
});
