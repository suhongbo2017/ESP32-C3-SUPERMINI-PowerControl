import { randomBytes, scryptSync, timingSafeEqual } from "node:crypto";

export type DashboardUser = {
  username: string;
  passwordHash: string;
  enabled: boolean;
  createdAt: string;
  updatedAt: string;
};

export function hashPassword(password: string): string {
  const salt = randomBytes(16);
  const derived = scryptSync(password, salt, 32, { N: 16_384, r: 8, p: 1 });
  return `scrypt$${salt.toString("base64url")}$${derived.toString("base64url")}`;
}

export function verifyPassword(password: string, encoded: string): boolean {
  const parts = encoded.split("$");
  if (parts.length !== 3 || parts[0] !== "scrypt") return false;
  try {
    const saltText = parts[1];
    const expectedText = parts[2];
    if (!saltText || !expectedText) return false;
    const salt = Buffer.from(saltText, "base64url");
    const expected = Buffer.from(expectedText, "base64url");
    const actual = scryptSync(password, salt, expected.length, { N: 16_384, r: 8, p: 1 });
    return expected.length === actual.length && timingSafeEqual(expected, actual);
  } catch {
    return false;
  }
}

export function validUsername(username: string): boolean {
  return /^[a-zA-Z0-9._-]{3,40}$/.test(username);
}
