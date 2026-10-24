import { randomUUID } from "node:crypto";
import { validateCommandResult } from "../protocol/messages.js";
import type { CommandRecord, DeviceDiagnostics, PairingRepository } from "../pairing/repository.js";

const onlineWindowMs = 45_000;
const commandLifetimeMs = 15_000;

export class CommandError extends Error {
  constructor(public readonly statusCode: 400 | 404 | 409, message: string) { super(message); }
}

export class CommandService {
  constructor(private readonly repository: PairingRepository, private readonly clock: () => number = Date.now) {}

  createPowerPress(deviceId: string, idempotencyKey: string): CommandRecord {
    const now = new Date(this.clock()).toISOString();
    const existing = this.repository.findCommandByIdempotency(deviceId, idempotencyKey);
    if (existing) return existing;
    const device = this.repository.findDevice(deviceId);
    if (!device || device.revokedAt) throw new CommandError(404, "device not found");
    if (!device.lastSeenAt || this.clock() - Date.parse(device.lastSeenAt) > onlineWindowMs) {
      throw new CommandError(409, "device offline");
    }
    const record: CommandRecord = {
      commandId: randomUUID(), deviceId, idempotencyKey, status: "queued",
      issuedAt: now, expiresAt: new Date(this.clock() + commandLifetimeMs).toISOString(),
      claimedAt: null, completedAt: null, errorCode: null,
    };
    if (!this.repository.createCommand(record)) {
      return this.repository.findCommandByIdempotency(deviceId, idempotencyKey) ?? record;
    }
    this.repository.recordAudit({ kind: "command.created", deviceId, occurredAt: now });
    return record;
  }

  reportSeen(deviceId: string, seenAt: string, diagnostics?: DeviceDiagnostics): boolean { return this.repository.markDeviceSeen(deviceId, seenAt, diagnostics); }

  claim(deviceId: string): CommandRecord | null {
    return this.repository.claimNextCommand(deviceId, new Date(this.clock()).toISOString());
  }

  complete(deviceId: string, body: unknown): boolean {
    if (!validateCommandResult(body)) throw new CommandError(400, "invalid command result");
    const command = this.repository.findCommand(body.commandId);
    if (!command || command.deviceId !== deviceId) throw new CommandError(404, "command not found");
    return this.repository.completeCommand(body.commandId, body.status, body.completedAt, body.errorCode ?? null);
  }
}
