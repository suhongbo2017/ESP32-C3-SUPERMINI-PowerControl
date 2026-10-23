import assert from "node:assert/strict";
import test from "node:test";
import { buildServer } from "./server.js";
import { PairingService } from "./pairing/service.js";
import { SqlitePairingRepository } from "./pairing/sqlite-repository.js";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const adminToken = "test-admin-token-which-is-at-least-32-characters-long";

test("admin pairing API is authenticated and a paired device credential can be revoked", async () => {
  const dir = mkdtempSync(join(tmpdir(), "pc-power-cloud-"));
  const repository = await SqlitePairingRepository.open(join(dir, "test.sqlite"));
  const app = buildServer(new PairingService(repository), repository, adminToken);
  try {
    const denied = await app.inject({ method: "POST", url: "/api/admin/pairing-codes" });
    assert.equal(denied.statusCode, 401);

    const created = await app.inject({ method: "POST", url: "/api/admin/pairing-codes", headers: { authorization: `Bearer ${adminToken}` } });
    assert.equal(created.statusCode, 201);
    const code = created.json<{ pairingCode: string; expiresAt: string }>();

    const paired = await app.inject({ method: "POST", url: "/api/device/pair", payload: { protocolVersion: 1, deviceId: "c3-001", pairingCode: code.pairingCode } });
    assert.equal(paired.statusCode, 201);
    assert.equal(paired.json<{ deviceId: string }>().deviceId, "c3-001");

    const repeated = await app.inject({ method: "POST", url: "/api/device/pair", payload: { protocolVersion: 1, deviceId: "c3-002", pairingCode: code.pairingCode } });
    assert.equal(repeated.statusCode, 401);

    const revoked = await app.inject({ method: "DELETE", url: "/api/admin/devices/c3-001/credential", headers: { authorization: `Bearer ${adminToken}` } });
    assert.equal(revoked.statusCode, 204);
  } finally {
    await app.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
