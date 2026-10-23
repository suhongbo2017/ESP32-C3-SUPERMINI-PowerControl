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
};

export interface PairingRepository {
  createPairingCode(record: PairingCodeRecord): void;
  findActivePairingCodes(now: string): PairingCodeRecord[];
  completePairing(codeId: string, usedAt: string, device: DeviceRecord): boolean;
  findDevice(deviceId: string): DeviceRecord | null;
  revokeDevice(deviceId: string, revokedAt: string): boolean;
  recordAudit(event: { kind: string; deviceId?: string; occurredAt: string }): void;
}
