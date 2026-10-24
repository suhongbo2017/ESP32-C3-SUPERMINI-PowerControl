import initSqlJs from "sql.js";
import type { Database, SqlJsStatic } from "sql.js";
import type { PairingRepository } from "./repository.js";
import { mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import type { CommandRecord, DeviceDiagnostics, DeviceRecord, PairingCodeRecord } from "./repository.js";
import type { DashboardUser } from "../auth.js";

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
      this.db.run("INSERT INTO devices(device_id, credential_hash, paired_at, revoked_at, last_seen_at) VALUES (?, ?, ?, ?, ?) ON CONFLICT(device_id) DO UPDATE SET credential_hash=excluded.credential_hash, paired_at=excluded.paired_at, revoked_at=NULL, last_seen_at=NULL",
        [device.deviceId, device.credentialHash, device.pairedAt, device.revokedAt, device.lastSeenAt ?? null]);
      this.db.run("COMMIT");
      this.persist();
      return true;
    } catch (error) {
      this.db.run("ROLLBACK");
      throw error;
    }
  }

  findDevice(deviceId: string): DeviceRecord | null {
    const row = this.one("SELECT device_id, credential_hash, paired_at, revoked_at, last_seen_at FROM devices WHERE device_id = ?", [deviceId]);
    return row ? { deviceId: String(row.device_id), credentialHash: String(row.credential_hash), pairedAt: String(row.paired_at), revokedAt: row.revoked_at === null ? null : String(row.revoked_at), lastSeenAt: row.last_seen_at === null ? null : String(row.last_seen_at), diagnostics: this.diagnosticsFromRow(row) } : null;
  }

  listDevices(): DeviceRecord[] {
    return this.all("SELECT device_id, credential_hash, paired_at, revoked_at, last_seen_at, diagnostics_json FROM devices ORDER BY device_id", []).map((row) => ({
      deviceId: String(row.device_id), credentialHash: String(row.credential_hash), pairedAt: String(row.paired_at),
      revokedAt: row.revoked_at === null ? null : String(row.revoked_at), lastSeenAt: row.last_seen_at === null ? null : String(row.last_seen_at),
      diagnostics: this.diagnosticsFromRow(row),
    }));
  }

  findUser(username: string): DashboardUser | null {
    const row = this.one("SELECT username, password_hash, enabled, created_at, updated_at FROM dashboard_users WHERE username = ?", [username]);
    return row ? { username: String(row.username), passwordHash: String(row.password_hash), enabled: Boolean(row.enabled), createdAt: String(row.created_at), updatedAt: String(row.updated_at) } : null;
  }

  listUsers(): Array<Omit<DashboardUser, "passwordHash">> {
    return this.all("SELECT username, enabled, created_at, updated_at FROM dashboard_users ORDER BY username", []).map((row) => ({ username: String(row.username), enabled: Boolean(row.enabled), createdAt: String(row.created_at), updatedAt: String(row.updated_at) }));
  }

  upsertUser(user: DashboardUser): void {
    this.db.run("INSERT INTO dashboard_users(username, password_hash, enabled, created_at, updated_at) VALUES (?, ?, ?, ?, ?) ON CONFLICT(username) DO UPDATE SET password_hash=excluded.password_hash, enabled=excluded.enabled, updated_at=excluded.updated_at", [user.username, user.passwordHash, user.enabled ? 1 : 0, user.createdAt, user.updatedAt]);
    this.persist();
  }

  setUserEnabled(username: string, enabled: boolean, updatedAt: string): boolean {
    this.db.run("UPDATE dashboard_users SET enabled = ?, updated_at = ? WHERE username = ?", [enabled ? 1 : 0, updatedAt, username]);
    const changed = this.db.getRowsModified() === 1;
    if (changed) this.persist();
    return changed;
  }

  deleteUser(username: string): boolean {
    this.db.run("DELETE FROM dashboard_users WHERE username = ?", [username]);
    const changed = this.db.getRowsModified() === 1;
    if (changed) this.persist();
    return changed;
  }

  listCommands(deviceId?: string, limit = 50): CommandRecord[] {
    const rows = deviceId
      ? this.all("SELECT * FROM commands WHERE device_id = ? ORDER BY issued_at DESC LIMIT ?", [deviceId, String(limit)])
      : this.all("SELECT * FROM commands ORDER BY issued_at DESC LIMIT ?", [String(limit)]);
    return rows.map((row) => this.commandFromRow(row)!).filter(Boolean);
  }

  markDeviceSeen(deviceId: string, seenAt: string, diagnostics?: DeviceDiagnostics): boolean {
    this.db.run("UPDATE devices SET last_seen_at = ?, diagnostics_json = ? WHERE device_id = ? AND revoked_at IS NULL", [seenAt, diagnostics ? JSON.stringify(diagnostics) : null, deviceId]);
    const changed = this.db.getRowsModified() === 1;
    if (changed) this.persist();
    return changed;
  }

  createCommand(record: CommandRecord): boolean {
    try {
      this.db.run("INSERT INTO commands(command_id, device_id, idempotency_key, status, issued_at, expires_at, claimed_at, completed_at, error_code) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", [record.commandId, record.deviceId, record.idempotencyKey, record.status, record.issuedAt, record.expiresAt, record.claimedAt, record.completedAt, record.errorCode]);
      this.persist();
      return true;
    } catch (error) {
      if (String(error).includes("UNIQUE")) return false;
      throw error;
    }
  }

  findCommandByIdempotency(deviceId: string, idempotencyKey: string): CommandRecord | null {
    return this.commandFromRow(this.one("SELECT * FROM commands WHERE device_id = ? AND idempotency_key = ?", [deviceId, idempotencyKey]));
  }

  claimNextCommand(deviceId: string, now: string): CommandRecord | null {
    this.db.run("BEGIN IMMEDIATE");
    try {
      this.db.run("UPDATE commands SET status = 'expired' WHERE device_id = ? AND status = 'queued' AND expires_at <= ?", [deviceId, now]);
      const row = this.one("SELECT * FROM commands WHERE device_id = ? AND status = 'queued' AND expires_at > ? ORDER BY issued_at LIMIT 1", [deviceId, now]);
      if (!row) { this.db.run("COMMIT"); this.persist(); return null; }
      this.db.run("UPDATE commands SET status = 'claimed', claimed_at = ? WHERE command_id = ? AND status = 'queued'", [now, String(row.command_id)]);
      this.db.run("COMMIT");
      this.persist();
      return this.commandFromRow({ ...row, status: "claimed", claimed_at: now });
    } catch (error) { this.db.run("ROLLBACK"); throw error; }
  }

  completeCommand(commandId: string, status: "executed" | "failed" | "rejected", completedAt: string, errorCode: string | null): boolean {
    this.db.run("UPDATE commands SET status = ?, completed_at = ?, error_code = ? WHERE command_id = ? AND status = 'claimed'", [status, completedAt, errorCode, commandId]);
    const changed = this.db.getRowsModified() === 1;
    if (changed) this.persist();
    return changed;
  }

  findCommand(commandId: string): CommandRecord | null {
    return this.commandFromRow(this.one("SELECT * FROM commands WHERE command_id = ?", [commandId]));
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
      CREATE TABLE IF NOT EXISTS devices (device_id TEXT PRIMARY KEY, credential_hash TEXT NOT NULL, paired_at TEXT NOT NULL, revoked_at TEXT, last_seen_at TEXT, diagnostics_json TEXT);
      CREATE TABLE IF NOT EXISTS commands (command_id TEXT PRIMARY KEY, device_id TEXT NOT NULL, idempotency_key TEXT NOT NULL, status TEXT NOT NULL, issued_at TEXT NOT NULL, expires_at TEXT NOT NULL, claimed_at TEXT, completed_at TEXT, error_code TEXT, UNIQUE(device_id, idempotency_key));
      CREATE TABLE IF NOT EXISTS audit_events (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, device_id TEXT, occurred_at TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS dashboard_users (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL, enabled INTEGER NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL);
      INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES (1, datetime('now'));`);
    try { this.db.run("ALTER TABLE devices ADD COLUMN last_seen_at TEXT"); } catch { /* existing database already migrated */ }
    try { this.db.run("ALTER TABLE devices ADD COLUMN diagnostics_json TEXT"); } catch { /* existing database already migrated */ }
    this.persist();
  }

  private diagnosticsFromRow(row: Record<string, unknown>): DeviceDiagnostics | null {
    if (typeof row.diagnostics_json !== "string") return null;
    try { return JSON.parse(row.diagnostics_json) as DeviceDiagnostics; } catch { return null; }
  }

  private commandFromRow(row: Record<string, unknown> | null): CommandRecord | null {
    if (!row) return null;
    return { commandId: String(row.command_id), deviceId: String(row.device_id), idempotencyKey: String(row.idempotency_key), status: String(row.status) as CommandRecord["status"], issuedAt: String(row.issued_at), expiresAt: String(row.expires_at), claimedAt: row.claimed_at === null ? null : String(row.claimed_at), completedAt: row.completed_at === null ? null : String(row.completed_at), errorCode: row.error_code === null ? null : String(row.error_code) };
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
