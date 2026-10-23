export type PairingCodeRecord = {
  id: string;
  codeHash: string;
  expiresAt: string;
  usedAt: string | null;
  createdAt: string;
};

export type DeviceRecord = {
  deviceId: string;
  credentialHash: string;
  pairedAt: string;
  revokedAt: string | null;
  lastSeenAt?: string | null;
};

export type CommandRecord = {
  commandId: string;
  deviceId: string;
  idempotencyKey: string;
  status: "queued" | "claimed" | "executed" | "failed" | "rejected" | "expired" | "timed_out";
  issuedAt: string;
  expiresAt: string;
  claimedAt: string | null;
  completedAt: string | null;
  errorCode: string | null;
};

export interface PairingRepository {
  createPairingCode(record: PairingCodeRecord): void;
  findActivePairingCodes(now: string): PairingCodeRecord[];
  completePairing(codeId: string, usedAt: string, device: DeviceRecord): boolean;
  findDevice(deviceId: string): DeviceRecord | null;
  revokeDevice(deviceId: string, revokedAt: string): boolean;
  recordAudit(event: { kind: string; deviceId?: string; occurredAt: string }): void;
  markDeviceSeen(deviceId: string, seenAt: string): boolean;
  createCommand(record: CommandRecord): boolean;
  findCommandByIdempotency(deviceId: string, idempotencyKey: string): CommandRecord | null;
  claimNextCommand(deviceId: string, now: string): CommandRecord | null;
  completeCommand(commandId: string, status: "executed" | "failed" | "rejected", completedAt: string, errorCode: string | null): boolean;
  findCommand(commandId: string): CommandRecord | null;
}
