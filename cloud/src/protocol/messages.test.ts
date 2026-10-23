import assert from "node:assert/strict";
import test from "node:test";
import {
  validateCommand,
  validateCommandResult,
  validateDevicePoll,
  validatePairingRequest,
  validateReachabilityEvent,
} from "./messages.js";

test("accepts a valid one-time pairing request", () => {
  assert.equal(
    validatePairingRequest({
      protocolVersion: 1,
      deviceId: "c3-001",
      pairingCode: "AbCdEf0123456789",
    }),
    true,
  );
});

test("rejects a pairing request with an empty code", () => {
  assert.equal(
    validatePairingRequest({ protocolVersion: 1, deviceId: "c3-001", pairingCode: "" }),
    false,
  );
});

test("accepts a device poll with reported availability and reachability", () => {
  assert.equal(
    validateDevicePoll({
      protocolVersion: 1,
      deviceId: "c3-001",
      reportedAt: "2026-10-20T12:00:00.000Z",
      reachability: "reachable",
    }),
    true,
  );
});

test("rejects a poll that attempts to infer PC power from ping", () => {
  assert.equal(
    validateDevicePoll({
      protocolVersion: 1,
      deviceId: "c3-001",
      reportedAt: "2026-10-20T12:00:00.000Z",
      reachability: "reachable",
      pcPowerState: "on",
    }),
    false,
  );
});

test("accepts only a bounded power-press command", () => {
  assert.equal(
    validateCommand({
      commandId: "00000000-0000-4000-8000-000000000001",
      kind: "power_press",
      issuedAt: "2026-10-20T12:00:00.000Z",
      expiresAt: "2026-10-20T12:00:15.000Z",
    }),
    true,
  );
  assert.equal(
    validateCommand({
      commandId: "00000000-0000-4000-8000-000000000001",
      kind: "force_shutdown",
      issuedAt: "2026-10-20T12:00:00.000Z",
      expiresAt: "2026-10-20T12:00:15.000Z",
    }),
    false,
  );
});

test("accepts command execution acknowledgements without a PC power claim", () => {
  assert.equal(
    validateCommandResult({
      protocolVersion: 1,
      commandId: "00000000-0000-4000-8000-000000000001",
      status: "executed",
      completedAt: "2026-10-20T12:00:00.300Z",
    }),
    true,
  );
  assert.equal(
    validateCommandResult({
      protocolVersion: 1,
      commandId: "00000000-0000-4000-8000-000000000001",
      status: "executed",
      completedAt: "2026-10-20T12:00:00.300Z",
      pcPowerState: "on",
    }),
    false,
  );
});

test("accepts reachability diagnostic events", () => {
  assert.equal(
    validateReachabilityEvent({
      protocolVersion: 1,
      eventId: "00000000-0000-4000-8000-000000000002",
      kind: "reachability",
      occurredAt: "2026-10-20T12:00:00.000Z",
      reachability: "unknown",
    }),
    true,
  );
});
