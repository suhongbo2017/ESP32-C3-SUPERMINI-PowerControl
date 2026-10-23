import Fastify from "fastify";
import type { FastifyInstance } from "fastify";
import { PairingError, PairingService } from "./pairing/service.js";
import type { PairingRepository } from "./pairing/repository.js";

export function buildServer(pairing: PairingService, repository: PairingRepository, adminToken: string): FastifyInstance {
  if (adminToken.length < 32) throw new Error("PC_POWER_ADMIN_TOKEN must contain at least 32 characters");
  const app = Fastify({ logger: { redact: ["req.headers.authorization", "req.body.pairingCode"] } });
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

  app.get("/health", async () => ({ status: "ok" }));
  app.addHook("onClose", async () => {
    const closable = repository as PairingRepository & { close?: () => void };
    closable.close?.();
  });
  return app;
}
