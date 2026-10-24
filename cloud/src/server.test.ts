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

    const seen = await app.inject({ method: "POST", url: "/api/device/c3-001/seen", headers: { authorization: `Bearer ${paired.json<{ credential: string }>().credential}` } });
    assert.equal(seen.statusCode, 200);

    assert.equal((await app.inject({ method: "GET", url: "/api/dashboard/devices" })).statusCode, 401);
    const login = await app.inject({ method: "POST", url: "/api/dashboard/login", payload: { token: adminToken } });
    assert.equal(login.statusCode, 200);
    const cookie = login.cookies[0]!;
    assert.equal(cookie.httpOnly, true);
    assert.equal(cookie.secure, true);
    const csrfToken = login.json<{ csrfToken: string }>().csrfToken;
    const headers = { cookie: `${cookie.name}=${cookie.value}`, "x-csrf-token": csrfToken };
    const devices = await app.inject({ method: "GET", url: "/api/dashboard/devices", headers });
    assert.equal(devices.json<Array<{ online: boolean }>>()[0]?.online, true);
    const createdCommand = await app.inject({ method: "POST", url: "/api/dashboard/devices/c3-001/power-press", headers, payload: {} });
    assert.equal(createdCommand.statusCode, 201);
    const deviceAuth = { authorization: `Bearer ${paired.json<{ credential: string }>().credential}` };
    const next = await app.inject({ method: "GET", url: "/api/device/c3-001/commands/next", headers: deviceAuth });
    assert.equal(next.statusCode, 200);
    assert.equal(next.json<{ command: { kind: string } }>().command.kind, "power_press");

    const revoked = await app.inject({ method: "DELETE", url: "/api/admin/devices/c3-001/credential", headers: { authorization: `Bearer ${adminToken}` } });
    assert.equal(revoked.statusCode, 204);
  } finally {
    await app.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test("dashboard users can be created and authenticate", async () => {
  const dir = mkdtempSync(join(tmpdir(), "pc-power-cloud-users-"));
  const repository = await SqlitePairingRepository.open(join(dir, "test.sqlite"));
  const app = buildServer(new PairingService(repository), repository, adminToken);
  try {
    const login = await app.inject({ method: "POST", url: "/api/dashboard/login", payload: { token: adminToken } });
    const cookie = login.cookies[0]!;
    const csrfToken = login.json<{ csrfToken: string }>().csrfToken;
    const headers = { cookie: `${cookie.name}=${cookie.value}`, "x-csrf-token": csrfToken };
    const created = await app.inject({ method: "POST", url: "/api/dashboard/users", headers, payload: { username: "operator", password: "correct-horse-battery" } });
    assert.equal(created.statusCode, 201, created.body);
    const userLogin = await app.inject({ method: "POST", url: "/api/dashboard/login", payload: { username: "operator", password: "correct-horse-battery" } });
    assert.equal(userLogin.statusCode, 200, userLogin.body);
    const users = await app.inject({ method: "GET", url: "/api/dashboard/users", headers });
    assert.equal(users.statusCode, 200);
    assert.equal(users.json<Array<{ username: string }>>()[0]?.username, "operator");
  } finally {
    await app.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
