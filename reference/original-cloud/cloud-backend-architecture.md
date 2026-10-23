# Cloud Backend Architecture

## Goal

The phone talks only to the cloud. The ESP32 connects outbound to the cloud after Wi-Fi provisioning. No inbound port, VPN, local Home Assistant connection, or direct phone-to-device connection is required.

```text
Phone/PWA -- HTTPS --> Cloud app -- MQTT/TLS --> ESP32-C3 -- GPIO1 --> PC power button
                         |
                         +-- PostgreSQL
                         +-- Mosquitto
                         +-- reverse proxy/TLS
```

The first release supports one administrator and one device. The data model and device protocol support multiple users and devices from the start.

## Deployment

Use Docker Compose on the Ubuntu server:

- `app`: TypeScript Node.js monolith serving the web UI and HTTP API.
- `postgres`: PostgreSQL for users, devices, grants, commands, and audit records.
- `mosquitto`: MQTT broker, reachable by devices over MQTT/TLS and by the app over the private Compose network.
- `caddy` or `nginx`: public HTTPS endpoint and reverse proxy to `app`.
- Optional later: `redis` for rate limits, sessions, or jobs. Do not add it in the first release.

Only the reverse proxy and MQTT TLS listener should be exposed publicly. PostgreSQL and the app's internal port stay on the Docker network. MQTT plain TCP must not be exposed publicly.

## Application modules

The app should have a small external interface and hide the details behind these modules:

- **Identity**: login, password hashing, session/token handling.
- **Device registry**: device enrollment, serial number, status, last seen time.
- **Authorization**: user-to-device grants with `viewer` and `operator` roles.
- **Command service**: validates a request, creates a command ID, checks online state, publishes MQTT, and records the result.
- **Device event consumer**: consumes availability, acknowledgements, and telemetry from MQTT.
- **Audit log**: immutable record of who requested what, when, and the resulting device response.

The HTTP handlers should call the command service rather than publishing MQTT directly. This keeps authorization, idempotency, timeout handling, and auditing in one deep module.

## First release data model

```text
users
  id, email, password_hash, role, created_at

devices
  id, device_uid, name, mqtt_client_id, status, last_seen_at, created_at

device_grants
  user_id, device_id, role, created_at

commands
  id, device_id, requested_by, kind, status, requested_at,
  published_at, acknowledged_at, error_code, idempotency_key

device_events
  id, device_id, event_type, payload_json, received_at
```

Use UUIDs for database IDs and a stable opaque `device_uid` for MQTT topics. Do not use a user's email or a physical IP address in a topic.

## HTTP interface

All endpoints require an authenticated HTTPS session except login and health checks.

```text
POST /api/auth/login
POST /api/auth/logout
GET  /api/me
GET  /api/devices
GET  /api/devices/:deviceId
POST /api/devices/:deviceId/commands/power-press
GET  /api/devices/:deviceId/commands/:commandId
GET  /api/devices/:deviceId/events
```

`POST /commands/power-press` returns a command ID, not a false claim that the computer powered on:

```json
{
  "commandId": "uuid",
  "status": "queued"
}
```

Possible command states:

```text
queued -> published -> acknowledged
queued -> rejected
published -> timed_out
published -> failed
```

The command endpoint must accept an idempotency key. A mobile retry must not produce two physical power-button presses.

## MQTT contract

Each device gets a unique credential and topic prefix. Example device UID: `pc-power-c3-001`.

```text
pc-power-c3-001/status/availability   retained: online|offline
pc-power-c3-001/status/telemetry      retained: JSON
pc-power-c3-001/events/ack
pc-power-c3-001/commands/power-press
```

Command payload:

```json
{
  "commandId": "uuid",
  "kind": "power_press",
  "issuedAt": "2026-01-01T12:00:00Z",
  "expiresAt": "2026-01-01T12:00:15Z"
}
```

Acknowledgement payload:

```json
{
  "commandId": "uuid",
  "status": "accepted",
  "executedAt": "2026-01-01T12:00:00.350Z"
}
```

The device must reject expired or malformed commands and publish an acknowledgement only after the GPIO pulse has completed. The app treats MQTT publish success as delivery to the broker, not execution by the device.

Use MQTT ACLs so a device can subscribe only to its own command topic and publish only to its own status/event topics. Use TLS and a unique password per device. Do not ship a shared production MQTT password.

## Provisioning

For the first single-user release:

1. The backend creates the device record and its unique MQTT credentials.
2. The firmware image is built with the device UID and MQTT credentials, while Wi-Fi credentials remain configurable.
3. The user configures only Wi-Fi through the ESP32 captive portal.
4. The ESP32 connects outbound to the broker and becomes online.
5. The backend administrator assigns the device to the initial user.

For later multi-device manufacturing, replace per-build credentials with a one-time enrollment token or device certificate. The device should authenticate to a provisioning endpoint, receive short-lived MQTT credentials, and never expose those credentials in the web UI.

## Security baseline

- HTTPS for the web interface and API.
- MQTT over TLS only.
- Argon2id or bcrypt password hashing.
- Secure, HttpOnly, SameSite cookies for the PWA session.
- CSRF protection if cookie-authenticated state-changing requests are used.
- Rate limiting on login and command endpoints.
- Server-side authorization on every device operation.
- Audit every command with user, device, request ID, result, and timestamp.
- Never infer computer power state from device online state or command acknowledgement.
- Store secrets only in server environment/secret files, never in Git.
- Daily PostgreSQL backups and a documented restore test.

## Computer state detection

Add this after remote power control is stable. The ESP32 needs a separate electrically safe input, preferably an optocoupler connected to the motherboard power LED. Publish it as telemetry:

```json
{
  "pc_power_state": "on",
  "source": "pwr_led",
  "observedAt": "2026-01-01T12:00:10Z"
}
```

Keep these states separate:

- `device_online`: ESP32 has a live MQTT connection.
- `pc_power_state`: hardware indicates the computer is on or off.
- `last_command`: the last requested pulse and its execution result.

## Delivery phases

### Phase 1: cloud skeleton

- Docker Compose for Mosquitto, PostgreSQL, app, and HTTPS proxy.
- Database migrations and seed one administrator/device.
- Login, device list, online status, and power-press endpoint.
- MQTT command publishing, acknowledgement timeout, and audit records.

### Phase 2: device integration

- Add Wi-Fi STA plus captive provisioning AP to ESPHome.
- Add MQTT/TLS configuration and the command/acknowledgement contract.
- Keep local web server as a maintenance fallback, not the remote control path.
- Test reconnects, duplicate mobile requests, broker restarts, and offline devices.

### Phase 3: multi-user and multi-device

- Device enrollment flow.
- Device grants and operator/viewer roles.
- User invitation and revocation.
- Per-device MQTT ACL generation and credential rotation.

### Phase 4: PC state

- Add PWR-LED input hardware and debounced GPIO binary sensor.
- Report PC state to the cloud.
- Add UI distinction between `offline`, `online + PC off`, and `online + PC on`.

## Initial technology choice

Use TypeScript with Fastify, PostgreSQL, MQTT.js, and a small server-rendered or React-based responsive web UI. This is one deployable application with clear internal modules, not a microservice fleet. Docker Compose keeps operations simple on the existing Ubuntu server, while the MQTT and persistence interfaces remain replaceable if scale later requires separate workers or managed services.
