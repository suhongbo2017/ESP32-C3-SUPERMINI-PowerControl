import initSqlJs from "sql.js";
import type { Database, SqlJsStatic } from "sql.js";
import type { PairingRepository } from "./repository.js";
import { mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import type { DeviceRecord, PairingCodeRecord } from "./repository.js";

type AuditEvent = { kind: string; deviceId?: string; occurredAt: string };

export class SqlitePairingRepository implements PairingRepository {
  private constructor(private readonly SQL: SqlJsStatic, private readonly db: Database, private readonly filePath: string) {}

  static async open(filePath: string): Promise<SqlitePairingRepository> {
    const SQL = await initSqlJs();
    let db: Database;
    try {
      db = new SQL.Database(new Uint8Array(readFileSync(filePath)));
    } catch {
      db = new SQL.Database();
    }
    const repository = new SqlitePairingRepository(SQL, db, filePath);
    repository.migrate();
    return repository;
  }

  close(): void {
    this.persist();
    this.db.close();
  }

  createPairingCode(record: PairingCodeRecord): void {
    this.db.run("INSERT INTO pairing_codes(id, code_hash, expires_at, used_at, created_at) VALUES (?, ?, ?, ?, ?)",
      [record.id, record.codeHash, record.expiresAt, record.usedAt, record.createdAt]);
    this.persist();
  }

  findActivePairingCodes(now: string): PairingCodeRecord[] {
    const rows = this.all("SELECT id, code_hash, expires_at, used_at, created_at FROM pairing_codes WHERE used_at IS NULL AND expires_at > ?", [now]);
    return rows.map((row) => ({ id: String(row.id), codeHash: String(row.code_hash), expiresAt: String(row.expires_at), usedAt: null, createdAt: String(row.created_at) }));
  }

  completePairing(codeId: string, usedAt: string, device: DeviceRecord): boolean {
    this.db.run("BEGIN IMMEDIATE");
    try {
      this.db.run("UPDATE pairing_codes SET used_at = ? WHERE id = ? AND used_at IS NULL AND expires_at > ?", [usedAt, codeId, usedAt]);
      if (this.db.getRowsModified() !== 1) {
        this.db.run("ROLLBACK");
        return false;
      }
      this.db.run("INSERT INTO devices(device_id, credential_hash, paired_at, revoked_at) VALUES (?, ?, ?, ?) ON CONFLICT(device_id) DO UPDATE SET credential_hash=excluded.credential_hash, paired_at=excluded.paired_at, revoked_at=NULL",
        [device.deviceId, device.credentialHash, device.pairedAt, device.revokedAt]);
      this.db.run("COMMIT");
      this.persist();
      return true;
    } catch (error) {
      this.db.run("ROLLBACK");
      throw error;
    }
  }

  findDevice(deviceId: string): DeviceRecord | null {
    const row = this.one("SELECT device_id, credential_hash, paired_at, revoked_at FROM devices WHERE device_id = ?", [deviceId]);
    return row ? { deviceId: String(row.device_id), credentialHash: String(row.credential_hash), pairedAt: String(row.paired_at), revokedAt: row.revoked_at === null ? null : String(row.revoked_at) } : null;
  }

  revokeDevice(deviceId: string, revokedAt: string): boolean {
    this.db.run("UPDATE devices SET revoked_at = ? WHERE device_id = ? AND revoked_at IS NULL", [revokedAt, deviceId]);
    const changed = this.db.getRowsModified() === 1;
    if (changed) this.persist();
    return changed;
  }

  recordAudit(event: AuditEvent): void {
    this.db.run("INSERT INTO audit_events(kind, device_id, occurred_at) VALUES (?, ?, ?)", [event.kind, event.deviceId ?? null, event.occurredAt]);
    this.persist();
  }

  private migrate(): void {
    this.db.run(`CREATE TABLE IF NOT EXISTS schema_migrations (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS pairing_codes (id TEXT PRIMARY KEY, code_hash TEXT NOT NULL, expires_at TEXT NOT NULL, used_at TEXT, created_at TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS devices (device_id TEXT PRIMARY KEY, credential_hash TEXT NOT NULL, paired_at TEXT NOT NULL, revoked_at TEXT);
      CREATE TABLE IF NOT EXISTS audit_events (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, device_id TEXT, occurred_at TEXT NOT NULL);
      INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES (1, datetime('now'));`);
    this.persist();
  }

  private all(sql: string, params: Array<string | null>): Array<Record<string, unknown>> {
    const statement = this.db.prepare(sql);
    try {
      statement.bind(params);
      const rows: Array<Record<string, unknown>> = [];
      while (statement.step()) rows.push(statement.getAsObject());
      return rows;
    } finally { statement.free(); }
  }

  private one(sql: string, params: string[]): Record<string, unknown> | null {
    return this.all(sql, params)[0] ?? null;
  }

  private persist(): void {
    mkdirSync(dirname(this.filePath), { recursive: true });
    const tempPath = `${this.filePath}.tmp`;
    writeFileSync(tempPath, Buffer.from(this.db.export()), { mode: 0o600 });
    renameSync(tempPath, this.filePath);
  }
}
