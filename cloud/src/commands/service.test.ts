import assert from "node:assert/strict";
import test from "node:test";
import { CommandService, CommandError } from "./service.js";
import type { CommandRecord, DeviceRecord, PairingRepository } from "../pairing/repository.js";

class CommandsMemory implements PairingRepository {
  devices = new Map<string, DeviceRecord>();
  commands = new Map<string, CommandRecord>();
  audits: Array<{ kind: string; deviceId?: string; occurredAt: string }> = [];
  createPairingCode(): void {} findActivePairingCodes(): never[] { return []; } completePairing(): boolean { return false; }
  findDevice(id: string): DeviceRecord | null { return this.devices.get(id) ?? null; }
  revokeDevice(): boolean { return false; } recordAudit(event: { kind: string; deviceId?: string; occurredAt: string }): void { this.audits.push(event); }
  markDeviceSeen(id: string, seenAt: string): boolean { const d = this.devices.get(id); if (!d) return false; this.devices.set(id, { ...d, lastSeenAt: seenAt }); return true; }
  createCommand(record: CommandRecord): boolean { if (this.findCommandByIdempotency(record.deviceId, record.idempotencyKey)) return false; this.commands.set(record.commandId, record); return true; }
  findCommandByIdempotency(deviceId: string, key: string): CommandRecord | null { return [...this.commands.values()].find((c) => c.deviceId === deviceId && c.idempotencyKey === key) ?? null; }
  claimNextCommand(id: string, now: string): CommandRecord | null { const c = [...this.commands.values()].find((item) => item.deviceId === id && item.status === "queued" && item.expiresAt > now); if (!c) return null; const result = { ...c, status: "claimed" as const, claimedAt: now }; this.commands.set(c.commandId, result); return result; }
  completeCommand(id: string, status: "executed" | "failed" | "rejected", completedAt: string, errorCode: string | null): boolean { const c = this.commands.get(id); if (!c || c.status !== "claimed") return false; this.commands.set(id, { ...c, status, completedAt, errorCode }); return true; }
  findCommand(id: string): CommandRecord | null { return this.commands.get(id) ?? null; }
}

test("command service rejects offline devices, deduplicates, claims once, and records result", () => {
  let now = Date.parse("2026-10-23T00:00:00.000Z");
  const repository = new CommandsMemory();
  repository.devices.set("c3-001", { deviceId: "c3-001", credentialHash: "x", pairedAt: new Date(now).toISOString(), revokedAt: null, lastSeenAt: new Date(now).toISOString() });
  const service = new CommandService(repository, () => now);
  const first = service.createPowerPress("c3-001", "request-1");
  assert.equal(service.createPowerPress("c3-001", "request-1").commandId, first.commandId);
  assert.equal(service.claim("c3-001")?.commandId, first.commandId);
  assert.equal(service.claim("c3-001"), null);
  assert.equal(service.complete("c3-001", { protocolVersion: 1, commandId: first.commandId, status: "executed", completedAt: new Date(now).toISOString() }), true);
  assert.equal(repository.findCommand(first.commandId)?.status, "executed");
  now += 46_000;
  assert.throws(() => service.createPowerPress("c3-001", "request-2"), CommandError);
});
