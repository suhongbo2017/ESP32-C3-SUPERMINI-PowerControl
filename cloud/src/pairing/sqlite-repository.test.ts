import assert from "node:assert/strict";
import test from "node:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { SqlitePairingRepository } from "./sqlite-repository.js";

 test("pairing state survives reopening the SQLite database", async () => {
  const dir = mkdtempSync(join(tmpdir(), "pc-power-db-"));
  const path = join(dir, "state.sqlite");
  try {
    const first = await SqlitePairingRepository.open(path);
    first.createPairingCode({ id: "pair-1", codeHash: "salt:digest", expiresAt: "2026-10-24T00:00:00.000Z", usedAt: null, createdAt: "2026-10-23T00:00:00.000Z" });
    first.close();

    const second = await SqlitePairingRepository.open(path);
    assert.equal(second.findActivePairingCodes("2026-10-23T01:00:00.000Z").length, 1);
    assert.equal(second.completePairing("pair-1", "2026-10-23T01:00:00.000Z", {
      deviceId: "c3-001", credentialHash: "salt:credential", pairedAt: "2026-10-23T01:00:00.000Z", revokedAt: null,
    }), true);
    second.close();

    const third = await SqlitePairingRepository.open(path);
    assert.equal(third.findActivePairingCodes("2026-10-23T02:00:00.000Z").length, 0);
    assert.equal(third.findDevice("c3-001")?.credentialHash, "salt:credential");
    third.close();
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
