# PC Power Cloud Project Plan

## Versioning Rules

- `main` contains reviewed, deployable changes.
- Each milestone is one focused commit with a clear verification result.
- Never commit `secrets.yaml`, `cloud/.env`, MQTT password files, TLS private keys, Cloudflare tokens, database data, or device credentials.
- Server runtime files live under `/opt/pc-power-cloud`; the repository is the source of configuration and application code.
- A deployment records the repository commit, image digest, database migration version, and service health result.
- Existing `futu-tracker` services and the Arduino fallback are managed separately and must not be changed by cloud milestones.

## Current Baseline

- ESPHome power-button firmware exists and drives GPIO1 for approximately 300 ms.
- Device currently supports family Wi-Fi, fallback AP, ESPHome API, OTA, and authenticated local Web Server.
- Ubuntu server has Docker Compose and an existing Cloudflare Tunnel for `futu.19831018.xyz`.
- A separate `pc-power` Cloudflare Tunnel is installed and healthy.
- `pc.19831018.xyz` reaches the bootstrap app through the new Tunnel.
- `mqtt.19831018.xyz` resolves to `47.116.199.150` and is intended to be DNS-only.

## Milestones

### M0: Repository and deployment foundation

Status: in progress.

Deliverables:

- Cloud architecture document.
- Docker Compose project for the cloud stack.
- Bootstrap HTTP app and health endpoint.
- Separate Cloudflare Tunnel route for the control domain.
- Git ignore rules and deployment notes.

Verification:

- `git diff --check` passes.
- `docker compose config` passes on Ubuntu.
- `GET https://pc.19831018.xyz/health` returns HTTP 200.
- Existing containers and `futu-tracker` Tunnel remain healthy.

### M1: Secure broker and persistence

Deliverables:

- PostgreSQL service with persistent volume and health check.
- Mosquitto service with TLS listener on `8883` only.
- App-to-broker private network connection.
- Separate app and device MQTT credentials.
- ACL restricting every device to its own topic prefix.
- Backup and restore procedure.

Exit criteria:

- Anonymous MQTT is rejected.
- Plain MQTT `1883` is not exposed publicly.
- A test client can connect using TLS and is denied topics outside its ACL.
- PostgreSQL data survives container recreation.

### M2: Single-user end-to-end control

Deliverables:

- User login with secure password hashing and session handling.
- One seeded administrator account created through a server-side secret.
- Device registry and initial device record.
- Device online/offline status from MQTT availability messages.
- Power-press command endpoint and responsive phone page.
- Command IDs, idempotency keys, expiry, acknowledgement timeout, and audit records.

Exit criteria:

- An authenticated user can see the device online state.
- One tap creates at most one physical command for one idempotency key.
- The UI distinguishes queued, acknowledged, failed, timed out, and offline.
- No unauthenticated endpoint can publish a power command.

### M3: ESP32 cloud transport

Deliverables:

- ESP32 outbound Wi-Fi configuration and captive provisioning flow.
- MQTT/TLS connection to `mqtt.19831018.xyz:8883`.
- Device-specific identity and credentials.
- Command validation, expiry check, GPIO pulse, and acknowledgement.
- Reconnect and broker outage behavior.

Exit criteria:

- A provisioned device becomes online without inbound port forwarding.
- A cloud command triggers exactly one 300 ms GPIO1 pulse.
- Expired, malformed, or duplicate commands are rejected.
- Credentials are not stored in the public repository.

### M4: Multi-user and multi-device

Deliverables:

- Device enrollment flow.
- User invitations and account revocation.
- Per-device grants with viewer/operator roles.
- Credential rotation and device decommissioning.
- Paginated device list and audit history.

Exit criteria:

- A user can control only granted devices.
- A viewer cannot issue power commands.
- Removing a grant takes effect without rebuilding firmware.
- A compromised device credential cannot access another device's topics.

### M5: PC power-state feedback

Deliverables:

- Safe PWR-LED optocoupler input hardware.
- Debounced ESP32 binary sensor.
- Cloud telemetry and state history.
- UI distinction between device online and PC powered on.

Exit criteria:

- Device online state and PC power state are independent.
- The UI never claims that a PC powered on solely because a command was acknowledged.
- Sensor loss and stale telemetry are visible.

## Deployment Sequence

1. Review and commit M0 locally.
2. Deploy the reviewed M0 commit to `/opt/pc-power-cloud`.
3. Start PostgreSQL and validate persistence.
4. Add Mosquitto TLS, credentials, and ACLs; validate with test clients.
5. Implement M2 against a simulated device before changing firmware.
6. Implement M3 and flash one test device over USB.
7. Test reconnects, retries, duplicate taps, broker restart, and power loss.
8. Add M4 only after the single-user flow is reliable.
9. Add PWR-LED hardware and M5 separately.

## Explicit Non-Goals for M0-M2

- No direct public access to ESP32.
- No router port forwarding.
- No shared MQTT account for all devices.
- No microservice split.
- No native mobile application before the responsive web flow works.
- No assumption that command acknowledgement means the computer is powered on.
