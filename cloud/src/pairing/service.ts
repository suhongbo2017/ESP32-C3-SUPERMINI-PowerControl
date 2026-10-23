import { createHash, randomBytes, timingSafeEqual } from "node:crypto";
import { validatePairingRequest } from "../protocol/messages.js";
import type { DeviceRecord, PairingCodeRecord, PairingRepository } from "./repository.js";

const pairingLifetimeMs = 10 * 60_000;

type Clock = () => number;

export class PairingError extends Error {
  constructor(public readonly statusCode: 400 | 401 | 404, message: string) {
    super(message);
  }
}

function hashSecret(secret: string, salt: string): string {
  return createHash("sha256").update(`${salt}:${secret}`).digest("hex");
}

function matchesSecret(secret: string, stored: string): boolean {
  const [salt, digest] = stored.split(":");
  if (!salt || !digest) return false;
  const actual = Buffer.from(hashSecret(secret, salt), "hex");
  const expected = Buffer.from(digest, "hex");
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}

function storeSecret(secret: string): string {
  const salt = randomBytes(16).toString("hex");
  return `${salt}:${hashSecret(secret, salt)}`;
}

export class PairingService {
  constructor(private readonly repository: PairingRepository, private readonly clock: Clock = Date.now) {}

  async createPairingCode(): Promise<{ pairingCode: string; expiresAt: string }> {
    const pairingCode = randomBytes(24).toString("hex");
    const createdAt = new Date(this.clock()).toISOString();
    const expiresAt = new Date(this.clock() + pairingLifetimeMs).toISOString();
    const record: PairingCodeRecord = {
      id: randomBytes(16).toString("hex"),
      codeHash: storeSecret(pairingCode),
      expiresAt,
      usedAt: null,
      createdAt,
    };
    this.repository.createPairingCode(record);
    this.repository.recordAudit({ kind: "pairing.created", occurredAt: createdAt });
    return { pairingCode, expiresAt };
  }

  async pairDevice(value: unknown): Promise<{ deviceId: string; credential: string }> {
    if (!validatePairingRequest(value)) throw new PairingError(400, "invalid pairing request");
    const now = new Date(this.clock()).toISOString();
    const code = this.repository.findActivePairingCodes(now)
      .find((candidate) => matchesSecret(value.pairingCode, candidate.codeHash));
    if (!code) {
      this.repository.recordAudit({ kind: "pairing.failed", deviceId: value.deviceId, occurredAt: now });
      throw new PairingError(401, "invalid or expired pairing code");
    }
    const credential = randomBytes(32).toString("base64url");
    const device: DeviceRecord = {
      deviceId: value.deviceId,
      credentialHash: storeSecret(credential),
      pairedAt: now,
      revokedAt: null,
    };
    if (!this.repository.completePairing(code.id, now, device)) {
      throw new PairingError(401, "pairing code already used");
    }
    this.repository.recordAudit({ kind: "device.paired", deviceId: value.deviceId, occurredAt: now });
    return { deviceId: value.deviceId, credential };
  }

  authenticateDevice(deviceId: string, credential: string): boolean {
    const device = this.repository.findDevice(deviceId);
    return Boolean(device && !device.revokedAt && matchesSecret(credential, device.credentialHash));
  }

  revokeDevice(deviceId: string): boolean {
    const revoked = this.repository.revokeDevice(deviceId, new Date(this.clock()).toISOString());
    if (revoked) this.repository.recordAudit({ kind: "device.revoked", deviceId, occurredAt: new Date(this.clock()).toISOString() });
    return revoked;
  }
}
