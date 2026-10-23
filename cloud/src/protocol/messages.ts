export const protocolVersion = 1 as const;

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const isoDatePattern = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/;
const reachabilityValues = new Set(["reachable", "unreachable", "unknown"]);

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasProtocolVersion(value: Record<string, unknown>): boolean {
  return value.protocolVersion === protocolVersion;
}

function hasDeviceId(value: Record<string, unknown>): boolean {
  return typeof value.deviceId === "string" && /^[a-z0-9][a-z0-9-]{2,63}$/.test(value.deviceId);
}

function hasIsoDate(value: unknown): value is string {
  return typeof value === "string" && isoDatePattern.test(value) && !Number.isNaN(Date.parse(value));
}

export function validatePairingRequest(value: unknown): value is {
  protocolVersion: 1;
  deviceId: string;
  pairingCode: string;
} {
  if (!isRecord(value) || !hasProtocolVersion(value) || !hasDeviceId(value)) return false;
  return typeof value.pairingCode === "string" && /^[A-Za-z0-9]{16,64}$/.test(value.pairingCode);
}

export function validateDevicePoll(value: unknown): value is {
  protocolVersion: 1;
  deviceId: string;
  reportedAt: string;
  reachability: "reachable" | "unreachable" | "unknown";
} {
  if (!isRecord(value) || !hasProtocolVersion(value) || !hasDeviceId(value)) return false;
  const keys = Object.keys(value).sort().join(",");
  if (keys !== "deviceId,protocolVersion,reachability,reportedAt") return false;
  return hasIsoDate(value.reportedAt) && typeof value.reachability === "string" && reachabilityValues.has(value.reachability);
}

export function validateCommand(value: unknown): value is {
  commandId: string;
  kind: "power_press";
  issuedAt: string;
  expiresAt: string;
} {
  if (!isRecord(value) || typeof value.commandId !== "string" || !uuidPattern.test(value.commandId)) return false;
  if (value.kind !== "power_press" || !hasIsoDate(value.issuedAt) || !hasIsoDate(value.expiresAt)) return false;
  return Date.parse(value.expiresAt) > Date.parse(value.issuedAt);
}

export function validateCommandResult(value: unknown): value is {
  protocolVersion: 1;
  commandId: string;
  status: "executed" | "failed" | "rejected";
  completedAt: string;
  errorCode?: string;
} {
  if (!isRecord(value) || !hasProtocolVersion(value)) return false;
  if (typeof value.commandId !== "string" || !uuidPattern.test(value.commandId)) return false;
  if (!["executed", "failed", "rejected"].includes(String(value.status)) || !hasIsoDate(value.completedAt)) return false;
  if ("pcPowerState" in value) return false;
  return !("errorCode" in value) || typeof value.errorCode === "string";
}

export function validateReachabilityEvent(value: unknown): value is {
  protocolVersion: 1;
  eventId: string;
  kind: "reachability";
  occurredAt: string;
  reachability: "reachable" | "unreachable" | "unknown";
} {
  if (!isRecord(value) || !hasProtocolVersion(value)) return false;
  return typeof value.eventId === "string" && uuidPattern.test(value.eventId)
    && value.kind === "reachability"
    && hasIsoDate(value.occurredAt)
    && typeof value.reachability === "string"
    && reachabilityValues.has(value.reachability);
}
