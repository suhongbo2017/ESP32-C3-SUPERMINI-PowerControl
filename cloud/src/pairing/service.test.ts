import assert from "node:assert/strict";
import test from "node:test";
import { PairingService } from "./service.js";
import type { DeviceRecord, PairingCodeRecord, PairingRepository } from "./repository.js";

class MemoryRepository implements PairingRepository {
  codes = new Map<string, PairingCodeRecord>();
  devices = new Map<string, DeviceRecord>();
  audits: Array<{ kind: string; deviceId?: string; occurredAt: string }> = [];

  createPairingCode(record: PairingCodeRecord): void { this.codes.set(record.id, record); }
  findActivePairingCodes(now: string): PairingCodeRecord[] {
    return [...this.codes.values()].filter((code) => code.usedAt === null && code.expiresAt > now);
  }
  completePairing(id: string, usedAt: string, device: DeviceRecord): boolean {
    const record = this.codes.get(id);
    if (!record || record.usedAt || record.expiresAt <= usedAt) return false;
    this.codes.set(id, { ...record, usedAt });
    this.devices.set(device.deviceId, device);
    return true;
  }
  findDevice(id: string): DeviceRecord | null { return this.devices.get(id) ?? null; }
  revokeDevice(id: string, revokedAt: string): boolean {
    const record = this.devices.get(id);
    if (!record || record.revokedAt) return false;
    this.devices.set(id, { ...record, revokedAt });
    return true;
  }
  recordAudit(event: { kind: string; deviceId?: string; occurredAt: string }): void { this.audits.push(event); }
}

test("pairing codes are one-time, credentials are not stored in plaintext, and revocation invalidates them", async () => {
  let now = Date.parse("2026-10-23T00:00:00.000Z");
  const repository = new MemoryRepository();
  const service = new PairingService(repository, () => now);
  const created = await service.createPairingCode();

  assert.equal(created.expiresAt, new Date(now + 10 * 60_000).toISOString());
  const storedCode = [...repository.codes.values()][0]!;
  assert.notEqual(storedCode.codeHash, created.pairingCode);

  const paired = await service.pairDevice({ protocolVersion: 1, deviceId: "c3-001", pairingCode: created.pairingCode });
  assert.ok(paired.credential.length >= 40);
  assert.notEqual(repository.devices.get("c3-001")?.credentialHash, paired.credential);
  await assert.rejects(() => service.pairDevice({ protocolVersion: 1, deviceId: "c3-002", pairingCode: created.pairingCode }), { statusCode: 401 });

  assert.equal(service.authenticateDevice("c3-001", paired.credential), true);
  assert.equal(service.revokeDevice("c3-001"), true);
  assert.equal(service.authenticateDevice("c3-001", paired.credential), false);
  assert.ok(repository.audits.some((event) => event.kind === "device.paired"));
  assert.ok(repository.audits.some((event) => event.kind === "device.revoked"));

  now += 1;
});

test("expired pairing codes cannot be consumed", async () => {
  let now = Date.parse("2026-10-23T00:00:00.000Z");
  const repository = new MemoryRepository();
  const service = new PairingService(repository, () => now);
  const created = await service.createPairingCode();
  now += 10 * 60_000;
  await assert.rejects(() => service.pairDevice({ protocolVersion: 1, deviceId: "c3-001", pairingCode: created.pairingCode }), { statusCode: 401 });
});
