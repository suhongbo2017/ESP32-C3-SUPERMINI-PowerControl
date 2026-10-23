import { SqlitePairingRepository } from "./pairing/sqlite-repository.js";
import { PairingService } from "./pairing/service.js";
import { buildServer } from "./server.js";

const port = Number(process.env.PORT ?? 3000);
const host = process.env.HOST ?? "127.0.0.1";
const adminToken = process.env.PC_POWER_ADMIN_TOKEN;
const databasePath = process.env.PC_POWER_DATABASE ?? "./data/pc-power.sqlite";

if (!adminToken) throw new Error("PC_POWER_ADMIN_TOKEN is required");
const repository = await SqlitePairingRepository.open(databasePath);
const app = buildServer(new PairingService(repository), repository, adminToken);
await app.listen({ host, port });
