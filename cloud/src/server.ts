import Fastify from "fastify";
import crypto from "node:crypto";
import { CommandError, CommandService } from "./commands/service.js";
import type { FastifyInstance } from "fastify";
import { PairingError, PairingService } from "./pairing/service.js";
import type { PairingRepository } from "./pairing/repository.js";

export function buildServer(pairing: PairingService, repository: PairingRepository, adminToken: string): FastifyInstance {
  if (adminToken.length < 32) throw new Error("PC_POWER_ADMIN_TOKEN must contain at least 32 characters");
  const app = Fastify({ logger: { redact: ["req.headers.authorization", "req.body.pairingCode"] } });
  const commands = new CommandService(repository);
  const requireDevice = async (request: import("fastify").FastifyRequest<{ Params: { deviceId: string } }>, reply: import("fastify").FastifyReply) => {
    const authorization = request.headers.authorization;
    const credential = authorization?.startsWith("Bearer ") ? authorization.slice(7) : "";
    if (!pairing.authenticateDevice(request.params.deviceId, credential)) return reply.code(401).send({ error: "unauthorized" });
  };
  const requireAdmin = async (request: import("fastify").FastifyRequest, reply: import("fastify").FastifyReply) => {
    const authorization = request.headers.authorization;
    const expected = `Bearer ${adminToken}`;
    if (authorization !== expected) return reply.code(401).send({ error: "unauthorized" });
  };

  app.post("/api/device/pair", async (request, reply) => {
    try {
      const result = await pairing.pairDevice(request.body);
      return reply.code(201).send(result);
    } catch (error) {
      if (error instanceof PairingError) return reply.code(error.statusCode).send({ error: error.message });
      throw error;
    }
  });

  app.post("/api/admin/pairing-codes", { preHandler: requireAdmin }, async (_request, reply) => {
    return reply.code(201).send(await pairing.createPairingCode());
  });

  app.delete<{ Params: { deviceId: string } }>("/api/admin/devices/:deviceId/credential", { preHandler: requireAdmin }, async (request, reply) => {
    const revoked = pairing.revokeDevice(request.params.deviceId);
    return revoked ? reply.code(204).send() : reply.code(404).send({ error: "device not found" });
  });

  app.post<{ Params: { deviceId: string }; Body: { idempotencyKey?: string } }>("/api/admin/devices/:deviceId/power-press", { preHandler: requireAdmin }, async (request, reply) => {
    try {
      const idempotencyKey = request.body?.idempotencyKey ?? crypto.randomUUID();
      return reply.code(201).send(commands.createPowerPress(request.params.deviceId, idempotencyKey));
    } catch (error) {
      if (error instanceof CommandError) return reply.code(error.statusCode).send({ error: error.message });
      throw error;
    }
  });

  app.post<{ Params: { deviceId: string } }>("/api/device/:deviceId/seen", { preHandler: requireDevice }, async (request, reply) => {
    const seen = commands.reportSeen(request.params.deviceId, new Date().toISOString());
    return seen ? reply.send({ status: "ok" }) : reply.code(401).send({ error: "unauthorized" });
  });

  app.get<{ Params: { deviceId: string } }>("/api/device/:deviceId/commands/next", { preHandler: requireDevice }, async (request) => ({ command: commands.claim(request.params.deviceId) }));

  app.post<{ Params: { deviceId: string } }>("/api/device/:deviceId/commands/result", { preHandler: requireDevice }, async (request, reply) => {
    try { return reply.send({ accepted: commands.complete(request.params.deviceId, request.body) }); }
    catch (error) { if (error instanceof CommandError) return reply.code(error.statusCode).send({ error: error.message }); throw error; }
  });

  app.get<{ Params: { commandId: string } }>("/api/admin/commands/:commandId", { preHandler: requireAdmin }, async (request, reply) => {
    const command = repository.findCommand(request.params.commandId);
    return command ? reply.send(command) : reply.code(404).send({ error: "command not found" });
  });

  app.get("/health", async () => ({ status: "ok" }));
  app.addHook("onClose", async () => {
    const closable = repository as PairingRepository & { close?: () => void };
    closable.close?.();
  });
  return app;
}
